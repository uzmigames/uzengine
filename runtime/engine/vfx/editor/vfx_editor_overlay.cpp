/// VFX Editor Overlay — Implementation
///
/// Mouse-driven visual overlay for VFX emitter editing in the Application
/// viewport. Renders menu bar, stage tree, sliders, dropdowns, and emitter
/// tabs using the engine's TextRenderer (AddRect + AddText).

#include "vfx_editor_overlay.h"
#include "engine/vfx/components/particle_emitter.h"
#include "editor/font/system_font.h"
#include "input.h"
#include "upload_context.h"
#include "log.h"

#ifdef _WIN32
#include "shader_pipeline.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Enum Name Tables
// ─────────────────────────────────────────────────────────────────────────────

static const char* const kSpawnShapeNames[] = {
    "Point", "Sphere", "Box", "Ring", "Cone", "Cylinder", "Disc", "Line"
};
static constexpr uint32_t kSpawnShapeCount = 8;

static const char* const kBlendModeNames[] = {
    "AlphaBlend", "Additive", "PremulAlpha"
};
static constexpr uint32_t kBlendModeCount = 3;

static const char* const kAlignmentModeNames[] = {
    "Face Camera", "Vel Aligned", "Camera Plane", "Custom Vec", "Camera Pos"
};
static constexpr uint32_t kAlignmentModeCount = 5;

static const char* const kCollisionResponseNames[] = {
    "Bounce", "Stick", "Kill"
};
static constexpr uint32_t kCollisionResponseCount = 3;

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorOverlay::Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
                            ShaderPipeline& shaderPipeline, RHIFormat rtvFormat,
                            uint32_t screenW, uint32_t screenH, bool bindless) {
    m_screenW = screenW;
    m_screenH = screenH;
    m_device  = device;

    const std::string fontPath = ResolveDefaultMonoFont();
    constexpr float kFontSize = 14.0f;
    if (fontPath.empty() || !m_font.Init(device, cmdList, upload, fontPath, kFontSize)) {
        UZ_LOG(VFX, Error, "VfxEditorOverlay: Failed to init font (resolved='%s')",
               fontPath.c_str());
        return false;
    }

    if (!m_textRenderer.Init(device, shaderPipeline, rtvFormat, screenW, screenH, bindless)) {
        UZ_LOG(VFX, Error, "VfxEditorOverlay: Failed to init text renderer");
        return false;
    }

    m_initialized = true;
    return true;
}

void VfxEditorOverlay::Shutdown() {
    if (m_initialized) {
        m_textRenderer.Shutdown();
        // The font atlas is a GPU texture — without this the "FontAtlas"
        // allocation outlives the allocator and trips the D3D12MA
        // not-empty-block assert at device destruction.
        if (m_device) m_font.Shutdown(m_device);
        m_initialized = false;
    }
    m_device = nullptr;
}

