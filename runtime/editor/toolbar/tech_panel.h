/// TechPanel — in-engine overlay that switches the renderer's lighting
/// technologies on and off at runtime, one CVar per row.
///
/// Built on the same BitmapFont + TextRenderer stack as ViewportToolbar and
/// StatsOverlay — no ImGui, no external UI framework. Unlike the toolbar it
/// does not need editor mode: it is meant for looking at a scene (Sponza) and
/// finding which technology is responsible for what the light does.
///
/// Every row maps to exactly one CVar that a render pass reads each frame
/// (`ApplyCVars()` and `SyncShowFlagsFromCVars()` both run per frame), so a
/// click takes effect on the next frame and the row always shows the CVar's
/// current value — including values changed elsewhere (console, Lua, CLI).
///
/// Visibility: `r.TechPanel.Show` (registered by SceneRenderer) or the F3 key.
///
/// # Lifecycle
/// ```cpp
/// TechPanel panel;
/// panel.Init(device, cmdList, upload, shaderPipeline, rtvFormat, w, h, bindless);
/// // Per frame, before the camera controller consumes input:
/// panel.Update(input, sceneRenderer);
/// // Inside the overlay render pass, backbuffer bound:
/// panel.Render(cmdList, cbRing, frameIndex);
/// panel.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All calls on the render/main thread.

#pragma once

#include "bitmap_font.h"
#include "text_renderer.h"
#include "rhi_config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class UploadContext;
class ShaderPipeline;
class InputSystem;
class SceneRenderer;

class TechPanel {
public:
    TechPanel() = default;

    [[nodiscard]] bool Init(IRHIDevice* device, IRHICommandList* cmdList,
                            UploadContext& upload, ShaderPipeline& shaderPipeline,
                            RHIFormat rtvFormat,
                            uint32_t screenWidth, uint32_t screenHeight,
                            bool bindless = true);

    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    /// Reads the F3 toggle and `r.TechPanel.Show`, hit-tests the mouse and
    /// applies clicks. Must run every frame, visible or not, so the toggle key
    /// works; must run BEFORE Render() and before the camera controller.
    /// Returns true when the mouse is over the panel (the caller may use it to
    /// keep other UI from reacting to the same click).
    bool Update(InputSystem* input, SceneRenderer* renderer);

    /// Draws the panel. Requires a render pass with the backbuffer bound.
    void Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, uint32_t frameIndex);

    void SetScreenSize(uint32_t width, uint32_t height);

    [[nodiscard]] bool IsVisible() const { return m_visible; }

    /// Number of clickable rows — for tests and for the layout.
    [[nodiscard]] std::size_t GetRowCount() const { return m_rows.size(); }

private:
    enum class RowKind : uint8_t {
        Header,     ///< Group title, not clickable.
        Toggle,     ///< Bool CVar: click flips 0 <-> 1.
        Scalar,     ///< Float CVar: click flips 0 <-> the value it had (or 1).
        ViewMode,   ///< Debug view mode selector: click sets `viewMode`.
    };

    struct Row {
        RowKind      kind = RowKind::Header;
        std::string  label;       ///< Text shown to the user.
        std::string  cvar;        ///< CVar name (Toggle/Scalar) — also shown, dimmed.
        std::string  needs;       ///< CVar that must be on for this row to act (shown when off).
        int          viewMode = 0;///< DebugViewMode value (ViewMode rows).
        float        saved = 1.f; ///< Scalar rows: value to restore on re-enable.
        float        x = 0.f, y = 0.f, w = 0.f, h = 0.f; ///< Hit rect, screen pixels.
    };

    void BuildRows();
    void LayoutRows();
    void ApplyClick(Row& row, SceneRenderer* renderer);
    [[nodiscard]] static float ReadCVar(const std::string& name, float fallback);
    [[nodiscard]] std::string StateText(const Row& row, SceneRenderer* renderer) const;

    IRHIDevice*  m_device = nullptr;
    BitmapFont   m_font;
    TextRenderer m_textRenderer;
    bool         m_initialized = false;
    bool         m_visible     = false;

    uint32_t m_screenWidth  = 1280;
    uint32_t m_screenHeight = 720;

    std::vector<Row> m_rows;
    int   m_hoveredRow = -1;
    int   m_currentViewMode = 0;   ///< Renderer's DebugViewMode as of the last Update().
    float m_panelX = 0.f, m_panelY = 0.f, m_panelH = 0.f; ///< Panel rect (width is kPanelW).

    // ── Layout ────────────────────────────────────────────────────────────
    static constexpr float kFontSize   = 13.f;
    static constexpr float kRowH       = 18.f;
    static constexpr float kHeaderH    = 22.f;
    static constexpr float kPanelW     = 500.f;
    static constexpr float kPad        = 8.f;
    static constexpr float kTopMargin  = 40.f;  ///< Below the toolbar strip when it is shown.
    static constexpr float kRightMargin= 12.f;
    static constexpr float kCvarColumn = 290.f; ///< x offset of the dimmed CVar name inside a row (longest label ~35 chars at ~7.8 px).

    // ── Colours ───────────────────────────────────────────────────────────
    static constexpr Vec4 kColorPanel    = {0.08f, 0.08f, 0.09f, 0.90f};
    static constexpr Vec4 kColorHeader   = {0.85f, 0.75f, 0.45f, 1.00f};
    static constexpr Vec4 kColorRowHover = {0.24f, 0.24f, 0.26f, 1.00f};
    static constexpr Vec4 kColorOn       = {0.55f, 0.95f, 0.55f, 1.00f};
    static constexpr Vec4 kColorOff      = {0.80f, 0.45f, 0.45f, 1.00f};
    static constexpr Vec4 kColorText     = {0.92f, 0.92f, 0.92f, 1.00f};
    static constexpr Vec4 kColorDim      = {0.55f, 0.55f, 0.58f, 1.00f};
    static constexpr Vec4 kColorWarn     = {0.95f, 0.70f, 0.30f, 1.00f};
    static constexpr Vec4 kColorSelected = {0.28f, 0.48f, 0.74f, 1.00f};
};

} // namespace uz
