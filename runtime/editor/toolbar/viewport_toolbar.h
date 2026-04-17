/// ViewportToolbar — Runtime in-engine editor toolbar overlay.
///
/// Renders a fixed-height horizontal button strip at the top of the viewport.
/// Uses the same BitmapFont + TextRenderer system as StatsOverlay — no ImGui,
/// no external UI framework.
///
/// # Button Groups (left → right)
/// 1. **Transform tools**: [Q Select] [W Translate] [E Rotate] [R Scale]
/// 2. **View mode**: Compact label showing active mode + LEFT/RIGHT arrow click to cycle.
/// 3. **Show flags**: [Grid] [Fog] [AO] [Shadows] quick-toggle buttons.
/// 4. **Quality**: [Low] [Med] [High] [Ultra] buttons.
///
/// Each button is drawn as a rounded rect + text. The active/hovered button is
/// highlighted. Hit-testing maps mouse XY to button AABB. Clicks are forwarded
/// directly to EditorInputHandler / SceneRenderer — no IPC round-trip.
///
/// # Lifecycle
/// ```cpp
/// ViewportToolbar toolbar;
/// toolbar.Init(device, cmdList, upload, shaderPipeline, rtvFormat, w, h, bindless);
///
/// // Per frame:
/// toolbar.Update(input, inputHandler, sceneRenderer);
/// toolbar.Render(cmdList, cbRing);
///
/// toolbar.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All calls must be on the render/main thread.

#pragma once

#include "viewport_toolbar_state.h"
#include "bitmap_font.h"
#include "text_renderer.h"
#include "rhi_config.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class UploadContext;
class ShaderPipeline;
class InputSystem;
class EditorInputHandler;
class SceneRenderer;

// ─────────────────────────────────────────────────────────────────────────────
// ViewportToolbar
// ─────────────────────────────────────────────────────────────────────────────

/// In-engine editor toolbar — draws button strip, handles input.
class ViewportToolbar {
public:
    ViewportToolbar() = default;

    /// Initializes GPU resources.
    ///
    /// # Arguments
    /// * `device` — RHI device.
    /// * `cmdList` — Command list for font atlas upload (must be recording).
    /// * `upload` — Upload context for staging.
    /// * `shaderPipeline` — Shader compilation pipeline.
    /// * `rtvFormat` — Render target format matching the backbuffer.
    /// * `screenWidth`, `screenHeight` — Initial screen dimensions.
    /// * `bindless` — SM6.6 bindless mode flag (matches StatsOverlay).
    ///
    /// # Returns
    /// `true` on success.
    [[nodiscard]] bool Init(IRHIDevice* device, IRHICommandList* cmdList,
                            UploadContext& upload, ShaderPipeline& shaderPipeline,
                            RHIFormat rtvFormat,
                            uint32_t screenWidth, uint32_t screenHeight,
                            bool bindless = true);

    /// Releases all GPU resources.
    void Shutdown();

    /// Returns true when Init() completed successfully.
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    /// Processes input and updates button state.
    ///
    /// Must be called BEFORE Render() each frame.
    /// Reads EditorInputHandler + SceneRenderer to sync ViewportToolbarState.
    /// Performs hit-testing and fires button actions on LMB click.
    ///
    /// # Arguments
    /// * `input` — Active input system.
    /// * `handler` — Editor input handler that owns transform mode / gizmo space.
    /// * `renderer` — Scene renderer that owns view mode / show flags / quality.
    void Update(InputSystem* input, EditorInputHandler* handler,
                SceneRenderer* renderer);

    /// Draws the toolbar strip over the current render target.
    ///
    /// Must be called after a render pass with the backbuffer set as RTV.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list with backbuffer RTV bound.
    /// * `cbRing` — Constant buffer ring allocator.
    /// * `frameIndex` — Current frame index for triple-buffered vertex selection.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, uint32_t frameIndex);

    /// Updates screen dimensions (call on resize).
    void SetScreenSize(uint32_t width, uint32_t height);

    /// Sets the active toolbar profile.
    void SetProfile(ToolbarProfile profile) { m_state.profile = profile; }

    /// Returns the current toolbar state snapshot.
    [[nodiscard]] const ViewportToolbarState& GetState() const { return m_state; }

    /// Toggles toolbar visibility.
    void SetVisible(bool visible) { m_visible = visible; }

    /// Returns whether the toolbar is visible.
    [[nodiscard]] bool IsVisible() const { return m_visible; }

