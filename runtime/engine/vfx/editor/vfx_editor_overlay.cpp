/// VFX Editor Overlay — Implementation
///
/// Mouse-driven visual overlay for VFX emitter editing in the Application
/// viewport. Renders menu bar, stage tree, sliders, dropdowns, and emitter
/// tabs using the engine's TextRenderer (AddRect + AddText).

#include "vfx_editor_overlay.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/modules/vfx_module_metadata.h"
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
// Palette bindings — phase 12.1.0
//
// The legacy `kColor*` identifiers used throughout this file now alias into
// `VfxTheme().colors.*` so there is exactly one source of truth for every
// visual color. Swapping the theme is a one-file change (`VfxTheme()`).
// ─────────────────────────────────────────────────────────────────────────────

static const Vec4& kColorBarBg      = VfxThemeColors().barBg;
static const Vec4& kColorPanelBg    = VfxThemeColors().panelBg;
static const Vec4& kColorBtnNormal  = VfxThemeColors().cardBg;
static const Vec4& kColorBtnHover   = VfxThemeColors().cardBgHover;
static const Vec4& kColorBtnPress   = VfxThemeColors().accentPressed;
static const Vec4& kColorText       = VfxThemeColors().textPrimary;
static const Vec4& kColorAccent     = VfxThemeColors().accentPrimary;
static const Vec4& kColorSliderBg   = VfxThemeColors().sliderTrack;
static const Vec4& kColorSliderFill = VfxThemeColors().sliderThumb;
static const Vec4& kColorMenuHover  = VfxThemeColors().cardBgHover;
static const Vec4& kColorMenuActive = VfxThemeColors().accentMuted;
static const Vec4& kColorDropBg     = VfxThemeColors().popupBg;
static const Vec4& kColorStageHdr   = VfxThemeColors().barBg;
static const Vec4& kColorModuleRow  = VfxThemeColors().cardBg;
static const Vec4& kColorModuleExp  = VfxThemeColors().cardBgSelected;
static const Vec4& kColorCheckOn    = VfxThemeColors().stateSuccess;
static const Vec4& kColorCheckOff   = VfxThemeColors().borderStrong;
static const Vec4& kColorTextDim    = VfxThemeColors().textDim;

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

    const std::string fontPath = ResolveDefaultMonoFont();
    constexpr float kFontSize = 14.0f;
    // Phase 13.2.0 — bake the curated Unicode range set (ASCII + Arrows +
    // Box Drawing + Block Elements + Geometric Shapes + Misc Symbols +
    // Dingbats) so widgets can print real typographic glyphs inline.
    if (fontPath.empty() || !m_font.InitWithRanges(
            device, cmdList, upload, fontPath, kFontSize,
            GetDefaultGlyphRanges())) {
        UZ_LOG(VFX, Error, "VfxEditorOverlay: Failed to init font (resolved='%s')",
               fontPath.c_str());
        return false;
    }

    if (!m_textRenderer.Init(device, shaderPipeline, rtvFormat, screenW, screenH, bindless)) {
        UZ_LOG(VFX, Error, "VfxEditorOverlay: Failed to init text renderer");
        return false;
    }

    // Phase 13.1.0 — upload the procedural VFX icon atlas so widgets can
    // issue TextRenderer::AddSprite(VfxUiIcon::...). Failure is non-fatal
    // (sprite draws silently degrade to no-ops on backends without the
    // sprite pipeline, e.g. macOS MSL-only path).
    (void)m_textRenderer.LoadIconAtlas(cmdList, upload);

    // Phase 13.4.0 — bake a master-size MSDF atlas covering the same
    // Unicode range set as the bitmap font. Rendered via `fwidth`-based
    // AA so text stays crisp from 10 px captions to 48 px headers.
    // A master size of 32 px gives comfortable headroom across that
    // range without overshooting the atlas capacity.
    constexpr float kMsdfMasterSize = 32.0f;
    if (!m_msdfFont.InitWithRanges(device, cmdList, upload, fontPath,
                                    kMsdfMasterSize,
                                    GetDefaultGlyphRanges())) {
        UZ_LOG(VFX, Warning,
               "VfxEditorOverlay: MSDF atlas bake failed — falling back "
               "to bitmap text only (fontPath='%s')",
               fontPath.c_str());
    }

    // Phase 12.1.0 — verify every theme icon glyph is available in the bound
    // font. Missing glyphs don't fail init (they fall back to '?') but we
    // log a single warning per missing glyph so a font update is easy to
    // audit.
    {
        const VfxIconEntry* icons = VfxAllIcons();
        int missingCount = 0;
        for (int i = 0; i < kVfxIconCount; ++i) {
            const char* glyph = icons[i].glyph;
            bool ok = true;
            for (const char* p = glyph; *p != '\0'; ++p) {
                if (m_font.GetGlyph(*p) == nullptr) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                ++missingCount;
                UZ_LOG(VFX, Warning,
                       "VfxEditorOverlay: font missing glyph(s) for icon '%s' ('%s')",
                       icons[i].name, glyph);
            }
        }
        if (missingCount == 0) {
            UZ_LOG(VFX, Info,
                   "VfxEditorOverlay: theme icon set complete (%d glyphs)",
                   kVfxIconCount);
        }
    }

    m_initialized = true;
    return true;
}

