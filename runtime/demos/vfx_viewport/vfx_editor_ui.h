/// VFX Editor UI — Mouse-Driven Visual Overlay
///
/// Renders a clickable menu bar, collapsible property panels, toolbar buttons,
/// sliders, toggles, and dropdown menus for real-time VFX emitter editing.
/// Uses the engine's TextRenderer (AddRect + AddText) for all rendering and
/// InputSystem mouse queries for interaction.
///
/// # Architecture
/// Owns its own TextRenderer and BitmapFont (separate from StatsOverlay) so
/// that it can render independently on top of the scene. All positions are in
/// screen pixels with top-left origin.
///
/// # Thread Safety
/// NOT thread-safe. Call all methods from the render thread.

#pragma once

#include "bitmap_font.h"
#include "text_renderer.h"
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
struct VFXEmitterConfig;

// ─────────────────────────────────────────────────────────────────────────────
// VFXEditorUI
// ─────────────────────────────────────────────────────────────────────────────

/// Mouse-driven visual UI overlay for VFX emitter configuration.
///
/// Provides a top menu bar, left property panel, bottom toolbar, and popup
/// dropdown menus. All interaction is mouse-driven (click, drag sliders).
/// When the mouse hovers any UI element, `WantsMouseInput()` returns true
/// so that the caller can suppress camera/gizmo mouse handling.
class VFXEditorUI {
public:
    VFXEditorUI() = default;

