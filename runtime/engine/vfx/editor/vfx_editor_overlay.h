/// VFX Editor Overlay — Mouse-Driven Parameter Editor for Engine Application
///
/// Renders a clickable menu bar, collapsible stage tree, parameter sliders,
/// preset selector, and multi-emitter tabs using the engine's TextRenderer.
/// Designed for the Application-based viewport (works with ParticleEmitter).
///
/// # Architecture
/// Owns its own TextRenderer and BitmapFont (separate from StatsOverlay).
/// All positions are in screen pixels with top-left origin.
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the render thread.

#pragma once

#include "editor/font/bitmap_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_preview_clock.h"
#include "vector4.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class InputSystem;
class ShaderPipeline;
class UploadContext;
struct ParticleEmitter;

// ─────────────────────────────────────────────────────────────────────────────
// VfxEditorOverlay
// ────────��────────────────────────────────────────────────────────────────────

/// Mouse-driven visual UI overlay for VFX emitter configuration.
///
/// Provides a top menu bar, left property panel, bottom toolbar, and popup
/// dropdown menus. All interaction is mouse-driven (click, drag sliders).
/// When the mouse hovers any UI element, `WantsMouseInput()` returns true
/// so that the caller can suppress camera/gizmo mouse handling.
class VfxEditorOverlay {
public:
    VfxEditorOverlay() = default;

