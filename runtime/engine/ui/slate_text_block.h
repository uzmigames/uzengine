/// UzEngine Slate UI — TextBlock widget.
///
/// Phase 18 / Step 2. The simplest possible widget: a leaf that emits a
/// single `DrawKind::Text` element each frame. Drives the entire
/// Construct → Tick → Measure → Arrange → Paint cycle.
///
/// Properties (all `Attribute<T>` — accept literal or lambda):
///   - `Text`      : the string to render. Default empty.
///   - `TextColor` : RGBA. Default opaque white.
///   - `FontSize`  : Slate units. Default 11.5 (matches `body` from styles.css).
///
/// Visual cost: one `DrawKind::Text` element. Measurement is a string-
/// length × font-size approximation (the bitmap font's per-glyph width
/// table is consulted in Step 3 once HBox needs accurate widths for
/// horizontal layout).
///
/// Construction: prefer `std::make_shared<TextBlock>()`. `Construct()`
/// is a no-op for this widget; the literal `Text` defaults work without
/// it. The declarative DSL in Step 8 (`New<TextBlock>().Text("x")`)
/// produces the same shared_ptr.

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_widget.h"

#include <string>

namespace uz::ui {

class TextBlock : public Widget {
public:
    Attribute<std::string> Text;
    Attribute<Color>       TextColor{Color{1.0f, 1.0f, 1.0f, 1.0f}};
    Attribute<float>       FontSize{11.5f};

    [[nodiscard]] Vec2 Measure(Vec2 maxSize) override;
    void Paint(SlateRenderer& renderer) override;

    /// Approximate glyph advance used by Measure when no font metric is
    /// available. Bitmap fonts in the engine bake at ~6 Slate units per
    /// glyph for the default body size; the constant is exposed so
    /// container widgets in tests can predict layout deterministically.
    static constexpr float kApproxGlyphAdvanceRatio = 0.55f;
};

} // namespace uz::ui