    /// Initializes the TextRenderer, BitmapFont, and internal state.
    ///
    /// # Arguments
    /// * `device`   — RHI device for GPU resource creation.
    /// * `cmdList`  — Command list in recording state (for font atlas upload).
    /// * `upload`   — Upload context for staging buffer management.
    /// * `shaderPipeline` — Shader compilation pipeline for overlay PSOs.
    /// * `rtvFormat` — Render target format (typically RGBA8Unorm for backbuffer).
    /// * `screenW`, `screenH` — Initial screen dimensions in pixels.
    /// * `bindless` — If true, uses SM6.6 bindless rendering path.
    ///
    /// # Returns
    /// `true` on success, `false` if font or text renderer init fails.
    bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
              ShaderPipeline& shaderPipeline, RHIFormat rtvFormat,
              uint32_t screenW, uint32_t screenH, bool bindless);

    /// Destroys all GPU resources (font atlas, text renderer buffers/pipelines).
    void Shutdown();

    /// Updates screen dimensions on window resize.
    void SetScreenSize(uint32_t w, uint32_t h);

    /// Resets the text renderer vertex batch for a new frame.
    ///
    /// # Arguments
    /// * `frameIndex` — Current frame index for triple-buffered vertex selection.
    void BeginFrame(uint32_t frameIndex);

    /// Reads mouse state from the input system and updates UI interaction.
    ///
    /// # Arguments
    /// * `input` — Input system for mouse position and button state.
    /// * `dt`    — Frame delta time in seconds (unused currently, reserved).
    ///
    /// # Returns
    /// `true` if the UI consumed the mouse event (caller should suppress
    /// camera/gizmo input). `false` if mouse is not over any UI element.
    bool HandleMouse(const InputSystem* input, float dt);

    /// Builds all UI elements (menu bar, panels, toolbar) into the text renderer.
    ///
    /// Call after `BeginFrame()` and `HandleMouse()`, before `Render()`.
    ///
    /// # Arguments
    /// * `config`     — Pointer to the live VFXEmitterConfig to edit.
    /// * `spawnRate`  — Pointer to the spawn rate float.
    /// * `burstMode`  — Pointer to the burst mode bool.
    /// * `burstCount` — Pointer to the burst count uint32.
    /// * `vfxEnabled` — Pointer to the VFX enabled bool.
    void BuildUI(VFXEmitterConfig* config, float* spawnRate, bool* burstMode,
                 uint32_t* burstCount, bool* vfxEnabled);

    /// Uploads vertex data and draws all batched UI geometry.
    ///
    /// # Arguments
    /// * `cmdList` — Command list with render target set (backbuffer).
    /// * `cbRing`  — Constant buffer allocator for per-draw constants.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing);

    /// Returns `true` when the mouse is hovering any UI element.
    ///
    /// When this returns true, the caller should suppress camera/gizmo mouse
    /// input to prevent clicking "through" the UI.
    [[nodiscard]] bool WantsMouseInput() const { return m_wantsInput; }

    /// Sets the list of preset names for the Presets dropdown menu.
    void SetPresetNames(const std::vector<std::string>& names) { m_presetNames = names; }

    /// Sets the list of emitter names and the active emitter index for the
    /// emitter selector bar displayed between the menu bar and property panels.
    void SetEmitterNames(const std::vector<std::string>& names, int activeIndex) {
        m_emitterNames = names;
        m_activeEmitterIdx = activeIndex;
    }

    /// Returns the emitter index selected by the user via the emitter bar.
    /// Returns -1 if no emitter was clicked this frame.
    [[nodiscard]] int GetSelectedEmitterIndex() const { return m_selectedEmitterIdx; }

    // ── Callbacks ────────────────────────────────────────────────────────
    std::function<void()>  onRestart;      ///< Called when Restart button is clicked.
    std::function<void()>  onBurst;        ///< Called when Burst button is clicked.
    std::function<void()>  onFireball;     ///< Called when Fireball button is clicked.
    std::function<void(int)> onLoadPreset; ///< Called with preset index when a preset is selected.
    std::function<void(int)> onSelectEmitter; ///< Called with emitter index when an emitter tab is clicked.

    // ── Stage Tree Data Model ────────────────────────────────────────────

    /// Stages in the Niagara-style module stack. Controls grouping and
    /// visual hierarchy in the left panel tree view.
    enum class VfxStage : uint8_t {
        EmitterSpawn,    ///< Emitter-level spawn settings (burst, rate).
        EmitterUpdate,   ///< Emitter-level update (lifecycle, duration).
        ParticleSpawn,   ///< Per-particle initialization (position, velocity, size).
        ParticleUpdate,  ///< Per-particle simulation (forces, collision).
        Render,          ///< Visual output (billboard, color, size curves).
        Count
    };
    static constexpr int kStageCount = static_cast<int>(VfxStage::Count);

    /// A single editable parameter inside a module. Holds a typed pointer
    /// directly into the live VFXEmitterConfig so changes take effect immediately.
    struct VfxModuleParam {
        const char* label = nullptr;
        enum Type : uint8_t { Float, Uint, Bool, Enum } type = Float;
        float*    fPtr      = nullptr;  ///< For Float params.
        uint32_t* uPtr      = nullptr;  ///< For Uint/Bool/Enum params.
        float     min       = 0.0f;
        float     max       = 1.0f;
        float     step      = 0.01f;
        const char* const* enumNames = nullptr; ///< For Enum params.
        uint32_t  enumCount = 0;                ///< For Enum params.
    };

    /// A module groups related parameters under a stage. The `strengthPtr`
    /// is the "main enable" value — when zero the module is visually grayed
    /// out and can be toggled off/on via the checkbox. Modules with no
    /// strengthPtr (nullptr) are always considered active.
    struct VfxModuleDef {
        const char* name     = nullptr;
        VfxStage    stage    = VfxStage::ParticleUpdate;
        float*      strengthPtr = nullptr;  ///< Float enable (0=off). Null=always on.
        uint32_t*   enablePtr   = nullptr;  ///< Uint enable flag (0=off). Null=n/a.
        float       savedStrength = 0.0f;   ///< Saved value for toggle restore (float path).
        uint32_t    savedEnable   = 0;      ///< Saved value for toggle restore (uint path).
        std::vector<VfxModuleParam> params;

        /// Returns a short summary string for the module row (e.g., "Z:-3.0").
        /// Writes into the provided buffer.
        void FormatSummary(char* buf, int bufSize) const;

        /// Returns true when the module is considered active.
        [[nodiscard]] bool IsActive() const;
    };

