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

static const char* const kAlignmentModeNames[] = {
    "CameraFacing", "VelocityAligned"
};
static constexpr uint32_t kAlignmentModeCount = 2;

static const char* const kLitModeNames[] = {
    "Unlit", "Lit"
};
static constexpr uint32_t kLitModeCount = 2;

static const char* const kBoolEnumNames[] = {
    "Off", "On"
};
static constexpr uint32_t kBoolEnumCount = 2;

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

    // Layer 1: Background panels + toolbar (drawn first = behind)
    BuildMenuBar();
    BuildToolbar(burstMode, vfxEnabled);

    if (m_leftPanelOpen) {
        BuildLeftPanel(config, spawnRate, burstCount);
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
        *value = minVal + t * range;
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

void VFXEditorUI::BuildLeftPanel(VFXEmitterConfig* cfg, float* spawnRate, uint32_t* burstCount) {
    const float panelX = 0.0f;
    const float panelY = kMenuBarH;
    const float panelH = static_cast<float>(m_screenH) - kMenuBarH - kToolbarH;
    const float contentW = kLeftPanelW - 2.0f * kPanelPadX;

    // Panel background
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, panelH, kColorPanelBg);

    // Title bar
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, kCatHeaderH, kColorBarBg);
    m_textRenderer.AddText(m_font, "Emitter Properties", panelX + 8.0f, panelY + 5.0f, kColorText);

    // Close button [X]
    {
        const float closeX = panelX + kLeftPanelW - 24.0f;
        const float closeY = panelY + 2.0f;
        if (Button(closeX, closeY, 20.0f, 20.0f, "X")) {
            m_leftPanelOpen = false;
        }
    }

    float y = panelY + kCatHeaderH + kPanelPadY;
    const float cx = panelX + kPanelPadX;

    // Category definitions with their parameters
    struct CatDef {
        const char* name;
        int id;
    };
    static const CatDef kCategories[] = {
        {"Spawn",        0},
        {"Particle",     1},
        {"Color",        2},
        {"Size Over Life", 3},
        {"Physics",      4},
        {"Forces",       5},
        {"Rendering",    6},
    };
    constexpr int kCatCount = 7;

    for (int ci = 0; ci < kCatCount; ++ci) {
        if (y > static_cast<float>(m_screenH) - kToolbarH - kCatHeaderH) break;

        // Category header
        const bool catHovered = HitTest(cx, y, contentW, kCatHeaderH);
        const Vec4 catColor = catHovered ? kColorBtnHover : kColorCatHeader;
        m_textRenderer.AddRect(cx, y, contentW, kCatHeaderH, catColor);

        const char* arrow = (m_expandedCategory == ci) ? "[-]" : "[+]";
        char catLabel[64];
        std::snprintf(catLabel, sizeof(catLabel), "%s %s", arrow, kCategories[ci].name);
        m_textRenderer.AddText(m_font, catLabel, cx + 4.0f, y + 5.0f, kColorText);

        if (catHovered && m_mouseJustPressed) {
            m_expandedCategory = (m_expandedCategory == ci) ? -1 : ci;
        }

        y += kCatHeaderH + 2.0f;

        // Skip parameters if category is collapsed
        if (m_expandedCategory != ci) continue;

        // Render category parameters based on ID
        switch (ci) {
            case 0: { // Spawn
                Slider(cx, y, contentW, spawnRate, 1.0f, 5000.0f, "Rate");
                y += kSliderH + 2.0f;

                // Burst count as slider (cast uint32 to float, write back)
                {
                    float fBurst = static_cast<float>(*burstCount);
                    if (Slider(cx, y, contentW, &fBurst, 1.0f, 5000.0f, "Burst#")) {
                        *burstCount = static_cast<uint32_t>(std::clamp(fBurst, 1.0f, 5000.0f));
                    }
                }
                y += kSliderH + 2.0f;

                Slider(cx, y, contentW, &cfg->spawnRadius, 0.0f, 10.0f, "Radius");
                y += kSliderH + 2.0f;

                DropdownTrigger(cx, y, contentW, &cfg->spawnShape,
                                kSpawnShapeNames, kSpawnShapeCount, "Shape");
                y += kRowH + 2.0f;

                DropdownTrigger(cx, y, contentW, &cfg->spawnOnSurface,
                                kBoolEnumNames, kBoolEnumCount, "Surface");
                y += kRowH + 2.0f;

                Slider(cx, y, contentW, &cfg->shapeParam1, -20.0f, 20.0f, "Param1");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->shapeParam2, -20.0f, 20.0f, "Param2");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->shapeParam3, -20.0f, 20.0f, "Param3");
                y += kSliderH + 2.0f;
                break;
            }
            case 1: { // Particle
                Slider(cx, y, contentW, &cfg->minLifetime, 0.1f, 30.0f, "MinLife");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->maxLifetime, 0.1f, 30.0f, "MaxLife");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->initialSize, 0.001f, 5.0f, "Size");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->velocityVariation, 0.0f, 20.0f, "VelVar");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->spreadAngle, 0.0f, 3.14159f, "Spread");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->spawnVelocity[2], -50.0f, 50.0f, "VelZ");
                y += kSliderH + 2.0f;
                break;
            }
            case 2: { // Color
                Slider(cx, y, contentW, &cfg->colorLifeStartR, 0.0f, 1.0f, "Start R");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeStartG, 0.0f, 1.0f, "Start G");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeStartB, 0.0f, 1.0f, "Start B");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeStartA, 0.0f, 1.0f, "Start A");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeEndR, 0.0f, 1.0f, "End R");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeEndG, 0.0f, 1.0f, "End G");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeEndB, 0.0f, 1.0f, "End B");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->colorLifeEndA, 0.0f, 1.0f, "End A");
                y += kSliderH + 2.0f;
                break;
            }
            case 3: { // Size Over Life
                Slider(cx, y, contentW, &cfg->sizeOverLifeStart, 0.0f, 5.0f, "Start");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->sizeOverLifePeak, 0.0f, 5.0f, "Peak");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->sizeOverLifeEnd, 0.0f, 5.0f, "End");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->sizeOverLifePeakTime, 0.0f, 1.0f, "PeakT");
                y += kSliderH + 2.0f;
                break;
            }
            case 4: { // Physics
                Slider(cx, y, contentW, &cfg->gravity[2], -100.0f, 100.0f, "GravZ");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->damping, 0.0f, 2.0f, "Damping");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->curlStrength, 0.0f, 10.0f, "Curl");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->curlFrequency, 0.01f, 5.0f, "CurlFreq");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->curlSpeed, 0.0f, 5.0f, "CurlSpd");
                y += kSliderH + 2.0f;
                DropdownTrigger(cx, y, contentW, &cfg->groundCollision,
                                kBoolEnumNames, kBoolEnumCount, "Ground");
                y += kRowH + 2.0f;
                Slider(cx, y, contentW, &cfg->restitution, 0.0f, 1.0f, "Bounce");
                y += kSliderH + 2.0f;
                break;
            }
            case 5: { // Forces
                Slider(cx, y, contentW, &cfg->vortexTangentialStrength, -20.0f, 20.0f, "Swirl");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->vortexRadialStrength, -20.0f, 20.0f, "Radial");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->vortexMaxRadius, 0.1f, 50.0f, "VortRad");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->windStrength, 0.0f, 50.0f, "Wind");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->windDir[0], -1.0f, 1.0f, "WndX");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->windDir[1], -1.0f, 1.0f, "WndY");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->windDir[2], -1.0f, 1.0f, "WndZ");
                y += kSliderH + 2.0f;
                break;
            }
            case 6: { // Rendering
                DropdownTrigger(cx, y, contentW, &cfg->blendMode,
                                kBlendModeNames, kBlendModeCount, "Blend");
                y += kRowH + 2.0f;
                Slider(cx, y, contentW, &cfg->emissiveIntensity, 0.0f, 20.0f, "Emissive");
                y += kSliderH + 2.0f;
                Slider(cx, y, contentW, &cfg->softFadeDistance, 0.0f, 5.0f, "SoftFade");
                y += kSliderH + 2.0f;
                DropdownTrigger(cx, y, contentW, &cfg->alignmentMode,
                                kAlignmentModeNames, kAlignmentModeCount, "Align");
                y += kRowH + 2.0f;
                Slider(cx, y, contentW, &cfg->stretchFactor, 0.0f, 5.0f, "Stretch");
                y += kSliderH + 2.0f;
                DropdownTrigger(cx, y, contentW, &cfg->litMode,
                                kLitModeNames, kLitModeCount, "Lit");
                y += kRowH + 2.0f;
                break;
            }
            default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Right Panel — Module Stack