    /// Initializes the TextRenderer, BitmapFont, and internal state.
    ///
    /// # Arguments
    /// * `device`   — RHI device for GPU resource creation.
    /// * `cmdList`  — Command list in recording state (font atlas upload).
    /// * `upload`   — Upload context for staging buffer management.
    /// * `shaderPipeline` — Shader compilation pipeline for overlay PSOs.
    /// * `rtvFormat` — Render target format (typically RGBA8Unorm).
    /// * `screenW`, `screenH` — Initial screen dimensions in pixels.
    /// * `bindless` — If true, uses SM6.6 bindless rendering path.
    ///
    /// # Returns
    /// `true` on success, `false` if font or text renderer init fails.
    bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
              ShaderPipeline& shaderPipeline, RHIFormat rtvFormat,
              uint32_t screenW, uint32_t screenH, bool bindless);

    /// Destroys all GPU resources.
    void Shutdown();

    /// Updates screen dimensions on window resize.
    void SetScreenSize(uint32_t w, uint32_t h);

    /// Resets the text renderer vertex batch for a new frame.
    void BeginFrame(uint32_t frameIndex);

    /// Reads mouse state and updates UI interaction (hit-test, drag tracking).
    ///
    /// Must be called before `BuildUI()` each frame. Tracks just-pressed
    /// internally from `IsMouseDown` to avoid `BeginFrame` timing issues.
    ///
    /// # Arguments
    /// * `input` — Input system for mouse position and button state.
    /// * `dt`    — Frame delta time in seconds.
    ///
    /// # Returns
    /// `true` if the UI consumed the mouse event (suppress camera input).
    bool HandleMouse(const InputSystem* input, float dt);

    /// Builds all UI elements (menu bar, stage tree, sliders) into the text renderer.
    ///
    /// # Arguments
    /// * `emitter` — Pointer to the live ParticleEmitter to edit via sliders.
    /// * `input`   — Optional InputSystem for fresh mouse position re-read.
    void BuildUI(ParticleEmitter* emitter, const InputSystem* input = nullptr);

    /// Renders all batched UI geometry. Dropdowns are drawn in a second pass
    /// to ensure correct z-order over panel content.
    ///
    /// # Arguments
    /// * `cmdList` — Command list with render target set (backbuffer).
    /// * `cbRing`  — Constant buffer allocator for per-draw constants.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing);

    /// Returns true when the mouse is hovering any UI element.
    [[nodiscard]] bool WantsMouseInput() const { return m_wantsInput; }

    /// Returns true if the overlay is visible.
    [[nodiscard]] bool IsVisible() const { return m_visible; }

    /// Toggles overlay visibility.
    void SetVisible(bool v) { m_visible = v; }
    void ToggleVisible() { m_visible = !m_visible; }

    /// Sets the list of preset names for the Presets dropdown menu.
    void SetPresetNames(const std::vector<std::string>& names) { m_presetNames = names; }

    /// Sets emitter names and active index for the emitter selector bar.
    void SetEmitterNames(const std::vector<std::string>& names, int activeIndex) {
        m_emitterNames = names;
        m_activeEmitterIdx = activeIndex;
    }

    /// Returns the emitter index selected by the user this frame (-1 = none).
    [[nodiscard]] int GetSelectedEmitterIndex() const { return m_selectedEmitterIdx; }

    // ── Callbacks ────────────────────────────────────────────────────────
    std::function<void()>    onRestart;
    /// Called with preset index. The callback receives the ParticleEmitter*
    /// that the overlay is currently editing (stored as second arg).
    std::function<void(int, ParticleEmitter*)> onLoadPreset;
    std::function<void(int)> onSelectEmitter;

    /// Called when the user clicks "Re-capture thumbnail" in the inspector.
    /// The string argument is the asset path whose thumbnail should be
    /// regenerated. The app wires this to enqueue a
    /// `VfxThumbnailCaptureRequest` (phase 9.7.0).
    std::function<void(const std::string& assetPath)> onRecaptureThumbnail;

    /// Returns the currently-edited emitter pointer (set by last BuildUI call).
    [[nodiscard]] ParticleEmitter* GetCurrentEmitter() const { return m_lastEmitter; }

    /// Forces rebuild of the module list on next BuildUI (after preset switch).
    void ForceRebuildModules() { m_lastEmitter = nullptr; }

    // ── Preview clock (phase 8.4.0) ──────────────────────────────────────
    // Scrub / pause / frame-step for timing-critical VFX authoring. Drives
    // the simulation delta time that the caller forwards to VFXSystem::Tick.
    //
    // Usage:
    // ```cpp
    // const float wallDt = frameTimer.GetDeltaSeconds();
    // const float simDt  = overlay.SimulationDeltaTime(wallDt);
    // vfxSystem.Tick(simDt);
    // ```
    //
    // @ref engine/vfx/editor/vfx_preview_clock.h
    // @ref docs/engine/25-vfx/21-preview-timeline.md

    /// Returns the simulation dt the caller should feed into `VFXSystem::Tick`,
    /// based on the current preview-clock state and the provided wall-clock
    /// delta. Equivalent to `wallDt` when the overlay is Playing at 1× rate.
    [[nodiscard]] float SimulationDeltaTime(float wallDt) noexcept {
        return previewClock_.Tick(wallDt);
    }

    /// Returns the preview clock for direct state manipulation by the
    /// editor shell (binding keyboard shortcuts / rendering a timeline).
    [[nodiscard]] VfxPreviewClock& PreviewClock() noexcept { return previewClock_; }
    [[nodiscard]] const VfxPreviewClock& PreviewClock() const noexcept {
        return previewClock_;
    }

    // ── Stage Tree Data Model ────���───────────────────────────────────────

    enum class VfxStage : uint8_t {
        EmitterSpawn, EmitterUpdate, ParticleSpawn,
        ParticleUpdate, Render, Count
    };
    static constexpr int kStageCount = static_cast<int>(VfxStage::Count);

    struct VfxModuleParam {
        const char* label = nullptr;
        enum Type : uint8_t { Float, Uint, Bool, Enum } type = Float;
        float*    fPtr      = nullptr;
        uint32_t* uPtr      = nullptr;
        float     min       = 0.0f;
        float     max       = 1.0f;
        const char* const* enumNames = nullptr;
        uint32_t  enumCount = 0;
    };

    struct VfxModuleDef {
        const char* name     = nullptr;
        VfxStage    stage    = VfxStage::ParticleUpdate;
        float*      strengthPtr = nullptr;
        uint32_t*   enablePtr   = nullptr;
        float       savedStrength = 0.0f;
        uint32_t    savedEnable   = 0;
        std::vector<VfxModuleParam> params;
        [[nodiscard]] bool IsActive() const;
    };