private:
    // ── GPU resources ────────────────────────────────────────────────────
    TextRenderer m_textRenderer;
    BitmapFont   m_font;
    bool         m_initialized = false;
    uint32_t     m_lastFrameIndex = 0; ///< Saved for dropdown overlay second render pass.

    // ── Screen dimensions ────────────────────────────────────────────────
    uint32_t m_screenW = 1280;
    uint32_t m_screenH = 720;

    // ── Mouse state (updated each frame in HandleMouse) ──────────────────
    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
    bool  m_mouseDown = false;
    bool  m_mouseJustPressed = false;
    bool  m_mouseJustReleased = false;
    bool  m_wantsInput = false;
    bool  m_ctrlDown = false; ///< Ctrl held — enables fine-step slider drag (0.1x delta).
    float m_scrollDelta = 0.0f; ///< Mouse wheel delta this frame (positive=up).

    // ── Menu bar state ───────────────────────────────────────────────────
    static constexpr int kMenuCount = 5;
    int m_openMenu = -1; ///< -1=none, 0=File, 1=Emitter, 2=Modules, 3=Presets, 4=View.

    // ── Panel state ──────────────────────────────────────────────────────
    bool m_leftPanelOpen = false;
    bool m_rightPanelOpen = false;
    int  m_expandedCategory = 0; ///< Expanded category index in left panel.

    // ── Stage tree state ─────────────────────────────────────────────────
    std::vector<VfxModuleDef> m_modules;           ///< All modules, grouped by stage.
    bool  m_stageExpanded[kStageCount] = {true, true, true, true, true}; ///< Per-stage expand state.
    int   m_expandedModuleIndex = -1;              ///< Which module shows full params (accordion).
    float m_leftPanelScroll = 0.0f;                ///< Scroll offset for left panel content.
    float m_leftPanelContentH = 0.0f;              ///< Total content height (computed each frame).
    VFXEmitterConfig* m_lastConfig = nullptr;       ///< Tracks config pointer for rebuild.

    // ── Slider drag state ────────────────────────────────────────────────
    float* m_dragSliderPtr = nullptr; ///< Pointer to the float being dragged, or nullptr.
    float  m_dragSliderMin = 0.0f;
    float  m_dragSliderMax = 1.0f;

    // ── Dropdown state ───────────────────────────────────────────────────
    uint32_t* m_dropdownPtr = nullptr;   ///< Pointer to the uint32_t being edited, or nullptr.
    float     m_dropdownX = 0.0f;        ///< Screen X of the open dropdown.
    float     m_dropdownY = 0.0f;        ///< Screen Y of the open dropdown.
    float     m_dropdownW = 0.0f;        ///< Width of the open dropdown.
    const char* const* m_dropdownNames = nullptr; ///< Names array for the open dropdown.
    uint32_t  m_dropdownCount = 0;       ///< Number of items in the open dropdown.

    // ── Preset names ─────────────────────────────────────────────────────
    std::vector<std::string> m_presetNames;

    // ── Multi-emitter selector state ─────────────────────────────────────
    std::vector<std::string> m_emitterNames;   ///< Names of active emitters.
    int  m_activeEmitterIdx  = 0;              ///< Currently selected emitter tab.
    int  m_selectedEmitterIdx = -1;            ///< Emitter clicked this frame (-1 = none).

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
    static constexpr Vec4 kColorCatHeader = {0.18f, 0.18f, 0.24f, 1.00f};
    static constexpr Vec4 kColorMenuHover = {0.30f, 0.30f, 0.40f, 1.00f};
    static constexpr Vec4 kColorMenuActive= {0.25f, 0.40f, 0.70f, 1.00f};
    static constexpr Vec4 kColorDropBg    = {0.14f, 0.14f, 0.18f, 1.00f}; // Fully opaque — must cover panels beneath
    static constexpr Vec4 kColorStageHdr  = {0.10f, 0.10f, 0.14f, 1.00f}; ///< Stage header row background.
    static constexpr Vec4 kColorModuleRow = {0.16f, 0.16f, 0.21f, 1.00f}; ///< Module row background.
    static constexpr Vec4 kColorModuleExp = {0.19f, 0.22f, 0.30f, 1.00f}; ///< Expanded module highlight.
    static constexpr Vec4 kColorCheckOn   = {0.25f, 0.65f, 0.35f, 1.00f}; ///< Green checkbox fill (active).
    static constexpr Vec4 kColorCheckOff  = {0.35f, 0.35f, 0.40f, 1.00f}; ///< Gray checkbox border (inactive).
    static constexpr Vec4 kColorTextDim   = {0.50f, 0.50f, 0.55f, 1.00f}; ///< Dimmed text for disabled modules.
    static constexpr Vec4 kColorSummary   = {0.65f, 0.70f, 0.80f, 1.00f}; ///< Summary value text color.

    // ── Layout constants ─────────────────────────────────────────────────
    static constexpr float kMenuBarH       = 30.0f;
    static constexpr float kEmitterBarH   = 26.0f; ///< Emitter selector tab bar height.
    static constexpr float kToolbarH      = 40.0f;
    static constexpr float kLeftPanelW    = 280.0f;
    static constexpr float kRightPanelW   = 240.0f;
    static constexpr float kMenuItemW     = 80.0f;
    static constexpr float kMenuItemGap   = 4.0f;
    static constexpr float kMenuItemPadX  = 10.0f;
    static constexpr float kSliderH       = 18.0f;
    static constexpr float kRowH          = 22.0f;
    static constexpr float kCatHeaderH    = 24.0f;
    static constexpr float kBtnPadX       = 8.0f;
    static constexpr float kPanelPadX     = 6.0f;
    static constexpr float kPanelPadY     = 4.0f;
    static constexpr float kDropItemH     = 22.0f;
    static constexpr float kStageHeaderH  = 26.0f;  ///< Stage header row height.
    static constexpr float kModuleRowH    = 22.0f;  ///< Module row height (checkbox + name + summary).
    static constexpr float kCheckboxSize  = 14.0f;  ///< Module checkbox size (pixels).
    static constexpr float kModuleIndent  = 12.0f;  ///< Param indent under expanded module.
    static constexpr float kScrollSpeed   = 30.0f;  ///< Pixels per scroll wheel notch.

    // ── Widget helpers ───────────────────────────────────────────────────

    /// Returns `true` if the current mouse position is inside the rectangle.
    [[nodiscard]] bool HitTest(float x, float y, float w, float h) const;

    /// Renders a clickable button. Returns `true` if clicked this frame.
    bool Button(float x, float y, float w, float h, const char* label);

    /// Renders a horizontal slider. Returns `true` if the value changed.
    bool Slider(float x, float y, float w, float* value, float minVal, float maxVal, const char* label);

    /// Renders a boolean toggle. Returns `true` if the value changed.
    bool Toggle(float x, float y, bool* value, const char* label);

    /// Renders an enum dropdown trigger. Click opens the dropdown overlay.
    void DropdownTrigger(float x, float y, float w, uint32_t* value,
                         const char* const* names, uint32_t count, const char* label);

    // ── Panel builders ───────────────────────────────────────────────────

    /// Builds the top menu bar and any open dropdown menus.
    void BuildMenuBar();
    void BuildMenuDropdowns(); ///< Renders open menu dropdown AFTER panels (z-order on top).

    /// Builds the emitter selector bar (below menu bar, above panels).
    /// Shows clickable tabs for each emitter with an active highlight.
    void BuildEmitterBar();

    /// Builds the bottom toolbar with action buttons and preset navigation.
    void BuildToolbar(bool* burstMode, bool* vfxEnabled);

    /// Populates m_modules from the given VFXEmitterConfig pointer.
    /// Called once on init and whenever the active emitter changes.
    void BuildModuleList(VFXEmitterConfig* config, float* spawnRate, uint32_t* burstCount);

    /// Builds the left panel as a Niagara-style stage tree with collapsible
    /// stages and toggleable modules. Replaces the old flat BuildLeftPanel.
    void BuildStageTree(VFXEmitterConfig* config, float* spawnRate, uint32_t* burstCount);

    /// Builds the right panel showing expanded module detail sliders.
    /// When a module is expanded in the left tree, right panel shows its
    /// full parameter set as large sliders. Otherwise shows "Select a module".
    void BuildRightPanel(VFXEmitterConfig* config);

    /// Renders an open dropdown overlay (drawn last, on top of everything).
    void BuildDropdownOverlay();

    /// Closes any open dropdown.
    void CloseDropdown();
};

} // namespace uz
