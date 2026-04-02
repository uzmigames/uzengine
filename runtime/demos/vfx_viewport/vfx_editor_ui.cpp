/// VFX Editor UI — Implementation
///
/// Mouse-driven visual overlay for VFX emitter editing. Renders menu bar,
/// property panels, toolbar, sliders, toggles, and dropdown menus using the
/// engine's TextRenderer (AddRect + AddText) system.

#include "vfx_editor_ui.h"
#include "vfx_viewport_vfx.h"
#include "input.h"
#include "upload_context.h"
#include "uzengine.h"

#ifdef _WIN32
#include "shader_pipeline.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Enum Name Tables (shared with parameter panel)
// ─────────────────────────────────────────────────────────────────────────────

static const char* const kSpawnShapeNames[] = {
    "Point", "Sphere", "Box", "Ring", "Cone", "Cylinder", "Disc", "Line"
};
static constexpr uint32_t kSpawnShapeCount = 8;

static const char* const kBlendModeNames[] = {
    "AlphaBlend", "Additive", "PremulAlpha"
};
static constexpr uint32_t kBlendModeCount = 3;

/// Facing mode names for the Rendering dropdown in the editor canvas UI.
/// @ref Latta 2004 "Inside Geometry Instancing" — camera-position billboard
/// @ref Valve 2006 "Rendering in Left 4 Dead" — axis-aligned billboard
static const char* const kAlignmentModeNames[] = {
    "Face Camera", "Velocity Aligned", "Camera Plane", "Custom Vector", "Camera Position"
};
static constexpr uint32_t kAlignmentModeCount = 5;

static const char* const kLitModeNames[] = {
    "Unlit", "Lit"
};
static constexpr uint32_t kLitModeCount = 2;

static const char* const kBoolEnumNames[] = {
    "Off", "On"
};
static constexpr uint32_t kBoolEnumCount = 2;

/// Collision response mode names for the editor dropdown.
/// Indices match VFXEmitterConfig::collisionResponse (0=Bounce, 1=Stick, 2=Kill).
/// @ref Müller 2006 "Position Based Dynamics" — multi-mode collision response
static const char* const kCollisionResponseNames[] = {
    "Bounce", "Stick", "Kill"
};
static constexpr uint32_t kCollisionResponseCount = 3;

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEditorUI::Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
                       ShaderPipeline& shaderPipeline, RHIFormat rtvFormat,
                       uint32_t screenW, uint32_t screenH, bool bindless) {
    m_screenW = screenW;
    m_screenH = screenH;

    // Font atlas — same as StatsOverlay: Consolas 14px
    const std::string fontPath = "C:/Windows/Fonts/consola.ttf";
    constexpr float kFontSize = 14.0f;
    if (!m_font.Init(device, cmdList, upload, fontPath, kFontSize)) {
        UZ_LOG(App, Error, "VFXEditorUI: Failed to init font");
        return false;
    }

    // Text renderer — uses ShaderPipeline overload (same as StatsOverlay)
    if (!m_textRenderer.Init(device, shaderPipeline, rtvFormat, screenW, screenH, bindless)) {
        UZ_LOG(App, Error, "VFXEditorUI: Failed to init text renderer");
        return false;
    }

    m_initialized = true;
    UZ_LOG(App, Info, "VFXEditorUI: Initialized ({}x{}, bindless={})", screenW, screenH, bindless);
    return true;
}

void VFXEditorUI::Shutdown() {
    if (m_initialized) {
        m_textRenderer.Shutdown();
        m_initialized = false;
    }
}

