#include "engine/ui/slate_text_block.h"

#include "engine/ui/slate_renderer.h"

namespace uz::ui {

Vec2 TextBlock::Measure(Vec2 maxSize) {
    const std::string text = Text.Get();
    const float       size = FontSize.Get();

    // Approximate width: glyphs × (size × advanceRatio). Heights snap to
    // size + 2px leading. This is intentionally a low-fidelity estimate;
    // the Layer 3 layout boxes (HBox/VBox) work fine with it for ASCII
    // bodies, and Step 7 (composite widgets) will switch to real font
    // metrics when EditableText needs caret-precise widths.
    const float width = static_cast<float>(text.size()) * size * kApproxGlyphAdvanceRatio;
    const float height = size + 2.0f;

    Vec2 desired{width, height};
    if (maxSize.x > 0.0f && desired.x > maxSize.x) desired.x = maxSize.x;
    if (maxSize.y > 0.0f && desired.y > maxSize.y) desired.y = maxSize.y;
    return desired;
}

void TextBlock::Paint(SlateRenderer& renderer) {
    DrawElement e{};
    e.kind     = DrawKind::Text;
    e.rect     = Rect{m_geometry.absolutePos, m_geometry.size};
    e.color    = TextColor.Get();
    e.text     = Text.Get();
    e.fontSize = FontSize.Get();
    e.msdf     = false; // Step 2 uses bitmap path; MSDF opt-in in Step 6+.
    renderer.PushElement(e);
}

} // namespace uz::ui