void VfxEditorOverlay::SetScreenSize(uint32_t w, uint32_t h) {
    m_screenW = w;
    m_screenH = h;
    m_textRenderer.SetScreenSize(w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BeginFrame(uint32_t frameIndex) {
    m_lastFrameIndex = frameIndex;
    m_textRenderer.BeginFrame(frameIndex);
}

bool VfxEditorOverlay::HandleMouse(const InputSystem* input, float /*dt*/) {
    if (!input) return false;
    m_mouseX = input->GetMouseX();
    m_mouseY = input->GetMouseY();

    // Track just-pressed/released internally from raw down state,
    // because InputSystem::IsMouseJustPressed depends on BeginFrame timing
    // which may not align with when HandleMouse is called.
    const bool currentDown = input->IsMouseDown(MouseButton::Left);
    m_mouseJustPressed  = currentDown && !m_mouseDown;
    m_mouseJustReleased = !currentDown && m_mouseDown;
    m_mouseDown = currentDown;

    m_scrollDelta = input->GetScrollDelta();

    if (m_mouseJustPressed) {
        UZ_LOG(VFX, Info, "CLICK ({:.0f},{:.0f}) down={}", m_mouseX, m_mouseY, m_mouseDown);
    }

    if (m_mouseJustReleased && m_dragSliderPtr != nullptr) {
        m_dragSliderPtr = nullptr;
    }

    if (m_dragSliderPtr != nullptr && m_mouseDown) {
        m_wantsInput = true;
        return true;
    }

    const float sw = static_cast<float>(m_screenW);
    m_wantsInput = false;

    // Menu bar
    if (HitTest(0.0f, 0.0f, sw, kMenuBarH)) m_wantsInput = true;

    // Left panel (always open when overlay visible)
    if (HitTest(0.0f, kMenuBarH, kLeftPanelW, static_cast<float>(m_screenH) - kMenuBarH))
        m_wantsInput = true;

    // Open dropdown menu
    if (m_openMenu >= 0) {
        const float dropX = 10.0f + m_openMenu * (kMenuItemW + 4.0f);
        const float dropW = 160.0f;
        float dropH = 4 * kDropItemH;
        if (m_openMenu == 2) dropH = static_cast<float>(m_presetNames.size()) * kDropItemH + kDropItemH;
        if (HitTest(dropX, kMenuBarH, dropW, dropH)) m_wantsInput = true;
    }

    // Enum dropdown
    if (m_dropdownPtr != nullptr) {
        const float totalH = m_dropdownCount * kDropItemH;
        if (HitTest(m_dropdownX, m_dropdownY, m_dropdownW, totalH)) m_wantsInput = true;
    }

    if (m_mouseJustPressed && !m_wantsInput) {
        m_openMenu = -1;
        CloseDropdown();
    }

    return m_wantsInput;
}

void VfxEditorOverlay::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing) {
    if (!m_initialized || !m_visible) return;

    // Pass 1: base UI (menu bar, emitter bar, stage tree, sliders)
    // No dropdowns in this pass — avoids panel text overwriting dropdown bg.
    m_textRenderer.Render(cmdList, cbRing, m_font);

    // Pass 2: dropdown ONLY — drawn after pass 1 is fully composited.
    // Dropdown rects (alpha=1.0) overwrite pass 1 pixels underneath.
    // Click processing also happens here (m_mouseJustPressed still valid).
    bool hasDropdown = (m_dropdownPtr != nullptr) || (m_openMenu >= 0);
    if (hasDropdown) {
        m_textRenderer.BeginFrame(m_lastFrameIndex);
        BuildMenuDropdowns();
        BuildDropdownOverlay();
        m_textRenderer.Render(cmdList, cbRing, m_font);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildUI
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BuildUI(ParticleEmitter* emitter, const InputSystem* input) {
    if (!m_initialized || !m_visible) return;

    // Re-read mouse position (may have moved since HandleMouse).
    // justPressed/justReleased are preserved from HandleMouse — don't recompute
    // (HandleMouse already updated m_mouseDown, so re-comparing would always be false).
    if (input) {
        m_mouseX = input->GetMouseX();
        m_mouseY = input->GetMouseY();
    }

    m_selectedEmitterIdx = -1;

    // Layer 1: base UI (menu bar, panels)
    BuildMenuBar();
    BuildEmitterBar();

    if (emitter) {
        if (emitter != m_lastEmitter) {
            BuildModuleList(emitter);
            m_lastEmitter = emitter;
        }
        BuildStageTree(emitter);
    }

    // Dropdowns are drawn + click-processed in Render() pass 2 only.
    // NOT called here — avoids adding dropdown rects/text to pass 1 batch
    // where panel text would overwrite them.
}

// ─────────────────────────────────────────────────────────────────────────────
// Widgets
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorOverlay::HitTest(float x, float y, float w, float h) const {
    return m_mouseX >= x && m_mouseX < x + w &&
           m_mouseY >= y && m_mouseY < y + h;
}

bool VfxEditorOverlay::Button(float x, float y, float w, float h, const char* label) {
    const bool hovered = HitTest(x, y, w, h);
    Vec4 color = kColorBtnNormal;
    if (hovered && m_mouseDown) color = kColorBtnPress;
    else if (hovered) color = kColorBtnHover;

    m_textRenderer.AddRect(x, y, w, h, color);
    m_textRenderer.AddText(m_font, label, x + 8.0f, y + (h - 14.0f) * 0.5f, kColorText);
    return hovered && m_mouseJustPressed;
}

bool VfxEditorOverlay::Slider(float x, float y, float w, float* value,
                               float minVal, float maxVal, const char* label) {
    const float range = maxVal - minVal;
    if (range <= 0.0f) return false;

    const float norm = std::clamp((*value - minVal) / range, 0.0f, 1.0f);
    m_textRenderer.AddRect(x, y, w, kSliderH, kColorSliderBg);
    m_textRenderer.AddRect(x, y, w * norm, kSliderH, kColorSliderFill);
    m_textRenderer.AddText(m_font, label, x + 4.0f, y + 2.0f, kColorText);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", *value);
    m_textRenderer.AddText(m_font, buf, x + w - 50.0f, y + 2.0f, {1.0f, 1.0f, 1.0f, 1.0f});

    if (HitTest(x, y, w, kSliderH) && m_mouseJustPressed) {
        m_dragSliderPtr = value;
        m_dragSliderMin = minVal;
        m_dragSliderMax = maxVal;
    }

    if (m_dragSliderPtr == value && m_mouseDown) {
        float t = std::clamp((m_mouseX - x) / w, 0.0f, 1.0f);
        *value = minVal + t * range;
        return true;
    }
    return false;
}

void VfxEditorOverlay::DropdownTrigger(float x, float y, float w, uint32_t* value,
                                        const char* const* names, uint32_t count,
                                        const char* label) {
    const bool hovered = HitTest(x, y, w, kRowH);
    m_textRenderer.AddRect(x, y, w, kRowH, hovered ? kColorBtnHover : kColorBtnNormal);
    m_textRenderer.AddText(m_font, label, x + 4.0f, y + 4.0f, kColorText);

    const uint32_t idx = (*value < count) ? *value : 0;
    m_textRenderer.AddText(m_font, names[idx], x + w * 0.5f, y + 4.0f, kColorAccent);

    if (hovered && m_mouseJustPressed) {
        if (m_dropdownPtr == value) {
            CloseDropdown();
        } else {
            m_dropdownPtr = value;
            m_dropdownX = x;
            m_dropdownY = y + kRowH;
            m_dropdownW = w;
            m_dropdownNames = names;
            m_dropdownCount = count;
        }
    }
}

void VfxEditorOverlay::CloseDropdown() {
    m_dropdownPtr = nullptr;
    m_dropdownNames = nullptr;
    m_dropdownCount = 0;
}

void VfxEditorOverlay::BuildDropdownOverlay() {
    if (m_dropdownPtr == nullptr || m_dropdownNames == nullptr) return;

    const float totalH = m_dropdownCount * kDropItemH;
    m_textRenderer.AddRect(m_dropdownX, m_dropdownY, m_dropdownW, totalH, kColorDropBg);

    for (uint32_t i = 0; i < m_dropdownCount; ++i) {
        const float iy = m_dropdownY + i * kDropItemH;
        const bool hovered = HitTest(m_dropdownX, iy, m_dropdownW, kDropItemH);
        const bool selected = (*m_dropdownPtr == i);

        Vec4 itemColor = kColorDropBg;
        if (hovered) itemColor = kColorBtnHover;
        if (selected) itemColor = kColorMenuActive;

        m_textRenderer.AddRect(m_dropdownX, iy, m_dropdownW, kDropItemH, itemColor);
        m_textRenderer.AddText(m_font, m_dropdownNames[i], m_dropdownX + 8.0f, iy + 4.0f, kColorText);

        if (hovered && m_mouseJustPressed) {
            *m_dropdownPtr = i;
            CloseDropdown();
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu Bar
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BuildMenuBar() {
    const float sw = static_cast<float>(m_screenW);
    m_textRenderer.AddRect(0.0f, 0.0f, sw, kMenuBarH, kColorBarBg);

    static const char* kMenuLabels[kMenuCount] = {"File", "Emitter", "Presets", "View"};

    float x = 10.0f;
    for (int i = 0; i < kMenuCount; ++i) {
        const bool hovered = HitTest(x, 0.0f, kMenuItemW, kMenuBarH);
        Vec4 color = {0.18f, 0.18f, 0.22f, 1.0f};
        if (m_openMenu == i) color = kColorMenuActive;
        else if (hovered) color = kColorMenuHover;

        m_textRenderer.AddRect(x, 2.0f, kMenuItemW, kMenuBarH - 4.0f, color);
        m_textRenderer.AddText(m_font, kMenuLabels[i], x + 10.0f, 8.0f, kColorText);

        if (hovered && m_mouseJustPressed) {
            m_openMenu = (m_openMenu == i) ? -1 : i;
            CloseDropdown();
        }
        if (hovered && m_openMenu >= 0 && m_openMenu != i) {
            m_openMenu = i;
            CloseDropdown();
        }

        x += kMenuItemW + 4.0f;
    }
}

void VfxEditorOverlay::BuildMenuDropdowns() {
    if (m_openMenu < 0) return;

    const float dropX = 10.0f + m_openMenu * (kMenuItemW + 4.0f);
    constexpr float kDropW = 160.0f;
    const float dropY = kMenuBarH;

    switch (m_openMenu) {
        case 0: { // File
            const char* items[] = {"Restart Effect"};
            constexpr int kItemCount = 1;
            m_textRenderer.AddRect(dropX, dropY, kDropW, kItemCount * kDropItemH, kColorDropBg);
            for (int j = 0; j < kItemCount; ++j) {
                const float iy = dropY + j * kDropItemH;
                const bool ih = HitTest(dropX, iy, kDropW, kDropItemH);
                if (ih) m_textRenderer.AddRect(dropX, iy, kDropW, kDropItemH, kColorBtnHover);
                m_textRenderer.AddText(m_font, items[j], dropX + 8.0f, iy + 4.0f, kColorText);
                if (ih && m_mouseJustPressed) {
                    if (j == 0 && onRestart) onRestart();
                    m_openMenu = -1;
                }
            }
            break;
        }
        case 1: { // Emitter — info
            m_textRenderer.AddRect(dropX, dropY, kDropW, kDropItemH, kColorDropBg);
            m_textRenderer.AddText(m_font, "Properties: left panel", dropX + 8.0f, dropY + 4.0f,
                                   kColorTextDim);
            break;
        }
        case 2: { // Presets
            const int count = static_cast<int>(m_presetNames.size());
            const int totalItems = count > 0 ? count : 1;
            const float totalH = totalItems * kDropItemH;
            m_textRenderer.AddRect(dropX, dropY, kDropW, totalH, kColorDropBg);

            if (count == 0) {
                m_textRenderer.AddText(m_font, "(no presets)", dropX + 8.0f, dropY + 4.0f,
                                       {0.6f, 0.6f, 0.6f, 1.0f});
            } else {
                for (int j = 0; j < count; ++j) {
                    const float iy = dropY + j * kDropItemH;
                    const bool ih = HitTest(dropX, iy, kDropW, kDropItemH);
                    if (ih) m_textRenderer.AddRect(dropX, iy, kDropW, kDropItemH, kColorBtnHover);
                    m_textRenderer.AddText(m_font, m_presetNames[j].c_str(),
                                           dropX + 8.0f, iy + 4.0f, kColorText);
                    if (ih && m_mouseJustPressed) {
                        if (onLoadPreset) onLoadPreset(j, m_lastEmitter);
                        m_openMenu = -1;
                    }
                }
            }
            break;
        }
        case 3: { // View
            const char* items[] = {"Toggle Stats"};
            constexpr int kItemCount = 1;
            m_textRenderer.AddRect(dropX, dropY, kDropW, kItemCount * kDropItemH, kColorDropBg);
            for (int j = 0; j < kItemCount; ++j) {
                const float iy = dropY + j * kDropItemH;
                const bool ih = HitTest(dropX, iy, kDropW, kDropItemH);
                if (ih) m_textRenderer.AddRect(dropX, iy, kDropW, kDropItemH, kColorBtnHover);
                m_textRenderer.AddText(m_font, items[j], dropX + 8.0f, iy + 4.0f, kColorText);
                if (ih && m_mouseJustPressed) m_openMenu = -1;
            }
            break;
        }
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter Selector Bar
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BuildEmitterBar() {
    const float sw = static_cast<float>(m_screenW);
    const float barY = kMenuBarH;
    m_textRenderer.AddRect(0.0f, barY, sw, kEmitterBarH, kColorBarBg);

    float btnX = 10.0f;
    const float btnY = barY + 2.0f;
    const float btnW = 80.0f;
    const float btnH = kEmitterBarH - 4.0f;

    // "Start" button — applies slider edits by restarting the emitter
    if (Button(btnX, btnY, btnW, btnH, "Start")) {
        if (onRestart) onRestart();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxModuleDef helpers
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorOverlay::VfxModuleDef::IsActive() const {
    if (enablePtr != nullptr) return *enablePtr != 0;
    if (strengthPtr != nullptr) return *strengthPtr > 0.0f;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildModuleList — populate m_modules from ParticleEmitter field pointers
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BuildModuleList(ParticleEmitter* e) {
    m_modules.clear();
    m_expandedModuleIndex = -1;

    auto addFloat = [](std::vector<VfxModuleParam>& v, const char* label, float* ptr,
                       float mn, float mx) {
        VfxModuleParam p;
        p.label = label; p.type = VfxModuleParam::Float;
        p.fPtr = ptr; p.min = mn; p.max = mx;
        v.push_back(p);
    };
    auto addEnum = [](std::vector<VfxModuleParam>& v, const char* label, uint32_t* ptr,
                      const char* const* names, uint32_t count) {
        VfxModuleParam p;
        p.label = label; p.type = VfxModuleParam::Enum;
        p.uPtr = ptr; p.enumNames = names; p.enumCount = count;
        v.push_back(p);
    };

    // ── Emitter Spawn ────────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Emission"; m.stage = VfxStage::EmitterSpawn;
        addFloat(m.params, "Rate", &e->emissionRate, 1.0f, 50000.0f);
        m.params.push_back({});
        auto& bp = m.params.back();
        bp.label = "Max Particles"; bp.type = VfxModuleParam::Uint;
        bp.uPtr = &e->maxParticles; bp.min = 100.0f; bp.max = 50000.0f;
        m_modules.push_back(std::move(m));
    }

    // ── Emitter Update ───────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Lifecycle"; m.stage = VfxStage::EmitterUpdate;
        addFloat(m.params, "Duration", &e->emitterDuration, 0.0f, 30.0f);
        m.params.push_back({});
        auto& lp = m.params.back();
        lp.label = "Loop Count"; lp.type = VfxModuleParam::Uint;
        lp.uPtr = &e->loopCount; lp.min = 0.0f; lp.max = 100.0f;
        m_modules.push_back(std::move(m));
    }

    // ── Particle Spawn ───────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Init Particle"; m.stage = VfxStage::ParticleSpawn;
        addFloat(m.params, "Size", &e->initialSize, 0.1f, 500.0f);
        addFloat(m.params, "Lifetime", &e->lifetime, 0.1f, 30.0f);
        addFloat(m.params, "Life Var", &e->lifetimeVariation, 0.0f, 10.0f);
        addFloat(m.params, "Color R", &e->initialColor.x, 0.0f, 1.0f);
        addFloat(m.params, "Color G", &e->initialColor.y, 0.0f, 1.0f);
        addFloat(m.params, "Color B", &e->initialColor.z, 0.0f, 1.0f);
        addFloat(m.params, "Color A", &e->initialColor.w, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Shape Location"; m.stage = VfxStage::ParticleSpawn;
        addEnum(m.params, "Shape", &e->spawnShape, kSpawnShapeNames, kSpawnShapeCount);
        addFloat(m.params, "Radius", &e->spawnRadius, 0.0f, 1000.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Init Velocity"; m.stage = VfxStage::ParticleSpawn;
        addFloat(m.params, "Vel X", &e->spawnVelocity.x, -5000.0f, 5000.0f);
        addFloat(m.params, "Vel Y", &e->spawnVelocity.y, -5000.0f, 5000.0f);
        addFloat(m.params, "Vel Z", &e->spawnVelocity.z, -5000.0f, 5000.0f);
        addFloat(m.params, "Variation", &e->velocityVariation, 0.0f, 2000.0f);
        addFloat(m.params, "Spread", &e->spreadAngle, 0.0f, 180.0f);
        m_modules.push_back(std::move(m));
    }

    // ── Particle Update ──────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Gravity"; m.stage = VfxStage::ParticleUpdate;
        addFloat(m.params, "Gravity X", &e->gravity.x, -10000.0f, 10000.0f);
        addFloat(m.params, "Gravity Y", &e->gravity.y, -10000.0f, 10000.0f);
        addFloat(m.params, "Gravity Z", &e->gravity.z, -10000.0f, 10000.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Damping"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &e->damping;
        addFloat(m.params, "Damping", &e->damping, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Drag"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &e->dragCoefficient;
        addFloat(m.params, "Drag", &e->dragCoefficient, 0.0f, 10.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Curl Noise"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &e->curlStrength;
        addFloat(m.params, "Strength", &e->curlStrength, 0.0f, 10.0f);
        addFloat(m.params, "Frequency", &e->curlFrequency, 0.01f, 5.0f);
        addFloat(m.params, "Speed", &e->curlSpeed, 0.0f, 5.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Vortex"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &e->vortexTangentialStrength;
        addFloat(m.params, "Tangential", &e->vortexTangentialStrength, -2000.0f, 2000.0f);
        addFloat(m.params, "Radial", &e->vortexRadialStrength, -2000.0f, 2000.0f);
        addFloat(m.params, "Max Radius", &e->vortexMaxRadius, 10.0f, 5000.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Wind"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &e->windStrength;
        addFloat(m.params, "Strength", &e->windStrength, 0.0f, 5000.0f);
        addFloat(m.params, "Dir X", &e->windDirX, -1.0f, 1.0f);
        addFloat(m.params, "Dir Y", &e->windDirY, -1.0f, 1.0f);
        addFloat(m.params, "Dir Z", &e->windDirZ, -1.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Orbit"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &e->orbitEnabled;
        addFloat(m.params, "Radius", &e->orbitRadius, 0.0f, 2000.0f);
        addFloat(m.params, "Speed", &e->orbitSpeed, -20.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Ground Collision"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &e->groundCollision;
        addEnum(m.params, "Response", &e->collisionResponse,
                kCollisionResponseNames, kCollisionResponseCount);
        addFloat(m.params, "Restitution", &e->restitution, 0.0f, 1.0f);
        addFloat(m.params, "Friction", &e->collisionFriction, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Rotation"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &e->rotationRate;
        addFloat(m.params, "Rate", &e->rotationRate, -20.0f, 20.0f);
        addFloat(m.params, "Life Start", &e->rotationOverLifeStart, -20.0f, 20.0f);
        addFloat(m.params, "Life End", &e->rotationOverLifeEnd, -20.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Acceleration"; m.stage = VfxStage::ParticleUpdate;
        addFloat(m.params, "Accel X", &e->accelX, -5000.0f, 5000.0f);
        addFloat(m.params, "Accel Y", &e->accelY, -5000.0f, 5000.0f);
        addFloat(m.params, "Accel Z", &e->accelZ, -5000.0f, 5000.0f);
        m_modules.push_back(std::move(m));
    }

    // ── Render ───────────────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Billboard"; m.stage = VfxStage::Render;
        addEnum(m.params, "Facing", &e->facingMode, kAlignmentModeNames, kAlignmentModeCount);
        addFloat(m.params, "Stretch", &e->stretchFactor, 0.0f, 5.0f);
        addFloat(m.params, "Cam Offset", &e->cameraOffset, -500.0f, 500.0f);
        addFloat(m.params, "Pivot X", &e->pivotOffsetX, -1.0f, 1.0f);
        addFloat(m.params, "Pivot Y", &e->pivotOffsetY, -1.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Size Over Life"; m.stage = VfxStage::Render;
        addFloat(m.params, "Start", &e->sizeOverLifeStart, 0.0f, 5.0f);
        addFloat(m.params, "Peak", &e->sizeOverLifePeak, 0.0f, 5.0f);
        addFloat(m.params, "End", &e->sizeOverLifeEnd, 0.0f, 5.0f);
        addFloat(m.params, "Peak Time", &e->sizeOverLifePeakTime, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Color Over Life"; m.stage = VfxStage::Render;
        addFloat(m.params, "Start R", &e->colorLifeStartR, 0.0f, 1.0f);
        addFloat(m.params, "Start G", &e->colorLifeStartG, 0.0f, 1.0f);
        addFloat(m.params, "Start B", &e->colorLifeStartB, 0.0f, 1.0f);
        addFloat(m.params, "Start A", &e->colorLifeStartA, 0.0f, 1.0f);
        addFloat(m.params, "End R", &e->colorLifeEndR, 0.0f, 1.0f);
        addFloat(m.params, "End G", &e->colorLifeEndG, 0.0f, 1.0f);
        addFloat(m.params, "End B", &e->colorLifeEndB, 0.0f, 1.0f);
        addFloat(m.params, "End A", &e->colorLifeEndA, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Size by Speed"; m.stage = VfxStage::Render;
        m.enablePtr = &e->sizeBySpeedEnabled;
        addFloat(m.params, "Min Size", &e->sizeBySpeedMin, 0.0f, 5.0f);
        addFloat(m.params, "Max Size", &e->sizeBySpeedMax, 0.0f, 10.0f);
        addFloat(m.params, "Speed Max", &e->speedRangeMax, 10.0f, 10000.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Emissive"; m.stage = VfxStage::Render;
        m.strengthPtr = &e->emissiveIntensity;
        addFloat(m.params, "Intensity", &e->emissiveIntensity, 0.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Soft Fade"; m.stage = VfxStage::Render;
        addFloat(m.params, "Distance", &e->softParticleFadeRange, 0.0f, 500.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Blend Mode"; m.stage = VfxStage::Render;
        addEnum(m.params, "Mode", &e->blendMode, kBlendModeNames, kBlendModeCount);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Screen Space"; m.stage = VfxStage::Render;
        m.enablePtr = &e->screenSpaceSizeEnabled;
        addFloat(m.params, "Pixel Size", &e->screenSpacePixelSize, 1.0f, 64.0f);
        m_modules.push_back(std::move(m));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildStageTree — hierarchical stage/module panel
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BuildStageTree(ParticleEmitter* /*emitter*/) {
    const float panelX = 0.0f;
    const float panelY = kMenuBarH + kEmitterBarH;
    const float panelH = static_cast<float>(m_screenH) - panelY;
    const float contentW = kLeftPanelW - 2.0f * kPanelPadX;

    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, panelH, kColorPanelBg);

    // Title bar
    constexpr float kCatHeaderH = 24.0f;
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, kCatHeaderH, kColorBarBg);
    m_textRenderer.AddText(m_font, "Module Stack", panelX + 8.0f, panelY + 5.0f, kColorText);

    const float contentTop = panelY + kCatHeaderH + 4.0f;
    const float contentBot = static_cast<float>(m_screenH);
    const float visibleH = contentBot - contentTop;

    if (HitTest(panelX, contentTop, kLeftPanelW, visibleH) && m_scrollDelta != 0.0f) {
        m_leftPanelScroll -= m_scrollDelta * kScrollSpeed;
    }

    // Compute total content height
    float totalH = 0.0f;
    static const char* kStageNames[] = {
        "Emitter Spawn", "Emitter Update", "Particle Spawn", "Particle Update", "Render"
    };
    for (int si = 0; si < kStageCount; ++si) {
        totalH += kStageHeaderH + 2.0f;
        if (!m_stageExpanded[si]) continue;
        for (int mi = 0; mi < static_cast<int>(m_modules.size()); ++mi) {
            if (static_cast<int>(m_modules[mi].stage) != si) continue;
            totalH += kModuleRowH + 1.0f;
            if (mi == m_expandedModuleIndex) {
                totalH += static_cast<float>(m_modules[mi].params.size()) * (kSliderH + 2.0f);
            }
        }
    }
    m_leftPanelContentH = totalH;

    const float maxScroll = std::max(0.0f, totalH - visibleH);
    m_leftPanelScroll = std::clamp(m_leftPanelScroll, 0.0f, maxScroll);

    const float cx = panelX + kPanelPadX;
    float y = contentTop - m_leftPanelScroll;

    for (int si = 0; si < kStageCount; ++si) {
        const VfxStage stage = static_cast<VfxStage>(si);

        // Stage header
        if (y + kStageHeaderH > contentTop && y < contentBot) {
            const bool hdrHovered = HitTest(cx, std::max(y, contentTop), contentW,
                                            std::min(kStageHeaderH, contentBot - y));
            m_textRenderer.AddRect(cx, y, contentW, kStageHeaderH,
                                   hdrHovered ? kColorBtnHover : kColorStageHdr);

            const char* arrow = m_stageExpanded[si] ? "v" : ">";
            char hdrLabel[80];
            std::snprintf(hdrLabel, sizeof(hdrLabel), "%s %s", arrow, kStageNames[si]);
            m_textRenderer.AddText(m_font, hdrLabel, cx + 4.0f, y + 6.0f, kColorText);

            if (hdrHovered && m_mouseJustPressed) {
                m_stageExpanded[si] = !m_stageExpanded[si];
            }
        }
        y += kStageHeaderH + 2.0f;

        if (!m_stageExpanded[si]) continue;

        // Module rows
        for (int mi = 0; mi < static_cast<int>(m_modules.size()); ++mi) {
            auto& mod = m_modules[mi];
            if (mod.stage != stage) continue;

            const bool isExpanded = (mi == m_expandedModuleIndex);
            const bool isActive = mod.IsActive();

            if (y + kModuleRowH > contentTop && y < contentBot) {
                m_textRenderer.AddRect(cx + 2.0f, y, contentW - 4.0f, kModuleRowH,
                                       isExpanded ? kColorModuleExp : kColorModuleRow);

                // Checkbox
                const float cbX = cx + 6.0f;
                const float cbY = y + (kModuleRowH - kCheckboxSize) * 0.5f;
                m_textRenderer.AddRect(cbX, cbY, kCheckboxSize, kCheckboxSize,
                                       isActive ? kColorCheckOn : kColorCheckOff);
                if (isActive) {
                    m_textRenderer.AddRect(cbX + 3.0f, cbY + 3.0f,
                                           kCheckboxSize - 6.0f, kCheckboxSize - 6.0f, kColorText);
                }

                // Checkbox click
                if (HitTest(cbX, cbY, kCheckboxSize + 4.0f, kCheckboxSize + 4.0f) && m_mouseJustPressed) {
                    if (isActive) {
                        if (mod.enablePtr) { mod.savedEnable = *mod.enablePtr; if (!mod.savedEnable) mod.savedEnable = 1; *mod.enablePtr = 0; }
                        if (mod.strengthPtr) { mod.savedStrength = *mod.strengthPtr; if (mod.savedStrength == 0.0f) mod.savedStrength = 1.0f; *mod.strengthPtr = 0.0f; }
                    } else {
                        if (mod.enablePtr) { *mod.enablePtr = mod.savedEnable ? mod.savedEnable : 1u; }
                        if (mod.strengthPtr) { *mod.strengthPtr = mod.savedStrength > 0.0f ? mod.savedStrength : 1.0f; }
                    }
                }

                // Module name
                const float nameX = cbX + kCheckboxSize + 6.0f;
                m_textRenderer.AddText(m_font, mod.name, nameX, y + 4.0f,
                                       isActive ? kColorText : kColorTextDim);

                // Click to expand
                if (HitTest(nameX, y, contentW - kCheckboxSize - 20.0f, kModuleRowH) && m_mouseJustPressed) {
                    m_expandedModuleIndex = isExpanded ? -1 : mi;
                }
            }
            y += kModuleRowH + 1.0f;

            // Expanded params
            if (isExpanded) {
                for (auto& param : mod.params) {
                    if (y + kSliderH + 2.0f <= contentTop || y >= contentBot) {
                        y += kSliderH + 2.0f;
                        continue;
                    }

                    const float px = cx + kModuleIndent;
                    const float pw = contentW - kModuleIndent - 4.0f;

                    switch (param.type) {
                        case VfxModuleParam::Float:
                            if (param.fPtr) Slider(px, y, pw, param.fPtr, param.min, param.max, param.label);
                            break;
                        case VfxModuleParam::Uint:
                            if (param.uPtr) {
                                float fVal = static_cast<float>(*param.uPtr);
                                if (Slider(px, y, pw, &fVal, param.min, param.max, param.label)) {
                                    *param.uPtr = static_cast<uint32_t>(std::clamp(fVal, param.min, param.max));
                                }
                            }
                            break;
                        case VfxModuleParam::Enum:
                            if (param.uPtr && param.enumNames) {
                                DropdownTrigger(px, y, pw, param.uPtr, param.enumNames, param.enumCount, param.label);
                            }
                            break;
                        default: break;
                    }
                    y += kSliderH + 2.0f;
                }
            }
        }
    }
}

} // namespace uz