void VFXEditorUI::SetScreenSize(uint32_t w, uint32_t h) {
    m_screenW = w;
    m_screenH = h;
    m_textRenderer.SetScreenSize(w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BeginFrame(uint32_t frameIndex) {
    m_lastFrameIndex = frameIndex;
    m_textRenderer.BeginFrame(frameIndex);
}

bool VFXEditorUI::HandleMouse(const InputSystem* input, float dt) {
    (void)dt;

    m_mouseX = input->GetMouseX();
    m_mouseY = input->GetMouseY();
    m_mouseDown = input->IsMouseDown(MouseButton::Left);
    m_mouseJustPressed = input->IsMouseJustPressed(MouseButton::Left);
    m_mouseJustReleased = input->IsMouseJustReleased(MouseButton::Left);
    m_ctrlDown = input->IsKeyDown(Key::LeftCtrl) || input->IsKeyDown(Key::RightCtrl);
    m_scrollDelta = input->GetScrollDelta();

    // Release slider drag on mouse up
    if (m_mouseJustReleased && m_dragSliderPtr != nullptr) {
        m_dragSliderPtr = nullptr;
    }

    // Continue slider drag while mouse is held
    if (m_dragSliderPtr != nullptr && m_mouseDown) {
        m_wantsInput = true;
        return true;
    }

    // Check if mouse is over any UI region
    const float sw = static_cast<float>(m_screenW);
    const float sh = static_cast<float>(m_screenH);

    m_wantsInput = false;

    // Menu bar
    if (HitTest(0.0f, 0.0f, sw, kMenuBarH)) {
        m_wantsInput = true;
    }

    // Bottom toolbar
    if (HitTest(0.0f, sh - kToolbarH, sw, kToolbarH)) {
        m_wantsInput = true;
    }

    // Left panel
    if (m_leftPanelOpen && HitTest(0.0f, kMenuBarH, kLeftPanelW, sh - kMenuBarH - kToolbarH)) {
        m_wantsInput = true;
    }

    // Right panel
    if (m_rightPanelOpen && HitTest(sw - kRightPanelW, kMenuBarH, kRightPanelW, sh - kMenuBarH - kToolbarH)) {
        m_wantsInput = true;
    }

    // Open dropdown menu
    if (m_openMenu >= 0) {
        // Dropdown region below menu bar
        const float dropX = kMenuItemPadX + m_openMenu * (kMenuItemW + kMenuItemGap);
        const float dropW = 160.0f;
        float dropH = 0.0f;
        // Estimate dropdown height (varies by menu)
        switch (m_openMenu) {
            case 0: dropH = 4 * kDropItemH; break; // File: 4 items
            case 1: dropH = 1 * kDropItemH; break; // Emitter: 1 item
            case 2: dropH = 1 * kDropItemH; break; // Modules: 1 item
            case 3: dropH = static_cast<float>(m_presetNames.size()) * kDropItemH + kDropItemH; break;
            case 4: dropH = 4 * kDropItemH; break; // View: 4 items
            default: break;
        }
        if (HitTest(dropX, kMenuBarH, dropW, dropH)) {
            m_wantsInput = true;
        }
    }

    // Enum dropdown overlay
    if (m_dropdownPtr != nullptr) {
        const float totalH = m_dropdownCount * kDropItemH;
        if (HitTest(m_dropdownX, m_dropdownY, m_dropdownW, totalH)) {
            m_wantsInput = true;
        }
    }

    // Close menus on click outside any UI region
    if (m_mouseJustPressed && !m_wantsInput) {
        m_openMenu = -1;
        CloseDropdown();
    }

    return m_wantsInput;
}

void VFXEditorUI::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing) {
    if (!m_initialized) return;
    // Pass 1: render base UI (menu bar, panels, toolbar)
    m_textRenderer.Render(cmdList, cbRing, m_font);

    // Pass 2: render dropdown overlays ON TOP in a separate batch.
    // By calling BeginFrame again (same frame index → same buffer slot), we
    // clear the vertex arrays and rebuild with ONLY dropdown content. This
    // second Render() draws dropdown rects+text after all base UI rects+text,
    // ensuring dropdowns visually cover everything beneath them.
    bool hasDropdown = (m_dropdownPtr != nullptr) || (m_openMenu >= 0);
    if (hasDropdown) {
        m_textRenderer.BeginFrame(m_lastFrameIndex);
        BuildMenuDropdowns();
        BuildDropdownOverlay();
        m_textRenderer.Render(cmdList, cbRing, m_font);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildUI — assembles all UI elements into the text renderer
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildUI(VFXEmitterConfig* config, float* spawnRate, bool* burstMode,
                          uint32_t* burstCount, bool* vfxEnabled) {
    if (!m_initialized) return;

    // Reset per-frame emitter selection state
    m_selectedEmitterIdx = -1;

    // Layer 1: Background panels + toolbar (drawn first = behind)
    BuildMenuBar();
    BuildEmitterBar();
    BuildToolbar(burstMode, vfxEnabled);

    if (m_leftPanelOpen) {
        // Rebuild module list when config pointer changes (emitter switch)
        if (config != m_lastConfig) {
            BuildModuleList(config, spawnRate, burstCount);
            m_lastConfig = config;
        }
        BuildStageTree(config, spawnRate, burstCount);
    }

    if (m_rightPanelOpen) {
        BuildRightPanel(config);
    }

    // Layer 2: Dropdowns are rendered in a SEPARATE batch in Render()
    // so both their rects AND text appear on top of panel content.
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit Test
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEditorUI::HitTest(float x, float y, float w, float h) const {
    return m_mouseX >= x && m_mouseX < x + w &&
           m_mouseY >= y && m_mouseY < y + h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Button Widget
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEditorUI::Button(float x, float y, float w, float h, const char* label) {
    const bool hovered = HitTest(x, y, w, h);
    Vec4 color = kColorBtnNormal;
    if (hovered && m_mouseDown) {
        color = kColorBtnPress;
    } else if (hovered) {
        color = kColorBtnHover;
    }

    m_textRenderer.AddRect(x, y, w, h, color);
    m_textRenderer.AddText(m_font, label, x + kBtnPadX, y + (h - 14.0f) * 0.5f, kColorText);

    return hovered && m_mouseJustPressed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slider Widget
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEditorUI::Slider(float x, float y, float w, float* value, float minVal, float maxVal,
                         const char* label) {
    const float range = maxVal - minVal;
    if (range <= 0.0f) return false;

    const float norm = std::clamp((*value - minVal) / range, 0.0f, 1.0f);

    // Background
    m_textRenderer.AddRect(x, y, w, kSliderH, kColorSliderBg);
    // Fill bar
    m_textRenderer.AddRect(x, y, w * norm, kSliderH, kColorSliderFill);
    // Label
    m_textRenderer.AddText(m_font, label, x + 4.0f, y + 2.0f, kColorText);
    // Value text
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", *value);
    m_textRenderer.AddText(m_font, buf, x + w - 50.0f, y + 2.0f, {1.0f, 1.0f, 1.0f, 1.0f});

    // Start drag
    if (HitTest(x, y, w, kSliderH) && m_mouseJustPressed) {
        m_dragSliderPtr = value;
        m_dragSliderMin = minVal;
        m_dragSliderMax = maxVal;
    }

    // Update value during drag
    if (m_dragSliderPtr == value && m_mouseDown) {
        float t = (m_mouseX - x) / w;
        t = std::clamp(t, 0.0f, 1.0f);
        const float rawVal = minVal + t * range;
        // Ctrl = fine control: blend 90% current + 10% target for smooth micro-adjustment
        if (m_ctrlDown) {
            *value = *value * 0.9f + rawVal * 0.1f;
        } else {
            *value = rawVal;
        }
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Toggle Widget
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEditorUI::Toggle(float x, float y, bool* value, const char* label) {
    constexpr float kBoxSize = 16.0f;
    constexpr float kGap = 6.0f;

    // Checkbox box
    const bool hovered = HitTest(x, y, kBoxSize + kGap + 100.0f, kRowH);
    const Vec4 boxColor = hovered ? kColorBtnHover : kColorBtnNormal;
    m_textRenderer.AddRect(x, y + 2.0f, kBoxSize, kBoxSize, boxColor);

    // Checkmark (filled inner rect when true)
    if (*value) {
        m_textRenderer.AddRect(x + 3.0f, y + 5.0f, kBoxSize - 6.0f, kBoxSize - 6.0f, kColorAccent);
    }

    // Label
    m_textRenderer.AddText(m_font, label, x + kBoxSize + kGap, y + 3.0f, kColorText);

    // Click to toggle
    if (hovered && m_mouseJustPressed) {
        *value = !(*value);
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dropdown Trigger Widget
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::DropdownTrigger(float x, float y, float w, uint32_t* value,
                                   const char* const* names, uint32_t count, const char* label) {
    const bool hovered = HitTest(x, y, w, kRowH);
    const Vec4 bgColor = hovered ? kColorBtnHover : kColorBtnNormal;

    m_textRenderer.AddRect(x, y, w, kRowH, bgColor);

    // Label on left
    m_textRenderer.AddText(m_font, label, x + 4.0f, y + 4.0f, kColorText);

    // Current value name on right
    const uint32_t idx = (*value < count) ? *value : 0;
    m_textRenderer.AddText(m_font, names[idx], x + w * 0.5f, y + 4.0f, kColorAccent);

    // Click to open dropdown overlay
    if (hovered && m_mouseJustPressed) {
        if (m_dropdownPtr == value) {
            // Toggle off if already open for this field
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

void VFXEditorUI::CloseDropdown() {
    m_dropdownPtr = nullptr;
    m_dropdownNames = nullptr;
    m_dropdownCount = 0;
}

void VFXEditorUI::BuildDropdownOverlay() {
    if (m_dropdownPtr == nullptr || m_dropdownNames == nullptr) return;

    const float totalH = m_dropdownCount * kDropItemH;
    // Background
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

void VFXEditorUI::BuildMenuBar() {
    const float sw = static_cast<float>(m_screenW);

    // Bar background
    m_textRenderer.AddRect(0.0f, 0.0f, sw, kMenuBarH, kColorBarBg);

    static const char* kMenuLabels[kMenuCount] = {"File", "Emitter", "Modules", "Presets", "View"};

    float x = kMenuItemPadX;
    for (int i = 0; i < kMenuCount; ++i) {
        const bool hovered = HitTest(x, 0.0f, kMenuItemW, kMenuBarH);
        Vec4 color = {0.18f, 0.18f, 0.22f, 1.0f};
        if (m_openMenu == i) {
            color = kColorMenuActive;
        } else if (hovered) {
            color = kColorMenuHover;
        }

        m_textRenderer.AddRect(x, 2.0f, kMenuItemW, kMenuBarH - 4.0f, color);
        m_textRenderer.AddText(m_font, kMenuLabels[i], x + 10.0f, 8.0f, kColorText);

        // Click to toggle menu
        if (hovered && m_mouseJustPressed) {
            if (m_openMenu == i) {
                m_openMenu = -1;
            } else {
                m_openMenu = i;
                CloseDropdown();
            }
        }

        // Hover-open adjacent menus when one is already open
        if (hovered && m_openMenu >= 0 && m_openMenu != i) {
            m_openMenu = i;
            CloseDropdown();
        }

        x += kMenuItemW + kMenuItemGap;
    }

    // Menu dropdown content is rendered by BuildMenuDropdowns() AFTER panels
    // so it visually covers any overlapping panel content beneath.
}

void VFXEditorUI::BuildMenuDropdowns() {
    if (m_openMenu < 0) return;

    {
        const float dropX = kMenuItemPadX + m_openMenu * (kMenuItemW + kMenuItemGap);
        constexpr float kDropW = 160.0f;
        float dropY = kMenuBarH;

        switch (m_openMenu) {
            case 0: { // File
                const char* items[] = {"Restart Effect", "Burst", "Fireball", "Screenshot"};
                constexpr int kItemCount = 4;
                m_textRenderer.AddRect(dropX, dropY, kDropW, kItemCount * kDropItemH, kColorDropBg);
                for (int j = 0; j < kItemCount; ++j) {
                    const float iy = dropY + j * kDropItemH;
                    const bool ih = HitTest(dropX, iy, kDropW, kDropItemH);
                    if (ih) {
                        m_textRenderer.AddRect(dropX, iy, kDropW, kDropItemH, kColorBtnHover);
                    }
                    m_textRenderer.AddText(m_font, items[j], dropX + 8.0f, iy + 4.0f, kColorText);

                    if (ih && m_mouseJustPressed) {
                        if (j == 0 && onRestart) onRestart();
                        if (j == 1 && onBurst) onBurst();
                        if (j == 2 && onFireball) onFireball();
                        // j == 3: screenshot — no callback wired yet
                        m_openMenu = -1;
                    }
                }
                break;
            }
            case 1: { // Emitter → toggle left panel
                m_textRenderer.AddRect(dropX, dropY, kDropW, kDropItemH, kColorDropBg);
                const char* label = m_leftPanelOpen ? "Hide Properties" : "Show Properties";
                const bool ih = HitTest(dropX, dropY, kDropW, kDropItemH);
                if (ih) {
                    m_textRenderer.AddRect(dropX, dropY, kDropW, kDropItemH, kColorBtnHover);
                }
                m_textRenderer.AddText(m_font, label, dropX + 8.0f, dropY + 4.0f, kColorText);
                if (ih && m_mouseJustPressed) {
                    m_leftPanelOpen = !m_leftPanelOpen;
                    m_openMenu = -1;
                }
                break;
            }
            case 2: { // Modules → toggle right panel
                m_textRenderer.AddRect(dropX, dropY, kDropW, kDropItemH, kColorDropBg);
                const char* label = m_rightPanelOpen ? "Hide Modules" : "Show Modules";
                const bool ih = HitTest(dropX, dropY, kDropW, kDropItemH);
                if (ih) {
                    m_textRenderer.AddRect(dropX, dropY, kDropW, kDropItemH, kColorBtnHover);
                }
                m_textRenderer.AddText(m_font, label, dropX + 8.0f, dropY + 4.0f, kColorText);
                if (ih && m_mouseJustPressed) {
                    m_rightPanelOpen = !m_rightPanelOpen;
                    m_openMenu = -1;
                }
                break;
            }
            case 3: { // Presets
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
                        if (ih) {
                            m_textRenderer.AddRect(dropX, iy, kDropW, kDropItemH, kColorBtnHover);
                        }
                        m_textRenderer.AddText(m_font, m_presetNames[j].c_str(),
                                               dropX + 8.0f, iy + 4.0f, kColorText);
                        if (ih && m_mouseJustPressed) {
                            if (onLoadPreset) onLoadPreset(j);
                            m_openMenu = -1;
                        }
                    }
                }
                break;
            }
            case 4: { // View
                const char* items[] = {"Toggle Bloom", "Toggle Grid", "Toggle Stats", "Toggle Panels"};
                constexpr int kItemCount = 4;
                m_textRenderer.AddRect(dropX, dropY, kDropW, kItemCount * kDropItemH, kColorDropBg);
                for (int j = 0; j < kItemCount; ++j) {
                    const float iy = dropY + j * kDropItemH;
                    const bool ih = HitTest(dropX, iy, kDropW, kDropItemH);
                    if (ih) {
                        m_textRenderer.AddRect(dropX, iy, kDropW, kDropItemH, kColorBtnHover);
                    }
                    m_textRenderer.AddText(m_font, items[j], dropX + 8.0f, iy + 4.0f, kColorText);

                    if (ih && m_mouseJustPressed) {
                        if (j == 3) {
                            // Toggle both panels
                            const bool allOpen = m_leftPanelOpen && m_rightPanelOpen;
                            m_leftPanelOpen = !allOpen;
                            m_rightPanelOpen = !allOpen;
                        }
                        m_openMenu = -1;
                    }
                }
                break;
            }
            default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter Selector Bar
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildEmitterBar() {
    if (m_emitterNames.empty()) return;
    if (m_emitterNames.size() <= 1) return; // No bar needed for single emitter

    const float sw = static_cast<float>(m_screenW);
    const float barY = kMenuBarH;

    // Bar background
    m_textRenderer.AddRect(0.0f, barY, sw, kEmitterBarH, kColorBarBg);

    float x = 10.0f;
    constexpr float tabH = 22.0f;
    const float tabY = barY + (kEmitterBarH - tabH) * 0.5f;

    for (int i = 0; i < static_cast<int>(m_emitterNames.size()); ++i) {
        const float tabW = 8.0f + static_cast<float>(m_emitterNames[i].size()) * 8.0f;
        const bool isActive = (i == m_activeEmitterIdx);
        const bool hovered = HitTest(x, tabY, tabW, tabH);

        Vec4 color;
        if (isActive) {
            color = kColorAccent;
        } else if (hovered && m_mouseDown) {
            color = kColorBtnPress;
        } else if (hovered) {
            color = kColorBtnHover;
        } else {
            color = kColorBtnNormal;
        }

        m_textRenderer.AddRect(x, tabY, tabW, tabH, color);
        m_textRenderer.AddText(m_font, m_emitterNames[i].c_str(),
                               x + 4.0f, tabY + (tabH - 14.0f) * 0.5f, kColorText);

        if (hovered) m_wantsInput = true;

        if (hovered && m_mouseJustPressed && !isActive) {
            m_selectedEmitterIdx = i;
            if (onSelectEmitter) onSelectEmitter(i);
        }

        x += tabW + 4.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bottom Toolbar
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildToolbar(bool* burstMode, bool* vfxEnabled) {
    const float sw = static_cast<float>(m_screenW);
    const float sh = static_cast<float>(m_screenH);
    const float barY = sh - kToolbarH;

    // Bar background
    m_textRenderer.AddRect(0.0f, barY, sw, kToolbarH, kColorBarBg);

    float x = 10.0f;
    const float btnY = barY + 5.0f;
    constexpr float btnH = 30.0f;

    // VFX On/Off toggle
    {
        const char* label = (vfxEnabled && *vfxEnabled) ? "VFX: ON" : "VFX: OFF";
        const float w = 70.0f;
        if (Button(x, btnY, w, btnH, label)) {
            if (vfxEnabled) *vfxEnabled = !(*vfxEnabled);
        }
        x += w + 8.0f;
    }

    // Restart
    if (Button(x, btnY, 70.0f, btnH, "Restart")) {
        if (onRestart) onRestart();
    }
    x += 78.0f;

    // Burst
    if (Button(x, btnY, 60.0f, btnH, "Burst")) {
        if (onBurst) onBurst();
    }
    x += 68.0f;

    // Fireball
    if (Button(x, btnY, 70.0f, btnH, "Fireball")) {
        if (onFireball) onFireball();
    }
    x += 78.0f;

    // Burst Mode toggle
    {
        const char* label = (burstMode && *burstMode) ? "Mode:Burst" : "Mode:Cont";
        const float w = 90.0f;
        if (Button(x, btnY, w, btnH, label)) {
            if (burstMode) *burstMode = !(*burstMode);
        }
        x += w + 8.0f;
    }

    // Separator
    x += 8.0f;

    // Preset navigation
    if (Button(x, btnY, 30.0f, btnH, "<")) {
        // Previous preset
        if (onLoadPreset && !m_presetNames.empty()) {
            // Load preset index wrapping is handled by the callback
            static int presetIdx = 0;
            presetIdx = (presetIdx + static_cast<int>(m_presetNames.size()) - 1)
                        % static_cast<int>(m_presetNames.size());
            onLoadPreset(presetIdx);
        }
    }
    x += 38.0f;

    if (Button(x, btnY, 30.0f, btnH, ">")) {
        // Next preset
        if (onLoadPreset && !m_presetNames.empty()) {
            static int presetIdx = 0;
            presetIdx = (presetIdx + 1) % static_cast<int>(m_presetNames.size());
            onLoadPreset(presetIdx);
        }
    }
    x += 38.0f;

    // Show panels toggle
    x = sw - 180.0f;
    if (Button(x, btnY, 80.0f, btnH, "Props")) {
        m_leftPanelOpen = !m_leftPanelOpen;
    }
    x += 88.0f;
    if (Button(x, btnY, 80.0f, btnH, "Modules")) {
        m_rightPanelOpen = !m_rightPanelOpen;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Left Panel — Emitter Properties
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// VfxModuleDef helpers
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEditorUI::VfxModuleDef::IsActive() const {
    // If there is a uint enable flag, it takes priority
    if (enablePtr != nullptr) return *enablePtr != 0;
    // Otherwise check the float strength pointer
    if (strengthPtr != nullptr) return *strengthPtr > 0.0f;
    // Modules with no enable mechanism are always active
    return true;
}

void VFXEditorUI::VfxModuleDef::FormatSummary(char* buf, int bufSize) const {
    buf[0] = '\0';
    if (params.empty()) return;

    // Show the first param's current value as the summary
    const auto& p = params[0];
    switch (p.type) {
        case VfxModuleParam::Float:
            if (p.fPtr != nullptr) {
                std::snprintf(buf, static_cast<size_t>(bufSize), "%.2f", static_cast<double>(*p.fPtr));
            }
            break;
        case VfxModuleParam::Uint:
            if (p.uPtr != nullptr) {
                std::snprintf(buf, static_cast<size_t>(bufSize), "%u", *p.uPtr);
            }
            break;
        case VfxModuleParam::Bool:
            if (p.uPtr != nullptr) {
                std::snprintf(buf, static_cast<size_t>(bufSize), "%s", *p.uPtr ? "On" : "Off");
            }
            break;
        case VfxModuleParam::Enum:
            if (p.uPtr != nullptr && p.enumNames != nullptr && *p.uPtr < p.enumCount) {
                std::snprintf(buf, static_cast<size_t>(bufSize), "%s", p.enumNames[*p.uPtr]);
            }
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildModuleList — populate m_modules from VFXEmitterConfig pointers
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildModuleList(VFXEmitterConfig* cfg, float* spawnRate, uint32_t* burstCount) {
    m_modules.clear();
    m_expandedModuleIndex = -1;

    // Helper lambdas for adding params
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
    auto addBool = [](std::vector<VfxModuleParam>& v, const char* label, uint32_t* ptr) {
        VfxModuleParam p;
        p.label = label; p.type = VfxModuleParam::Bool;
        p.uPtr = ptr;
        v.push_back(p);
    };

    // ── Emitter Spawn ────────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Burst Config"; m.stage = VfxStage::EmitterSpawn;
        addFloat(m.params, "Rate", spawnRate, 1.0f, 5000.0f);
        // burstCount is uint32 — wrap as float param with helper in BuildStageTree
        m.params.push_back({});
        auto& bp = m.params.back();
        bp.label = "Burst Count"; bp.type = VfxModuleParam::Uint;
        bp.uPtr = burstCount; bp.min = 1.0f; bp.max = 5000.0f;
        m_modules.push_back(std::move(m));
    }

    // ── Emitter Update ───────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Emitter Lifecycle"; m.stage = VfxStage::EmitterUpdate;
        addFloat(m.params, "Duration", &cfg->emitterDuration, 0.0f, 30.0f);
        m.params.push_back({});
        auto& lp = m.params.back();
        lp.label = "Loop Count"; lp.type = VfxModuleParam::Uint;
        lp.uPtr = &cfg->loopCount; lp.min = 0.0f; lp.max = 100.0f;
        m_modules.push_back(std::move(m));
    }

    // ── Particle Spawn ───────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Initialize Particle"; m.stage = VfxStage::ParticleSpawn;
        addFloat(m.params, "Size", &cfg->initialSize, 0.001f, 5.0f);
        addFloat(m.params, "Min Life", &cfg->minLifetime, 0.1f, 30.0f);
        addFloat(m.params, "Max Life", &cfg->maxLifetime, 0.1f, 30.0f);
        addFloat(m.params, "Color R", &cfg->initialColor[0], 0.0f, 1.0f);
        addFloat(m.params, "Color G", &cfg->initialColor[1], 0.0f, 1.0f);
        addFloat(m.params, "Color B", &cfg->initialColor[2], 0.0f, 1.0f);
        addFloat(m.params, "Color A", &cfg->initialColor[3], 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Shape Location"; m.stage = VfxStage::ParticleSpawn;
        addEnum(m.params, "Shape", &cfg->spawnShape, kSpawnShapeNames, kSpawnShapeCount);
        addFloat(m.params, "Radius", &cfg->spawnRadius, 0.0f, 10.0f);
        addBool(m.params, "Surface", &cfg->spawnOnSurface);
        addFloat(m.params, "Param1", &cfg->shapeParam1, -20.0f, 20.0f);
        addFloat(m.params, "Param2", &cfg->shapeParam2, -20.0f, 20.0f);
        addFloat(m.params, "Param3", &cfg->shapeParam3, -20.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Init Velocity"; m.stage = VfxStage::ParticleSpawn;
        addFloat(m.params, "Vel Z", &cfg->spawnVelocity[2], -50.0f, 50.0f);
        addFloat(m.params, "Vel Y", &cfg->spawnVelocity[1], -50.0f, 50.0f);
        addFloat(m.params, "Vel X", &cfg->spawnVelocity[0], -50.0f, 50.0f);
        addFloat(m.params, "Variation", &cfg->velocityVariation, 0.0f, 20.0f);
        addFloat(m.params, "Spread", &cfg->spreadAngle, 0.0f, 3.14159f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Init Rotation"; m.stage = VfxStage::ParticleSpawn;
        addFloat(m.params, "Rot Min", &cfg->initialRotationMin, 0.0f, 6.284f);
        addFloat(m.params, "Rot Max", &cfg->initialRotationMax, 0.0f, 6.284f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Velocity Inherit"; m.stage = VfxStage::ParticleSpawn;
        m.strengthPtr = &cfg->velocityInheritance;
        addFloat(m.params, "Factor", &cfg->velocityInheritance, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }

    // ── Particle Update ──────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Gravity"; m.stage = VfxStage::ParticleUpdate;
        addFloat(m.params, "Gravity Z", &cfg->gravity[2], -100.0f, 100.0f);
        addFloat(m.params, "Gravity Y", &cfg->gravity[1], -100.0f, 100.0f);
        addFloat(m.params, "Gravity X", &cfg->gravity[0], -100.0f, 100.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Damping"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &cfg->damping;
        addFloat(m.params, "Damping", &cfg->damping, 0.0f, 2.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Drag"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &cfg->dragCoefficient;
        addFloat(m.params, "Drag", &cfg->dragCoefficient, 0.0f, 10.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Curl Noise"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &cfg->curlStrength;
        addFloat(m.params, "Strength", &cfg->curlStrength, 0.0f, 10.0f);
        addFloat(m.params, "Frequency", &cfg->curlFrequency, 0.01f, 5.0f);
        addFloat(m.params, "Speed", &cfg->curlSpeed, 0.0f, 5.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Vortex Force"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &cfg->vortexTangentialStrength;
        addFloat(m.params, "Tangential", &cfg->vortexTangentialStrength, -20.0f, 20.0f);
        addFloat(m.params, "Radial", &cfg->vortexRadialStrength, -20.0f, 20.0f);
        addFloat(m.params, "Max Radius", &cfg->vortexMaxRadius, 0.1f, 50.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Wind"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &cfg->windStrength;
        addFloat(m.params, "Strength", &cfg->windStrength, 0.0f, 50.0f);
        addFloat(m.params, "Dir X", &cfg->windDir[0], -1.0f, 1.0f);
        addFloat(m.params, "Dir Y", &cfg->windDir[1], -1.0f, 1.0f);
        addFloat(m.params, "Dir Z", &cfg->windDir[2], -1.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Point Force"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &cfg->pointForceEnabled;
        m.strengthPtr = &cfg->pointForceStrength;
        addFloat(m.params, "Strength", &cfg->pointForceStrength, -50.0f, 50.0f);
        addFloat(m.params, "Pos X", &cfg->pointForceX, -50.0f, 50.0f);
        addFloat(m.params, "Pos Y", &cfg->pointForceY, -50.0f, 50.0f);
        addFloat(m.params, "Pos Z", &cfg->pointForceZ, -50.0f, 50.0f);
        addFloat(m.params, "Radius", &cfg->pointForceRadius, 0.1f, 100.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Vector Field"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &cfg->vectorFieldEnabled;
        m.strengthPtr = &cfg->vectorFieldStrength;
        addFloat(m.params, "Strength", &cfg->vectorFieldStrength, 0.0f, 20.0f);
        addFloat(m.params, "Tightness", &cfg->vectorFieldTightness, 0.0f, 1.0f);
        addFloat(m.params, "Scale", &cfg->vectorFieldScale, 0.01f, 10.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Orbit"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &cfg->orbitEnabled;
        addFloat(m.params, "Radius", &cfg->orbitRadius, 0.0f, 20.0f);
        addFloat(m.params, "Speed", &cfg->orbitSpeed, -20.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Ground Collision"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &cfg->groundCollision;
        addEnum(m.params, "Response", &cfg->collisionResponse,
                kCollisionResponseNames, kCollisionResponseCount);
        addFloat(m.params, "Restitution", &cfg->restitution, 0.0f, 1.0f);
        addFloat(m.params, "Friction", &cfg->collisionFriction, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Terrain Collision"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &cfg->terrainCollisionEnabled;
        addFloat(m.params, "Amplitude", &cfg->terrainAmplitude, 0.0f, 20.0f);
        addFloat(m.params, "Frequency", &cfg->terrainFrequency, 0.001f, 2.0f);
        addFloat(m.params, "Base Height", &cfg->terrainBaseHeight, -20.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Rotation Rate"; m.stage = VfxStage::ParticleUpdate;
        m.strengthPtr = &cfg->rotationRate;
        addFloat(m.params, "Rate", &cfg->rotationRate, -20.0f, 20.0f);
        addFloat(m.params, "Life Start", &cfg->rotationOverLifeStart, -20.0f, 20.0f);
        addFloat(m.params, "Life End", &cfg->rotationOverLifeEnd, -20.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Acceleration"; m.stage = VfxStage::ParticleUpdate;
        addFloat(m.params, "Accel X", &cfg->accelX, -50.0f, 50.0f);
        addFloat(m.params, "Accel Y", &cfg->accelY, -50.0f, 50.0f);
        addFloat(m.params, "Accel Z", &cfg->accelZ, -50.0f, 50.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Death Spawn"; m.stage = VfxStage::ParticleUpdate;
        m.enablePtr = &cfg->deathSpawnEnabled;
        m.params.push_back({});
        auto& dc = m.params.back();
        dc.label = "Count"; dc.type = VfxModuleParam::Uint;
        dc.uPtr = &cfg->deathSpawnCount; dc.min = 1.0f; dc.max = 32.0f;
        addFloat(m.params, "Spread", &cfg->deathSpawnSpread, 0.0f, 3.14159f);
        addFloat(m.params, "Speed Scale", &cfg->deathSpawnSpeedScale, 0.0f, 2.0f);
        m_modules.push_back(std::move(m));
    }

    // ── Render ───────────────────────────────────────────────────────────
    {
        VfxModuleDef m;
        m.name = "Billboard"; m.stage = VfxStage::Render;
        addEnum(m.params, "Facing", &cfg->facingMode, kAlignmentModeNames, kAlignmentModeCount);
        addFloat(m.params, "Stretch", &cfg->stretchFactor, 0.0f, 5.0f);
        addFloat(m.params, "Camera Offset", &cfg->cameraOffset, -5.0f, 5.0f);
        addFloat(m.params, "Pivot X", &cfg->pivotOffsetX, -1.0f, 1.0f);
        addFloat(m.params, "Pivot Y", &cfg->pivotOffsetY, -1.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Size Over Life"; m.stage = VfxStage::Render;
        addFloat(m.params, "Start", &cfg->sizeOverLifeStart, 0.0f, 5.0f);
        addFloat(m.params, "Peak", &cfg->sizeOverLifePeak, 0.0f, 5.0f);
        addFloat(m.params, "End", &cfg->sizeOverLifeEnd, 0.0f, 5.0f);
        addFloat(m.params, "Peak Time", &cfg->sizeOverLifePeakTime, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Color Over Life"; m.stage = VfxStage::Render;
        addFloat(m.params, "Start R", &cfg->colorLifeStartR, 0.0f, 1.0f);
        addFloat(m.params, "Start G", &cfg->colorLifeStartG, 0.0f, 1.0f);
        addFloat(m.params, "Start B", &cfg->colorLifeStartB, 0.0f, 1.0f);
        addFloat(m.params, "Start A", &cfg->colorLifeStartA, 0.0f, 1.0f);
        addFloat(m.params, "End R", &cfg->colorLifeEndR, 0.0f, 1.0f);
        addFloat(m.params, "End G", &cfg->colorLifeEndG, 0.0f, 1.0f);
        addFloat(m.params, "End B", &cfg->colorLifeEndB, 0.0f, 1.0f);
        addFloat(m.params, "End A", &cfg->colorLifeEndA, 0.0f, 1.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Size by Speed"; m.stage = VfxStage::Render;
        m.enablePtr = &cfg->sizeBySpeedEnabled;
        addFloat(m.params, "Min Size", &cfg->sizeBySpeedMin, 0.0f, 5.0f);
        addFloat(m.params, "Max Size", &cfg->sizeBySpeedMax, 0.0f, 10.0f);
        addFloat(m.params, "Speed Max", &cfg->speedRangeMax, 0.1f, 100.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Color by Speed"; m.stage = VfxStage::Render;
        m.enablePtr = &cfg->colorBySpeedEnabled;
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Emissive"; m.stage = VfxStage::Render;
        m.strengthPtr = &cfg->emissiveIntensity;
        addFloat(m.params, "Intensity", &cfg->emissiveIntensity, 0.0f, 20.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Soft Fade"; m.stage = VfxStage::Render;
        addFloat(m.params, "Distance", &cfg->softFadeDistance, 0.0f, 5.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Lit Mode"; m.stage = VfxStage::Render;
        m.enablePtr = &cfg->litMode;
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Blend Mode"; m.stage = VfxStage::Render;
        addEnum(m.params, "Mode", &cfg->blendMode, kBlendModeNames, kBlendModeCount);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Screen Space Size"; m.stage = VfxStage::Render;
        m.enablePtr = &cfg->screenSpaceSizeEnabled;
        addFloat(m.params, "Pixel Size", &cfg->screenSpacePixelSize, 1.0f, 64.0f);
        m_modules.push_back(std::move(m));
    }
    {
        VfxModuleDef m;
        m.name = "Particle Light"; m.stage = VfxStage::Render;
        m.enablePtr = &cfg->particleLightEnabled;
        addFloat(m.params, "Radius", &cfg->particleLightRadius, 1.0f, 2000.0f);
        addFloat(m.params, "Intensity", &cfg->particleLightIntensity, 0.0f, 20.0f);
        addFloat(m.params, "Flicker", &cfg->particleLightFlicker, 0.0f, 1.0f);
        addFloat(m.params, "Flicker Speed", &cfg->particleLightFlickerSpeed, 0.1f, 30.0f);
        m_modules.push_back(std::move(m));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildStageTree — Niagara-style hierarchical stage/module panel
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildStageTree(VFXEmitterConfig* cfg, float* spawnRate, uint32_t* burstCount) {
    (void)cfg; (void)spawnRate; (void)burstCount;

    const float panelX = 0.0f;
    const float panelY = kMenuBarH;
    const float panelH = static_cast<float>(m_screenH) - kMenuBarH - kToolbarH;
    const float contentW = kLeftPanelW - 2.0f * kPanelPadX;

    // Panel background
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, panelH, kColorPanelBg);

    // Title bar
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, kCatHeaderH, kColorBarBg);
    m_textRenderer.AddText(m_font, "Module Stack", panelX + 8.0f, panelY + 5.0f, kColorText);

    // Close button [X]
    {
        const float closeX = panelX + kLeftPanelW - 24.0f;
        const float closeY = panelY + 2.0f;
        if (Button(closeX, closeY, 20.0f, 20.0f, "X")) {
            m_leftPanelOpen = false;
            return;
        }
    }

    // Scrollable content area
    const float contentTop = panelY + kCatHeaderH + kPanelPadY;
    const float contentBot = static_cast<float>(m_screenH) - kToolbarH;
    const float visibleH = contentBot - contentTop;

    // Handle scroll wheel when mouse is over left panel
    if (HitTest(panelX, contentTop, kLeftPanelW, visibleH) && m_scrollDelta != 0.0f) {
        m_leftPanelScroll -= m_scrollDelta * kScrollSpeed;
    }

    // First pass: compute total content height for scroll clamping
    float totalH = 0.0f;
    {
        static const char* kStageNames[] = {
            "Emitter Spawn", "Emitter Update", "Particle Spawn", "Particle Update", "Render"
        };

        for (int si = 0; si < kStageCount; ++si) {
            (void)kStageNames[si];
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
    }
    m_leftPanelContentH = totalH;

    // Clamp scroll so content doesn't overscroll
    const float maxScroll = std::max(0.0f, totalH - visibleH);
    m_leftPanelScroll = std::clamp(m_leftPanelScroll, 0.0f, maxScroll);

    // Second pass: render content with scroll offset
    const float cx = panelX + kPanelPadX;
    float y = contentTop - m_leftPanelScroll;

    static const char* kStageNames[] = {
        "Emitter Spawn", "Emitter Update", "Particle Spawn", "Particle Update", "Render"
    };

    for (int si = 0; si < kStageCount; ++si) {
        const VfxStage stage = static_cast<VfxStage>(si);

        // ── Stage header ─────────────────────────────────────────────
        if (y + kStageHeaderH > contentTop && y < contentBot) {
            const bool hdrHovered = HitTest(cx, std::max(y, contentTop), contentW,
                                            std::min(kStageHeaderH, contentBot - y));
            const Vec4 hdrColor = hdrHovered ? kColorBtnHover : kColorStageHdr;
            m_textRenderer.AddRect(cx, y, contentW, kStageHeaderH, hdrColor);

            const char* arrow = m_stageExpanded[si] ? "\xE2\x96\xBC" : "\xE2\x96\xB6"; // ▼ / ▶
            char hdrLabel[80];
            std::snprintf(hdrLabel, sizeof(hdrLabel), "%s  %s", arrow, kStageNames[si]);
            m_textRenderer.AddText(m_font, hdrLabel, cx + 4.0f, y + 6.0f, kColorText);

            if (hdrHovered && m_mouseJustPressed) {
                m_stageExpanded[si] = !m_stageExpanded[si];
            }
        }
        y += kStageHeaderH + 2.0f;

        if (!m_stageExpanded[si]) continue;

        // ── Module rows for this stage ───────────────────────────────
        for (int mi = 0; mi < static_cast<int>(m_modules.size()); ++mi) {
            auto& mod = m_modules[mi];
            if (mod.stage != stage) continue;

            const bool isExpanded = (mi == m_expandedModuleIndex);
            const bool isActive = mod.IsActive();

            // Module row: checkbox + name + summary
            if (y + kModuleRowH > contentTop && y < contentBot) {
                const Vec4 rowBg = isExpanded ? kColorModuleExp : kColorModuleRow;
                m_textRenderer.AddRect(cx + 2.0f, y, contentW - 4.0f, kModuleRowH, rowBg);

                // Checkbox (14x14)
                const float cbX = cx + 6.0f;
                const float cbY = y + (kModuleRowH - kCheckboxSize) * 0.5f;
                const Vec4 cbColor = isActive ? kColorCheckOn : kColorCheckOff;
                m_textRenderer.AddRect(cbX, cbY, kCheckboxSize, kCheckboxSize, cbColor);

                // Checkmark indicator when active
                if (isActive) {
                    m_textRenderer.AddRect(cbX + 3.0f, cbY + 3.0f,
                                           kCheckboxSize - 6.0f, kCheckboxSize - 6.0f,
                                           kColorText);
                }

                // Checkbox click
                if (HitTest(cbX, cbY, kCheckboxSize + 4.0f, kCheckboxSize + 4.0f) && m_mouseJustPressed) {
                    if (isActive) {
                        // Deactivate: save and zero
                        if (mod.enablePtr != nullptr) {
                            mod.savedEnable = *mod.enablePtr;
                            if (mod.savedEnable == 0) mod.savedEnable = 1;
                            *mod.enablePtr = 0;
                        }
                        if (mod.strengthPtr != nullptr) {
                            mod.savedStrength = *mod.strengthPtr;
                            if (mod.savedStrength == 0.0f) mod.savedStrength = 1.0f;
                            *mod.strengthPtr = 0.0f;
                        }
                    } else {
                        // Reactivate: restore saved
                        if (mod.enablePtr != nullptr) {
                            *mod.enablePtr = (mod.savedEnable != 0) ? mod.savedEnable : 1u;
                        }
                        if (mod.strengthPtr != nullptr) {
                            *mod.strengthPtr = (mod.savedStrength > 0.0f) ? mod.savedStrength : 1.0f;
                        }
                    }
                }

                // Module name — click to expand/collapse
                const float nameX = cbX + kCheckboxSize + 6.0f;
                const Vec4 nameColor = isActive ? kColorText : kColorTextDim;
                m_textRenderer.AddText(m_font, mod.name, nameX, y + 4.0f, nameColor);

                // Summary text on the right side of the row
                char summary[32];
                mod.FormatSummary(summary, sizeof(summary));
                if (summary[0] != '\0') {
                    // Right-align the summary roughly
                    const float sumX = cx + contentW - 8.0f - static_cast<float>(std::strlen(summary)) * 7.0f;
                    m_textRenderer.AddText(m_font, summary, std::max(sumX, nameX + 80.0f),
                                           y + 4.0f, kColorSummary);
                }

                // Expand indicator
                if (isExpanded && !mod.params.empty()) {
                    m_textRenderer.AddText(m_font, "\xE2\x96\xBC", cx + contentW - 16.0f, y + 4.0f, kColorAccent);
                }

                // Click name area to expand/collapse params (accordion)
                const float nameHitW = contentW - kCheckboxSize - 20.0f;
                if (HitTest(nameX, y, nameHitW, kModuleRowH) && m_mouseJustPressed) {
                    m_expandedModuleIndex = isExpanded ? -1 : mi;
                }
            }
            y += kModuleRowH + 1.0f;

            // ── Expanded module parameters ───────────────────────────
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
                            if (param.fPtr != nullptr) {
                                Slider(px, y, pw, param.fPtr, param.min, param.max, param.label);
                            }
                            break;
                        case VfxModuleParam::Uint:
                            if (param.uPtr != nullptr) {
                                // Render uint as float slider, write back clamped
                                float fVal = static_cast<float>(*param.uPtr);
                                if (Slider(px, y, pw, &fVal, param.min, param.max, param.label)) {
                                    *param.uPtr = static_cast<uint32_t>(
                                        std::clamp(fVal, param.min, param.max));
                                }
                            }
                            break;
                        case VfxModuleParam::Bool:
                            if (param.uPtr != nullptr) {
                                bool bVal = (*param.uPtr != 0);
                                if (Toggle(px, y, &bVal, param.label)) {
                                    *param.uPtr = bVal ? 1u : 0u;
                                }
                            }
                            break;
                        case VfxModuleParam::Enum:
                            if (param.uPtr != nullptr && param.enumNames != nullptr) {
                                DropdownTrigger(px, y, pw, param.uPtr,
                                                param.enumNames, param.enumCount, param.label);
                            }
                            break;
                    }
                    y += kSliderH + 2.0f;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Right Panel — Module Stack
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildRightPanel(VFXEmitterConfig* cfg) {
    (void)cfg;

    const float sw = static_cast<float>(m_screenW);
    const float panelX = sw - kRightPanelW;
    const float panelY = kMenuBarH;
    const float panelH = static_cast<float>(m_screenH) - kMenuBarH - kToolbarH;
    const float contentW = kRightPanelW - 2.0f * kPanelPadX;
    const float cx = panelX + kPanelPadX;

    // Panel background
    m_textRenderer.AddRect(panelX, panelY, kRightPanelW, panelH, kColorPanelBg);

    // Title bar
    m_textRenderer.AddRect(panelX, panelY, kRightPanelW, kCatHeaderH, kColorBarBg);

    // Close button [X]
    {
        const float closeX = panelX + kRightPanelW - 24.0f;
        const float closeY = panelY + 2.0f;
        if (Button(closeX, closeY, 20.0f, 20.0f, "X")) {
            m_rightPanelOpen = false;
        }
    }

    float y = panelY + kCatHeaderH + kPanelPadY;
    const float contentBot = static_cast<float>(m_screenH) - kToolbarH;

    // Show expanded module's parameters as large sliders, or a hint
    if (m_expandedModuleIndex < 0 ||
        m_expandedModuleIndex >= static_cast<int>(m_modules.size())) {
        m_textRenderer.AddText(m_font, "Module Detail", panelX + 8.0f, panelY + 5.0f, kColorText);
        m_textRenderer.AddText(m_font, "Select a module", cx, y + 4.0f, kColorTextDim);
        return;
    }

    auto& mod = m_modules[m_expandedModuleIndex];

    // Title shows the module name
    char title[64];
    std::snprintf(title, sizeof(title), "%s", mod.name);
    m_textRenderer.AddText(m_font, title, panelX + 8.0f, panelY + 5.0f, kColorText);

    // Active state indicator
    const bool isActive = mod.IsActive();
    const Vec4 statusColor = isActive ? kColorCheckOn : kColorCheckOff;
    m_textRenderer.AddRect(cx, y, 8.0f, 8.0f, statusColor);
    m_textRenderer.AddText(m_font, isActive ? "Active" : "Inactive",
                           cx + 14.0f, y, isActive ? kColorText : kColorTextDim);
    y += kRowH + 4.0f;

    // Render all parameters as full-width sliders
    for (auto& param : mod.params) {
        if (y + kSliderH + 6.0f > contentBot) break;

        // Parameter label
        m_textRenderer.AddText(m_font, param.label, cx, y + 2.0f, kColorText);
        y += kRowH;

        switch (param.type) {
            case VfxModuleParam::Float:
                if (param.fPtr != nullptr) {
                    Slider(cx, y, contentW, param.fPtr, param.min, param.max, "");
                }
                break;
            case VfxModuleParam::Uint:
                if (param.uPtr != nullptr) {
                    float fVal = static_cast<float>(*param.uPtr);
                    if (Slider(cx, y, contentW, &fVal, param.min, param.max, "")) {
                        *param.uPtr = static_cast<uint32_t>(
                            std::clamp(fVal, param.min, param.max));
                    }
                }
                break;
            case VfxModuleParam::Bool:
                if (param.uPtr != nullptr) {
                    bool bVal = (*param.uPtr != 0);
                    if (Toggle(cx, y, &bVal, param.label)) {
                        *param.uPtr = bVal ? 1u : 0u;
                    }
                }
                break;
            case VfxModuleParam::Enum:
                if (param.uPtr != nullptr && param.enumNames != nullptr) {
                    DropdownTrigger(cx, y, contentW, param.uPtr,
                                    param.enumNames, param.enumCount, "");
                }
                break;
        }
        y += kSliderH + 6.0f;
    }
}

} // namespace uz