private:
    // ── GPU resources ────────────────────────────────────────────────────
    TextRenderer m_textRenderer;
    BitmapFont   m_font;
    bool         m_initialized = false;
    bool         m_visible = false;
    uint32_t     m_lastFrameIndex = 0;

    // ── Screen dimensions ─────────────────────────────────────────��──────
    uint32_t m_screenW = 1280;
    uint32_t m_screenH = 720;

    // ── Mouse state ──────────────────────────────────────────────────────
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    bool  m_mouseDown = false;
    bool  m_mouseJustPressed = false;
    bool  m_mouseJustReleased = false;
    bool  m_wantsInput = false;
    float m_scrollDelta = 0.0f;

    // ── Menu bar state ─────────��─────────────────────────────────────────
    static constexpr int kMenuCount = 4;
    int m_openMenu = -1;

    // ── Stage tree state ─────────────────────────────────────────────────
    std::vector<VfxModuleDef> m_modules;
    bool  m_stageExpanded[kStageCount] = {true, true, true, true, true};
    int   m_expandedModuleIndex = -1;
    float m_leftPanelScroll = 0.0f;
    float m_leftPanelContentH = 0.0f;
    ParticleEmitter* m_lastEmitter = nullptr;

    // ── Slider drag state ─────────��──────────────────────────────────────
    float* m_dragSliderPtr = nullptr;
    float  m_dragSliderMin = 0.0f;
    float  m_dragSliderMax = 1.0f;

    // ── Number input (text edit) state ──────────────────────────────────
    float* m_editingPtr = nullptr;
    float  m_editingMin = 0.0f;
    float  m_editingMax = 1.0f;
    std::string m_editBuffer;
    const InputSystem* m_inputRef = nullptr;

    // ── Tag input state (phase 9.1.0) ───────────────────────────────────
    /// True while the "+ Add tag" input field is receiving keystrokes for
    /// the currently-active emitter. Mutually exclusive with number-input
    /// editing (both paths clear the other when activating).
    bool        m_tagEditing = false;
    std::string m_tagEditBuffer;

    // ── Unified string-input focus (phase 10.1.0) ───────────────────────
    /// Pointer to the currently focused StringInput buffer. Only one
    /// string input can have focus at a time. Null when no StringInput
    /// is active. `m_editingPtr` (number) and `m_tagEditing` (tag) are
    /// legacy focus trackers that still work; `m_focusedStringInput`
    /// provides a unified mechanism for future consumers.
    std::string* m_focusedStringInput = nullptr;
    std::string  m_stringInputBackup; ///< Snapshot for Escape restore.

    // ── Dropdown state ─────────���─────────────────────────────────────────
    uint32_t* m_dropdownPtr = nullptr;
    float     m_dropdownX = 0.0f;
    float     m_dropdownY = 0.0f;
    float     m_dropdownW = 0.0f;
    const char* const* m_dropdownNames = nullptr;
    uint32_t  m_dropdownCount = 0;

    // ── Preset / emitter names ───────────────────────────────────────────
    std::vector<std::string> m_presetNames;
    std::vector<std::string> m_emitterNames;
    int  m_activeEmitterIdx  = 0;
    int  m_selectedEmitterIdx = -1;

    // ── Theme colors ─────────────────────────────────────────────────────
    static constexpr Vec4 kColorBarBg     = {0.12f, 0.12f, 0.16f, 0.95f};
    static constexpr Vec4 kColorPanelBg   = {0.15f, 0.15f, 0.20f, 0.95f};
    static constexpr Vec4 kColorBtnNormal = {0.25f, 0.25f, 0.30f, 1.00f};
    static constexpr Vec4 kColorBtnHover  = {0.35f, 0.35f, 0.40f, 1.00f};
    static constexpr Vec4 kColorBtnPress  = {0.20f, 0.20f, 0.25f, 1.00f};
    static constexpr Vec4 kColorText      = {0.90f, 0.90f, 0.90f, 1.00f};
    static constexpr Vec4 kColorAccent    = {0.30f, 0.60f, 1.00f, 1.00f};
    static constexpr Vec4 kColorSliderBg  = {0.15f, 0.15f, 0.20f, 1.00f};
    static constexpr Vec4 kColorSliderFill= {0.20f, 0.50f, 0.90f, 0.70f};
    static constexpr Vec4 kColorMenuHover = {0.30f, 0.30f, 0.40f, 1.00f};
    static constexpr Vec4 kColorMenuActive= {0.25f, 0.40f, 0.70f, 1.00f};
    static constexpr Vec4 kColorDropBg    = {0.14f, 0.14f, 0.18f, 1.00f};
    static constexpr Vec4 kColorStageHdr  = {0.10f, 0.10f, 0.14f, 1.00f};
    static constexpr Vec4 kColorModuleRow = {0.16f, 0.16f, 0.21f, 1.00f};
    static constexpr Vec4 kColorModuleExp = {0.19f, 0.22f, 0.30f, 1.00f};
    static constexpr Vec4 kColorCheckOn   = {0.25f, 0.65f, 0.35f, 1.00f};
    static constexpr Vec4 kColorCheckOff  = {0.35f, 0.35f, 0.40f, 1.00f};
    static constexpr Vec4 kColorTextDim   = {0.50f, 0.50f, 0.55f, 1.00f};

    // ── Layout constants ─────────────────────────────────────────────────
    static constexpr float kMenuBarH      = 30.0f;
    static constexpr float kEmitterBarH   = 26.0f;
    static constexpr float kLeftPanelW    = 280.0f;
    static constexpr float kMenuItemW     = 80.0f;
    static constexpr float kSliderH       = 18.0f;
    static constexpr float kRowH          = 22.0f;
    static constexpr float kStageHeaderH  = 26.0f;
    static constexpr float kModuleRowH    = 22.0f;
    static constexpr float kCheckboxSize  = 14.0f;
    static constexpr float kModuleIndent  = 12.0f;
    static constexpr float kDropItemH     = 22.0f;
    static constexpr float kScrollSpeed   = 30.0f;
    static constexpr float kPanelPadX     = 6.0f;

