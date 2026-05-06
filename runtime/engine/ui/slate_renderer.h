/// UzEngine Slate UI — DrawElement queue + Submit bridge to TextRenderer.
///
/// Phase 18 / Step 1. The `SlateRenderer` is the only piece of the UI
/// stack that knows about the engine's `TextRenderer` / DPI scale; widget
/// paint code below it speaks only in `DrawElement`s and Slate units.
///
/// Pipeline:
///   1. Widgets emit `DrawElement`s via `PushElement` during their Paint.
///   2. The application calls `Submit(textRenderer, font, msdfFont,
///      dpiScale)` to translate the queue into TextRenderer batches
///      (multiplied by dpiScale once, here).
///   3. `Clear()` resets the queue for the next frame.
///
/// Scissor: `ScissorPush` / `ScissorPop` maintain a CPU-side stack inside
/// `Submit`. Elements emitted while the stack is non-empty are clipped
/// against the top rect — fully outside elements are skipped, partially
/// outside elements are still emitted (the SDF rect shaders handle their
/// own bounds clipping; the GPU scissor itself is wired in Step 6 once
/// the invalidation system needs precise clipping).
///
/// Threading (Decision 4): all methods on `SlateRenderer` MUST be called
/// from the render thread. The output of `Submit` is a series of writes
/// into the `TextRenderer` batches — those are also single-threaded.

#pragma once

#include "engine/ui/slate_core.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {
class TextRenderer;
class BitmapFont;
class MsdfFont;
} // namespace uz

namespace uz::ui {

// ─────────────────────────────────────────────────────────────────────────────
// DrawElement — the only currency between widgets and the renderer
// ─────────────────────────────────────────────────────────────────────────────

enum class DrawKind : std::uint8_t {
    Quad,         ///< Rectangle (rounded / bordered / shadowed via SDF).
    Line,         ///< 1px-thick line segment via degenerate rect.
    Text,         ///< Bitmap or MSDF text (chosen by msdf flag).
    Sprite,       ///< Icon from the VfxIconAtlas.
    ScissorPush,  ///< Push `rect` onto the scissor stack.
    ScissorPop,   ///< Pop the scissor stack.
};

/// Plain-data record describing one renderable element. POD layout — copyable
/// without alloc, comparable with `memcmp` for tests.
struct DrawElement {
    DrawKind kind{DrawKind::Quad};

    // Geometry (Slate units; multiplied by dpiScale in Submit).
    Rect  rect{};

    // Common color / effects.
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float cornerRadius{0.0f};
    float borderThickness{0.0f};
    Color borderColor{0.0f, 0.0f, 0.0f, 0.0f};
    Vec2  shadowOffset{0.0f, 0.0f};
    float shadowBlur{0.0f};
    Color shadowColor{0.0f, 0.0f, 0.0f, 0.0f};

    // Text-specific.
    std::string text;        ///< Used iff kind == Text.
    bool        msdf{false}; ///< Use MSDF font instead of bitmap.
    float       fontSize{12.0f}; ///< Slate units; only consulted by MSDF path.

    // Sprite-specific.
    VfxUiIcon iconId{VfxUiIcon::None};

    // Line-specific.
    Vec2 lineEnd{0.0f, 0.0f}; ///< Used iff kind == Line; rect.pos is the start.
    float lineThickness{1.0f};
};

// ─────────────────────────────────────────────────────────────────────────────
// SlateRenderer
// ─────────────────────────────────────────────────────────────────────────────

class SlateRenderer {
public:
    SlateRenderer();

    /// Begins a new frame's queue; clears any leftover state.
    void BeginFrame();

    /// Appends a draw element to the queue.
    void PushElement(const DrawElement& e);

    /// Returns the number of elements currently queued.
    [[nodiscard]] std::size_t ElementCount() const noexcept { return m_elements.size(); }

    /// Direct read-only access for tests / introspection.
    [[nodiscard]] const std::vector<DrawElement>& Elements() const noexcept { return m_elements; }

    /// Translates the queue into TextRenderer calls.
    ///
    /// `font` is required for bitmap-text elements. `msdfFont` may be
    /// `nullptr` if no MSDF text was emitted; otherwise it is used for
    /// every `kind == Text && msdf == true` element. `dpiScale` is the
    /// final multiplier applied to all positions and sizes (Decision 5).
    void Submit(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdfFont, float dpiScale);

    /// Drops all elements without invoking `TextRenderer`. Useful for
    /// frames that should be discarded (e.g. window minimised).
    void Clear() noexcept;

private:
    std::vector<DrawElement> m_elements;
    std::vector<Rect>        m_scissorScratch; ///< Reused across frames.
};

} // namespace uz::ui