void VfxEditorOverlay::Shutdown(IRHIDevice* device) {
    if (m_initialized) {
        m_textRenderer.Shutdown();
        // Release the GPU font atlas textures owned by the bitmap + MSDF
        // fonts. Without this the two 1024x1024 R8 atlases (~2 MiB total)
        // leak past device shutdown and trip D3D12MA's
        // `m_pMetadata->IsEmpty()` assertion in `~NormalBlock`.
        if (device != nullptr) {
            m_font.Shutdown(device);
            m_msdfFont.Shutdown(device);
        }
        m_initialized = false;
    }
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

void VfxEditorOverlay::PushStatus(const std::string& msg,
                                   VfxStatusLevel level) {
    m_statusBar.PushNotification(msg, level);
}

void VfxEditorOverlay::TickStatus(float dt) {
    m_statusBar.Tick(dt);
    // Timeline scrubber + preview clock disabled for now. Re-enable by
    // restoring `previewClock_.Tick(dt)` and `TickAutoLoop()` once the
    // playback / seek UX is back in scope.
}

void VfxEditorOverlay::UpdateTelemetry(float fps, uint32_t drawCalls,
                                        float vramUsedMb,
                                        float vramTotalMb) {
    m_statusBar.SetFps(fps);
    m_statusBar.SetDrawCalls(drawCalls);
    m_statusBar.SetVramUsed(vramUsedMb, vramTotalMb);
}

void VfxEditorOverlay::SetLayoutMode(VfxEditorLayoutMode mode) {
    auto cfg = m_composition.LayoutConfig();
    cfg.layoutMode = mode;
    m_composition.SetLayoutConfig(cfg);
}

void VfxEditorOverlay::OpenContentBrowser() {
    constexpr float kModalW = 720.0f;
    constexpr float kModalH = 480.0f;
    const float sw = static_cast<float>(m_screenW);
    const float sh = static_cast<float>(m_screenH);
    m_contentBrowserBounds = {
        (sw - kModalW) * 0.5f,
        (sh - kModalH) * 0.5f,
        kModalW,
        kModalH
    };
    m_contentBrowserOpen = true;

    if (!m_contentBrowserPopulated) {
        const auto& pal = VfxThemeColors();
        struct DemoTile { const char* name; Vec4 tint; };
        const DemoTile demos[] = {
            { "Sparks",    pal.accentPrimary },
            { "Smoke",     pal.stateWarning },
            { "Fire",      pal.stateError   },
            { "Dust",      pal.accentOrange },
            { "Magic",     pal.stateSuccess },
            { "Explosion", pal.stateInfo    },
        };
        for (uint32_t i = 0; i < 6; ++i) {
            VfxAssetTile t{};
            t.id       = i + 1u;
            t.label    = demos[i].name;
            t.kindTint = demos[i].tint;
            m_contentBrowser.AddTile(t);
        }
        m_contentBrowserPopulated = true;
    }
}

void VfxEditorOverlay::CloseContentBrowser() {
    m_contentBrowserOpen = false;
}

bool VfxEditorOverlay::HandleAcceleratorKey(char ascii, bool altHeld) {
    // Alt held → open the top menu whose accelerator matches.
    if (altHeld) {
        const int top = VfxMatchAccelerator(ascii);
        if (top < 0) return false;
        m_openMenu = top;
        m_wantsInput = true;
        return true;
    }

    // No modifier + dropdown open → fire the matching item.
    if (m_openMenu < 0) return false;
    const VfxTopMenu* menu = VfxMenuBarTopAt(static_cast<size_t>(m_openMenu));
    if (menu == nullptr) return false;
    const int itemIdx = VfxMatchAcceleratorInMenu(ascii, menu->items,
                                                    menu->itemCount);
    if (itemIdx < 0) return false;
    const VfxMenuItem& it = menu->items[itemIdx];
    if (onMenuCommand) onMenuCommand(it.command);
    if (it.label != nullptr) {
        PushStatus(std::string(it.label), VfxStatusLevel::Info);
    }
    m_openMenu = -1;
    m_wantsInput = true;
    return true;
}

bool VfxEditorOverlay::HandleMouse(const InputSystem* input, float /*dt*/) {
    if (!input) return false;
    m_inputRef = input;
    m_mouseX = input->GetMouseX();
    m_mouseY = input->GetMouseY();

    // Track just-pressed/released internally from raw down state,
    // because InputSystem::IsMouseJustPressed depends on BeginFrame timing
    // which may not align with when HandleMouse is called.
    const bool currentDown = input->IsMouseDown(MouseButton::Left);
    m_mouseJustPressed  = currentDown && !m_mouseDown;
    m_mouseJustReleased = !currentDown && m_mouseDown;
    m_mouseDown = currentDown;

    // Phase 12.10.0 — right-button edge detection for context menus.
    const bool rightDown = input->IsMouseDown(MouseButton::Right);
    m_mouseRightJustPressed = rightDown && !m_mouseRightDown;
    m_mouseRightDown = rightDown;

    // m_scrollDelta is set by InjectScrollDelta() before HandleMouse,
    // because BeginFrame() clears it before we get here.

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
    m_wantsInput = (m_editingPtr != nullptr);

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

    // Pass 2: dropdown / module picker — drawn after pass 1 is fully
    // composited so rects overwrite pass 1 pixels cleanly.
    // Click processing also happens here (m_mouseJustPressed still valid).
    const bool hasDropdown = (m_dropdownPtr != nullptr) || (m_openMenu >= 0);
    const bool hasPicker   = m_modulePicker.IsOpen();
    if (hasDropdown || hasPicker) {
        m_textRenderer.BeginFrame(m_lastFrameIndex);
        BuildMenuDropdowns();
        BuildDropdownOverlay();
        if (hasPicker) {
            m_modulePicker.Render(m_textRenderer, m_font);

            // Phase 14.11 — picker scrollbar. Approximate content
            // height from the dialog's computed layout extent.
            const auto pickerBounds = m_modulePicker.Bounds();
            const auto pLayout = m_modulePicker.ComputeLayout();
            float contentBottom = pickerBounds.y;
            for (const auto& it : pLayout) {
                const float b = it.rect.y + it.rect.height;
                if (b > contentBottom) contentBottom = b;
            }
            const float pickerContentH = contentBottom - pickerBounds.y;
            if (pickerContentH > pickerBounds.height) {
                m_pickerScrollbar.SetView(pickerBounds, pickerContentH);
                m_pickerScrollbar.SetHovered(
                    pickerBounds.Contains(m_mouseX, m_mouseY));
                m_pickerScrollbar.Render(m_textRenderer);
            }
        }
        m_textRenderer.Render(cmdList, cbRing, m_font);
    }

    // Pass 3: shared context menu (phase 12.8.0) — drawn above every
    // other UI so the popup is always on top of panels + dropdowns.
    if (m_contextMenu.IsOpen()) {
        m_textRenderer.BeginFrame(m_lastFrameIndex);
        m_contextMenu.Render(m_textRenderer, m_font);
        m_textRenderer.Render(cmdList, cbRing, m_font);
    }

    // Pass 4: color picker popup. Dedicated pass so the pane's label
    // text cannot overprint the popup chrome — rects + text from the
    // picker submit into a fresh frame that draws strictly above the
    // earlier passes.
    if (m_colorPicker.isOpen) {
        m_textRenderer.BeginFrame(m_lastFrameIndex);
        RenderColorPicker();
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

    // Phase 15.5 — modal popup click routing runs first so an open
    // popup always wins over the panels beneath it. The prior call sites
    // lived after the Niagara early-return and were therefore unreachable
    // in Niagara / split-layout mode, so popups visually opened but
    // their rows, tabs, and buttons were never clickable. When the popup
    // absorbs the press we clear `m_mouseJustPressed` so downstream
    // dispatch (menu bar, stack panel, editor pane, etc.) does not
    // double-process the same click.
    if (m_contextMenu.IsOpen()) {
        m_contextMenu.SetHover(m_mouseX, m_mouseY);
        m_wantsInput = true;
        if (m_mouseJustPressed) {
            m_contextMenu.ClickAt(m_mouseX, m_mouseY);
            m_mouseJustPressed = false;
        }
    }

    // Phase 15.11 — color picker absorbs clicks + typed chars when
    // open. Runs before the Niagara branch so clicks on presets /
    // HEX / close button win over the panels beneath.
    if (m_colorPicker.isOpen) {
        HandleColorPickerInput();
        // If the click was consumed inside the popup, prevent other
        // widgets from also reacting. `HandleColorPickerInput` already
        // sets wantsInput when the click landed on interactive chrome.
        if (m_wantsInput) m_mouseJustPressed = false;
    }

    if (m_modulePicker.IsOpen()) {
        m_wantsInput = true; // suppress camera input while dialog is up
        if (m_mouseJustPressed) {
            const auto layout = m_modulePicker.ComputeLayout();
            const auto hit    = m_modulePicker.HitTest(layout, m_mouseX, m_mouseY);
            switch (hit.type) {
            case VfxPickerItemType::CloseButton:
            case VfxPickerItemType::ButtonCancel:
                // Cancel and close both dismiss without committing.
                m_modulePicker.Close();
                break;
            case VfxPickerItemType::CategoryTab: {
                // Tab labels embed the count badge ("Velocity 4"); the
                // final space separates the canonical name from the
                // numeric badge. Strip it before calling SetCategory,
                // which validates against Categories().
                const std::string& full = hit.label;
                const size_t sp = full.rfind(' ');
                m_modulePicker.SetCategory(sp == std::string::npos
                                            ? full
                                            : full.substr(0, sp));
                break;
            }
            case VfxPickerItemType::ModuleTile:
                // Single-click selects; the Create button (or Enter in
                // the search input) commits. Matches Niagara authoring.
                m_modulePicker.SelectIndex(hit.index);
                break;
            case VfxPickerItemType::ButtonCreate:
                // Only fires when there is a valid selection; the
                // dialog's ConfirmSelection is a no-op otherwise.
                m_modulePicker.ConfirmSelection();
                break;
            case VfxPickerItemType::SearchInput:
            case VfxPickerItemType::PreviewArea:
                // Absorb click so the dialog stays open.
                break;
            case VfxPickerItemType::FilterBox:
            case VfxPickerItemType::None:
                // Click outside the dialog → dismiss.
                if (!m_modulePicker.Bounds().Contains(m_mouseX, m_mouseY)) {
                    m_modulePicker.Close();
                }
                break;
            }
            m_mouseJustPressed = false;
        }

        // Typed-character input routing for the search field. Printable
        // ASCII appends to the filter; Backspace deletes the last char;
        // Enter confirms selection (same as pressing Create); Escape
        // closes the dialog. Feeds directly from the InputSystem so the
        // host does not need extra wiring.
        if (m_inputRef != nullptr) {
            const auto& chars = m_inputRef->GetTypedChars();
            if (!chars.empty()) {
                std::string next = m_modulePicker.Filter();
                bool mutated = false;
                for (char ch : chars) {
                    if (ch == '\r' || ch == '\n') {
                        m_modulePicker.ConfirmSelection();
                        mutated = false;
                        break;
                    }
                    if (ch == 0x1B) {
                        m_modulePicker.Close();
                        mutated = false;
                        break;
                    }
                    if (ch == '\b' || ch == 0x7F) {
                        if (!next.empty()) next.pop_back();
                        mutated = true;
                        continue;
                    }
                    // Accept printable ASCII only (32..126). The bitmap
                    // font bakes the same range, so anything else would
                    // render as a replacement glyph.
                    if (static_cast<unsigned char>(ch) >= 32u &&
                        static_cast<unsigned char>(ch) < 127u) {
                        next.push_back(ch);
                        mutated = true;
                    }
                }
                if (mutated) m_modulePicker.SetFilter(next);
                m_wantsInput = true;
            }
        }
    }

    // Layer 1: base UI (menu bar, panels)
    BuildMenuBar();
    // Phase 0 polish — skip legacy emitter bar in Niagara mode. The
    // stack panel's own emitter header + the right-panel system
    // overview cover tab/status/add affordances cleanly; the legacy
    // Start + Re-capture buttons overlap the Niagara content.
    if (!m_niagaraMode) {
        BuildEmitterBar();
    }

    // Phase 11.14.0: when Niagara mode is enabled the composition takes
    // over the main panel area. Menu bar and emitter bar are still drawn
    // so the user can switch presets + toggle preset menus.
    if (m_niagaraMode && emitter != nullptr) {
        BuildNiagaraUI(emitter);
        // Color picker draws on its own pass inside Render() so the
        // pane's label text does not overprint the popup chrome.
        return;
    }

    if (emitter) {
        if (emitter != m_lastEmitter) {
            BuildModuleList(emitter);
            m_lastEmitter = emitter;
            // Clear tag-editing state when switching emitters so the new
            // emitter's "+ Add tag" input starts empty.
            m_tagEditing = false;
            m_tagEditBuffer.clear();
        }
        // Tags row — sits at the top of the left panel, above the Module
        // Stack. Returns the vertical space it consumed so subsequent
        // panels can offset accordingly. The current implementation
        // renders tags over the panel start; full layout integration is
        // a follow-up (the existing stage tree uses absolute Y positions
        // so they don't conflict today).
        constexpr float kLeftPanelTopY = 30.0f + 26.0f; // kMenuBarH + kEmitterBarH
        BuildTagsRow(emitter, 0.0f, kLeftPanelTopY, 280.0f);

        // Phase 11.2: when the emitter has a populated stack, the Niagara-
        // parity stack panel is authoritative. Legacy flat assets keep the
        // existing stage tree.
        if (emitter->stack.IsEmpty()) {
            BuildStageTree(emitter);
        } else {
            BuildStackPanel(emitter);
        }
    }

    // Phase 11.3 module-picker routing moved to the top of BuildUI
    // alongside the context menu; see the block after
    // `m_selectedEmitterIdx`.

    // Phase 12.8.0 context-menu routing moved to the top of BuildUI
    // (above the Niagara branch) so popups are clickable in every
    // layout mode; see the block right after `m_selectedEmitterIdx`.

    // Color picker draws in its own pass from `Render()` — see the
    // dedicated `BeginFrame` + `RenderColorPicker` block below.

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

bool VfxEditorOverlay::NumberInput(float x, float y, float w, float* value,
                                   float minVal, float maxVal, const char* label) {
    const bool isEditing = (m_editingPtr == value);
    const bool hovered = HitTest(x, y, w, kSliderH);

    // Background — highlight when editing or hovered.
    const Vec4 bgColor = isEditing ? Vec4{0.20f, 0.25f, 0.35f, 1.0f}
                       : hovered   ? kColorBtnHover
                                   : kColorSliderBg;
    m_textRenderer.AddRect(x, y, w, kSliderH, bgColor);

    // Label on the left.
    m_textRenderer.AddText(m_font, label, x + 4.0f, y + 2.0f, kColorText);

    // Value display or edit buffer on the right.
    char displayBuf[32];
    if (isEditing) {
        if (m_editBuffer.empty()) {
            // Show old value as dim placeholder + cursor
            std::snprintf(displayBuf, sizeof(displayBuf), "%.2f_", *value);
            m_textRenderer.AddText(m_font, displayBuf, x + w - 80.0f, y + 2.0f,
                                   Vec4{0.5f, 0.5f, 0.4f, 1.0f});
        } else {
            std::snprintf(displayBuf, sizeof(displayBuf), "%s_", m_editBuffer.c_str());
            m_textRenderer.AddText(m_font, displayBuf, x + w - 80.0f, y + 2.0f,
                                   Vec4{1.0f, 1.0f, 0.6f, 1.0f});
        }
    } else {
        std::snprintf(displayBuf, sizeof(displayBuf), "%.2f", *value);
        m_textRenderer.AddText(m_font, displayBuf, x + w - 80.0f, y + 2.0f, Vec4{1,1,1,1});
    }

    // Click to start editing — buffer starts EMPTY so the first keystroke
    // replaces the old value entirely (select-all behavior).
    if (hovered && m_mouseJustPressed && !isEditing) {
        m_editingPtr = value;
        m_editingMin = minVal;
        m_editingMax = maxVal;
        m_editBuffer.clear();
        m_wantsInput = true;
        return false;
    }

    // Click elsewhere to commit (empty buffer = keep old value).
    if (isEditing && m_mouseJustPressed && !hovered) {
        if (!m_editBuffer.empty()) {
            char* end = nullptr;
            const double parsed = std::strtod(m_editBuffer.c_str(), &end);
            if (end != m_editBuffer.c_str()) {
                *value = std::clamp(static_cast<float>(parsed), minVal, maxVal);
            }
        }
        m_editingPtr = nullptr;
        m_editBuffer.clear();
        return !m_editBuffer.empty();
    }

    // Process typed characters while editing.
    if (isEditing && m_inputRef) {
        const auto& chars = m_inputRef->GetTypedChars();
        if (!chars.empty()) {
            UZ_LOG(VFX, Info, "NumberInput: received {} chars, buf='{}'", chars.size(), m_editBuffer);
        }
        bool changed = false;
        for (char ch : chars) {
            if (ch == '\r' || ch == '\n') {
                // Enter — commit.
                char* end = nullptr;
                const double parsed = std::strtod(m_editBuffer.c_str(), &end);
                if (end != m_editBuffer.c_str()) {
                    *value = std::clamp(static_cast<float>(parsed), minVal, maxVal);
                    changed = true;
                }
                m_editingPtr = nullptr;
                m_editBuffer.clear();
                return changed;
            } else if (ch == 0x1B) {
                // Escape — cancel.
                m_editingPtr = nullptr;
                m_editBuffer.clear();
                return false;
            } else if (ch == '\b') {
                if (!m_editBuffer.empty()) m_editBuffer.pop_back();
            } else if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') {
                m_editBuffer += ch;
            }
        }
        m_wantsInput = true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// StringInput — generic text-input widget (phase 10.1.0)
// ─────────────────────────────────────────────────────────────────────────────

/// Filters a character through the selected InputFilter. Returns the
/// accepted character (possibly case-folded) or '\0' if rejected.
static char FilterChar(char ch, VfxEditorOverlay::InputFilter filter) {
    switch (filter) {
        case VfxEditorOverlay::InputFilter::Any:
            return (ch >= 0x20) ? ch : '\0'; // printable only
        case VfxEditorOverlay::InputFilter::LowercaseASCII:
            if (ch >= 'a' && ch <= 'z') return ch;
            if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
            if (ch >= '0' && ch <= '9') return ch;
            if (ch == '-' || ch == '_') return ch;
            return '\0';
        case VfxEditorOverlay::InputFilter::Identifier:
            if (ch >= 'a' && ch <= 'z') return ch;
            if (ch >= 'A' && ch <= 'Z') return ch;
            if (ch >= '0' && ch <= '9') return ch;
            if (ch == '_') return ch;
            return '\0';
        case VfxEditorOverlay::InputFilter::Numeric:
            if (ch >= '0' && ch <= '9') return ch;
            if (ch == '.' || ch == '-') return ch;
            return '\0';
    }
    return '\0';
}

bool VfxEditorOverlay::StringInput(float x, float y, float w,
                                    std::string* buffer,
                                    InputFilter filter,
                                    const char* placeholder) {
    const bool isFocused = (m_focusedStringInput == buffer);
    const bool hovered   = HitTest(x, y, w, kSliderH);

    // Background — highlight when focused or hovered.
    const Vec4 bgColor = isFocused ? Vec4{0.20f, 0.25f, 0.35f, 1.0f}
                        : hovered  ? kColorBtnHover
                                   : kColorSliderBg;
    m_textRenderer.AddRect(x, y, w, kSliderH, bgColor);

    // Display text: focused → buffer + cursor, unfocused → buffer or placeholder.
    if (isFocused) {
        std::string disp = buffer->empty() ? std::string("_") : *buffer + "_";
        m_textRenderer.AddText(m_font, disp.c_str(), x + 4.0f, y + 2.0f,
                               Vec4{1.0f, 1.0f, 0.6f, 1.0f});
    } else if (buffer->empty() && placeholder[0] != '\0') {
        m_textRenderer.AddText(m_font, placeholder, x + 4.0f, y + 2.0f, kColorTextDim);
    } else {
        m_textRenderer.AddText(m_font, buffer->c_str(), x + 4.0f, y + 2.0f, kColorText);
    }

    // Click to gain focus.
    if (hovered && m_mouseJustPressed && !isFocused) {
        // Steal focus from any other input.
        m_focusedStringInput = buffer;
        m_stringInputBackup  = *buffer;
        m_editingPtr = nullptr;
        m_editBuffer.clear();
        m_tagEditing = false;
        m_tagEditBuffer.clear();
        m_wantsInput = true;
        return false;
    }

    // Click outside to commit.
    if (isFocused && m_mouseJustPressed && !hovered) {
        m_focusedStringInput = nullptr;
        return true; // committed
    }

    // Process keystrokes while focused.
    if (isFocused && m_inputRef) {
        const auto& chars = m_inputRef->GetTypedChars();
        for (char ch : chars) {
            if (ch == '\r' || ch == '\n') {
                m_focusedStringInput = nullptr;
                return true; // committed via Enter
            } else if (ch == 0x1B) {
                // Escape — restore backup.
                *buffer = m_stringInputBackup;
                m_focusedStringInput = nullptr;
                return false;
            } else if (ch == '\b') {
                if (!buffer->empty()) buffer->pop_back();
            } else {
                const char accepted = FilterChar(ch, filter);
                if (accepted != '\0') {
                    *buffer += accepted;
                }
            }
        }
        m_wantsInput = true;
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
    // Phase 12.7.0 — labels and item lists come from the static menu
    // definition in `vfx_editor_menu.h`. The bar draws the top entries;
    // `BuildMenuDropdowns` handles the dropdown body for the active one.
    const float sw = static_cast<float>(m_screenW);
    const auto& palette = VfxThemeColors();
    m_textRenderer.AddRect(0.0f, 0.0f, sw, kMenuBarH, palette.barBg);

    const size_t topCount = VfxMenuBarTopCount();

    float x = 10.0f;
    for (size_t i = 0; i < topCount; ++i) {
        const VfxTopMenu* menu = VfxMenuBarTopAt(i);
        if (menu == nullptr) continue;
        const bool hovered = HitTest(x, 0.0f, kMenuItemW, kMenuBarH);
        const bool active  = (m_openMenu == static_cast<int>(i));

        Vec4 bg = palette.barBg;
        if (active)       bg = palette.cardBgSelected;
        else if (hovered) bg = palette.cardBgHover;

        m_textRenderer.AddRect(x, 2.0f, kMenuItemW, kMenuBarH - 4.0f, bg);
        // Phase 13.4.0 — menu-bar labels render through the MSDF path
        // at 16 px so zooming / high-DPI displays keep crisp edges.
        // Bitmap fallback when the MSDF atlas failed to bake.
        constexpr float kMsdfLabelPx = 16.0f;
        if (const MsdfFont* msdf = GetMsdfFont()) {
            m_textRenderer.AddMsdfText(*msdf, menu->label,
                                        x + 10.0f, 6.0f, kMsdfLabelPx,
                                        palette.textPrimary);
        } else {
            m_textRenderer.AddText(m_font, menu->label, x + 10.0f, 8.0f,
                                   palette.textPrimary);
        }

        // Phase 14.8 — UE5 accelerator underline. Computes the x-offset
        // of the accelerator character by accumulating bitmap-font
        // xAdvance values, scaled by `kMsdfLabelPx / font.pixelHeight`
        // so the underline lands under the glyph even on the MSDF path.
        const int accIdx = menu->acceleratorIndex;
        if (accIdx >= 0 && accIdx < static_cast<int>(std::strlen(menu->label))) {
            const float scale = kMsdfLabelPx / m_font.GetPixelHeight();
            float cursor = x + 10.0f;
            for (int i = 0; i < accIdx; ++i) {
                if (const auto* g = m_font.GetGlyph(menu->label[i])) {
                    cursor += g->xAdvance * scale;
                }
            }
            float glyphAdvance = 7.0f;  // bitmap-font fallback
            if (const auto* g = m_font.GetGlyph(menu->label[accIdx])) {
                glyphAdvance = g->xAdvance * scale;
            }
            // 1-px underline just below the MSDF baseline.
            m_textRenderer.AddRect(cursor, 6.0f + kMsdfLabelPx,
                                    glyphAdvance, 1.0f,
                                    palette.textPrimary);
        }

        if (hovered && m_mouseJustPressed) {
            m_openMenu = active ? -1 : static_cast<int>(i);
            CloseDropdown();
        }
        if (hovered && m_openMenu >= 0 &&
            m_openMenu != static_cast<int>(i)) {
            m_openMenu = static_cast<int>(i);
            CloseDropdown();
        }

        x += kMenuItemW + 4.0f;
    }
}

void VfxEditorOverlay::BuildMenuDropdowns() {
    if (m_openMenu < 0) return;
    const VfxTopMenu* menu = VfxMenuBarTopAt(static_cast<size_t>(m_openMenu));
    if (menu == nullptr) return;

    const auto& palette = VfxThemeColors();
    const float dropX = 10.0f + m_openMenu * (kMenuItemW + 4.0f);
    constexpr float kDropW       = 200.0f;
    constexpr float kSeparatorH  = 7.0f;
    constexpr float kIconColW    = 16.0f;
    constexpr float kHotkeyColW  = 72.0f;
    const float dropY = kMenuBarH;

    // Presets is driven by runtime state — expand `m_presetNames` into
    // a virtual item list instead of using the static placeholder.
    const bool isPresets = (menu == VfxMenuBarTopAt(3));
    size_t dynamicCount = menu->itemCount;
    if (isPresets) {
        dynamicCount = std::max<size_t>(1u, m_presetNames.size());
    }

    // Total height from every item (separators are shorter).
    float totalH = 0.0f;
    for (size_t j = 0; j < dynamicCount; ++j) {
        if (isPresets) {
            totalH += kDropItemH;
            continue;
        }
        const auto& it = menu->items[j];
        totalH += it.isSeparator ? kSeparatorH : kDropItemH;
    }

    // Phase 13.5 — dropdown backdrop is a single rounded SDF rect
    // with a 1-px border and drop shadow. Bottom corners are
    // rounded (4 px) — top is also rounded but visually sits flush
    // against the menu bar so the shadow hides behind the bar.
    const auto& radius  = VfxThemeRadius();
    const auto& shadows = VfxThemeShadows();
    Vec4 shadowColor = palette.shadow;
    shadowColor.w = shadows.popup.alpha;
    m_textRenderer.AddRoundedRect(dropX, dropY, kDropW, totalH,
                                   radius.sm /*4 px*/,
                                   palette.popupBg,
                                   1.0f, palette.borderStrong,
                                   Vec2{0.0f, shadows.popup.offsetY},
                                   shadows.popup.blur,
                                   shadowColor);

    float cy = dropY;
    for (size_t j = 0; j < dynamicCount; ++j) {
        if (isPresets) {
            const bool hasPresets = !m_presetNames.empty();
            const bool ih = HitTest(dropX, cy, kDropW, kDropItemH);
            if (ih && hasPresets) {
                m_textRenderer.AddRect(dropX, cy, kDropW, kDropItemH,
                                       palette.cardBgHover);
            }
            if (hasPresets) {
                m_textRenderer.AddText(m_font, m_presetNames[j].c_str(),
                                       dropX + 8.0f, cy + 4.0f,
                                       palette.textPrimary);
                if (ih && m_mouseJustPressed) {
                    if (onLoadPreset) onLoadPreset(static_cast<int>(j), m_lastEmitter);
                    if (onMenuCommand) {
                        onMenuCommand(static_cast<VfxMenuCommand>(
                            static_cast<uint16_t>(VfxMenuCommand::Presets_Base) +
                            static_cast<uint16_t>(j)));
                    }
                    m_openMenu = -1;
                }
            } else {
                m_textRenderer.AddText(m_font, "(no presets)",
                                       dropX + 8.0f, cy + 4.0f,
                                       palette.textDim);
            }
            cy += kDropItemH;
            continue;
        }

        const auto& item = menu->items[j];

        if (item.isSeparator) {
            // 1-px divider with 3-px padding top + bottom.
            m_textRenderer.AddRect(dropX + 6.0f, cy + kSeparatorH * 0.5f,
                                   kDropW - 12.0f, 1.0f,
                                   palette.borderSubtle);
            cy += kSeparatorH;
            continue;
        }

        const bool ih = HitTest(dropX, cy, kDropW, kDropItemH);
        if (ih && item.enabled) {
            m_textRenderer.AddRect(dropX, cy, kDropW, kDropItemH,
                                   palette.cardBgHover);
        }

        const Vec4 fg = item.enabled ? palette.textPrimary
                                        : palette.textDisabled;

        // Left: optional icon.
        if (item.iconGlyph != nullptr && item.iconGlyph[0] != '\0') {
            m_textRenderer.AddText(m_font, item.iconGlyph,
                                   dropX + 6.0f, cy + 4.0f, fg);
        }
        // Centre: label.
        m_textRenderer.AddText(m_font, item.label != nullptr ? item.label : "",
                               dropX + kIconColW + 4.0f, cy + 4.0f, fg);
        // Right: hotkey hint (consistent x per dropdown so rows align).
        if (item.hotkey != nullptr && item.hotkey[0] != '\0') {
            m_textRenderer.AddText(m_font, item.hotkey,
                                   dropX + kDropW - kHotkeyColW,
                                   cy + 4.0f, palette.textDim);
        }

        if (ih && m_mouseJustPressed && item.enabled) {
            if (onMenuCommand) onMenuCommand(item.command);
            m_openMenu = -1;
        }
        cy += kDropItemH;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter Selector Bar
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::BuildEmitterBar() {
    // Phase 12.7.0 — horizontal tab row with one tab per emitter in
    // `m_emitterNames`, plus a trailing `+` button that fires
    // `onAddEmitterRequested`. Falls back to the legacy Start +
    // Re-capture toolbar when no emitter names are set (keeps the
    // phase-9 thumbnail flow working).

    const auto& palette = VfxThemeColors();
    const float sw = static_cast<float>(m_screenW);
    const float barY = kMenuBarH;
    m_textRenderer.AddRect(0.0f, barY, sw, kEmitterBarH, palette.barBg);

    // ── Legacy toolbar path ───────────────────────────────────────────
    if (m_emitterNames.empty()) {
        float btnX = 10.0f;
        const float btnY = barY + 2.0f;
        const float btnW = 80.0f;
        const float btnH = kEmitterBarH - 4.0f;

        if (Button(btnX, btnY, btnW, btnH, "Start")) {
            if (onRestart) onRestart();
        }
        btnX += btnW + 6.0f;

        const float recapW = 100.0f;
        if (Button(btnX, btnY, recapW, btnH, "Re-capture")) {
            if (onRecaptureThumbnail && m_lastEmitter) {
                onRecaptureThumbnail("");
            }
        }
        return;
    }

    // ── Phase 12.7.0 tab layout ───────────────────────────────────────
    const float tabY = barY + 2.0f;
    const float tabH = kEmitterBarH - 4.0f;
    const float dotRadius = 4.0f;
    const float padX = 10.0f;

    // Horizontal scroll: clamped to [0, max(0, contentW - viewportW)].
    // Content width = sum of each tab's computed width + `+` button.
    const auto measureTab = [&](const std::string& name) -> float {
        // Approximate width: 6 px border padding + dot 10 px + name
        // chars * 7 px advance.
        return 16.0f + dotRadius * 2.0f +
               static_cast<float>(name.size()) * 7.0f + padX;
    };

    float contentW = 0.0f;
    for (const auto& n : m_emitterNames) contentW += measureTab(n);
    const float addBtnW = 24.0f;
    contentW += addBtnW + padX;

    const float viewportX0 = 10.0f;
    const float viewportW  = sw - viewportX0 - 10.0f;
    const float maxScroll  = std::max(0.0f, contentW - viewportW);
    if (m_emitterTabScrollOffset > maxScroll) {
        m_emitterTabScrollOffset = maxScroll;
    }
    if (m_emitterTabScrollOffset < 0.0f) m_emitterTabScrollOffset = 0.0f;

    float cursorX = viewportX0 - m_emitterTabScrollOffset;

    // Phase 14.2.0 — rebuild the tab strip widget each frame from
    // the current `m_emitterNames`. The UE5 tab strip renders
    // rounded-top / flat-bottom tabs with a 2-px accent bar on
    // the active one, merging visually with the panel body below.
    m_emitterTabStrip.Clear();
    for (const std::string& n : m_emitterNames) {
        VfxTabEntry entry;
        entry.label = n;
        entry.icon = VfxUiIcon::Dot;   // running-state dot; colour is `textPrimary`
        entry.closable = true;
        m_emitterTabStrip.AddTab(entry);
    }
    if (m_activeEmitterIdx >= 0) {
        m_emitterTabStrip.SetActiveIndex(static_cast<size_t>(m_activeEmitterIdx));
    }

    // Hover detection BEFORE render so close-`x` appears.
    {
        VfxStackRect probeBounds{ cursorX, tabY, 9999.0f, tabH };
        const auto probeLayout = m_emitterTabStrip.ComputeLayout(probeBounds);
        const auto hit = m_emitterTabStrip.HitTest(probeLayout, m_mouseX, m_mouseY);
        m_emitterTabStrip.SetHoveredIndex(
            (hit.type != VfxTabItemType::None)
                ? hit.tabIndex : static_cast<size_t>(-1));

        // Propagate clicks.
        if (m_mouseJustPressed && hit.type == VfxTabItemType::Tab) {
            m_selectedEmitterIdx = static_cast<int>(hit.tabIndex);
            m_activeEmitterIdx   = static_cast<int>(hit.tabIndex);
            if (onSelectEmitter) onSelectEmitter(static_cast<int>(hit.tabIndex));
            m_wantsInput = true;
        }
    }

    // Render the tab strip into the emitter-bar strip.
    {
        VfxStackRect stripBounds{ cursorX, tabY, viewportW, tabH };
        m_emitterTabStrip.Render(m_textRenderer, m_font,
                                   GetMsdfFont(), stripBounds);

        // Advance cursor past the tab content for the trailing `+` button.
        for (size_t i = 0; i < m_emitterNames.size(); ++i) {
            cursorX += measureTab(m_emitterNames[i]);
        }
    }

    // Trailing `+` button.
    {
        const bool visible = (cursorX + addBtnW > viewportX0) &&
                              (cursorX < viewportX0 + viewportW);
        const bool hovered = HitTest(cursorX, tabY, addBtnW, tabH);
        if (visible) {
            m_textRenderer.AddRect(cursorX, tabY, addBtnW, tabH,
                                   hovered ? palette.cardBgHover
                                              : palette.cardBg);
            m_textRenderer.AddText(m_font, "+",
                                   cursorX + addBtnW * 0.5f - 3.0f,
                                   tabY + 4.0f, palette.accentPrimary);
        }
        if (hovered && m_mouseJustPressed && visible) {
            if (onAddEmitterRequested) onAddEmitterRequested();
            m_wantsInput = true;
        }
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

    // ── Render Sort (Niagara EmitterSortPriority parity) ────────────────
    {
        VfxModuleDef m;
        m.name = "Render Sort"; m.stage = VfxStage::EmitterUpdate;
        m.params.push_back({});
        auto& sp = m.params.back();
        sp.label = "Priority"; sp.type = VfxModuleParam::Int;
        sp.iPtr = &e->renderPriority; sp.min = -100.0f; sp.max = 100.0f;
        m_modules.push_back(std::move(m));
    }

    // ── Attach (Niagara AttachToComponent + socket parity) ──────────────
    // `attachBoneNameDefault` is the asset-serialized bone name. Scene sync
    // treats it as the fallback when the ECS component does not supply a
    // placement-specific override.
    {
        VfxModuleDef m;
        m.name = "Attach"; m.stage = VfxStage::EmitterUpdate;
        m.params.push_back({});
        auto& bp = m.params.back();
        bp.label = "Bone Name"; bp.type = VfxModuleParam::String;
        bp.sPtr  = &e->attachBoneNameDefault;
        bp.hint  = "e.g. hand_r (empty = entity root)";
        m_modules.push_back(std::move(m));
    }

    // ── User Attributes (Niagara "Parameters" parity) ───────────────────
    // Read-only listing of the user-declared per-particle channels. Each
    // entry is driven by a float (default value for Float attrs) or a Vec4
    // (Vec4 attrs) so designers can tweak the spawn default. Declaration
    // and removal still flow through Lua / the asset file — the editor
    // only exposes the runtime values.
    if (!e->userAttributes.empty()) {
        VfxModuleDef m;
        m.name = "User Attributes"; m.stage = VfxStage::ParticleSpawn;
        for (auto& a : e->userAttributes) {
            m.params.push_back({});
            auto& p = m.params.back();
            p.label = a.name.c_str();
            if (a.type == UserAttributeType::Float) {
                p.type = VfxModuleParam::Float;
                p.fPtr = &a.defaultFloat;
                p.min  = -1000.0f;
                p.max  = 1000.0f;
            } else {
                // Expose the Vec4 as four sequential float rows so the
                // existing NumberInput renderer can edit each component.
                p.type = VfxModuleParam::Float;
                p.fPtr = &a.defaultVec4.x;
                p.min  = 0.0f;
                p.max  = 1.0f;
            }
        }
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
// BuildTagsRow — chip editor + text input (phase 9.1.0)
// ─────────────────────────────────────────────────────────────────────────────
//
// Layout (single row, wraps when chips exceed width):
//   ┌─────────────────────────────────────┐
//   │ Tags: [fire ×] [hero ×] [+ New tag] │
//   └─────────────────────────────────────┘
//
// Returns the total vertical pixels the row consumed so the caller can
// place the module stack beneath it.

float VfxEditorOverlay::BuildTagsRow(ParticleEmitter* emitter,
                                       float x, float y, float w) {
    if (emitter == nullptr) return 0.0f;

    constexpr float kRowPad       = 4.0f;
    constexpr float kLabelW       = 38.0f;
    constexpr float kChipH        = 18.0f;
    constexpr float kChipPadX     = 6.0f;
    constexpr float kChipGap      = 4.0f;
    constexpr float kAddBtnW      = 90.0f;
    constexpr float kCharW        = 7.0f; // approximate char advance for width calc

    // "Tags:" label.
    m_textRenderer.AddText(m_font, "Tags:", x + kRowPad, y + kRowPad, kColorText);

    float cx = x + kLabelW;
    const float cy = y + kRowPad;
    const float rowRight = x + w - kRowPad;

    // Existing chips. Click the × to remove. Layout wraps to a new line
    // when the chip would exceed the row's right edge.
    uint32_t removeIdx = UINT32_MAX;
    float curY = cy;
    for (uint32_t i = 0; i < emitter->tags.size(); ++i) {
        const std::string& t = emitter->tags[i];
        const float chipW = kChipPadX * 2.0f + (t.size() + 2u) * kCharW; // +2 for " ×"
        if (cx + chipW > rowRight) {
            cx = x + kLabelW;
            curY += kChipH + kChipGap;
        }
        // Chip background.
        m_textRenderer.AddRect(cx, curY, chipW, kChipH,
                               Vec4{0.25f, 0.35f, 0.50f, 1.0f});
        m_textRenderer.AddText(m_font, t.c_str(), cx + kChipPadX, curY + 2.0f, kColorText);

        // × remove button — hit-test the last char width.
        const float xBtnX = cx + chipW - kChipPadX - kCharW;
        m_textRenderer.AddText(m_font, "x", xBtnX, curY + 2.0f,
                               Vec4{1.0f, 0.7f, 0.7f, 1.0f});
        if (HitTest(xBtnX - 2.0f, curY, kCharW + 4.0f, kChipH) &&
            m_mouseJustPressed) {
            removeIdx = i;
        }
        cx += chipW + kChipGap;
    }

    if (removeIdx != UINT32_MAX && removeIdx < emitter->tags.size()) {
        emitter->tags.erase(emitter->tags.begin() + removeIdx);
    }

    // "+ Add tag" input. Wraps to a new line when it doesn't fit.
    if (cx + kAddBtnW > rowRight) {
        cx = x + kLabelW;
        curY += kChipH + kChipGap;
    }
    const bool hovered = HitTest(cx, curY, kAddBtnW, kChipH);
    m_textRenderer.AddRect(cx, curY, kAddBtnW, kChipH,
                           m_tagEditing ? Vec4{0.20f, 0.25f, 0.35f, 1.0f}
                                        : hovered ? kColorBtnHover
                                                  : kColorBtnNormal);

    if (m_tagEditing) {
        std::string disp = m_tagEditBuffer.empty() ? std::string("_") : m_tagEditBuffer + "_";
        m_textRenderer.AddText(m_font, disp.c_str(), cx + 4.0f, curY + 2.0f,
                               Vec4{1.0f, 1.0f, 0.6f, 1.0f});
    } else {
        m_textRenderer.AddText(m_font, "+ Add tag", cx + 4.0f, curY + 2.0f, kColorTextDim);
    }

    // Click to start editing — clears the num-input state so keystrokes
    // route here.
    if (hovered && m_mouseJustPressed && !m_tagEditing) {
        m_tagEditing = true;
        m_tagEditBuffer.clear();
        m_editingPtr = nullptr; // steal focus from numeric editor
        m_editBuffer.clear();
        m_wantsInput = true;
    }

    // Click outside to commit + close.
    if (m_tagEditing && m_mouseJustPressed && !hovered) {
        if (!m_tagEditBuffer.empty()) {
            emitter->tags.push_back(m_tagEditBuffer);
        }
        m_tagEditing = false;
        m_tagEditBuffer.clear();
    }

    // Process typed characters. Validation: lowercase letters, digits,
    // hyphen, underscore. Enter commits, Escape cancels.
    if (m_tagEditing && m_inputRef) {
        const auto& chars = m_inputRef->GetTypedChars();
        for (char ch : chars) {
            if (ch == '\r' || ch == '\n') {
                if (!m_tagEditBuffer.empty()) {
                    emitter->tags.push_back(m_tagEditBuffer);
                }
                m_tagEditing = false;
                m_tagEditBuffer.clear();
                break;
            } else if (ch == 0x1B) { // Escape
                m_tagEditing = false;
                m_tagEditBuffer.clear();
                break;
            } else if (ch == '\b') {
                if (!m_tagEditBuffer.empty()) m_tagEditBuffer.pop_back();
            } else if ((ch >= 'a' && ch <= 'z') ||
                       (ch >= '0' && ch <= '9') ||
                       ch == '-' || ch == '_') {
                m_tagEditBuffer += ch;
            } else if (ch >= 'A' && ch <= 'Z') {
                // Force lowercase (matches the VfxAssetTagMatches contract).
                m_tagEditBuffer += static_cast<char>(ch - 'A' + 'a');
            }
            // Other chars (whitespace, punctuation) silently dropped.
        }
        m_wantsInput = true;
    }

    const float consumed = (curY + kChipH + kRowPad) - y;
    return consumed;
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
                            if (param.fPtr) NumberInput(px, y, pw, param.fPtr, param.min, param.max, param.label);
                            break;
                        case VfxModuleParam::Uint:
                            if (param.uPtr) {
                                float fVal = static_cast<float>(*param.uPtr);
                                if (NumberInput(px, y, pw, &fVal, param.min, param.max, param.label)) {
                                    *param.uPtr = static_cast<uint32_t>(std::clamp(fVal, param.min, param.max));
                                }
                            }
                            break;
                        case VfxModuleParam::Int:
                            if (param.iPtr) {
                                float fVal = static_cast<float>(*param.iPtr);
                                if (NumberInput(px, y, pw, &fVal, param.min, param.max, param.label)) {
                                    *param.iPtr = static_cast<int32_t>(std::clamp(fVal, param.min, param.max));
                                }
                            }
                            break;
                        case VfxModuleParam::String:
                            if (param.sPtr) {
                                StringInput(px, y, pw, param.sPtr,
                                            InputFilter::Any,
                                            (param.hint && *param.hint) ? param.hint : param.label);
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

// ─────────────────────────────────────────────────────────────────────────────
// BuildStackPanel — Niagara-parity stack editor (phase 11.2)
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::EnsureStackPanelCallbacks() {
    if (m_stackPanelCallbacksBound) return;
    m_stackPanelCallbacksBound = true;

    // "+ Add Module" → open the module picker at the mouse anchor.
    // Fully qualify the stack's `::uz::VfxStage` — the overlay class
    // also defines an inner `VfxStage` enum (legacy UI, 5 stages).
    m_stackPanel.onAddModuleRequested =
        [this](::uz::VfxStage stage) {
            m_modulePicker.Open(stage, m_mouseX, m_mouseY,
                                 static_cast<float>(m_screenW),
                                 static_cast<float>(m_screenH));
        };

    // Module picker confirm → append a new VfxModuleInstance to the
    // target VfxStackEntry on the currently-edited emitter. Undo/redo
    // integration lands in phase 11.9; for now the change is applied
    // directly so the author sees immediate feedback.
    m_modulePicker.onModuleChosen =
        [this](const std::string& name, ::uz::VfxStage stage) {
            ParticleEmitter* emitter = m_lastEmitter;
            if (emitter == nullptr) return;
            ::uz::VfxModuleInstance mod;
            mod.moduleTypeName = name;
            // Stable per-instance id (monotonic count suffix — matches
            // phase 11.1 contract without pulling a full UUID library).
            static uint64_t s_next = 0;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "m_%llu",
                           static_cast<unsigned long long>(++s_next));
            mod.instanceId = buf;
            emitter->stack.GetEntry(stage).Add(std::move(mod));
        };

    // Phase 12.8.0 — stack panel overflow menu → open context menu
    // at the clicked coords with the module-card actions (Rename /
    // Duplicate / Delete / Revert / Isolate). Callback identity keeps
    // references to `this` + `instanceId` for per-command dispatch.
    m_stackPanel.onModuleContextMenuRequested =
        [this](const std::string& instanceId, float x, float y) {
            std::vector<VfxContextMenuItem> items;
            {
                VfxContextMenuItem it;
                it.label      = "Rename";
                it.hotkey     = "F2";
                it.onActivate = [] {}; // host wires via 12.10
                items.push_back(it);
            }
            {
                VfxContextMenuItem it;
                it.label      = "Duplicate";
                it.hotkey     = "Ctrl+D";
                it.onActivate = [this, instanceId] {
                    if (m_stackPanel.onModuleDuplicateRequested) {
                        m_stackPanel.onModuleDuplicateRequested(instanceId);
                    }
                };
                items.push_back(it);
            }
            { VfxContextMenuItem sep; sep.isSeparator = true; items.push_back(sep); }
            {
                VfxContextMenuItem it;
                it.label      = "Delete";
                it.hotkey     = "Del";
                it.onActivate = [this, instanceId] {
                    if (m_stackPanel.onModuleRemoveRequested) {
                        m_stackPanel.onModuleRemoveRequested(instanceId);
                    }
                };
                items.push_back(it);
            }
            {
                VfxContextMenuItem it;
                it.label      = "Revert to Parent";
                it.onActivate = [this, instanceId] {
                    if (m_stackPanel.onRevertOverrideRequested) {
                        m_stackPanel.onRevertOverrideRequested(instanceId);
                    }
                };
                items.push_back(it);
            }
            { VfxContextMenuItem sep; sep.isSeparator = true; items.push_back(sep); }
            {
                VfxContextMenuItem it;
                it.label      = "Isolate";
                it.onActivate = [] {}; // host wires via 12.10
                items.push_back(it);
            }
            m_contextMenu.Open(x, y, std::move(items),
                                static_cast<float>(m_screenW),
                                static_cast<float>(m_screenH));
        };
}

void VfxEditorOverlay::BuildStackPanel(ParticleEmitter* emitter) {
    if (emitter == nullptr) return;
    m_stackPanel.Bind(&emitter->stack);
    m_stackPanel.SetEmitterEnabledPtr(&emitter->isEmitting);
    {
        // Prefer the host-provided emitter name (set via SetEmitterNames)
        // for the active index; fall back to a stable default.
        const int idx = m_activeEmitterIdx;
        const std::string name =
            (idx >= 0 && idx < static_cast<int>(m_emitterNames.size()))
                ? m_emitterNames[idx]
                : std::string("Emitter");
        m_stackPanel.SetEmitterName(name);
    }
    EnsureStackPanelCallbacks();

    const float panelX = 0.0f;
    const float panelY = kMenuBarH + kEmitterBarH;
    const float panelH = static_cast<float>(m_screenH) - panelY;

    // Panel background
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, panelH, kColorPanelBg);

    // Title bar
    constexpr float kCatHeaderH = 24.0f;
    m_textRenderer.AddRect(panelX, panelY, kLeftPanelW, kCatHeaderH, kColorBarBg);
    m_textRenderer.AddText(m_font, "Module Stack  (Niagara)",
                           panelX + 8.0f, panelY + 5.0f, kColorText);

    // Stack widget renders into the remaining panel area.
    const float contentTop = panelY + kCatHeaderH + 4.0f;
    const VfxStackRect bounds{
        panelX + kPanelPadX,
        contentTop,
        kLeftPanelW - 2.0f * kPanelPadX,
        panelH - kCatHeaderH - 4.0f
    };

    // Scroll feed-through
    if (HitTest(panelX, contentTop, kLeftPanelW, bounds.height) &&
        m_scrollDelta != 0.0f) {
        m_stackPanel.SetScrollOffset(
            std::max(0.0f, m_stackPanel.ScrollOffset()
                               - m_scrollDelta * kScrollSpeed));
    }
    const float contentH = m_stackPanel.ComputeContentHeight();
    const float maxScroll = std::max(0.0f, contentH - bounds.height);
    if (m_stackPanel.ScrollOffset() > maxScroll) {
        m_stackPanel.SetScrollOffset(maxScroll);
    }

    // Hit-test for click / hover. Relies on the widget's pure-data layout.
    const auto layout = m_stackPanel.ComputeLayout(bounds);
    const auto hit    = m_stackPanel.HitTest(layout, m_mouseX, m_mouseY);

    // Hover feedback
    if (hit.type == VfxStackItemType::ModuleCard ||
        hit.type == VfxStackItemType::CardCheckbox) {
        m_stackPanel.SetHoveredInstanceId(hit.instanceId);
    } else {
        m_stackPanel.SetHoveredInstanceId(std::string{});
    }

    // Click dispatch
    if (m_mouseJustPressed && hit.IsHit()) {
        switch (hit.type) {
        case VfxStackItemType::StageHeader:
            m_stackPanel.ToggleStage(hit.stage);
            break;
        case VfxStackItemType::CardCheckbox:
            if (auto* stage = &emitter->stack.GetEntry(hit.stage);
                stage != nullptr && hit.moduleIndex < stage->modules.size()) {
                auto& mod = stage->modules[hit.moduleIndex];
                mod.enabled = !mod.enabled;
                if (m_stackPanel.onModuleToggled) {
                    m_stackPanel.onModuleToggled(mod.instanceId, mod.enabled);
                }
            }
            break;
        case VfxStackItemType::ModuleCard:
            m_stackPanel.SelectModule(hit.instanceId);
            break;
        case VfxStackItemType::StageAddButton:
            if (m_stackPanel.onAddModuleRequested) {
                m_stackPanel.onAddModuleRequested(hit.stage);
            }
            break;
        case VfxStackItemType::CardDragHandle:
            // Clicking the drag handle both selects and begins a drag
            // (drag completion is dispatched by the overlay's mouse-up
            // handler once the reorder gesture is fully wired).
            m_stackPanel.SelectModule(hit.instanceId);
            m_stackPanel.BeginDrag(hit.instanceId);
            break;
        case VfxStackItemType::CardOverflowMenu:
            if (m_stackPanel.onModuleContextMenuRequested) {
                m_stackPanel.onModuleContextMenuRequested(
                    hit.instanceId, m_mouseX, m_mouseY);
            }
            break;
        case VfxStackItemType::EmitterHeader:
            // Emitter-header click is reserved for future emitter-wide
            // actions (rename, isolate); the enable checkbox is
            // handled internally by the widget via `m_emitterEnabledPtr`.
            break;
        case VfxStackItemType::AddRow:  // legacy — 12.2.0 never emits
        case VfxStackItemType::None:
            break;
        }
        m_wantsInput = true;
    }

    // Render the widget geometry.
    m_stackPanel.Render(m_textRenderer, m_font, bounds);
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildNiagaraUI — Full Niagara-parity composition (phase 11.14.0)
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorOverlay::EnsureCompositionBound() {
    if (m_compositionBound) return;
    m_compositionBound = true;

    m_composition.BindStackPanel(&m_stackPanel);
    m_composition.BindParameterInspector(&m_paramInspector);
    m_composition.BindCurvesEditor(&m_curvesEditor);
    m_composition.BindTimelineScrubber(&m_timelineScrubber);
    m_composition.BindModulePicker(&m_modulePicker);
    m_composition.BindUndoStack(&m_undoStack);
    m_composition.BindThumbnailDrainer(&m_thumbnailDrainer);
    m_composition.BindKeybindings(&m_keybindings);
    m_composition.BindSystemOverview(&m_systemOverview);

    // Phase 12.9.0 — bind overview widget to the overlay's backing view
    // list. `BuildNiagaraUI` refreshes the vector each frame.
    m_systemOverview.Bind(&m_overviewViews);

    // Phase 15.12 — forward the System Overview widget callbacks through
    // the overlay's public `on*` members so the host only wires one set
    // of handlers. Lambda captures `this` so re-assigning the public
    // callbacks at runtime takes effect immediately.
    m_systemOverview.onEmitterEnableToggled =
        [this](size_t idx, bool enabled) {
            if (onEmitterEnableToggled) onEmitterEnableToggled(idx, enabled);
        };
    m_systemOverview.onEmitterMoved =
        [this](size_t src, size_t dst) {
            if (onEmitterMoved) onEmitterMoved(src, dst);
        };
    m_systemOverview.onEmitterDuplicated =
        [this](size_t idx) {
            if (onEmitterDuplicated) onEmitterDuplicated(idx);
        };
    m_systemOverview.onEmitterDeleted =
        [this](size_t idx) {
            if (onEmitterDeleted) onEmitterDeleted(idx);
        };
    m_systemOverview.onAddEmitterRequested =
        [this]() {
            if (onAddEmitterRequested) onAddEmitterRequested();
        };

    // Timeline scrubber drives the overlay's existing preview clock so
    // play/pause/speed changes take effect on the next
    // `SimulationDeltaTime()` call the host performs.
    m_timelineScrubber.Bind(&previewClock_);

    EnsureStackPanelCallbacks();
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 15.5 split-layout input dispatch helpers.
//
// These mirror the input-routing blocks that live inside the legacy 3-column
// branch of `BuildNiagaraUI`. The 3-column code renders its own panels and
// handles input against their rects directly. In split-layout mode the
// widgets are rendered by `VfxEditorPaneWidget::Render`, so the overlay
// needs to dispatch input into them separately — otherwise clicks fall
// through and the stack / parameter / emitter panels become non-interactive.
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Phase 15.11 — color picker popup (HEX + RGBA + preset palette).
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Industry-standard preset palette surfaced in the color picker.
/// Matches the short-list used by most DCC colorpickers (Photoshop /
/// Figma / Unreal swatches) plus a transparent slot so authors can
/// zero alpha with one click.
struct ColorPreset { const char* label; Vec4 color; };
constexpr ColorPreset kColorPresets[] = {
    {"White",       {1.00f, 1.00f, 1.00f, 1.00f}},
    {"Black",       {0.00f, 0.00f, 0.00f, 1.00f}},
    {"Red",         {1.00f, 0.00f, 0.00f, 1.00f}},
    {"Green",       {0.00f, 1.00f, 0.00f, 1.00f}},
    {"Blue",        {0.00f, 0.00f, 1.00f, 1.00f}},
    {"Yellow",      {1.00f, 1.00f, 0.00f, 1.00f}},
    {"Cyan",        {0.00f, 1.00f, 1.00f, 1.00f}},
    {"Magenta",     {1.00f, 0.00f, 1.00f, 1.00f}},
    {"Orange",      {1.00f, 0.50f, 0.00f, 1.00f}},
    {"Purple",      {0.50f, 0.00f, 0.50f, 1.00f}},
    {"Pink",        {1.00f, 0.41f, 0.71f, 1.00f}},
    {"Teal",        {0.00f, 0.50f, 0.50f, 1.00f}},
    {"Gray 75",     {0.75f, 0.75f, 0.75f, 1.00f}},
    {"Gray 50",     {0.50f, 0.50f, 0.50f, 1.00f}},
    {"Gray 25",     {0.25f, 0.25f, 0.25f, 1.00f}},
    {"Transparent", {0.00f, 0.00f, 0.00f, 0.00f}},
};
constexpr size_t kColorPresetCount =
    sizeof(kColorPresets) / sizeof(kColorPresets[0]);

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

void RgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float cmax = std::max({r, g, b});
    const float cmin = std::min({r, g, b});
    const float d = cmax - cmin;
    v = cmax;
    s = (cmax > 0.0f) ? d / cmax : 0.0f;
    if (d < 1e-6f) { h = 0.0f; return; }
    float hh;
    if (cmax == r)      hh = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (cmax == g) hh = (b - r) / d + 2.0f;
    else                hh = (r - g) / d + 4.0f;
    h = hh / 6.0f;
}

void HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    h = h * 6.0f;
    if (h >= 6.0f) h -= 6.0f;
    if (h < 0.0f)  h += 6.0f;
    const int   i = static_cast<int>(std::floor(h)) % 6;
    const float f = h - std::floor(h);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        default: r = g = b = 0.0f; break;
    }
}

// Writes RRGGBBAA hex string into `out` (8 chars + NUL).
void FormatHex(const Vec4& col, char (&out)[16]) {
    const int rI = std::clamp(static_cast<int>(col.x * 255.0f + 0.5f), 0, 255);
    const int gI = std::clamp(static_cast<int>(col.y * 255.0f + 0.5f), 0, 255);
    const int bI = std::clamp(static_cast<int>(col.z * 255.0f + 0.5f), 0, 255);
    const int aI = std::clamp(static_cast<int>(col.w * 255.0f + 0.5f), 0, 255);
    std::snprintf(out, sizeof(out), "#%02X%02X%02X%02X", rI, gI, bI, aI);
}

// Parses "#RRGGBB" / "RRGGBB" / "#RRGGBBAA" / "RRGGBBAA" into RGBA.
// Returns true + out on success. Partial/invalid input = false.
bool ParseHex(const std::string& s, Vec4& out) {
    size_t i = 0;
    if (!s.empty() && s[0] == '#') i = 1;
    const size_t n = s.size() - i;
    if (n != 6u && n != 8u) return false;
    int vals[8];
    for (size_t k = 0; k < n; ++k) {
        const int v = HexNibble(s[i + k]);
        if (v < 0) return false;
        vals[k] = v;
    }
    out.x = static_cast<float>((vals[0] << 4) | vals[1]) / 255.0f;
    out.y = static_cast<float>((vals[2] << 4) | vals[3]) / 255.0f;
    out.z = static_cast<float>((vals[4] << 4) | vals[5]) / 255.0f;
    out.w = (n == 8u)
        ? static_cast<float>((vals[6] << 4) | vals[7]) / 255.0f : 1.0f;
    return true;
}

} // namespace

void VfxEditorOverlay::OpenColorPicker(float anchorX, float anchorY,
                                        float* r, float* g,
                                        float* b, float* a) {
    m_colorPicker.isOpen   = true;
    m_colorPicker.rgba[0]  = r;
    m_colorPicker.rgba[1]  = g;
    m_colorPicker.rgba[2]  = b;
    m_colorPicker.rgba[3]  = a;
    m_colorPicker.anchorX  = anchorX;
    m_colorPicker.anchorY  = anchorY;
    m_colorPicker.hexEditing = false;
    m_colorPicker.dragKind   = ColorPickerState::DragNone;
    if (r != nullptr && g != nullptr && b != nullptr && a != nullptr) {
        char buf[16];
        FormatHex(Vec4{*r, *g, *b, *a}, buf);
        m_colorPicker.hexBuffer = buf;
        RgbToHsv(*r, *g, *b,
                 m_colorPicker.hsv[0],
                 m_colorPicker.hsv[1],
                 m_colorPicker.hsv[2]);
    }
}

void VfxEditorOverlay::CloseColorPicker() {
    m_colorPicker.isOpen     = false;
    m_colorPicker.rgba[0]    = nullptr;
    m_colorPicker.rgba[1]    = nullptr;
    m_colorPicker.rgba[2]    = nullptr;
    m_colorPicker.rgba[3]    = nullptr;
    m_colorPicker.hexEditing = false;
}

// ── Popup geometry (shared by Render + Input so clicks match pixels) ──
namespace {
struct ColorPickerLayout {
    float px, py;
    float popupW, popupH;
    float quadX, quadY, quadSize;
    float hueX,  hueY,  hueW;
    float alphaX, alphaY, alphaW, alphaH;
    float prevX, prevY, prevSize;
    float hexX, hexY, hexW, hexH;
    float xBtnX, xBtnY, xBtnSize;
    float gridX, gridY;
    float cellSize, cellGap;
};

ColorPickerLayout ComputeColorPickerLayout(float anchorX, float anchorY,
                                             float screenW, float screenH) {
    ColorPickerLayout L{};
    L.popupW = 360.0f;
    L.popupH = 400.0f;

    L.px = anchorX;
    L.py = anchorY;
    if (L.px + L.popupW > screenW) L.px = screenW - L.popupW;
    if (L.py + L.popupH > screenH) L.py = screenH - L.popupH;
    if (L.px < 0.0f) L.px = 0.0f;
    if (L.py < 0.0f) L.py = 0.0f;

    L.xBtnSize = 16.0f;
    L.xBtnX    = L.px + L.popupW - L.xBtnSize - 6.0f;
    L.xBtnY    = L.py + 4.0f;

    L.quadSize = 180.0f;
    L.quadX    = L.px + 12.0f;
    L.quadY    = L.py + 34.0f;

    L.hueW = 18.0f;
    L.hueX = L.quadX + L.quadSize + 8.0f;
    L.hueY = L.quadY;

    // Preview + HEX column on the right of the hue strip.
    L.prevSize = 60.0f;
    L.prevX    = L.hueX + L.hueW + 12.0f;
    L.prevY    = L.quadY;

    L.hexX = L.prevX;
    L.hexY = L.prevY + L.prevSize + 10.0f;
    L.hexW = L.px + L.popupW - L.hexX - 12.0f;
    L.hexH = 22.0f;

    L.alphaX = L.quadX;
    L.alphaY = L.quadY + L.quadSize + 10.0f;
    L.alphaW = L.quadSize + 8.0f + L.hueW;
    L.alphaH = 18.0f;

    L.cellSize = 28.0f;
    L.cellGap  = 4.0f;
    L.gridX    = L.px + 12.0f;
    L.gridY    = L.alphaY + L.alphaH + 18.0f;

    return L;
}
} // namespace

void VfxEditorOverlay::RenderColorPicker() {
    if (!m_colorPicker.isOpen) return;
    if (m_colorPicker.rgba[0] == nullptr) { CloseColorPicker(); return; }

    const auto& palette = VfxThemeColors();

    const float sw = static_cast<float>(m_screenW);
    const float sh = static_cast<float>(m_screenH);
    const ColorPickerLayout L = ComputeColorPickerLayout(
        m_colorPicker.anchorX, m_colorPicker.anchorY, sw, sh);
    const float px = L.px, py = L.py;
    const float kPopupW = L.popupW, kPopupH = L.popupH;

    // Backdrop + border.
    m_textRenderer.AddRect(px, py, kPopupW, kPopupH, palette.popupBg);
    m_textRenderer.AddRect(px, py, kPopupW, 1.0f, palette.borderStrong);
    m_textRenderer.AddRect(px, py + kPopupH - 1.0f, kPopupW, 1.0f, palette.borderStrong);
    m_textRenderer.AddRect(px, py, 1.0f, kPopupH, palette.borderStrong);
    m_textRenderer.AddRect(px + kPopupW - 1.0f, py, 1.0f, kPopupH, palette.borderStrong);

    // Header + close X.
    m_textRenderer.AddRect(px, py, kPopupW, 24.0f, palette.barBg);
    m_textRenderer.AddText(m_font, "Color Picker",
                            px + 8.0f, py + 6.0f, palette.textPrimary);
    m_textRenderer.AddText(m_font, "X",
                            L.xBtnX + 4.0f, L.xBtnY + 2.0f, palette.textPrimary);

    const Vec4 col{
        *m_colorPicker.rgba[0], *m_colorPicker.rgba[1],
        *m_colorPicker.rgba[2], *m_colorPicker.rgba[3]
    };

    // ── SV quadrant ───────────────────────────────────────────────
    // Photoshop-style saturation (x) × value (y-inverted) square. The
    // cell grid is 24×24 so 576 tiny rects light up the square at an
    // acceptable per-frame cost for a modal popup.
    {
        constexpr int kCells = 24;
        const float cellW = L.quadSize / kCells;
        const float hue = m_colorPicker.hsv[0];
        for (int yy = 0; yy < kCells; ++yy) {
            for (int xx = 0; xx < kCells; ++xx) {
                const float s = (static_cast<float>(xx) + 0.5f) / kCells;
                const float v = 1.0f - (static_cast<float>(yy) + 0.5f) / kCells;
                float rr, gg, bb;
                HsvToRgb(hue, s, v, rr, gg, bb);
                m_textRenderer.AddRect(L.quadX + static_cast<float>(xx) * cellW,
                                         L.quadY + static_cast<float>(yy) * cellW,
                                         cellW + 0.5f, cellW + 0.5f,
                                         Vec4{rr, gg, bb, 1.0f});
            }
        }
        // Border.
        m_textRenderer.AddRect(L.quadX, L.quadY, L.quadSize, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.quadX, L.quadY + L.quadSize - 1.0f, L.quadSize, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.quadX, L.quadY, 1.0f, L.quadSize, palette.borderStrong);
        m_textRenderer.AddRect(L.quadX + L.quadSize - 1.0f, L.quadSize + L.quadY, 1.0f, -L.quadSize, palette.borderStrong);
        m_textRenderer.AddRect(L.quadX + L.quadSize - 1.0f, L.quadY, 1.0f, L.quadSize, palette.borderStrong);
        // Crosshair at (sat, val).
        const float cx = L.quadX + m_colorPicker.hsv[1] * L.quadSize;
        const float cy = L.quadY + (1.0f - m_colorPicker.hsv[2]) * L.quadSize;
        m_textRenderer.AddRect(cx - 6.0f, cy - 1.0f, 12.0f, 2.0f, Vec4{1,1,1,1});
        m_textRenderer.AddRect(cx - 1.0f, cy - 6.0f, 2.0f, 12.0f, Vec4{1,1,1,1});
        m_textRenderer.AddRect(cx - 5.0f, cy, 10.0f, 1.0f, Vec4{0,0,0,1});
        m_textRenderer.AddRect(cx, cy - 5.0f, 1.0f, 10.0f, Vec4{0,0,0,1});
    }

    // ── Hue strip (vertical rainbow) ──────────────────────────────
    {
        constexpr int kSlices = 48;
        const float sliceH = L.quadSize / kSlices;
        for (int i = 0; i < kSlices; ++i) {
            const float h = (static_cast<float>(i) + 0.5f) / kSlices;
            float rr, gg, bb;
            HsvToRgb(h, 1.0f, 1.0f, rr, gg, bb);
            m_textRenderer.AddRect(L.hueX,
                                     L.hueY + static_cast<float>(i) * sliceH,
                                     L.hueW, sliceH + 0.5f,
                                     Vec4{rr, gg, bb, 1.0f});
        }
        m_textRenderer.AddRect(L.hueX, L.hueY, L.hueW, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.hueX, L.hueY + L.quadSize - 1.0f, L.hueW, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.hueX, L.hueY, 1.0f, L.quadSize, palette.borderStrong);
        m_textRenderer.AddRect(L.hueX + L.hueW - 1.0f, L.hueY, 1.0f, L.quadSize, palette.borderStrong);
        // Hue indicator (horizontal bar).
        const float hy = L.hueY + m_colorPicker.hsv[0] * L.quadSize;
        m_textRenderer.AddRect(L.hueX - 3.0f, hy - 1.5f, L.hueW + 6.0f, 3.0f, Vec4{1,1,1,1});
        m_textRenderer.AddRect(L.hueX - 2.0f, hy - 0.5f, L.hueW + 4.0f, 1.0f, Vec4{0,0,0,1});
    }

    // ── Alpha slider (horizontal) ─────────────────────────────────
    {
        // Checker underlay so transparent reads.
        const Vec4 chkA{0.22f, 0.22f, 0.22f, 1.0f};
        const Vec4 chkB{0.32f, 0.32f, 0.32f, 1.0f};
        const float chk = L.alphaH * 0.5f;
        const int chkCols = std::max(1, static_cast<int>(L.alphaW / chk));
        for (int qx = 0; qx < chkCols; ++qx) {
            for (int qy = 0; qy < 2; ++qy) {
                const Vec4 cc = (((qx + qy) & 1) != 0) ? chkA : chkB;
                const float cxx = L.alphaX + static_cast<float>(qx) * chk;
                const float cyy = L.alphaY + static_cast<float>(qy) * chk;
                const float cw = std::min(chk, L.alphaX + L.alphaW - cxx);
                m_textRenderer.AddRect(cxx, cyy, cw, chk, cc);
            }
        }
        // Opacity gradient (RGB constant, alpha 0 → 1).
        constexpr int kSlices = 48;
        const float sliceW = L.alphaW / kSlices;
        for (int i = 0; i < kSlices; ++i) {
            const float a = (static_cast<float>(i) + 0.5f) / kSlices;
            m_textRenderer.AddRect(L.alphaX + static_cast<float>(i) * sliceW,
                                     L.alphaY, sliceW + 0.5f, L.alphaH,
                                     Vec4{col.x, col.y, col.z, a});
        }
        // Border + marker.
        m_textRenderer.AddRect(L.alphaX, L.alphaY, L.alphaW, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.alphaX, L.alphaY + L.alphaH - 1.0f, L.alphaW, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.alphaX, L.alphaY, 1.0f, L.alphaH, palette.borderStrong);
        m_textRenderer.AddRect(L.alphaX + L.alphaW - 1.0f, L.alphaY, 1.0f, L.alphaH, palette.borderStrong);
        const float ax = L.alphaX + col.w * L.alphaW;
        m_textRenderer.AddRect(ax - 1.5f, L.alphaY - 3.0f, 3.0f, L.alphaH + 6.0f, Vec4{1,1,1,1});
        m_textRenderer.AddRect(ax - 0.5f, L.alphaY - 2.0f, 1.0f, L.alphaH + 4.0f, Vec4{0,0,0,1});
    }

    // ── Preview + HEX + RGB readout ────────────────────────────────
    {
        const Vec4 chkA{0.22f, 0.22f, 0.22f, 1.0f};
        const Vec4 chkB{0.32f, 0.32f, 0.32f, 1.0f};
        const float chk = L.prevSize / 6.0f;
        for (int cy = 0; cy < 6; ++cy) {
            for (int cx = 0; cx < 6; ++cx) {
                const Vec4 cc = (((cx + cy) & 1) != 0) ? chkA : chkB;
                m_textRenderer.AddRect(L.prevX + static_cast<float>(cx) * chk,
                                         L.prevY + static_cast<float>(cy) * chk,
                                         chk, chk, cc);
            }
        }
        m_textRenderer.AddRect(L.prevX, L.prevY, L.prevSize, L.prevSize, col);
        m_textRenderer.AddRect(L.prevX, L.prevY, L.prevSize, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.prevX, L.prevY + L.prevSize - 1.0f, L.prevSize, 1.0f, palette.borderStrong);
        m_textRenderer.AddRect(L.prevX, L.prevY, 1.0f, L.prevSize, palette.borderStrong);
        m_textRenderer.AddRect(L.prevX + L.prevSize - 1.0f, L.prevY, 1.0f, L.prevSize, palette.borderStrong);
    }

    // HEX field.
    {
        const Vec4 hexBg = m_colorPicker.hexEditing
            ? palette.cardBgSelected : palette.inputBg;
        m_textRenderer.AddRect(L.hexX, L.hexY, L.hexW, L.hexH, hexBg);
        std::string hexDisplay = m_colorPicker.hexEditing
            ? (m_colorPicker.hexBuffer + "_")
            : m_colorPicker.hexBuffer;
        m_textRenderer.AddText(m_font, hexDisplay,
                                L.hexX + 6.0f, L.hexY + 5.0f,
                                palette.textPrimary);

        char rgbBuf[96];
        const int rI = std::clamp(static_cast<int>(col.x * 255.0f + 0.5f), 0, 255);
        const int gI = std::clamp(static_cast<int>(col.y * 255.0f + 0.5f), 0, 255);
        const int bI = std::clamp(static_cast<int>(col.z * 255.0f + 0.5f), 0, 255);
        const int aI = std::clamp(static_cast<int>(col.w * 255.0f + 0.5f), 0, 255);
        std::snprintf(rgbBuf, sizeof(rgbBuf), "R %3d G %3d B %3d A %3d",
                       rI, gI, bI, aI);
        m_textRenderer.AddText(m_font, rgbBuf,
                                L.hexX, L.hexY + L.hexH + 6.0f,
                                palette.textSecondary);
    }

    // ── Preset palette ────────────────────────────────────────────
    {
        m_textRenderer.AddText(m_font, "Presets",
                                L.gridX, L.gridY - 14.0f, palette.textSecondary);
        constexpr int kCols = 4;
        for (size_t i = 0; i < kColorPresetCount; ++i) {
            const int row = static_cast<int>(i) / kCols;
            const int c   = static_cast<int>(i) % kCols;
            const float cx = L.gridX + static_cast<float>(c) * (L.cellSize + L.cellGap);
            const float cy = L.gridY + static_cast<float>(row) * (L.cellSize + L.cellGap);
            const Vec4 chkA{0.22f, 0.22f, 0.22f, 1.0f};
            const Vec4 chkB{0.32f, 0.32f, 0.32f, 1.0f};
            const float chk = L.cellSize * 0.25f;
            for (int qy = 0; qy < 4; ++qy) {
                for (int qx = 0; qx < 4; ++qx) {
                    const Vec4 cc = (((qx + qy) & 1) != 0) ? chkA : chkB;
                    m_textRenderer.AddRect(cx + static_cast<float>(qx) * chk,
                                             cy + static_cast<float>(qy) * chk,
                                             chk, chk, cc);
                }
            }
            m_textRenderer.AddRect(cx, cy, L.cellSize, L.cellSize, kColorPresets[i].color);
            m_textRenderer.AddRect(cx, cy, L.cellSize, 1.0f, palette.borderSubtle);
            m_textRenderer.AddRect(cx, cy + L.cellSize - 1.0f, L.cellSize, 1.0f, palette.borderSubtle);
            m_textRenderer.AddRect(cx, cy, 1.0f, L.cellSize, palette.borderSubtle);
            m_textRenderer.AddRect(cx + L.cellSize - 1.0f, cy, 1.0f, L.cellSize, palette.borderSubtle);
        }
    }
}

void VfxEditorOverlay::HandleColorPickerInput() {
    if (!m_colorPicker.isOpen) return;
    if (m_colorPicker.rgba[0] == nullptr) { CloseColorPicker(); return; }

    const float sw = static_cast<float>(m_screenW);
    const float sh = static_cast<float>(m_screenH);
    const ColorPickerLayout L = ComputeColorPickerLayout(
        m_colorPicker.anchorX, m_colorPicker.anchorY, sw, sh);

    const bool cursorInside =
        m_mouseX >= L.px && m_mouseX < L.px + L.popupW &&
        m_mouseY >= L.py && m_mouseY < L.py + L.popupH;
    if (cursorInside) m_wantsInput = true;

    auto writeFromHsv = [this]() {
        float rr, gg, bb;
        HsvToRgb(m_colorPicker.hsv[0], m_colorPicker.hsv[1],
                 m_colorPicker.hsv[2], rr, gg, bb);
        if (m_colorPicker.rgba[0]) *m_colorPicker.rgba[0] = rr;
        if (m_colorPicker.rgba[1]) *m_colorPicker.rgba[1] = gg;
        if (m_colorPicker.rgba[2]) *m_colorPicker.rgba[2] = bb;
        // alpha untouched.
    };

    // ── Active drag: update values based on cursor position ───────
    if (m_colorPicker.dragKind != ColorPickerState::DragNone) {
        if (!m_mouseDown) {
            m_colorPicker.dragKind = ColorPickerState::DragNone;
        } else {
            m_wantsInput = true;
            switch (m_colorPicker.dragKind) {
            case ColorPickerState::DragSV: {
                const float s = std::clamp(
                    (m_mouseX - L.quadX) / L.quadSize, 0.0f, 1.0f);
                const float v = std::clamp(
                    1.0f - (m_mouseY - L.quadY) / L.quadSize, 0.0f, 1.0f);
                m_colorPicker.hsv[1] = s;
                m_colorPicker.hsv[2] = v;
                writeFromHsv();
                break;
            }
            case ColorPickerState::DragHue: {
                const float h = std::clamp(
                    (m_mouseY - L.hueY) / L.quadSize, 0.0f, 1.0f);
                m_colorPicker.hsv[0] = h;
                writeFromHsv();
                break;
            }
            case ColorPickerState::DragAlpha: {
                const float a = std::clamp(
                    (m_mouseX - L.alphaX) / L.alphaW, 0.0f, 1.0f);
                if (m_colorPicker.rgba[3]) *m_colorPicker.rgba[3] = a;
                break;
            }
            default: break;
            }
            // Resync HEX buffer when not editing.
            if (!m_colorPicker.hexEditing) {
                const Vec4 cur{
                    *m_colorPicker.rgba[0], *m_colorPicker.rgba[1],
                    *m_colorPicker.rgba[2], *m_colorPicker.rgba[3]
                };
                char buf[16];
                FormatHex(cur, buf);
                m_colorPicker.hexBuffer = buf;
            }
        }
    }

    // ── Typed HEX input ───────────────────────────────────────────
    if (m_colorPicker.hexEditing && m_inputRef != nullptr) {
        const auto& chars = m_inputRef->GetTypedChars();
        for (char ch : chars) {
            if (ch == '\r' || ch == '\n') {
                Vec4 parsed{};
                if (ParseHex(m_colorPicker.hexBuffer, parsed)) {
                    if (m_colorPicker.rgba[0]) *m_colorPicker.rgba[0] = parsed.x;
                    if (m_colorPicker.rgba[1]) *m_colorPicker.rgba[1] = parsed.y;
                    if (m_colorPicker.rgba[2]) *m_colorPicker.rgba[2] = parsed.z;
                    if (m_colorPicker.rgba[3]) *m_colorPicker.rgba[3] = parsed.w;
                    RgbToHsv(parsed.x, parsed.y, parsed.z,
                             m_colorPicker.hsv[0],
                             m_colorPicker.hsv[1],
                             m_colorPicker.hsv[2]);
                }
                m_colorPicker.hexEditing = false;
            } else if (ch == 0x1B) {
                const Vec4 cur{
                    *m_colorPicker.rgba[0], *m_colorPicker.rgba[1],
                    *m_colorPicker.rgba[2], *m_colorPicker.rgba[3]
                };
                char buf[16];
                FormatHex(cur, buf);
                m_colorPicker.hexBuffer  = buf;
                m_colorPicker.hexEditing = false;
            } else if (ch == '\b' || ch == 0x7F) {
                if (!m_colorPicker.hexBuffer.empty())
                    m_colorPicker.hexBuffer.pop_back();
            } else if ((ch >= '0' && ch <= '9') ||
                        (ch >= 'a' && ch <= 'f') ||
                        (ch >= 'A' && ch <= 'F') ||
                        ch == '#') {
                if (m_colorPicker.hexBuffer.size() < 9u) {
                    m_colorPicker.hexBuffer.push_back(ch);
                }
            }
        }
        Vec4 parsed{};
        if (ParseHex(m_colorPicker.hexBuffer, parsed)) {
            if (m_colorPicker.rgba[0]) *m_colorPicker.rgba[0] = parsed.x;
            if (m_colorPicker.rgba[1]) *m_colorPicker.rgba[1] = parsed.y;
            if (m_colorPicker.rgba[2]) *m_colorPicker.rgba[2] = parsed.z;
            if (m_colorPicker.rgba[3]) *m_colorPicker.rgba[3] = parsed.w;
            RgbToHsv(parsed.x, parsed.y, parsed.z,
                     m_colorPicker.hsv[0],
                     m_colorPicker.hsv[1],
                     m_colorPicker.hsv[2]);
        }
        m_wantsInput = true;
    } else if (m_colorPicker.dragKind == ColorPickerState::DragNone) {
        // Idle: keep HEX mirror fresh so reopens / preset clicks show
        // the right code.
        const Vec4 cur{
            *m_colorPicker.rgba[0], *m_colorPicker.rgba[1],
            *m_colorPicker.rgba[2], *m_colorPicker.rgba[3]
        };
        char buf[16];
        FormatHex(cur, buf);
        m_colorPicker.hexBuffer = buf;
    }

    if (!m_mouseJustPressed) return;

    // ── Close X ───────────────────────────────────────────────────
    if (m_mouseX >= L.xBtnX && m_mouseX < L.xBtnX + L.xBtnSize &&
        m_mouseY >= L.xBtnY && m_mouseY < L.xBtnY + L.xBtnSize) {
        CloseColorPicker();
        m_wantsInput = true;
        return;
    }

    // ── SV quadrant press → start SV drag ─────────────────────────
    if (m_mouseX >= L.quadX && m_mouseX < L.quadX + L.quadSize &&
        m_mouseY >= L.quadY && m_mouseY < L.quadY + L.quadSize) {
        m_colorPicker.dragKind = ColorPickerState::DragSV;
        const float s = std::clamp((m_mouseX - L.quadX) / L.quadSize, 0.0f, 1.0f);
        const float v = std::clamp(1.0f - (m_mouseY - L.quadY) / L.quadSize, 0.0f, 1.0f);
        m_colorPicker.hsv[1] = s;
        m_colorPicker.hsv[2] = v;
        writeFromHsv();
        m_wantsInput = true;
        return;
    }

    // ── Hue strip press → start hue drag ──────────────────────────
    if (m_mouseX >= L.hueX && m_mouseX < L.hueX + L.hueW &&
        m_mouseY >= L.hueY && m_mouseY < L.hueY + L.quadSize) {
        m_colorPicker.dragKind = ColorPickerState::DragHue;
        m_colorPicker.hsv[0] = std::clamp(
            (m_mouseY - L.hueY) / L.quadSize, 0.0f, 1.0f);
        writeFromHsv();
        m_wantsInput = true;
        return;
    }

    // ── Alpha strip press → start alpha drag ──────────────────────
    if (m_mouseX >= L.alphaX && m_mouseX < L.alphaX + L.alphaW &&
        m_mouseY >= L.alphaY && m_mouseY < L.alphaY + L.alphaH) {
        m_colorPicker.dragKind = ColorPickerState::DragAlpha;
        const float a = std::clamp((m_mouseX - L.alphaX) / L.alphaW, 0.0f, 1.0f);
        if (m_colorPicker.rgba[3]) *m_colorPicker.rgba[3] = a;
        m_wantsInput = true;
        return;
    }

    // ── HEX field press → focus for typing ────────────────────────
    if (m_mouseX >= L.hexX && m_mouseX < L.hexX + L.hexW &&
        m_mouseY >= L.hexY && m_mouseY < L.hexY + L.hexH) {
        m_colorPicker.hexEditing = true;
        m_wantsInput = true;
        return;
    }

    // ── Preset grid press → apply RGBA ────────────────────────────
    {
        constexpr int kCols = 4;
        for (size_t i = 0; i < kColorPresetCount; ++i) {
            const int row = static_cast<int>(i) / kCols;
            const int c   = static_cast<int>(i) % kCols;
            const float cx = L.gridX + static_cast<float>(c) * (L.cellSize + L.cellGap);
            const float cy = L.gridY + static_cast<float>(row) * (L.cellSize + L.cellGap);
            if (m_mouseX >= cx && m_mouseX < cx + L.cellSize &&
                m_mouseY >= cy && m_mouseY < cy + L.cellSize) {
                const Vec4 pc = kColorPresets[i].color;
                if (m_colorPicker.rgba[0]) *m_colorPicker.rgba[0] = pc.x;
                if (m_colorPicker.rgba[1]) *m_colorPicker.rgba[1] = pc.y;
                if (m_colorPicker.rgba[2]) *m_colorPicker.rgba[2] = pc.z;
                if (m_colorPicker.rgba[3]) *m_colorPicker.rgba[3] = pc.w;
                RgbToHsv(pc.x, pc.y, pc.z,
                         m_colorPicker.hsv[0],
                         m_colorPicker.hsv[1],
                         m_colorPicker.hsv[2]);
                char buf[16];
                FormatHex(pc, buf);
                m_colorPicker.hexBuffer = buf;
                m_wantsInput = true;
                return;
            }
        }
    }

    // Click outside popup closes it; click inside on no element
    // merely exits HEX-edit mode.
    if (!cursorInside) {
        CloseColorPicker();
    } else {
        m_colorPicker.hexEditing = false;
    }
}

void VfxEditorOverlay::DispatchStackPanelInput(const VfxStackRect& bounds,
                                                 ParticleEmitter* emitter) {
    if (emitter == nullptr) return;
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    // Scroll feed-through when the cursor is over the stack body.
    if (bounds.Contains(m_mouseX, m_mouseY) && m_scrollDelta != 0.0f) {
        m_stackPanel.SetScrollOffset(
            std::max(0.0f, m_stackPanel.ScrollOffset()
                               - m_scrollDelta * kScrollSpeed));
    }
    const float contentH = m_stackPanel.ComputeContentHeight();
    const float maxScroll = std::max(0.0f, contentH - bounds.height);
    if (m_stackPanel.ScrollOffset() > maxScroll) {
        m_stackPanel.SetScrollOffset(maxScroll);
    }

    const auto layout = m_stackPanel.ComputeLayout(bounds);
    const auto hit    = m_stackPanel.HitTest(layout, m_mouseX, m_mouseY);

    if (hit.type == VfxStackItemType::ModuleCard ||
        hit.type == VfxStackItemType::CardCheckbox) {
        m_stackPanel.SetHoveredInstanceId(hit.instanceId);
    } else {
        m_stackPanel.SetHoveredInstanceId(std::string{});
    }

    if (m_mouseJustPressed && hit.IsHit()) {
        switch (hit.type) {
        case VfxStackItemType::StageHeader:
            m_stackPanel.ToggleStage(hit.stage);
            break;
        case VfxStackItemType::CardCheckbox:
            if (auto* stage = &emitter->stack.GetEntry(hit.stage);
                stage != nullptr && hit.moduleIndex < stage->modules.size()) {
                auto& mod = stage->modules[hit.moduleIndex];
                mod.enabled = !mod.enabled;
                if (m_stackPanel.onModuleToggled) {
                    m_stackPanel.onModuleToggled(mod.instanceId, mod.enabled);
                }
            }
            break;
        case VfxStackItemType::ModuleCard:
            m_stackPanel.SelectModule(hit.instanceId);
            m_composition.SetFocusedPanel(VfxFocusedPanel::Stack);
            break;
        case VfxStackItemType::StageAddButton:
            if (m_stackPanel.onAddModuleRequested) {
                m_stackPanel.onAddModuleRequested(hit.stage);
            }
            break;
        case VfxStackItemType::CardDragHandle:
            m_stackPanel.SelectModule(hit.instanceId);
            m_stackPanel.BeginDrag(hit.instanceId);
            m_composition.SetFocusedPanel(VfxFocusedPanel::Stack);
            break;
        case VfxStackItemType::CardOverflowMenu:
            if (m_stackPanel.onModuleContextMenuRequested) {
                m_stackPanel.onModuleContextMenuRequested(
                    hit.instanceId, m_mouseX, m_mouseY);
            }
            break;
        case VfxStackItemType::EmitterHeader:
        case VfxStackItemType::AddRow:
        case VfxStackItemType::None:
            break;
        }
        m_wantsInput = true;
    }
}

void VfxEditorOverlay::DispatchParamInspectorInput(const VfxStackRect& bounds) {
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    auto* store = m_paramInspector.Store();
    const auto layout = m_paramInspector.ComputeLayout(bounds);
    const auto hit    = m_paramInspector.HitTest(layout, m_mouseX, m_mouseY);

    // ── Left-click ─────────────────────────────────────────────────────
    //
    // EditorBool   — press toggles the bool (stored as a Vec4 with x=0/1).
    // EditorFloat  — press starts a horizontal scrub drag on component 0.
    // EditorVec3   — press starts a drag on component 0 (X). Finer-grained
    //                per-component editing is a follow-up; for now X
    //                covers the common "tweak emission direction" flow.
    // EditorColor  — press starts a drag on R. Full RGBA picker is a
    //                follow-up.
    if (m_mouseJustPressed && bounds.Contains(m_mouseX, m_mouseY) &&
        store != nullptr) {
        switch (hit.type) {
        case VfxParamItemType::EditorBool: {
            const Vec4 cur = store->GetVec4(hit.scope, hit.name, Vec4{});
            const float nx = (cur.x != 0.0f) ? 0.0f : 1.0f;
            store->SetVec4(hit.scope, hit.name,
                           Vec4{nx, cur.y, cur.z, cur.w});
            m_wantsInput = true;
            break;
        }
        case VfxParamItemType::EditorFloat:
        case VfxParamItemType::EditorVec3:
        case VfxParamItemType::EditorColor: {
            const Vec4 cur = store->GetVec4(hit.scope, hit.name, Vec4{});
            m_paramDragActive        = true;
            m_paramDragScopeInt      = static_cast<int>(hit.scope);
            m_paramDragName          = hit.name;
            m_paramDragComponent     = 0u;
            m_paramDragMouseXAtStart = m_mouseX;
            m_paramDragValueAtStart  = cur.x;
            // Sensitivity: EditorColor is 0..1 so use fine step; Float /
            // Vec3 use coarser step since typical ranges are larger.
            m_paramDragScale =
                (hit.type == VfxParamItemType::EditorColor) ? 0.005f : 0.05f;
            m_wantsInput = true;
            break;
        }
        default:
            break;
        }
    }

    // Active drag: update value based on horizontal cursor delta. Stops
    // on mouse release. Vertical motion is ignored so the user can keep
    // the cursor comfortably inside the row.
    if (m_paramDragActive && store != nullptr) {
        if (m_mouseDown) {
            const float dx = m_mouseX - m_paramDragMouseXAtStart;
            const float newVal = m_paramDragValueAtStart + dx * m_paramDragScale;
            const auto scope = static_cast<VfxAttributeScope>(m_paramDragScopeInt);
            const Vec4 cur = store->GetVec4(scope, m_paramDragName, Vec4{});
            Vec4 nxt = cur;
            switch (m_paramDragComponent) {
            case 0u: nxt.x = newVal; break;
            case 1u: nxt.y = newVal; break;
            case 2u: nxt.z = newVal; break;
            case 3u: nxt.w = newVal; break;
            default: break;
            }
            store->SetVec4(scope, m_paramDragName, nxt);
            m_wantsInput = true;
        } else {
            m_paramDragActive = false;
        }
    }

    // ── Right-click context menu (copy / paste / reset / bind) ────────
    if (!m_mouseRightJustPressed) return;
    if (!bounds.Contains(m_mouseX, m_mouseY)) return;

    std::vector<VfxContextMenuItem> items;
    { VfxContextMenuItem copy;  copy.label   = "Copy Value";  copy.hotkey = "Ctrl+C"; items.push_back(copy); }
    { VfxContextMenuItem paste; paste.label  = "Paste Value"; paste.hotkey = "Ctrl+V"; items.push_back(paste); }
    { VfxContextMenuItem sep;   sep.isSeparator = true; items.push_back(sep); }
    VfxContextMenuItem reset; reset.label = "Reset to Default";
    if (!hit.name.empty() && m_paramInspector.onParameterReset) {
        const auto scope = hit.scope;
        const std::string n = hit.name;
        reset.onActivate = [this, scope, n] {
            if (m_paramInspector.onParameterReset) m_paramInspector.onParameterReset(scope, n);
        };
    }
    items.push_back(reset);
    { VfxContextMenuItem sep; sep.isSeparator = true; items.push_back(sep); }
    VfxContextMenuItem bind; bind.label = "Bind to Curve...";
    if (!hit.name.empty() && m_paramInspector.onParameterBindRequested) {
        const auto scope = hit.scope;
        const std::string n = hit.name;
        bind.onActivate = [this, scope, n] {
            if (m_paramInspector.onParameterBindRequested) m_paramInspector.onParameterBindRequested(scope, n);
        };
    }
    items.push_back(bind);

    m_contextMenu.Open(m_mouseX, m_mouseY, std::move(items),
                        static_cast<float>(m_screenW),
                        static_cast<float>(m_screenH));
    m_wantsInput = true;
}

void VfxEditorOverlay::DispatchTreeAndOverviewInput(
        const VfxStackRect& treeBounds,
        const VfxStackRect& overviewBounds,
        ParticleEmitter* emitter) {
    if (emitter == nullptr) return;

    const auto& pal = VfxThemeColors();

    // Rebuild the tree each frame from the current emitter list so new
    // emitters show up without a manual refresh.
    m_emitterTree.Clear();
    {
        VfxTreeNode root{};
        root.id          = 0u;
        root.label       = "Emitters";
        root.depth       = 0u;
        root.hasChildren = true;
        root.expanded    = m_emitterRootExpanded;
        root.iconTint    = pal.textSecondary;
        m_emitterTree.AddNode(root);
        for (size_t i = 0; i < m_emitterNames.size(); ++i) {
            VfxTreeNode n{};
            n.id          = static_cast<uint32_t>(i + 1);
            n.label       = m_emitterNames[i];
            n.depth       = 1u;
            n.icon        = VfxUiIcon::Dot;
            n.iconTint    = pal.stateSuccess;
            n.expanded    = true;
            n.hasChildren = false;
            m_emitterTree.AddNode(n);
        }
    }
    if (m_activeEmitterIdx >= 0) {
        m_emitterTree.SetSelectedId(
            static_cast<uint32_t>(m_activeEmitterIdx + 1));
    }

    if (treeBounds.width > 0.0f && treeBounds.height > 0.0f) {
        const auto treeLayout = m_emitterTree.ComputeLayout(treeBounds);
        const auto hit = m_emitterTree.HitTest(treeLayout, m_mouseX, m_mouseY);
        m_emitterTree.SetHoveredId(
            (hit.type != VfxTreeItemType::None) ? hit.nodeId : 0u);
        if (m_mouseJustPressed && hit.type != VfxTreeItemType::None) {
            if (hit.type == VfxTreeItemType::ExpandArrow && hit.nodeId == 0u) {
                m_emitterRootExpanded = !m_emitterRootExpanded;
                m_wantsInput = true;
            } else if (hit.type == VfxTreeItemType::Row && hit.nodeId >= 1u) {
                const size_t idx = hit.nodeId - 1u;
                if (idx < m_emitterNames.size()) {
                    m_selectedEmitterIdx = static_cast<int>(idx);
                    m_activeEmitterIdx   = static_cast<int>(idx);
                    if (onSelectEmitter) onSelectEmitter(static_cast<int>(idx));
                    m_wantsInput = true;
                }
            }
        }
    }

    // System overview view refresh + click dispatch mirrors the 3-column
    // branch so row / checkbox / move / duplicate / delete all work.
    m_overviewViews.clear();
    m_overviewViews.reserve(m_emitterNames.size());
    for (size_t i = 0; i < m_emitterNames.size(); ++i) {
        VfxOverviewEmitterView v;
        v.name = m_emitterNames[i];
        const bool isActive = (static_cast<int>(i) == m_activeEmitterIdx);
        if (isActive && emitter != nullptr) {
            v.isEmitting = emitter->isEmitting;
            v.isEnabled  = emitter->isEmitting;
            v.aliveCount = emitter->cpuAliveCount;
        } else {
            v.isEmitting = false;
            v.isEnabled  = false;
            v.aliveCount = 0;
        }
        m_overviewViews.push_back(std::move(v));
    }
    m_systemOverview.SelectIndex(
        static_cast<size_t>(m_activeEmitterIdx));

    if (!m_mouseJustPressed) return;
    if (overviewBounds.width <= 0.0f || overviewBounds.height <= 0.0f) return;

    const auto layout = m_systemOverview.ComputeLayout(overviewBounds);
    const auto hit    = m_systemOverview.HitTest(layout, m_mouseX, m_mouseY);
    switch (hit.type) {
    case VfxOverviewItemType::Row:
    case VfxOverviewItemType::NameLabel:
    case VfxOverviewItemType::StatusDot:
    case VfxOverviewItemType::ParticleCount:
    case VfxOverviewItemType::DragHandle:
        m_systemOverview.SelectIndex(hit.emitterIndex);
        m_activeEmitterIdx = static_cast<int>(hit.emitterIndex);
        m_selectedEmitterIdx = static_cast<int>(hit.emitterIndex);
        if (onSelectEmitter) onSelectEmitter(static_cast<int>(hit.emitterIndex));
        m_wantsInput = true;
        break;
    case VfxOverviewItemType::Checkbox:
        if (hit.emitterIndex < m_overviewViews.size() &&
            m_systemOverview.onEmitterEnableToggled) {
            const bool newState = !m_overviewViews[hit.emitterIndex].isEnabled;
            m_systemOverview.onEmitterEnableToggled(hit.emitterIndex, newState);
        }
        m_wantsInput = true;
        break;
    case VfxOverviewItemType::BtnMoveUp:
        if (hit.emitterIndex > 0u && m_systemOverview.onEmitterMoved) {
            m_systemOverview.onEmitterMoved(hit.emitterIndex, hit.emitterIndex - 1u);
        }
        m_wantsInput = true;
        break;
    case VfxOverviewItemType::BtnMoveDown:
        if (m_systemOverview.onEmitterMoved &&
            hit.emitterIndex + 1u < m_overviewViews.size()) {
            m_systemOverview.onEmitterMoved(hit.emitterIndex, hit.emitterIndex + 1u);
        }
        m_wantsInput = true;
        break;
    case VfxOverviewItemType::BtnDuplicate:
        if (m_systemOverview.onEmitterDuplicated) {
            m_systemOverview.onEmitterDuplicated(hit.emitterIndex);
        }
        m_wantsInput = true;
        break;
    case VfxOverviewItemType::BtnDelete:
        if (m_systemOverview.onEmitterDeleted) {
            m_systemOverview.onEmitterDeleted(hit.emitterIndex);
        }
        m_wantsInput = true;
        break;
    case VfxOverviewItemType::AddEmitterRow:
        if (m_systemOverview.onAddEmitterRequested) {
            m_systemOverview.onAddEmitterRequested();
        } else if (onAddEmitterRequested) {
            onAddEmitterRequested();
        }
        m_wantsInput = true;
        break;
    default:
        break;
    }
}

void VfxEditorOverlay::BuildNiagaraUI(ParticleEmitter* emitter) {
    if (emitter == nullptr) return;
    EnsureCompositionBound();

    // No auto-seed: the stack reflects exactly what the emitter runs.
    // Legacy presets drive the GPU sim via inline `ParticleEmitter`
    // attributes (emissionRate, lifetime, shape, etc.) and leave the
    // stack empty by design; faking modules here would mislead the
    // author into thinking turbulence/attractor/etc. are active when
    // they aren't dispatched.

    // Bind the stack panel to the emitter's VfxStack so authors can
    // visualise + edit modules. Empty stacks still yield a complete
    // panel (7 stage headers + inline "+" add buttons).
    m_stackPanel.Bind(&emitter->stack);
    m_stackPanel.SetEmitterEnabledPtr(&emitter->isEmitting);
    {
        const int idx = m_activeEmitterIdx;
        const std::string name =
            (idx >= 0 && idx < static_cast<int>(m_emitterNames.size()))
                ? m_emitterNames[idx]
                : std::string("Emitter");
        m_stackPanel.SetEmitterName(name);
    }
    // Switching to a different emitter invalidates the color picker's
    // pointer cache — close it so the popup does not write through
    // into the previous slot's RGBA.
    if (m_lastEmitter != emitter && m_colorPicker.isOpen) {
        CloseColorPicker();
    }
    m_lastEmitter = emitter;

    const float screenW = static_cast<float>(m_screenW);
    const float screenH = static_cast<float>(m_screenH);
    const auto rects = m_composition.ComputeLayout(screenW, screenH);

    // Phase 15.4 — SplitHorizontal: render viewport reserve + 6-px
    // gutter + editor pane composite. Three-column code below is
    // bypassed via early-return when this mode is active.
    if (m_composition.LayoutConfig().layoutMode ==
        VfxEditorLayoutMode::SplitHorizontal) {
        const auto& pal = VfxThemeColors();

        // Phase 15.5 fix — viewport region left TRANSPARENT so the 3D
        // scene rendered into the backbuffer shows through. Previously
        // painted `backgroundBase` here, fully hiding the viewport.

        // Status bar + timeline.
        if (rects.statusBar.width > 0.0f) {
            m_textRenderer.AddRect(rects.statusBar.x, rects.statusBar.y,
                                     rects.statusBar.width,
                                     rects.statusBar.height, pal.barBg);
        }
        // Timeline bar bg intentionally omitted — height is 0.

        // Lazy-bind editor pane to the existing widgets so the pane
        // reuses the same state across both layout modes.
        if (!m_editorPaneBound) {
            m_editorPane.Bind(&m_stackPanel, &m_paramInspector,
                                &m_emitterTree, &m_systemOverview);
            m_editorPaneBound = true;
        }

        // Splitter input dispatch.
        const bool splitterHit = m_splitter.HitTest(
            m_mouseX, m_mouseY, rects.splitter);
        m_splitter.SetHovered(splitterHit);
        if (m_mouseJustPressed && splitterHit) {
            m_splitter.BeginDrag(m_mouseX);
            m_wantsInput = true;
        }
        if (m_splitter.IsDragging()) {
            const float dx = m_splitter.UpdateDrag(m_mouseX);
            if (dx != 0.0f && screenW > 1.0f) {
                auto cfg = m_composition.LayoutConfig();
                cfg.splitFraction = std::clamp(
                    cfg.splitFraction + dx / screenW, 0.2f, 0.8f);
                m_composition.SetLayoutConfig(cfg);
            }
            if (m_mouseJustReleased) {
                m_splitter.EndDrag();
            }
            m_wantsInput = true;
        }

        // Editor-pane input: tab-strip click switches tab. Tab-strip
        // clicks are handled first so swapping tabs in the same frame
        // does not accidentally dispatch into the previously active
        // panel's body.
        bool tabClickConsumed = false;
        if (m_mouseJustPressed) {
            const auto h = m_editorPane.HandleClick(
                m_mouseX, m_mouseY, rects.editorPane);
            if (h.consumed) {
                m_editorPane.SetActiveTab(h.tab);
                m_wantsInput = true;
                tabClickConsumed = true;
            }
        }

        // Phase 15.9 — full inline attribute surface matching the old
        // vfx_viewport parameter panel. Core scalars always visible;
        // each modifier is opt-in and collapsed until toggled. Enabled
        // state derives from the struct itself (non-zero / flag == 1)
        // so toggles persist across preset switches.
        if (emitter != nullptr) {
            using PaneWidget = VfxEditorPaneWidget;
            std::vector<PaneWidget::InlineRow> rows;
            auto addCore = [&](const char* lbl, float* v, float lo, float hi) {
                PaneWidget::InlineRow r;
                r.kind     = PaneWidget::InlineRowKind::CoreField;
                r.label    = lbl;
                r.value    = v;
                r.minValue = lo;
                r.maxValue = hi;
                rows.push_back(std::move(r));
            };
            auto addSection = [&](const char* lbl) {
                PaneWidget::InlineRow r;
                r.kind  = PaneWidget::InlineRowKind::SectionHeader;
                r.label = lbl;
                rows.push_back(std::move(r));
            };
            auto addHeaderF = [&](const char* lbl, float* target,
                                   float onValue) {
                PaneWidget::InlineRow r;
                r.kind          = PaneWidget::InlineRowKind::ModifierHeader;
                r.label         = lbl;
                r.toggleTarget  = target;
                r.toggleOnValue = onValue;
                r.toggleOn      = (target != nullptr)
                                  ? (*target != 0.0f) : false;
                rows.push_back(std::move(r));
                return rows.back().toggleOn;
            };
            auto addHeaderU = [&](const char* lbl, uint32_t* target) {
                PaneWidget::InlineRow r;
                r.kind             = PaneWidget::InlineRowKind::ModifierHeader;
                r.label            = lbl;
                r.toggleTargetU32  = target;
                r.toggleOn         = (target != nullptr) && (*target != 0u);
                rows.push_back(std::move(r));
                return rows.back().toggleOn;
            };
            auto addCoreU = [&](const char* lbl, uint32_t* v,
                                  uint32_t lo, uint32_t hi) {
                PaneWidget::InlineRow r;
                r.kind   = PaneWidget::InlineRowKind::UintCoreField;
                r.label  = lbl;
                r.uValue = v;
                r.uMin   = lo;
                r.uMax   = hi;
                rows.push_back(std::move(r));
            };
            auto addSubU = [&](const char* lbl, uint32_t* v,
                                 uint32_t lo, uint32_t hi, bool visible) {
                PaneWidget::InlineRow r;
                r.kind       = PaneWidget::InlineRowKind::UintSubField;
                r.label      = lbl;
                r.uValue     = v;
                r.uMin       = lo;
                r.uMax       = hi;
                r.subVisible = visible;
                rows.push_back(std::move(r));
            };
            auto addCoreEnum = [&](const char* lbl, uint32_t* v,
                                     const char* const* names, uint32_t count) {
                PaneWidget::InlineRow r;
                r.kind       = PaneWidget::InlineRowKind::EnumField;
                r.label      = lbl;
                r.uValue     = v;
                r.enumNames  = names;
                r.enumCount  = count;
                rows.push_back(std::move(r));
            };
            auto addSubEnum = [&](const char* lbl, uint32_t* v,
                                    const char* const* names, uint32_t count,
                                    bool visible) {
                PaneWidget::InlineRow r;
                r.kind       = PaneWidget::InlineRowKind::EnumSubField;
                r.label      = lbl;
                r.uValue     = v;
                r.enumNames  = names;
                r.enumCount  = count;
                r.subVisible = visible;
                rows.push_back(std::move(r));
            };
            auto addSwatch = [&](const char* lbl, float* rPtr, float* gPtr,
                                   float* bPtr, float* aPtr) {
                PaneWidget::InlineRow r;
                r.kind            = PaneWidget::InlineRowKind::ColorSwatch;
                r.label           = lbl;
                r.swatchRgba[0]   = rPtr;
                r.swatchRgba[1]   = gPtr;
                r.swatchRgba[2]   = bPtr;
                r.swatchRgba[3]   = aPtr;
                rows.push_back(std::move(r));
            };
            auto addSub = [&](const char* lbl, float* v, float lo, float hi,
                               bool visible) {
                PaneWidget::InlineRow r;
                r.kind       = PaneWidget::InlineRowKind::SubField;
                r.label      = lbl;
                r.value      = v;
                r.minValue   = lo;
                r.maxValue   = hi;
                r.subVisible = visible;
                rows.push_back(std::move(r));
            };

            // ── Spawn + Particle core (always visible) ─────────────
            // ── Enum name tables (shared across frames) ────────────
            static const char* const kSpawnShapeNames[] = {
                "Point", "Sphere", "Box", "Ring", "Cone",
                "Cylinder", "Disc", "Line"
            };
            static const char* const kBlendModeNames[] = {
                "AlphaBlend", "Additive", "PremulAlpha"
            };
            static const char* const kFacingModeNames[] = {
                "Face Camera", "Velocity Aligned", "Face Camera Plane",
                "Custom Vector", "Face Camera Pos"
            };
            static const char* const kLitModeNames[] = { "Unlit", "Lit" };
            static const char* const kCollisionResponseNames[] = {
                "Bounce", "Stick", "Kill"
            };
            static const char* const kSpriteChannelNames[] = {
                "RGBA", "R", "G", "B", "A"
            };

            addCore("Emission Rate",   &emitter->emissionRate,      0.0f, 5000.0f);
            addCore("Lifetime",        &emitter->lifetime,          0.01f, 60.0f);
            addCore("Lifetime Var",    &emitter->lifetimeVariation, 0.0f, 10.0f);
            addCoreU("Max Particles",  &emitter->maxParticles,     100u, 50000u);
            addCoreU("Loop Count",     &emitter->loopCount,          0u, 100u);
            addCore("Duration",        &emitter->emitterDuration,   0.0f, 60.0f);
            addCore("Spawn Radius",    &emitter->spawnRadius,       0.0f, 1000.0f);
            addCoreEnum("Spawn Shape", &emitter->spawnShape,
                         kSpawnShapeNames,
                         sizeof(kSpawnShapeNames) / sizeof(kSpawnShapeNames[0]));
            addCore("Initial Size",    &emitter->initialSize,       0.001f, 500.0f);
            addCore("Spawn Vel X",     &emitter->spawnVelocity.x, -2000.0f, 2000.0f);
            addCore("Spawn Vel Y",     &emitter->spawnVelocity.y, -2000.0f, 2000.0f);
            addCore("Spawn Vel Z",     &emitter->spawnVelocity.z, -2000.0f, 2000.0f);
            addCore("Velocity Var",    &emitter->velocityVariation, 0.0f, 100.0f);
            addCore("Spread Angle",    &emitter->spreadAngle,       0.0f, 180.0f);
            addCore("Damping",         &emitter->damping,           0.0f, 2.0f);
            addCore("Emissive",        &emitter->emissiveIntensity, 0.0f, 20.0f);
            addCore("Stretch",         &emitter->stretchFactor,     0.0f, 5.0f);
            addCore("Soft Fade",       &emitter->softParticleFadeRange, 0.0f, 500.0f);
            addCore("Velocity Inherit",&emitter->velocityInheritance, 0.0f, 1.0f);
            addCore("Warmup Time",     &emitter->warmupTime,        0.0f, 10.0f);
            addCore("Warmup Tick",     &emitter->warmupTickRate,    0.001f, 0.2f);

            // ── Initial Color (per-particle birth color) ───────────
            addSection("Initial Color");
            addSwatch("Initial Color",
                      &emitter->initialColor.x, &emitter->initialColor.y,
                      &emitter->initialColor.z, &emitter->initialColor.w);

            // ── Rendering ──────────────────────────────────────────
            addSection("Rendering");
            addCoreEnum("Blend Mode",  &emitter->blendMode,
                         kBlendModeNames,
                         sizeof(kBlendModeNames) / sizeof(kBlendModeNames[0]));
            addCoreEnum("Facing Mode", &emitter->facingMode,
                         kFacingModeNames,
                         sizeof(kFacingModeNames) / sizeof(kFacingModeNames[0]));
            addCoreEnum("Lit Mode",    &emitter->litMode,
                         kLitModeNames,
                         sizeof(kLitModeNames) / sizeof(kLitModeNames[0]));
            addCore("Custom Facing X", &emitter->customFacingX, -1.0f, 1.0f);
            addCore("Custom Facing Y", &emitter->customFacingY, -1.0f, 1.0f);
            addCore("Custom Facing Z", &emitter->customFacingZ, -1.0f, 1.0f);
            addCore("Camera Offset",   &emitter->cameraOffset,   -500.0f, 500.0f);
            addCore("Pivot X",         &emitter->pivotOffsetX,   -1.0f, 1.0f);
            addCore("Pivot Y",         &emitter->pivotOffsetY,   -1.0f, 1.0f);

            // ── Sprite / Flipbook ──────────────────────────────────
            addSection("Sprite / Flipbook");
            addCoreU("Flipbook Rows",  &emitter->flipbookRows,       1u, 16u);
            addCoreU("Flipbook Cols",  &emitter->flipbookCols,       1u, 16u);
            addCoreU("Flipbook Frames",&emitter->flipbookTotalFrames,1u, 256u);
            addCore("Flipbook FPS",    &emitter->flipbookFPS,        0.0f, 120.0f);
            addCoreEnum("Channel Mask",&emitter->spriteChannelMask,
                         kSpriteChannelNames,
                         sizeof(kSpriteChannelNames) / sizeof(kSpriteChannelNames[0]));

            // ── Color Over Life (always runtime-active) ────────────
            addSection("Color Over Life");
            {
                // Colorbox preview row: horizontal gradient with two
                // clickable handles (start / end). Handle clicks swap
                // the selected stop — the four RGBA rows below are
                // bound to the selected endpoint so editing one stop
                // at a time mirrors the Unreal gradient control.
                using PaneWidget = VfxEditorPaneWidget;
                PaneWidget::InlineRow gr;
                gr.kind  = PaneWidget::InlineRowKind::ColorGradient;
                gr.label = "Gradient";
                gr.gradLeft[0]  = &emitter->colorLifeStartR;
                gr.gradLeft[1]  = &emitter->colorLifeStartG;
                gr.gradLeft[2]  = &emitter->colorLifeStartB;
                gr.gradLeft[3]  = &emitter->colorLifeStartA;
                gr.gradRight[0] = &emitter->colorLifeEndR;
                gr.gradRight[1] = &emitter->colorLifeEndG;
                gr.gradRight[2] = &emitter->colorLifeEndB;
                gr.gradRight[3] = &emitter->colorLifeEndA;
                gr.selectedStop   = m_colorStopSelected;
                gr.transitionHard = m_colorStopHardMode;
                rows.push_back(std::move(gr));
            }

            // Color swatch for the selected stop. Click opens the HEX
            // / RGBA / preset picker popup bound to the same pointers
            // so edits propagate straight to the emitter struct.
            float* selR = (m_colorStopSelected == 0u)
                ? &emitter->colorLifeStartR : &emitter->colorLifeEndR;
            float* selG = (m_colorStopSelected == 0u)
                ? &emitter->colorLifeStartG : &emitter->colorLifeEndG;
            float* selB = (m_colorStopSelected == 0u)
                ? &emitter->colorLifeStartB : &emitter->colorLifeEndB;
            float* selA = (m_colorStopSelected == 0u)
                ? &emitter->colorLifeStartA : &emitter->colorLifeEndA;
            const char* stopLbl = (m_colorStopSelected == 0u)
                ? "Start Color" : "End Color";
            {
                using PaneWidget = VfxEditorPaneWidget;
                PaneWidget::InlineRow sw;
                sw.kind  = PaneWidget::InlineRowKind::ColorSwatch;
                sw.label = stopLbl;
                sw.swatchRgba[0] = selR;
                sw.swatchRgba[1] = selG;
                sw.swatchRgba[2] = selB;
                sw.swatchRgba[3] = selA;
                rows.push_back(std::move(sw));
            }

            // Hard / Smooth transition toggle. Preview-only until the
            // particle-update CS supports the step path.
            {
                using PaneWidget = VfxEditorPaneWidget;
                PaneWidget::InlineRow hr;
                hr.kind             = PaneWidget::InlineRowKind::ModifierHeader;
                hr.label            = "Hard Transition";
                hr.toggleTargetU32  = &m_colorStopHardMode;
                hr.toggleOn         = (m_colorStopHardMode != 0u);
                rows.push_back(std::move(hr));
            }

            // ── Size Over Life (always runtime-active) ─────────────
            addSection("Size Over Life");
            addCore("Size Start", &emitter->sizeOverLifeStart,    0.0f, 5.0f);
            addCore("Size Peak",  &emitter->sizeOverLifePeak,     0.0f, 5.0f);
            addCore("Size End",   &emitter->sizeOverLifeEnd,      0.0f, 5.0f);
            addCore("Peak Time",  &emitter->sizeOverLifePeakTime, 0.0f, 1.0f);

            addSection("Modifiers");

            // ── Gravity (default: Z = -981) ────────────────────────
            const bool gravOn = addHeaderF("Gravity",
                                           &emitter->gravity.z, -981.0f);
            addSub("Gravity X", &emitter->gravity.x, -2000.0f, 2000.0f, gravOn);
            addSub("Gravity Y", &emitter->gravity.y, -2000.0f, 2000.0f, gravOn);
            addSub("Gravity Z", &emitter->gravity.z, -2000.0f, 2000.0f, gravOn);

            // ── Wind ───────────────────────────────────────────────
            const bool windOn = addHeaderF("Wind",
                                           &emitter->windStrength, 1.0f);
            addSub("Strength", &emitter->windStrength,  0.0f, 50.0f, windOn);
            addSub("Dir X",    &emitter->windDirX,     -1.0f,  1.0f, windOn);
            addSub("Dir Y",    &emitter->windDirY,     -1.0f,  1.0f, windOn);
            addSub("Dir Z",    &emitter->windDirZ,     -1.0f,  1.0f, windOn);

            // ── Drag ───────────────────────────────────────────────
            const bool dragOn = addHeaderF("Drag",
                                           &emitter->dragCoefficient, 0.5f);
            addSub("Coefficient", &emitter->dragCoefficient, 0.0f, 10.0f, dragOn);

            // ── Acceleration (non-gravity) ─────────────────────────
            const bool accelOn = addHeaderF("Acceleration",
                                            &emitter->accelY, 1.0f);
            addSub("Accel X", &emitter->accelX, -50.0f, 50.0f, accelOn);
            addSub("Accel Y", &emitter->accelY, -50.0f, 50.0f, accelOn);
            addSub("Accel Z", &emitter->accelZ, -50.0f, 50.0f, accelOn);

            // ── Rotation ───────────────────────────────────────────
            const bool rotOn = addHeaderF("Rotation",
                                          &emitter->rotationRate, 1.0f);
            addSub("Rate (rad/s)", &emitter->rotationRate,
                    -20.0f, 20.0f, rotOn);
            addSub("Over Life Start", &emitter->rotationOverLifeStart,
                    -20.0f, 20.0f, rotOn);
            addSub("Over Life End",   &emitter->rotationOverLifeEnd,
                    -20.0f, 20.0f, rotOn);

            // ── Curl Noise ─────────────────────────────────────────
            const bool curlOn = addHeaderF("Curl Noise",
                                           &emitter->curlStrength, 1.0f);
            addSub("Strength",  &emitter->curlStrength,  0.0f, 10.0f, curlOn);
            addSub("Frequency", &emitter->curlFrequency, 0.01f, 5.0f, curlOn);
            addSub("Speed",     &emitter->curlSpeed,     0.0f, 5.0f, curlOn);

            // ── Vortex ─────────────────────────────────────────────
            const bool vortexOn = addHeaderF("Vortex",
                                             &emitter->vortexTangentialStrength,
                                             5.0f);
            addSub("Tangential", &emitter->vortexTangentialStrength,
                    -20.0f, 20.0f, vortexOn);
            addSub("Radial",     &emitter->vortexRadialStrength,
                    -20.0f, 20.0f, vortexOn);
            addSub("Max Radius", &emitter->vortexMaxRadius,
                    0.1f, 1000.0f, vortexOn);
            addSub("Origin X",   &emitter->vortexAxisOriginX,
                    -500.0f, 500.0f, vortexOn);
            addSub("Origin Y",   &emitter->vortexAxisOriginY,
                    -500.0f, 500.0f, vortexOn);
            addSub("Origin Z",   &emitter->vortexAxisOriginZ,
                    -500.0f, 500.0f, vortexOn);
            addSub("Axis X",     &emitter->vortexAxisDirX, -1.0f, 1.0f, vortexOn);
            addSub("Axis Y",     &emitter->vortexAxisDirY, -1.0f, 1.0f, vortexOn);
            addSub("Axis Z",     &emitter->vortexAxisDirZ, -1.0f, 1.0f, vortexOn);

            // ── Orbit (uint32 enable) ──────────────────────────────
            const bool orbitOn = addHeaderU("Orbit", &emitter->orbitEnabled);
            addSub("Radius", &emitter->orbitRadius,  0.0f, 1000.0f, orbitOn);
            addSub("Speed",  &emitter->orbitSpeed,  -10.0f, 10.0f, orbitOn);
            addSub("Axis X", &emitter->orbitAxisX, -1.0f, 1.0f, orbitOn);
            addSub("Axis Y", &emitter->orbitAxisY, -1.0f, 1.0f, orbitOn);
            addSub("Axis Z", &emitter->orbitAxisZ, -1.0f, 1.0f, orbitOn);

            // ── Ground Collision (uint32 enable) ───────────────────
            const bool collOn = addHeaderU("Ground Collision",
                                            &emitter->groundCollision);
            addSub("Restitution", &emitter->restitution,       0.0f, 1.0f, collOn);
            addSub("Friction",    &emitter->collisionFriction, 0.0f, 1.0f, collOn);
            addSubEnum("Response", &emitter->collisionResponse,
                        kCollisionResponseNames,
                        sizeof(kCollisionResponseNames) /
                            sizeof(kCollisionResponseNames[0]),
                        collOn);

            // ── Particle Light (uint32 enable) ─────────────────────
            const bool lightOn = addHeaderU("Particle Light",
                                             &emitter->particleLightEnabled);
            addSub("Radius",        &emitter->particleLightRadius,
                    0.0f, 5000.0f, lightOn);
            addSub("Intensity",     &emitter->particleLightIntensity,
                    0.0f, 20.0f, lightOn);
            addSub("Flicker",       &emitter->particleLightFlicker,
                    0.0f, 1.0f, lightOn);
            addSub("Flicker Speed", &emitter->particleLightFlickerSpeed,
                    0.1f, 30.0f, lightOn);

            // ── Size By Speed (uint32 enable) ──────────────────────
            const bool sbsOn = addHeaderU("Size By Speed",
                                           &emitter->sizeBySpeedEnabled);
            addSub("Min",           &emitter->sizeBySpeedMin,
                    0.0f, 10.0f, sbsOn);
            addSub("Max",           &emitter->sizeBySpeedMax,
                    0.0f, 10.0f, sbsOn);
            addSub("Speed Range",   &emitter->speedRangeMax,
                    1.0f, 10000.0f, sbsOn);

            // ── Color By Speed (uint32 enable, reuses Color Over Life) ─
            (void)addHeaderU("Color By Speed",
                              &emitter->colorBySpeedEnabled);

            // ── Screen-Space Size (uint32 enable) ──────────────────
            const bool ssOn = addHeaderU("Screen-Space Size",
                                           &emitter->screenSpaceSizeEnabled);
            addSub("Pixel Size", &emitter->screenSpacePixelSize,
                    1.0f, 256.0f, ssOn);

            m_editorPane.SetInlineRows(std::move(rows));
        } else {
            m_editorPane.SetInlineRows({});
        }

        // Phase 15.5 — dispatch clicks / scroll into the widget that
        // owns the editor pane body for the currently active tab. The
        // body is the rect below the 28 px tab strip.
        const VfxStackRect editorBody{
            rects.editorPane.x,
            rects.editorPane.y + m_editorPane.tabStripHeight,
            rects.editorPane.width,
            std::max(0.0f,
                     rects.editorPane.height - m_editorPane.tabStripHeight)
        };
        if (!tabClickConsumed) {
            switch (m_editorPane.ActiveTab()) {
            case VfxEditorPaneTab::Stack:
                DispatchStackPanelInput(editorBody, emitter);
                break;
            case VfxEditorPaneTab::Parameters: {
                // Scroll wheel while the cursor is over the inline
                // strip advances the pane's scroll offset. 20 px per
                // wheel notch matches the row-height feel of other
                // scrollable widgets in the overlay.
                const VfxStackRect inlineRect =
                    m_editorPane.InlineStripRect(rects.editorPane);
                if (inlineRect.Contains(m_mouseX, m_mouseY) &&
                    m_scrollDelta != 0.0f) {
                    const float contentH =
                        m_editorPane.InlineContentHeight();
                    const float maxScroll = std::max(0.0f,
                        contentH - inlineRect.height);
                    float off = m_editorPane.InlineScrollOffset()
                                - m_scrollDelta * 20.0f;
                    off = std::clamp(off, 0.0f, maxScroll);
                    m_editorPane.SetInlineScrollOffset(off);
                    m_wantsInput = true;
                }

                // Inline strip first: modifier headers toggle on click,
                // scalar rows (core + sub-fields) start a drag.
                const size_t inlineIdx =
                    m_editorPane.HitTestInline(rects.editorPane,
                                                m_mouseX, m_mouseY);
                const bool inlineHit = (inlineIdx != static_cast<size_t>(-1));
                if (m_mouseJustPressed && inlineHit) {
                    const auto& rs = m_editorPane.InlineRows();
                    const auto& r  = rs[inlineIdx];
                    if (r.kind == VfxEditorPaneWidget::InlineRowKind::SectionHeader) {
                        // Pure visual divider — ignore clicks.
                    } else if (r.kind == VfxEditorPaneWidget::InlineRowKind::ColorGradient) {
                        // Split the swatch in half: left half selects
                        // Start (stop 0), right half selects End (1).
                        // The row's strip is full pane width; cursor x
                        // is absolute so compare against the midpoint.
                        const VfxStackRect stripRect =
                            m_editorPane.InlineStripRect(rects.editorPane);
                        const float midX = stripRect.x + stripRect.width * 0.5f;
                        m_colorStopSelected = (m_mouseX < midX) ? 0u : 1u;
                        m_wantsInput = true;
                    } else if (r.kind == VfxEditorPaneWidget::InlineRowKind::ColorSwatch) {
                        // Only open the picker when the cursor is
                        // inside the 24×24 swatch square, not just
                        // anywhere on the row. Geometry mirrors the
                        // pane widget's ColorSwatch render block.
                        const VfxStackRect rowR =
                            m_editorPane.InlineRowRect(rects.editorPane,
                                                        inlineIdx);
                        constexpr float kSwatchSize = 24.0f;
                        const float swX = rowR.x + rowR.width - kSwatchSize - 10.0f;
                        const float swY = rowR.y + (rowR.height - kSwatchSize) * 0.5f;
                        if (m_mouseX >= swX && m_mouseX < swX + kSwatchSize &&
                            m_mouseY >= swY && m_mouseY < swY + kSwatchSize) {
                            OpenColorPicker(m_mouseX - 340.0f, m_mouseY + 12.0f,
                                             r.swatchRgba[0], r.swatchRgba[1],
                                             r.swatchRgba[2], r.swatchRgba[3]);
                            m_wantsInput = true;
                        }
                    } else if (r.kind == VfxEditorPaneWidget::InlineRowKind::ModifierHeader) {
                        // Toggle the modifier's primary between off/on.
                        // Float path: 0 ↔ toggleOnValue.
                        // Uint32 path: 0 ↔ 1.
                        if (r.toggleTargetU32 != nullptr) {
                            *r.toggleTargetU32 = r.toggleOn ? 0u : 1u;
                            m_wantsInput = true;
                        } else if (r.toggleTarget != nullptr) {
                            *r.toggleTarget = r.toggleOn ? 0.0f : r.toggleOnValue;
                            m_wantsInput = true;
                        }
                    } else if (r.kind == VfxEditorPaneWidget::InlineRowKind::EnumField ||
                               r.kind == VfxEditorPaneWidget::InlineRowKind::EnumSubField) {
                        // Click cycles to the next enum value, wrapping
                        // at the end of the name table.
                        if (r.uValue != nullptr && r.enumCount > 0u) {
                            *r.uValue = (*r.uValue + 1u) % r.enumCount;
                            m_wantsInput = true;
                        }
                    } else if ((r.kind == VfxEditorPaneWidget::InlineRowKind::UintCoreField ||
                                 r.kind == VfxEditorPaneWidget::InlineRowKind::UintSubField) &&
                                r.uValue != nullptr) {
                        m_inlineUDragActive      = true;
                        m_inlineUDragField       = r.uValue;
                        m_inlineUDragMin         = r.uMin;
                        m_inlineUDragMax         = r.uMax;
                        m_inlineUDragMouseXStart = m_mouseX;
                        m_inlineUDragAccum       = static_cast<float>(*r.uValue);
                        const float range = static_cast<float>(r.uMax - r.uMin);
                        m_inlineUDragScale =
                            std::max(range / 200.0f, 1.0f / 40.0f);
                        m_wantsInput = true;
                    } else if (r.value != nullptr) {
                        m_inlineDragActive     = true;
                        m_inlineDragField      = r.value;
                        m_inlineDragMin        = r.minValue;
                        m_inlineDragMax        = r.maxValue;
                        m_inlineDragMouseXStart = m_mouseX;
                        m_inlineDragValueStart  = *r.value;
                        const float range = std::max(1e-3f,
                                                      r.maxValue - r.minValue);
                        m_inlineDragScale = std::max(range / 200.0f, 1e-4f);
                        m_wantsInput = true;
                    }
                }
                if (m_inlineDragActive && m_inlineDragField != nullptr) {
                    if (m_mouseDown) {
                        const float dx = m_mouseX - m_inlineDragMouseXStart;
                        const float nv = std::clamp(
                            m_inlineDragValueStart + dx * m_inlineDragScale,
                            m_inlineDragMin, m_inlineDragMax);
                        *m_inlineDragField = nv;
                        m_wantsInput = true;
                    } else {
                        m_inlineDragActive = false;
                        m_inlineDragField  = nullptr;
                    }
                }
                if (m_inlineUDragActive && m_inlineUDragField != nullptr) {
                    if (m_mouseDown) {
                        const float dx = m_mouseX - m_inlineUDragMouseXStart;
                        const float nv = static_cast<float>(m_inlineUDragAccum)
                                          + dx * m_inlineUDragScale;
                        const float clampedF = std::clamp(nv,
                            static_cast<float>(m_inlineUDragMin),
                            static_cast<float>(m_inlineUDragMax));
                        *m_inlineUDragField = static_cast<uint32_t>(
                            clampedF + 0.5f);
                        m_wantsInput = true;
                    } else {
                        m_inlineUDragActive = false;
                        m_inlineUDragField  = nullptr;
                    }
                }
                // Fall back to the store-driven inspector for anything
                // outside the inline strip.
                if (!inlineHit && !m_inlineDragActive) {
                    const VfxStackRect insBody =
                        m_editorPane.ParamInspectorRect(rects.editorPane);
                    DispatchParamInspectorInput(insBody);
                }
                break;
            }
            case VfxEditorPaneTab::Emitters: {
                const float half = editorBody.height * 0.5f;
                const VfxStackRect treeR{
                    editorBody.x, editorBody.y, editorBody.width, half
                };
                const VfxStackRect ovR{
                    editorBody.x, editorBody.y + half,
                    editorBody.width, editorBody.height - half
                };
                DispatchTreeAndOverviewInput(treeR, ovR, emitter);
                break;
            }
            }
        }

        // Render splitter + editor pane above the panel backgrounds.
        m_splitter.Render(m_textRenderer, rects.splitter);
        m_editorPane.Render(m_textRenderer, m_font, GetMsdfFont(),
                              rects.editorPane);

        // Menu bar + toolbar + status bar live telemetry are handled
        // by the normal flow above this early return — the split-mode
        // branch only swaps the middle-content composition.
        // Timeline scrubber is disabled (layout height 0).

        // Toolbar strip (lazy populate shared with Niagara branch).
        if (rects.toolbarBar.width > 0.0f && rects.toolbarBar.height > 0.0f) {
            if (!m_toolbarInitialized) {
                m_toolbar.AddButton({ 1u, "Save",    VfxUiIcon::Check,   VfxToolbarKind::Action, false });
                m_toolbar.AddButton({ 2u, "Restart", VfxUiIcon::Restart, VfxToolbarKind::Action, false });
                m_toolbar.AddSeparator();
                m_toolbar.AddButton({ 3u, "Play",    VfxUiIcon::Play,    VfxToolbarKind::Toggle, false });
                m_toolbar.AddButton({ 4u, "Thumb",   VfxUiIcon::Star,    VfxToolbarKind::Action, false });
                m_toolbar.AddSeparator();
                m_toolbar.AddButton({ 5u, "Assets",  VfxUiIcon::Search,  VfxToolbarKind::Action, false });
                m_toolbarInitialized = true;
            }
            // Paint the full-width toolbar background before shrinking
            // the content rect — otherwise the 10 px left/right margin
            // leaves the scene / particles visible at the strip edges.
            m_textRenderer.AddRect(rects.toolbarBar.x, rects.toolbarBar.y,
                                     rects.toolbarBar.width,
                                     rects.toolbarBar.height, pal.barBg);
            VfxStackRect tbBounds = rects.toolbarBar;
            tbBounds.x += 10.0f;
            tbBounds.width -= 20.0f;
            const auto layout = m_toolbar.ComputeLayout(tbBounds);
            const auto hit = m_toolbar.HitTest(layout, m_mouseX, m_mouseY);
            m_toolbar.SetHoveredId(hit.hit ? hit.id : 0u);
            if (hit.hit && m_mouseJustPressed) {
                switch (hit.id) {
                case 1u:
                    if (onMenuCommand) onMenuCommand(VfxMenuCommand::File_Save);
                    PushStatus("Saved", VfxStatusLevel::Info);
                    break;
                case 2u:
                    if (onRestart) onRestart();
                    PushStatus("Restarted", VfxStatusLevel::Info);
                    break;
                case 3u: {
                    const bool newToggled = !m_toolbar.IsToggled(3u);
                    m_toolbar.SetToggled(3u, newToggled);
                    PushStatus(newToggled ? "Playing" : "Paused",
                                VfxStatusLevel::Info);
                    break;
                }
                case 4u:
                    if (onRecaptureThumbnail && m_lastEmitter) {
                        onRecaptureThumbnail("");
                        PushStatus("Thumbnail captured", VfxStatusLevel::Info);
                    }
                    break;
                case 5u:
                    OpenContentBrowser();
                    PushStatus("Content Browser opened", VfxStatusLevel::Info);
                    break;
                default: break;
                }
                m_wantsInput = true;
            }
            m_toolbar.Render(m_textRenderer, m_font, GetMsdfFont(), tbBounds);
        }

        // Status bar "alive" count = authoritative GPU readback of the
        // `aliveCount` atomic (N-1 frames stale). Falls back to the
        // emission-rate × lifetime steady-state estimate only until the
        // readback ring has produced its first sample (first few frames
        // after the emitter spawns), so the display is never blank.
        if (rects.statusBar.width > 0.0f && rects.statusBar.height > 0.0f) {
            const uint64_t maxP = (emitter != nullptr)
                ? static_cast<uint64_t>(emitter->maxParticles) : 0ull;
            uint64_t alive = 0ull;
            if (emitter != nullptr) {
                if (emitter->buffer.readbackHasKickedOnce) {
                    alive = std::min<uint64_t>(
                        static_cast<uint64_t>(emitter->buffer.gpuAliveCount),
                        maxP);
                } else {
                    const float estF = emitter->emissionRate * emitter->lifetime;
                    const uint64_t est = (estF > 0.0f)
                        ? static_cast<uint64_t>(estF + 0.5f) : 0ull;
                    alive = std::min<uint64_t>(est, maxP);
                }
            }
            m_statusBar.SetParticleCount(alive, maxP);
            m_statusBar.SetDirty(m_composition.UndoStack() != nullptr &&
                                   m_composition.UndoStack()->CanUndo());
            m_statusBar.Render(m_textRenderer, m_font, GetMsdfFont(),
                                 rects.statusBar);
        }

        return; // Bypass the phase-14 three-column render below.
    }

    // ── Panel backgrounds + 1-px vertical separators ──────────────────
    const auto& palette = VfxThemeColors();
    const auto drawPanelBg = [&](const VfxStackRect& r, const Vec4& bg) {
        if (r.width <= 0.0f || r.height <= 0.0f) return;
        m_textRenderer.AddRect(r.x, r.y, r.width, r.height, bg);
    };
    drawPanelBg(rects.stackPanel,   palette.panelBg);
    drawPanelBg(rects.centerPanel,  palette.backgroundBase);
    drawPanelBg(rects.rightPanel,   palette.panelBg);
    drawPanelBg(rects.timelineBar,  palette.barBg);

    // Phase 14.10 — toolbar strip below the menu bar.
    if (rects.toolbarBar.width > 0.0f && rects.toolbarBar.height > 0.0f) {
        if (!m_toolbarInitialized) {
            m_toolbar.AddButton({ 1u, "Save",    VfxUiIcon::Check,   VfxToolbarKind::Action, false });
            m_toolbar.AddButton({ 2u, "Restart", VfxUiIcon::Restart, VfxToolbarKind::Action, false });
            m_toolbar.AddSeparator();
            m_toolbar.AddButton({ 3u, "Play",    VfxUiIcon::Play,    VfxToolbarKind::Toggle, false });
            m_toolbar.AddButton({ 4u, "Thumb",   VfxUiIcon::Star,    VfxToolbarKind::Action, false });
            m_toolbar.AddSeparator();
            m_toolbar.AddButton({ 5u, "Assets",  VfxUiIcon::Search,  VfxToolbarKind::Action, false });
            m_toolbarInitialized = true;
        }

        // Paint the full-width toolbar background first — the content
        // rect below is inset by 10 px on each side, so without this
        // the scene would show through at the strip edges.
        m_textRenderer.AddRect(rects.toolbarBar.x, rects.toolbarBar.y,
                                 rects.toolbarBar.width,
                                 rects.toolbarBar.height, palette.barBg);

        // Hover + click dispatch.
        VfxStackRect tbounds = rects.toolbarBar;
        tbounds.x += 10.0f;                 // left-pad the toolbar run
        tbounds.width -= 20.0f;
        const auto layout = m_toolbar.ComputeLayout(tbounds);
        const auto hit = m_toolbar.HitTest(layout, m_mouseX, m_mouseY);
        m_toolbar.SetHoveredId(hit.hit ? hit.id : 0u);
        if (hit.hit && m_mouseJustPressed) {
            switch (hit.id) {
            case 1u:
                if (onMenuCommand) onMenuCommand(VfxMenuCommand::File_Save);
                PushStatus("Saved", VfxStatusLevel::Info);
                break;
            case 2u:
                if (onRestart) onRestart();
                PushStatus("Restarted", VfxStatusLevel::Info);
                break;
            case 3u: {
                const bool newToggled = !m_toolbar.IsToggled(3u);
                m_toolbar.SetToggled(3u, newToggled);
                PushStatus(newToggled ? "Playing" : "Paused",
                            VfxStatusLevel::Info);
                break;
            }
            case 4u:
                if (onRecaptureThumbnail && m_lastEmitter) {
                    onRecaptureThumbnail("");
                    PushStatus("Thumbnail captured",
                                VfxStatusLevel::Info);
                } else {
                    PushStatus("No emitter to capture",
                                VfxStatusLevel::Warning);
                }
                break;
            case 5u:
                OpenContentBrowser();
                PushStatus("Content Browser opened",
                            VfxStatusLevel::Info);
                break;
            default:
                break;
            }
            m_wantsInput = true;
        }

        m_toolbar.Render(m_textRenderer, m_font, GetMsdfFont(), tbounds);
    }

    // Phase 14.10 — status bar strip above the timeline scrubber.
    if (rects.statusBar.width > 0.0f && rects.statusBar.height > 0.0f) {
        // Phase 14.14 — host drives FPS / DC / VRAM via UpdateTelemetry.
        // Particle count + dirty flag are computed inline since they
        // depend on overlay-local state.
        m_statusBar.SetParticleCount(
            (emitter != nullptr)
                ? static_cast<uint64_t>(emitter->cpuAliveCount) : 0ull,
            (emitter != nullptr)
                ? static_cast<uint64_t>(emitter->maxParticles) : 0ull);
        m_statusBar.SetDirty(m_composition.UndoStack() != nullptr &&
                              m_composition.UndoStack()->CanUndo());
        m_statusBar.Render(m_textRenderer, m_font, GetMsdfFont(),
                            rects.statusBar);
    }

    // V3 minimalist — no vertical dividers. Panel bg contrast alone
    // separates the columns. A dropped hairline = less visual noise.
    const auto drawVDiv = [&](float x, float y, float h) {
        (void)x; (void)y; (void)h;
    };
    if (rects.stackPanel.width > 0.0f) {
        drawVDiv(rects.stackPanel.x + rects.stackPanel.width - 1.0f,
                 rects.stackPanel.y, rects.stackPanel.height);
    }
    if (rects.centerPanel.width > 0.0f) {
        drawVDiv(rects.centerPanel.x + rects.centerPanel.width - 1.0f,
                 rects.centerPanel.y, rects.centerPanel.height);
    }
    if (rects.rightPanel.width > 0.0f) {
        drawVDiv(rects.rightPanel.x, rects.rightPanel.y,
                 rects.rightPanel.height);
    }
    // Horizontal divider above timeline strip.
    // Timeline bar separator intentionally omitted — height is 0.

    // ── Stack panel (left) ─────────────────────────────────────────────
    // Click + hover dispatch mirrors `BuildStackPanel`, but derives the
    // rect from the composition layout so all panels resize together.
    const VfxStackRect stackBounds = rects.stackPanel;
    if (stackBounds.width > 0.0f && stackBounds.height > 0.0f) {
        // Scroll feed-through when the mouse is over the left panel.
        if (stackBounds.Contains(m_mouseX, m_mouseY) && m_scrollDelta != 0.0f) {
            m_stackPanel.SetScrollOffset(
                std::max(0.0f, m_stackPanel.ScrollOffset()
                                   - m_scrollDelta * kScrollSpeed));
        }
        const float contentH = m_stackPanel.ComputeContentHeight();
        const float maxScroll = std::max(0.0f, contentH - stackBounds.height);
        if (m_stackPanel.ScrollOffset() > maxScroll) {
            m_stackPanel.SetScrollOffset(maxScroll);
        }

        const auto layout = m_stackPanel.ComputeLayout(stackBounds);
        const auto hit    = m_stackPanel.HitTest(layout, m_mouseX, m_mouseY);

        if (hit.type == VfxStackItemType::ModuleCard ||
            hit.type == VfxStackItemType::CardCheckbox) {
            m_stackPanel.SetHoveredInstanceId(hit.instanceId);
        } else {
            m_stackPanel.SetHoveredInstanceId(std::string{});
        }

        if (m_mouseJustPressed && hit.IsHit()) {
            switch (hit.type) {
            case VfxStackItemType::StageHeader:
                m_stackPanel.ToggleStage(hit.stage);
                break;
            case VfxStackItemType::CardCheckbox:
                if (auto* stage = &emitter->stack.GetEntry(hit.stage);
                    stage != nullptr && hit.moduleIndex < stage->modules.size()) {
                    auto& mod = stage->modules[hit.moduleIndex];
                    mod.enabled = !mod.enabled;
                    if (m_stackPanel.onModuleToggled) {
                        m_stackPanel.onModuleToggled(mod.instanceId, mod.enabled);
                    }
                }
                break;
            case VfxStackItemType::ModuleCard:
                m_stackPanel.SelectModule(hit.instanceId);
                m_composition.SetFocusedPanel(VfxFocusedPanel::Stack);
                break;
            case VfxStackItemType::StageAddButton:
                if (m_stackPanel.onAddModuleRequested) {
                    m_stackPanel.onAddModuleRequested(hit.stage);
                }
                break;
            case VfxStackItemType::CardDragHandle:
                m_stackPanel.SelectModule(hit.instanceId);
                m_stackPanel.BeginDrag(hit.instanceId);
                m_composition.SetFocusedPanel(VfxFocusedPanel::Stack);
                break;
            case VfxStackItemType::CardOverflowMenu:
                if (m_stackPanel.onModuleContextMenuRequested) {
                    m_stackPanel.onModuleContextMenuRequested(
                        hit.instanceId, m_mouseX, m_mouseY);
                }
                break;
            case VfxStackItemType::EmitterHeader:
                // reserved for future emitter-wide actions.
                break;
            case VfxStackItemType::AddRow:  // legacy
            case VfxStackItemType::None:
                break;
            }
            m_wantsInput = true;
        }

        m_stackPanel.Render(m_textRenderer, m_font, stackBounds);

        // Phase 14.11 — right-edge scrollbar reflects stack-panel
        // overflow. Reads back into `m_stackPanel.ScrollOffset` so
        // wheel + drag inputs stay consistent with the panel state.
        m_stackScrollbar.SetView(stackBounds, contentH);
        m_stackScrollbar.SetScrollY(m_stackPanel.ScrollOffset());
        m_stackScrollbar.SetHovered(
            stackBounds.Contains(m_mouseX, m_mouseY));
        m_stackScrollbar.Render(m_textRenderer);
        m_stackPanel.SetScrollOffset(m_stackScrollbar.ScrollY());
    }

    // ── Center panel title (shows the selected center mode) ────────────
    if (rects.centerPanel.width > 4.0f && rects.centerPanel.height > 8.0f) {
        constexpr float kCenterHeaderH = 22.0f;
        m_textRenderer.AddRect(rects.centerPanel.x, rects.centerPanel.y,
                                rects.centerPanel.width, kCenterHeaderH,
                                kColorBarBg);
        const char* modeName = VfxCenterModeName(m_composition.CenterMode());
        m_textRenderer.AddText(m_font, modeName,
                                rects.centerPanel.x + 8.0f,
                                rects.centerPanel.y + 4.0f, kColorText);

        const VfxStackRect centerContent{
            rects.centerPanel.x,
            rects.centerPanel.y + kCenterHeaderH,
            rects.centerPanel.width,
            std::max(0.0f, rects.centerPanel.height - kCenterHeaderH)
        };

        // Phase 12.10.0 — right-click on center panel opens a context
        // menu relevant to the current mode (Parameters or Curves).
        if (m_mouseRightJustPressed && centerContent.Contains(m_mouseX, m_mouseY)) {
            std::vector<VfxContextMenuItem> items;
            if (m_composition.CenterMode() == VfxCenterMode::Parameters) {
                const auto layout = m_paramInspector.ComputeLayout(centerContent);
                const auto hit    = m_paramInspector.HitTest(layout,
                                                              m_mouseX, m_mouseY);
                VfxContextMenuItem copy;   copy.label   = "Copy Value";   copy.hotkey = "Ctrl+C"; items.push_back(copy);
                VfxContextMenuItem paste;  paste.label  = "Paste Value";  paste.hotkey = "Ctrl+V"; items.push_back(paste);
                { VfxContextMenuItem sep;  sep.isSeparator = true; items.push_back(sep); }
                VfxContextMenuItem reset;  reset.label  = "Reset to Default";
                if (!hit.name.empty() && m_paramInspector.onParameterReset) {
                    const auto scope = hit.scope;
                    const std::string n = hit.name;
                    reset.onActivate = [this, scope, n] {
                        if (m_paramInspector.onParameterReset) m_paramInspector.onParameterReset(scope, n);
                    };
                }
                items.push_back(reset);
                { VfxContextMenuItem sep;  sep.isSeparator = true; items.push_back(sep); }
                VfxContextMenuItem bind;   bind.label   = "Bind to Curve...";
                if (!hit.name.empty() && m_paramInspector.onParameterBindRequested) {
                    const auto scope = hit.scope;
                    const std::string n = hit.name;
                    bind.onActivate = [this, scope, n] {
                        if (m_paramInspector.onParameterBindRequested) m_paramInspector.onParameterBindRequested(scope, n);
                    };
                }
                items.push_back(bind);
            } else if (m_composition.CenterMode() == VfxCenterMode::Curves) {
                // Curve key right-click: Tangent submenu + Delete Key.
                VfxContextMenuItem tan; tan.label = "Set Tangent";
                {
                    VfxContextMenuItem s; s.label = "Auto";       tan.submenu.push_back(s);
                }
                {
                    VfxContextMenuItem s; s.label = "Linear";     tan.submenu.push_back(s);
                }
                {
                    VfxContextMenuItem s; s.label = "Cubic";      tan.submenu.push_back(s);
                }
                {
                    VfxContextMenuItem s; s.label = "Constant";   tan.submenu.push_back(s);
                }
                items.push_back(tan);
                { VfxContextMenuItem sep; sep.isSeparator = true; items.push_back(sep); }
                VfxContextMenuItem del; del.label = "Delete Key"; del.hotkey = "Del";
                del.onActivate = [this] {
                    const size_t sel = m_curvesEditor.SelectedIndex();
                    if (sel != static_cast<size_t>(-1)) {
                        m_curvesEditor.RemoveKey(sel);
                    }
                };
                items.push_back(del);
            }
            if (!items.empty()) {
                m_contextMenu.Open(m_mouseX, m_mouseY, std::move(items),
                                    static_cast<float>(m_screenW),
                                    static_cast<float>(m_screenH));
                m_wantsInput = true;
            }
        }

        switch (m_composition.CenterMode()) {
        case VfxCenterMode::Curves:
            m_curvesEditor.Render(m_textRenderer, m_font, centerContent);
            break;
        case VfxCenterMode::Parameters: {
            m_paramInspector.Render(m_textRenderer, m_font, centerContent);

            // Phase 14.11 — inspector scrollbar. Content height is
            // derived from the last rendered layout's extent.
            const auto layout = m_paramInspector.ComputeLayout(centerContent);
            float contentBottom = centerContent.y;
            for (const auto& it : layout) {
                const float b = it.rect.y + it.rect.height;
                if (b > contentBottom) contentBottom = b;
            }
            const float contentH = contentBottom - centerContent.y;
            if (contentH > centerContent.height) {
                m_paramsScrollbar.SetView(centerContent, contentH);
                m_paramsScrollbar.SetHovered(
                    centerContent.Contains(m_mouseX, m_mouseY));
                m_paramsScrollbar.Render(m_textRenderer);
            }
            break;
        }
        case VfxCenterMode::ModuleParams:
        case VfxCenterMode::NodeGraph:
        default: {
            const std::string& sel = m_stackPanel.SelectedInstanceId();
            const auto& palettePanel = VfxThemeColors();
            if (sel.empty()) {
                m_textRenderer.AddText(m_font, "Select a module in the stack.",
                                        centerContent.x + 12.0f,
                                        centerContent.y + 16.0f,
                                        palettePanel.textDim);
                break;
            }
            // Resolve the selected module + its metadata to render a
            // compact "selected module" card with type / stage / cat /
            // description.
            ::uz::VfxStage foundStage = ::uz::VfxStage::ParticleUpdate;
            size_t foundIdx = 0;
            if (emitter->stack.FindInstance(sel, foundStage, foundIdx)) {
                const auto& mod =
                    emitter->stack.GetEntry(foundStage).modules[foundIdx];
                const auto* meta =
                    ::uz::VfxModuleMetadata::Find(mod.moduleTypeName);

                const float x0 = centerContent.x + 12.0f;
                float y = centerContent.y + 16.0f;

                // Icon + name headline.
                const char* glyph =
                    ::uz::VfxModuleMetadata::GetIconGlyph(mod.moduleTypeName);
                m_textRenderer.AddText(m_font, glyph, x0, y,
                                        palettePanel.accentPrimary);
                m_textRenderer.AddText(m_font, mod.moduleTypeName,
                                        x0 + 20.0f, y,
                                        palettePanel.textPrimary);
                y += 22.0f;

                // Stage + category line.
                std::string stageLine = "Stage: ";
                stageLine += ::uz::VfxStageName(foundStage);
                m_textRenderer.AddText(m_font, stageLine, x0, y,
                                        palettePanel.textSecondary);
                y += 18.0f;
                if (meta != nullptr && meta->category != nullptr) {
                    std::string catLine = "Category: ";
                    catLine += meta->category;
                    m_textRenderer.AddText(m_font, catLine, x0, y,
                                            palettePanel.textSecondary);
                    y += 18.0f;
                }

                // Instance id.
                std::string idLine = "Id: ";
                idLine += sel;
                m_textRenderer.AddText(m_font, idLine, x0, y,
                                        palettePanel.textDim);
                y += 22.0f;

                // 1-px separator.
                m_textRenderer.AddRect(x0, y, centerContent.width - 24.0f,
                                        1.0f, palettePanel.borderSubtle);
                y += 10.0f;

                // Description (wrap ~ every 40 chars for the narrow pane).
                if (meta != nullptr && meta->description != nullptr) {
                    const std::string desc = meta->description;
                    const size_t wrap = 40;
                    size_t cursor = 0;
                    while (cursor < desc.size()) {
                        size_t end = std::min(cursor + wrap, desc.size());
                        if (end < desc.size()) {
                            const size_t sp = desc.rfind(' ', end);
                            if (sp != std::string::npos && sp > cursor + 8) {
                                end = sp;
                            }
                        }
                        m_textRenderer.AddText(m_font,
                            desc.substr(cursor, end - cursor),
                            x0, y, palettePanel.textSecondary);
                        cursor = (end < desc.size() &&
                                   desc[end] == ' ') ? end + 1 : end;
                        y += 16.0f;
                    }
                }

                // Empty-state hint.
                y += 8.0f;
                m_textRenderer.AddText(m_font,
                    "(parameter editor coming in a later phase)",
                    x0, y, palettePanel.textDim);
            }
            break;
        }
        }
    }

    // ── Right panel (phase 12.9.0) — System Overview ──────────────────
    if (rects.rightPanel.width > 4.0f && rects.rightPanel.height > 8.0f) {
        constexpr float kRightHeaderH = 22.0f;
        m_textRenderer.AddRect(rects.rightPanel.x, rects.rightPanel.y,
                                rects.rightPanel.width, kRightHeaderH,
                                kColorBarBg);
        m_textRenderer.AddText(m_font, "Emitters",
                                rects.rightPanel.x + 8.0f,
                                rects.rightPanel.y + 4.0f, kColorText);
        // Phase 14.15 — top slice of the right panel reserved for the
        // emitter tree widget. Remaining area holds the system
        // overview.
        constexpr float kTreeH = 140.0f;
        const float availBelowHeader = std::max(
            0.0f, rects.rightPanel.height - kRightHeaderH);
        const float treeH = std::min(kTreeH, availBelowHeader);
        const VfxStackRect treeBounds{
            rects.rightPanel.x,
            rects.rightPanel.y + kRightHeaderH,
            rects.rightPanel.width,
            treeH
        };

        // Rebuild the tree each frame from the current emitter list.
        m_emitterTree.Clear();
        {
            VfxTreeNode root{};
            root.id          = 0u;
            root.label       = "Emitters";
            root.depth       = 0u;
            root.hasChildren = true;
            root.expanded    = m_emitterRootExpanded;
            root.iconTint    = palette.textSecondary;
            m_emitterTree.AddNode(root);
            for (size_t i = 0; i < m_emitterNames.size(); ++i) {
                VfxTreeNode n{};
                n.id          = static_cast<uint32_t>(i + 1);
                n.label       = m_emitterNames[i];
                n.depth       = 1u;
                n.icon        = VfxUiIcon::Dot;
                n.iconTint    = palette.stateSuccess;
                n.expanded    = true;
                n.hasChildren = false;
                m_emitterTree.AddNode(n);
            }
        }
        if (m_activeEmitterIdx >= 0) {
            m_emitterTree.SetSelectedId(
                static_cast<uint32_t>(m_activeEmitterIdx + 1));
        }

        // Hover + click dispatch against the tree.
        {
            const auto treeLayout = m_emitterTree.ComputeLayout(treeBounds);
            const auto hit = m_emitterTree.HitTest(
                treeLayout, m_mouseX, m_mouseY);
            m_emitterTree.SetHoveredId(
                (hit.type != VfxTreeItemType::None) ? hit.nodeId : 0u);

            if (m_mouseJustPressed && hit.type != VfxTreeItemType::None) {
                if (hit.type == VfxTreeItemType::ExpandArrow &&
                    hit.nodeId == 0u) {
                    m_emitterRootExpanded = !m_emitterRootExpanded;
                    m_wantsInput = true;
                } else if (hit.type == VfxTreeItemType::Row &&
                            hit.nodeId >= 1u) {
                    const size_t idx = hit.nodeId - 1u;
                    if (idx < m_emitterNames.size()) {
                        m_selectedEmitterIdx = static_cast<int>(idx);
                        m_activeEmitterIdx   = static_cast<int>(idx);
                        if (onSelectEmitter) {
                            onSelectEmitter(static_cast<int>(idx));
                        }
                        m_wantsInput = true;
                    }
                }
            }
        }

        m_emitterTree.Render(m_textRenderer, m_font, GetMsdfFont(),
                              treeBounds);

        const VfxStackRect rightContent{
            rects.rightPanel.x,
            rects.rightPanel.y + kRightHeaderH + treeH,
            rects.rightPanel.width,
            std::max(0.0f, rects.rightPanel.height - kRightHeaderH - treeH)
        };

        // Refresh the overview view vector once per frame from the
        // overlay's emitter-name list. The active emitter is the only
        // one we have authoritative state for (isEmitting + cpuAliveCount
        // via `emitter` arg); other tabs render as disabled placeholders.
        m_overviewViews.clear();
        m_overviewViews.reserve(m_emitterNames.size());
        for (size_t i = 0; i < m_emitterNames.size(); ++i) {
            VfxOverviewEmitterView v;
            v.name = m_emitterNames[i];
            const bool isActive =
                (static_cast<int>(i) == m_activeEmitterIdx);
            if (isActive && emitter != nullptr) {
                v.isEmitting = emitter->isEmitting;
                v.isEnabled  = emitter->isEmitting;
                v.aliveCount = emitter->cpuAliveCount;
            } else {
                v.isEmitting = false;
                v.isEnabled  = false;
                v.aliveCount = 0;
            }
            m_overviewViews.push_back(std::move(v));
        }
        m_systemOverview.SelectIndex(
            static_cast<size_t>(m_activeEmitterIdx));

        // Click dispatch before Render.
        if (m_mouseJustPressed) {
            const auto layout = m_systemOverview.ComputeLayout(rightContent);
            const auto hit    = m_systemOverview.HitTest(layout,
                                                          m_mouseX, m_mouseY);
            switch (hit.type) {
            case VfxOverviewItemType::Row:
            case VfxOverviewItemType::NameLabel:
            case VfxOverviewItemType::StatusDot:
            case VfxOverviewItemType::ParticleCount:
            case VfxOverviewItemType::DragHandle:
                m_systemOverview.SelectIndex(hit.emitterIndex);
                m_activeEmitterIdx = static_cast<int>(hit.emitterIndex);
                m_selectedEmitterIdx = static_cast<int>(hit.emitterIndex);
                if (onSelectEmitter) {
                    onSelectEmitter(static_cast<int>(hit.emitterIndex));
                }
                m_wantsInput = true;
                break;
            case VfxOverviewItemType::Checkbox:
                if (hit.emitterIndex < m_overviewViews.size() &&
                    m_systemOverview.onEmitterEnableToggled) {
                    const bool newState =
                        !m_overviewViews[hit.emitterIndex].isEnabled;
                    m_systemOverview.onEmitterEnableToggled(
                        hit.emitterIndex, newState);
                }
                m_wantsInput = true;
                break;
            case VfxOverviewItemType::BtnMoveUp:
                if (hit.emitterIndex > 0u &&
                    m_systemOverview.onEmitterMoved) {
                    m_systemOverview.onEmitterMoved(
                        hit.emitterIndex, hit.emitterIndex - 1u);
                }
                m_wantsInput = true;
                break;
            case VfxOverviewItemType::BtnMoveDown:
                if (m_systemOverview.onEmitterMoved &&
                    hit.emitterIndex + 1u < m_overviewViews.size()) {
                    m_systemOverview.onEmitterMoved(
                        hit.emitterIndex, hit.emitterIndex + 1u);
                }
                m_wantsInput = true;
                break;
            case VfxOverviewItemType::BtnDuplicate:
                if (m_systemOverview.onEmitterDuplicated) {
                    m_systemOverview.onEmitterDuplicated(hit.emitterIndex);
                }
                m_wantsInput = true;
                break;
            case VfxOverviewItemType::BtnDelete:
                if (m_systemOverview.onEmitterDeleted) {
                    m_systemOverview.onEmitterDeleted(hit.emitterIndex);
                }
                m_wantsInput = true;
                break;
            case VfxOverviewItemType::AddEmitterRow:
                if (m_systemOverview.onAddEmitterRequested) {
                    m_systemOverview.onAddEmitterRequested();
                } else if (onAddEmitterRequested) {
                    // Fall through to the menu-bar callback so one
                    // handler serves both entry points.
                    onAddEmitterRequested();
                }
                m_wantsInput = true;
                break;
            default:
                break;
            }
        }

        m_systemOverview.Render(m_textRenderer, m_font, rightContent);

        // Phase 14.11 — system overview scrollbar (content height
        // approximated from row count × 28 px).
        const float rowH = 28.0f;
        const float contentH = static_cast<float>(m_emitterNames.size()) * rowH;
        if (contentH > rightContent.height) {
            m_overviewScrollbar.SetView(rightContent, contentH);
            m_overviewScrollbar.SetHovered(
                rightContent.Contains(m_mouseX, m_mouseY));
            m_overviewScrollbar.Render(m_textRenderer);
        }
    }

    // ── Timeline scrubber (bottom strip) ───────────────────────────────
    // Timeline scrubber rendering disabled — layout height is 0.

    // Phase 14.16 — content-browser modal overlay. Renders last so
    // it sits above every other panel when open.
    if (m_contentBrowserOpen) {
        // Dimming backdrop — full-screen black at 60 % alpha.
        m_textRenderer.AddRect(0.0f, 0.0f,
                                 static_cast<float>(m_screenW),
                                 static_cast<float>(m_screenH),
                                 Vec4{0.0f, 0.0f, 0.0f, 0.60f});

        // Dialog body (rounded 6-px corners, popup drop shadow).
        const auto& pal    = VfxThemeColors();
        const auto& radius = VfxThemeRadius();
        const auto& shadow = VfxThemeShadows();
        Vec4 shadowColor = pal.shadow;
        shadowColor.w = shadow.dialog.alpha;
        m_textRenderer.AddRoundedRect(
            m_contentBrowserBounds.x, m_contentBrowserBounds.y,
            m_contentBrowserBounds.width, m_contentBrowserBounds.height,
            radius.lg, pal.popupBg,
            1.0f, pal.borderStrong,
            Vec2{0.0f, shadow.dialog.offsetY},
            shadow.dialog.blur, shadowColor);

        // Header row with a title + close glyph.
        constexpr float kHdrH = 26.0f;
        m_textRenderer.AddRect(m_contentBrowserBounds.x,
                                m_contentBrowserBounds.y,
                                m_contentBrowserBounds.width, kHdrH,
                                pal.barBg);
        if (GetMsdfFont() != nullptr) {
            m_textRenderer.AddMsdfText(*GetMsdfFont(), "Content Browser",
                                         m_contentBrowserBounds.x + 10.0f,
                                         m_contentBrowserBounds.y + 6.0f,
                                         12.0f, pal.textPrimary);
        } else {
            m_textRenderer.AddText(m_font, "Content Browser",
                                     m_contentBrowserBounds.x + 10.0f,
                                     m_contentBrowserBounds.y + 4.0f,
                                     pal.textPrimary);
        }

        // Close glyph top-right.
        const float closeSize = 14.0f;
        const VfxStackRect closeRect{
            m_contentBrowserBounds.x + m_contentBrowserBounds.width
                - closeSize - 10.0f,
            m_contentBrowserBounds.y + (kHdrH - closeSize) * 0.5f,
            closeSize, closeSize
        };
        m_textRenderer.AddSprite(VfxUiIcon::Cross,
                                   closeRect.x, closeRect.y,
                                   closeRect.width, closeRect.height,
                                   pal.textDim);

        // Grid body — content browser occupies the region below the
        // header, minus 8-px inner padding.
        const VfxStackRect gridBounds{
            m_contentBrowserBounds.x + 8.0f,
            m_contentBrowserBounds.y + kHdrH + 8.0f,
            m_contentBrowserBounds.width - 16.0f,
            m_contentBrowserBounds.height - kHdrH - 16.0f
        };

        // Hit-test + click dispatch before render so feedback is
        // same-frame.
        const auto tileId = m_contentBrowser.HitTest(
            m_contentBrowser.ComputeLayout(gridBounds),
            m_mouseX, m_mouseY);
        m_contentBrowser.SetHoveredId(tileId);

        // Close-icon click dismisses the modal.
        if (m_mouseJustPressed) {
            const bool closeHit =
                m_mouseX >= closeRect.x &&
                m_mouseX <  closeRect.x + closeRect.width &&
                m_mouseY >= closeRect.y &&
                m_mouseY <  closeRect.y + closeRect.height;
            if (closeHit) {
                CloseContentBrowser();
                m_wantsInput = true;
            } else if (tileId != 0u) {
                m_contentBrowser.SetSelectedId(tileId);
                if (onContentBrowserSelect) {
                    for (const auto& t : m_contentBrowser.Tiles()) {
                        if (t.id == tileId) {
                            onContentBrowserSelect(tileId, t.label);
                            break;
                        }
                    }
                }
                m_wantsInput = true;
            }
        }

        m_contentBrowser.Render(m_textRenderer, m_font, GetMsdfFont(),
                                  gridBounds);
    }

    // Mark the center reserve region so the 3D viewport knows to render
    // there. The overlay doesn't clip the viewport — the caller is free
    // to ignore this. Empty rect = full window.
    (void)rects.viewportReserve;
}

} // namespace uz