public:
    // ── Input filter (phase 10.1.0) ────────────────────────────────────

    /// Character filter for `StringInput`. Controls which keystrokes are
    /// accepted into the edit buffer.
    enum class InputFilter : uint8_t {
        Any,            ///< All printable characters.
        LowercaseASCII, ///< a-z 0-9 - _ (uppercase auto-lowered).
        Identifier,     ///< a-z A-Z 0-9 _ (C-style identifier chars).
        Numeric,        ///< 0-9 . - (decimal number input).
    };

    // ── Widget helpers ───────────────────────────────────────────────────
    [[nodiscard]] bool HitTest(float x, float y, float w, float h) const;
    bool Button(float x, float y, float w, float h, const char* label);
    bool Slider(float x, float y, float w, float* value, float minVal, float maxVal, const char* label);
public:
    /// Injects a scroll delta captured before InputSystem::BeginFrame() clears it.
    /// Called by frame_setup.cpp so the panel scroll works correctly.
    void InjectScrollDelta(float delta) { m_scrollDelta = delta; }

    /// Generic string-input widget (phase 10.1.0).
    ///
    /// Renders a clickable text field at `(x, y, w, kSliderH)`. When
    /// clicked, captures keyboard focus (only one StringInput can be
    /// active at a time). Typed characters are filtered through `filter`
    /// and appended to `*buffer`. Enter commits (returns true), Escape
    /// cancels (returns false and restores the original buffer).
    ///
    /// # Arguments
    /// * `x, y, w` — screen-pixel position and width.
    /// * `buffer`   — mutable string to edit. Must outlive the frame.
    /// * `filter`   — character filter (default Any).
    /// * `placeholder` — greyed-out text shown when buffer is empty and
    ///   the field is not focused (e.g. "Type here...").
    ///
    /// # Returns
    /// `true` when the user pressed Enter (committed). `false` otherwise.
    bool StringInput(float x, float y, float w, std::string* buffer,
                     InputFilter filter = InputFilter::Any,
                     const char* placeholder = "");

private:
    bool NumberInput(float x, float y, float w, float* value, float minVal, float maxVal, const char* label);
    void DropdownTrigger(float x, float y, float w, uint32_t* value,
                         const char* const* names, uint32_t count, const char* label);

    // ── Panel builders ───────────────────────────────────────────────────
    void BuildMenuBar();
    void BuildMenuDropdowns();
    void BuildEmitterBar();
    void BuildModuleList(ParticleEmitter* emitter);
    void BuildStageTree(ParticleEmitter* emitter);
    /// Renders the "Tags" row inside the left panel (phase 9.1.0):
    /// chip rendering with × remove buttons + "+ Add" text-input. Calls
    /// `VfxAssetTagMatches`-compatible lowercase validation. Returns the
    /// vertical height consumed so the caller can offset the module
    /// stack below it.
    float BuildTagsRow(ParticleEmitter* emitter, float x, float y, float w);
    void BuildDropdownOverlay();
    void CloseDropdown();

    /// Preview clock driving scrub / pause / frame-step (phase 8.4.0).
    /// Consumed via `SimulationDeltaTime(wallDt)` — see public API.
    VfxPreviewClock previewClock_{};
};

} // namespace uz
