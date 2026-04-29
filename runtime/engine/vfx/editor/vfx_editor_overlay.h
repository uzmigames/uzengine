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
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/undo/vfx_undo_stack.h"
#include "engine/vfx/editor/vfx_curves_editor_widget.h"
#include "engine/vfx/editor/vfx_editor_composition.h"
#include "engine/vfx/editor/vfx_content_browser_widget.h"
#include "engine/vfx/editor/vfx_context_menu_widget.h"
#include "engine/vfx/editor/vfx_editor_keybindings.h"
#include "engine/vfx/editor/vfx_editor_menu.h"
#include "engine/vfx/editor/vfx_editor_pane_widget.h"
#include "engine/vfx/editor/vfx_module_picker_dialog.h"
#include "engine/vfx/editor/vfx_splitter_widget.h"
#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/editor/vfx_scrollbar_widget.h"
#include "engine/vfx/editor/vfx_status_bar_widget.h"
#include "engine/vfx/editor/vfx_tab_strip_widget.h"
#include "engine/vfx/editor/vfx_toolbar_widget.h"
#include "engine/vfx/editor/vfx_tree_widget.h"
#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"
#include "engine/vfx/editor/vfx_system_overview_widget.h"
#include "engine/vfx/editor/vfx_thumbnail_drainer.h"
#include "engine/vfx/editor/vfx_timeline_scrubber_widget.h"
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

    /// Destroys all GPU resources owned by the overlay.
    ///
    /// Shuts down the text renderer, the bitmap font atlas, and the
    /// MSDF font atlas. Both atlases hold 1024x1024 R8 textures that
    /// D3D12MA otherwise reports as leaks at process exit
    /// (`m_pMetadata->IsEmpty()` assertion in `~NormalBlock`).
    ///
    /// # Parameters
    /// - `device`: RHI device used to free atlas textures. May be null
    ///   when the overlay was never initialised; in that case Shutdown
    ///   is a no-op.
    void Shutdown(IRHIDevice* device);

    /// Updates screen dimensions on window resize.
    void SetScreenSize(uint32_t w, uint32_t h);

    /// Resets the text renderer vertex batch for a new frame.
    void BeginFrame(uint32_t frameIndex);

    /// Phase 14.13 — pushes a transient notification onto the status
    /// bar's FIFO queue. Notification auto-dismisses after 3 seconds.
    void PushStatus(const std::string& msg,
                     VfxStatusLevel level = VfxStatusLevel::Info);

    /// Phase 14.13 — advances the status-bar notification timer.
    /// Host calls once per frame with the frame delta.
    void TickStatus(float dt);

    /// Phase 14.12 — routes an ASCII-letter key press through the
    /// menu accelerator tables. Returns `true` when the key was
    /// consumed (menu opened, or item command fired).
    ///
    /// # Arguments
    /// * `ascii` — Printable ASCII letter (case-insensitive).
    /// * `altHeld` — True when the Alt modifier is down.
    ///
    /// # Behaviour
    /// * `altHeld == true` + no menu open → open the top menu whose
    ///   accelerator char matches `ascii`; returns true on match.
    /// * `altHeld == false` + a dropdown is open → fire the first
    ///   enabled item whose accelerator char matches `ascii`; closes
    ///   the dropdown and returns true on match.
    /// * No match → returns false, caller continues its normal
    ///   key dispatch.
    bool HandleAcceleratorKey(char ascii, bool altHeld);

    /// Phase 14.14 — pushes live telemetry into the status-bar chips.
    /// Host calls once per frame; passing zeros hides the
    /// corresponding value (e.g. DC / VRAM when unavailable).
    void UpdateTelemetry(float fps, uint32_t drawCalls,
                          float vramUsedMb, float vramTotalMb);

    /// Phase 15.4 — swaps the live layout mode at runtime. Niagara
    /// keeps the phase-14 three-column composition; SplitHorizontal
    /// turns the editor into a 50/50 viewport + editor-pane split
    /// with a draggable gutter.
    void SetLayoutMode(VfxEditorLayoutMode mode);

    [[nodiscard]] VfxEditorLayoutMode LayoutMode() const {
        return m_composition.LayoutConfig().layoutMode;
    }

    /// Phase 14.16 — opens the content-browser modal centred on the
    /// current screen. Populates with demo tiles on first open.
    void OpenContentBrowser();

    /// Phase 14.16 — closes the content-browser modal. Tile list is
    /// preserved so a subsequent `OpenContentBrowser` restores state.
    void CloseContentBrowser();

    [[nodiscard]] bool IsContentBrowserOpen() const {
        return m_contentBrowserOpen;
    }

    /// Callbacks wired by the host.
    std::function<void(uint32_t, std::string)> onContentBrowserSelect;
    std::function<void(uint32_t, std::string)> onContentBrowserConfirm;

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

    /// Returns the active emitter slot — updated by clicks on the
    /// System Overview + preset-cycle keystrokes. Used by the frame
    /// renderer to bind `BuildUI` to the correct `ParticleEmitter*`.
    [[nodiscard]] int ActiveEmitterIdx() const noexcept {
        return m_activeEmitterIdx;
    }
    void SetActiveEmitterIdx(int idx) noexcept { m_activeEmitterIdx = idx; }

    /// Host-provided pointer to the active emitter. When set, the
    /// frame renderer uses this instead of indexing into
    /// `VFXSystem::GetActiveEmitters()` (which interleaves standalone
    /// + compound children and breaks slot-index alignment).
    [[nodiscard]] ParticleEmitter* ActiveEmitterOverride() const noexcept {
        return m_activeEmitterOverride;
    }
    void SetActiveEmitterOverride(ParticleEmitter* em) noexcept {
        m_activeEmitterOverride = em;
    }

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

    /// Phase 12.7.0 — fires once per click on an enabled, non-separator
    /// menu item in the top bar. Host translates the command to an
    /// action (toggling Niagara mode, invoking undo, etc.).
    std::function<void(VfxMenuCommand)> onMenuCommand;

    /// Phase 12.7.0 — fires when the `+` tab at the end of the emitter
    /// bar is clicked. Host creates a new emitter in the bound system.
    std::function<void()> onAddEmitterRequested;

    // Phase 15.12 — multi-emitter callbacks routed through the System
    // Overview widget. Fires with the emitter slot index (matching
    // `SetEmitterNames` order). Host keeps the ECS list in sync.
    std::function<void(size_t idx, bool enabled)> onEmitterEnableToggled;
    std::function<void(size_t src, size_t dst)>   onEmitterMoved;
    std::function<void(size_t idx)>               onEmitterDuplicated;
    std::function<void(size_t idx)>               onEmitterDeleted;

    // ── Spline Target entity picker (phase 16.3b) ────────────────────
    //
    // Host wires these before the first `BuildUI` call. The overlay
    // rebuilds the "Spline Target" row every frame from the data below.
    //
    // `splineTargetCurrent` — the entity currently set on the active
    //   emitter's `VfxEmitterComponent::splineTargetEntity`. Host
    //   updates this pointer each frame from the ECS query result. If
    //   the pointer is null the row uses `Entity::Invalid()`.
    //
    // `splineTargetCandidates` — a pre-built list of
    //   {entity, display-name} pairs filtered from the scene's ECS
    //   world (entities that own `SplineComponent`). Host repopulates
    //   it each frame so newly added/removed splines are reflected.
    //
    // `onSplineTargetChanged` — fires when the author selects a new
    //   entity or clicks "Clear". Host writes the received entity back
    //   to `VfxEmitterComponent::splineTargetEntity`.
    //
    // The row is only rendered when the active emitter has at least one
    // spline module (`SpawnAlongSplineModule`, `ForceTowardSplineModule`,
    // `FollowSplineModule`) on its module stack. When no spline module
    // is present the row is silently omitted so non-spline emitters are
    // not cluttered with a picker that has no effect.
    using SplineEntityCandidate =
        VfxEditorPaneWidget::InlineRow::EntityCandidate;
    const Entity*                             splineTargetCurrent    = nullptr;
    std::vector<SplineEntityCandidate>        splineTargetCandidates;
    std::function<void(Entity)>               onSplineTargetChanged;

    /// Returns the currently-edited emitter pointer (set by last BuildUI call).
    [[nodiscard]] ParticleEmitter* GetCurrentEmitter() const { return m_lastEmitter; }

    /// Forces rebuild of the module list on next BuildUI (after preset switch).
    void ForceRebuildModules() { m_lastEmitter = nullptr; }

    // ── Niagara mode (phase 11.14.0) ────────────────────────────────────
    //
    // When enabled, `BuildUI` renders the Niagara-parity composition
    // (`VfxEditorComposition` + the stack panel / parameter inspector /
    // curves editor / timeline scrubber) in place of the legacy slider
    // tree. The two paths share the same `TextRenderer` + `BitmapFont`
    // — only the batched geometry differs. Defaults to disabled so
    // existing callers are unaffected.

    /// Returns true when the overlay is in Niagara mode.
    [[nodiscard]] bool IsNiagaraMode() const noexcept { return m_niagaraMode; }

    /// Enables (`true`) or disables (`false`) Niagara mode.
    void SetNiagaraMode(bool enabled) noexcept { m_niagaraMode = enabled; }

    /// Flips Niagara mode and returns the new state.
    bool ToggleNiagaraMode() noexcept {
        m_niagaraMode = !m_niagaraMode;
        return m_niagaraMode;
    }

    /// Returns the editor composition that owns the phase-11 widget
    /// bindings. Exposed so hosts can register extra callbacks (e.g.
    /// thumbnail queue wiring) without reaching into the overlay's
    /// private state.
    [[nodiscard]] VfxEditorComposition& Composition() noexcept {
        return m_composition;
    }
    [[nodiscard]] const VfxEditorComposition& Composition() const noexcept {
        return m_composition;
    }

    /// Idempotent first-frame hook that binds every owned Niagara widget
    /// member to `m_composition`. Normally called implicitly by
    /// `BuildNiagaraUI`; exposed here so headless tests can verify the
    /// composition graph without standing up a GPU.
    void EnsureCompositionBoundForTest() { EnsureCompositionBound(); }

    /// Read-only access to the owned Niagara widget members so tests can
    /// verify binding identity without reaching into private state.
    [[nodiscard]] VfxStackPanelWidget&         StackPanelWidget()   noexcept { return m_stackPanel; }
    [[nodiscard]] VfxParameterInspectorWidget& ParameterInspector() noexcept { return m_paramInspector; }
    [[nodiscard]] VfxCurvesEditorWidget&       CurvesEditor()       noexcept { return m_curvesEditor; }
    [[nodiscard]] VfxTimelineScrubberWidget&   TimelineScrubber()   noexcept { return m_timelineScrubber; }
    [[nodiscard]] VfxModulePickerDialog&       ModulePickerDialog() noexcept { return m_modulePicker; }
    [[nodiscard]] VfxUndoStack&                UndoStack()          noexcept { return m_undoStack; }
    [[nodiscard]] VfxEditorKeybindingHandler&  KeybindingHandler()  noexcept { return m_keybindings; }
    [[nodiscard]] VfxThumbnailDrainer&         ThumbnailDrainer()   noexcept { return m_thumbnailDrainer; }
    [[nodiscard]] VfxContextMenuWidget&        ContextMenu()        noexcept { return m_contextMenu; }
    [[nodiscard]] VfxSystemOverviewWidget&     SystemOverview()     noexcept { return m_systemOverview; }

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
        enum Type : uint8_t { Float, Uint, Bool, Enum, Int, String } type = Float;
        float*       fPtr      = nullptr;
        uint32_t*    uPtr      = nullptr;
        int32_t*     iPtr      = nullptr;
        std::string* sPtr      = nullptr;
        float        min       = 0.0f;
        float        max       = 1.0f;
        const char*  hint      = "";
        const char* const* enumNames = nullptr;
        uint32_t     enumCount = 0;
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