// ─────────────────────────────────────────────────────────────────────────────

void VFXEditorUI::BuildRightPanel(VFXEmitterConfig* cfg) {
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
    m_textRenderer.AddText(m_font, "Modules", panelX + 8.0f, panelY + 5.0f, kColorText);

    // Close button [X]
    {
        const float closeX = panelX + kRightPanelW - 24.0f;
        const float closeY = panelY + 2.0f;
        if (Button(closeX, closeY, 20.0f, 20.0f, "X")) {
            m_rightPanelOpen = false;
        }
    }

    float y = panelY + kCatHeaderH + kPanelPadY;

    // Module rows — each is a toggle + label + optional inline param
    struct ModuleDef {
        const char* name;
        float* strengthPtr;   ///< Float param to show, or nullptr.
        float  minVal;
        float  maxVal;
    };

    // We use the config pointers directly for inline editing
    const ModuleDef modules[] = {
        {"Gravity Z",        &cfg->gravity[2],                -100.0f, 100.0f},
        {"Damping",          &cfg->damping,                   0.0f,    2.0f},
        {"Curl Noise",       &cfg->curlStrength,              0.0f,    10.0f},
        {"Vortex Swirl",     &cfg->vortexTangentialStrength,  -20.0f,  20.0f},
        {"Vortex Radial",    &cfg->vortexRadialStrength,      -20.0f,  20.0f},
        {"Wind",             &cfg->windStrength,              0.0f,    50.0f},
        {"Restitution",      &cfg->restitution,               0.0f,    1.0f},
        {"Emissive",         &cfg->emissiveIntensity,         0.0f,    20.0f},
        {"Soft Fade",        &cfg->softFadeDistance,           0.0f,    5.0f},
    };
    constexpr int kModuleCount = 9;

    for (int i = 0; i < kModuleCount; ++i) {
        if (y > static_cast<float>(m_screenH) - kToolbarH - kRowH) break;

        // Module name label
        m_textRenderer.AddText(m_font, modules[i].name, cx, y + 3.0f, kColorText);
        y += kRowH;

        // Inline slider for the strength parameter
        if (modules[i].strengthPtr != nullptr) {
            Slider(cx, y, contentW, modules[i].strengthPtr,
                   modules[i].minVal, modules[i].maxVal, "");
            y += kSliderH + 4.0f;
        }
    }
}

} // namespace uz