    /// Rebuilds the button list from the current profile and screen size.
    ///
    /// Called automatically by SetProfile() and SetScreenSize(). Exposed for
    /// unit testing (does not require GPU resources).
    void RebuildLayout();

    /// Returns the number of buttons in the current layout.
    ///
    /// Used by unit tests to verify that different profiles produce different
    /// numbers of buttons.
    [[nodiscard]] std::size_t GetButtonCount() const { return m_buttons.size(); }

private:
    // ── Button layout helpers ─────────────────────────────────────────────

    /// A single toolbar button.
    struct Button {
        float x, y, w, h;     ///< Screen-space bounding rect (pixels, top-left origin).
        std::string label;     ///< Display text.
        bool  active = false;  ///< Highlighted (pressed / current selection).
        bool  enabled = true;  ///< False = grayed out, not clickable.
        int   groupId = 0;     ///< Logical group for separator drawing.
        int   actionId = 0;    ///< Opaque ID passed to ExecuteAction().
    };

    /// Executes the action for the given button action ID.
    void ExecuteAction(int actionId, EditorInputHandler* handler,
                       SceneRenderer* renderer);

    /// Syncs button active-states from toolbar state.
    void SyncActiveStates();

    // ── GPU / font resources ──────────────────────────────────────────────

    IRHIDevice* m_device = nullptr;
    BitmapFont  m_font;
    TextRenderer m_textRenderer;
    bool        m_initialized  = false;
    bool        m_visible      = true;

    uint32_t m_screenWidth  = 1280;
    uint32_t m_screenHeight = 720;

    // ── Layout ────────────────────────────────────────────────────────────

    static constexpr float kBarHeight = 30.f;   ///< Toolbar strip height (pixels).
    static constexpr float kBtnPad    = 8.f;    ///< Horizontal padding inside button.
    static constexpr float kFontSize  = 13.f;   ///< Button label font size.
    static constexpr float kBtnH      = 24.f;   ///< Button height.
    static constexpr float kBtnTop    = 3.f;    ///< Y offset of buttons from strip top.
    static constexpr float kSepGap    = 10.f;   ///< Gap between button groups.

    std::vector<Button> m_buttons;  ///< All visible buttons this frame.
    int m_hoveredIdx = -1;          ///< Index into m_buttons of hovered button (-1=none).

    // ── State ─────────────────────────────────────────────────────────────

    ViewportToolbarState m_state;

    // ── Colors ────────────────────────────────────────────────────────────

    static constexpr Vec4 kColorBar      = {0.12f, 0.12f, 0.12f, 0.88f}; ///< Background.
    static constexpr Vec4 kColorBtn      = {0.20f, 0.20f, 0.20f, 1.00f}; ///< Normal button.
    static constexpr Vec4 kColorBtnHover = {0.30f, 0.30f, 0.30f, 1.00f}; ///< Hovered.
    static constexpr Vec4 kColorBtnActive= {0.28f, 0.48f, 0.74f, 1.00f}; ///< Active/pressed.
    static constexpr Vec4 kColorText     = {0.92f, 0.92f, 0.92f, 1.00f}; ///< Normal text.
    static constexpr Vec4 kColorTextDim  = {0.50f, 0.50f, 0.50f, 1.00f}; ///< Grayed text.
    static constexpr Vec4 kColorSep      = {0.35f, 0.35f, 0.35f, 0.80f}; ///< Group separator.

    // ── Action IDs ────────────────────────────────────────────────────────
    // Ranges: 100–199 = transform, 200–299 = view mode, 300–399 = show flags,
    //         400–499 = quality.

    static constexpr int kActSelect     = 100;
    static constexpr int kActTranslate  = 101;
    static constexpr int kActRotate     = 102;
    static constexpr int kActScale      = 103;

    static constexpr int kActViewBase   = 200; ///< DebugViewMode value + kActViewBase.

    static constexpr int kActShowGrid       = 300;
    static constexpr int kActShowFog        = 301;
    static constexpr int kActShowAO         = 302;
    static constexpr int kActShowShadows    = 303;
    static constexpr int kActShowBloom      = 304;
    static constexpr int kActShowSSR        = 305;

    static constexpr int kActQualLow    = 400;
    static constexpr int kActQualMed    = 401;
    static constexpr int kActQualHigh   = 402;
    static constexpr int kActQualUltra  = 403;
};

} // namespace uz