public:
    /// Returns the MSDF font bound for crisp large/small text
    /// rendering, or nullptr when the MSDF atlas failed to load (in
    /// which case callers should fall back to the bitmap font).
    [[nodiscard]] const MsdfFont* GetMsdfFont() const {
        return m_msdfFont.IsLoaded() ? &m_msdfFont : nullptr;
    }

    /// Returns the canonical editor bitmap font for ASCII / native-
    /// size text rendering.
    [[nodiscard]] const BitmapFont& GetFont() const { return m_font; }

private:
    // ── GPU resources ────────────────────────────────────────────────────
    TextRenderer m_textRenderer;
    BitmapFont   m_font;
    MsdfFont     m_msdfFont;  ///< Phase 13.4 — crisp scalable text atlas.
    VfxTabStripWidget m_emitterTabStrip;  ///< Phase 14.2 — UE5 tab strip.
    VfxToolbarWidget  m_toolbar;          ///< Phase 14.10 — UE5 toolbar strip.
    VfxStatusBarWidget m_statusBar;       ///< Phase 14.10 — UE5 status bar.
    bool              m_toolbarInitialized = false;

    // Phase 14.11 — one scrollbar per scrollable panel.
    VfxScrollBar m_stackScrollbar;
    VfxScrollBar m_paramsScrollbar;
    VfxScrollBar m_pickerScrollbar;
    VfxScrollBar m_overviewScrollbar;

    // Phase 14.15 — emitter tree widget in the right panel.
    VfxTreeWidget m_emitterTree;
    bool          m_emitterRootExpanded = true;

    // Phase 14.16 — content-browser modal.
    VfxContentBrowserWidget m_contentBrowser;
    bool                    m_contentBrowserOpen = false;
    bool                    m_contentBrowserPopulated = false;
    VfxStackRect            m_contentBrowserBounds{};

    // Phase 15.4 — split layout widgets.
    VfxSplitterWidget    m_splitter;
    VfxEditorPaneWidget  m_editorPane;
    bool                 m_editorPaneBound = false;
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
    // Phase 12.10.0 — right-click state for context menu wiring on the
    // parameter inspector + curves editor (which have no overflow menu
    // button of their own).
    bool  m_mouseRightDown        = false;
    bool  m_mouseRightJustPressed = false;
    bool  m_wantsInput = false;

    // Phase 15.7 — parameter-inspector drag state. A click on an
    // `EditorFloat` / `EditorVec3` / `EditorColor` starts a horizontal
    // scrub drag. Each px of horizontal motion steps the value by
    // `kParamDragSensitivity` × `m_paramDragScale`. The scope/name pair
    // plus component index identify the target parameter.
    bool        m_paramDragActive         = false;
    int         m_paramDragScopeInt       = 0;   ///< VfxAttributeScope as int.
    std::string m_paramDragName;
    uint32_t    m_paramDragComponent      = 0;   ///< 0..3
    float       m_paramDragMouseXAtStart  = 0.0f;
    float       m_paramDragValueAtStart   = 0.0f;
    float       m_paramDragScale          = 0.01f;

    // Phase 15.8 — inline emitter-attr drag state. Drags write directly
    // through a `float*` pointing into the active `ParticleEmitter`
    // struct (wind, drag, gravity, etc.). Clamped to the row's
    // `[min, max]` range every frame.
    bool   m_inlineDragActive      = false;
    float* m_inlineDragField       = nullptr;
    float  m_inlineDragMouseXStart = 0.0f;
    float  m_inlineDragValueStart  = 0.0f;
    float  m_inlineDragScale       = 0.01f;
    float  m_inlineDragMin         = 0.0f;
    float  m_inlineDragMax         = 1.0f;

    // Phase 15.13 — separate drag state for uint32 scalars. Intermediate
    // accumulator is stored as float so sub-pixel drag deltas don't
    // truncate to zero — only the final write casts back to uint32.
    bool      m_inlineUDragActive      = false;
    uint32_t* m_inlineUDragField       = nullptr;
    float     m_inlineUDragMouseXStart = 0.0f;
    float     m_inlineUDragAccum       = 0.0f;
    float     m_inlineUDragScale       = 0.05f;
    uint32_t  m_inlineUDragMin         = 0u;
    uint32_t  m_inlineUDragMax         = 0xFFFFFFFFu;

    // Phase 15.10 — Color-over-life gradient widget state. Persists
    // across frames since the pane rebuilds its row list every frame.
    // 0 = start (left handle), 1 = end (right handle).
    uint32_t m_colorStopSelected  = 0u;
    /// Preview-only for now — 0 = smooth lerp (shader default), 1 =
    /// hard step at 0.5. A follow-up can forward this to the shader
    /// once the CS supports per-emitter transition modes.
    uint32_t m_colorStopHardMode  = 0u;

    // Phase 15.11 — color picker popup state. Opens when the user
    // clicks a ColorSwatch row; writes through to the four pointers
    // captured here so preset / slider / HEX edits live-update the
    // emitter struct without extra plumbing.
    struct ColorPickerState {
        bool     isOpen = false;
        float*   rgba[4] = {nullptr, nullptr, nullptr, nullptr};
        float    anchorX = 0.0f;
        float    anchorY = 0.0f;
        /// Editable buffer for the HEX input. Kept in sync with `rgba`
        /// except while the user is actively typing — that window is
        /// gated by `hexEditing` so we don't fight their keystrokes.
        std::string hexBuffer;
        bool     hexEditing = false;

        // HSV working copy. Hue is decoupled from RGB so dragging on
        // the SV quadrant preserves the current hue even when RGB
        // degenerates to gray (sat=0). Refreshed from the bound rgba
        // pointers on open + when the user types HEX or picks a preset.
        float    hsv[3] = {0.0f, 1.0f, 1.0f};

        // Active drag region inside the popup.
        enum : uint8_t { DragNone = 0, DragSV = 1, DragHue = 2, DragAlpha = 3 };
        uint8_t  dragKind = DragNone;
    } m_colorPicker;

    void OpenColorPicker(float anchorX, float anchorY, float* r, float* g,
                          float* b, float* a);
    void CloseColorPicker();
    void RenderColorPicker();
    void HandleColorPickerInput();
    float m_scrollDelta = 0.0f;

    // ── Menu bar state ─────────��─────────────────────────────────────────
    // Phase 12.7.0 — grown to 5 (File / Edit / View / Presets / Help). The
    // per-menu content lives in `vfx_editor_menu.h` via
    // `VfxMenuBarDefinition()`.
    static constexpr int kMenuCount = 5;
    int m_openMenu = -1;

    // Phase 12.7.0 — horizontal scroll offset for the emitter tab bar
    // when tabs overflow the available width.
    float m_emitterTabScrollOffset = 0.0f;

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
    ParticleEmitter* m_activeEmitterOverride = nullptr;
    int  m_selectedEmitterIdx = -1;

    // ── Theme colors (phase 12.1.0) ──────────────────────────────────────
    // Palette tokens are sourced from `VfxTheme()` — declared at the .cpp's
    // anonymous namespace as `static const Vec4&` references so every
    // existing call site (`kColorBarBg`, `kColorPanelBg`, etc.) keeps
    // reading a Vec4 without any value-churn. See vfx_editor_theme.h.

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

    /// Phase 12.10.0 — forwards a keystroke to the bound keybindings
    /// handler. Returns true when the handler consumed the event. The
    /// demo forwards every key-down event through this entry point so
    /// Ctrl+Z / Ctrl+S / F2 / Tab / Esc fire the correct panel action.
    bool InjectKey(VfxEditorKeybindingHandler::Key key,
                    bool ctrlHeld, bool shiftHeld) noexcept {
        EnsureCompositionBound();
        // Context menu wins when open (Escape dismisses).
        if (m_contextMenu.IsOpen()) {
            if (m_contextMenu.ProcessKey(key)) return true;
        }
        return m_keybindings.ProcessKeyEvent(key, ctrlHeld, shiftHeld);
    }

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

    /// Renders the Niagara-parity stack panel (phase 11.2) when the bound
    /// emitter's `VfxStack` is non-empty. Routes the left panel area to
    /// `m_stackPanel` instead of the legacy flat module tree.
    void BuildStackPanel(ParticleEmitter* emitter);
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

    // ── Stack panel widget (phase 11.2) ──────────────────────────────────
    /// Niagara-parity stack editor. Active when `emitter->stack.IsEmpty()`
    /// is false; otherwise the legacy `BuildStageTree` path renders.
    VfxStackPanelWidget m_stackPanel{};

    // ── Module picker dialog (phase 11.3) ────────────────────────────────
    /// Niagara-style module picker anchored at the "+ Add Module" click.
    /// Opened by `m_stackPanel.onAddModuleRequested`; closed on confirm,
    /// Escape, or click-outside.
    VfxModulePickerDialog m_modulePicker{};

    /// Wires `m_stackPanel.onAddModuleRequested` → `m_modulePicker.Open`
    /// on first non-null emitter. Idempotent.
    bool m_stackPanelCallbacksBound = false;
    void EnsureStackPanelCallbacks();

    // ── Niagara mode members (phase 11.14.0) ────────────────────────────
    //
    // `m_stackPanel` (above) and `m_modulePicker` (above) are reused in
    // Niagara mode. The remaining widgets are owned exclusively by this
    // mode: parameter inspector, curves editor, timeline scrubber, undo
    // stack, keybinding handler, and thumbnail drainer. The composition
    // glues them together via its `Bind*` methods.

    bool m_niagaraMode = false;

    VfxParameterInspectorWidget m_paramInspector{};
    VfxCurvesEditorWidget       m_curvesEditor{};
    VfxTimelineScrubberWidget   m_timelineScrubber{};
    VfxUndoStack                m_undoStack{};
    VfxEditorKeybindingHandler  m_keybindings{};
    VfxThumbnailDrainer         m_thumbnailDrainer{};
    VfxEditorComposition        m_composition{};
    // Phase 12.8.0 — shared right-click / overflow-menu popup used by
    // stack panel, parameter inspector, and curves editor.
    VfxContextMenuWidget        m_contextMenu{};
    // Phase 12.9.0 — right-panel system overview. Backing view vector
    // is refreshed each frame inside `BuildNiagaraUI` from the bound
    // emitter names + runtime state.
    VfxSystemOverviewWidget     m_systemOverview{};
    std::vector<VfxOverviewEmitterView> m_overviewViews;

    /// Idempotent first-frame hook that wires `m_composition` to every
    /// Niagara widget member above.
    bool m_compositionBound = false;
    void EnsureCompositionBound();

    /// Renders the Niagara-parity editor into `m_textRenderer` for the
    /// current frame. Called by `BuildUI` when `m_niagaraMode` is true.
    void BuildNiagaraUI(ParticleEmitter* emitter);

    /// Phase 15.5 — split-layout input dispatch helpers. Route clicks
    /// and scroll wheel into the sub-widget that occupies the editor
    /// pane's body area for the currently active tab. Rendering of the
    /// widgets themselves stays in `VfxEditorPaneWidget::Render`.
    void DispatchStackPanelInput(const VfxStackRect& bounds,
                                  ParticleEmitter* emitter);
    void DispatchParamInspectorInput(const VfxStackRect& bounds);
    void DispatchTreeAndOverviewInput(const VfxStackRect& treeBounds,
                                        const VfxStackRect& overviewBounds,
                                        ParticleEmitter* emitter);
};

} // namespace uz
