#include "engine/ui/slate_editable_text.h"

#include "engine/ui/slate_renderer.h"

#include <algorithm>
#include <cmath>

namespace uz::ui {

namespace {
constexpr float kGlyphAdvanceRatio = 0.55f;     // Matches TextBlock.
constexpr float kCaretBlinkPeriod = 1.0f;       // Seconds.
constexpr float kPaddingX = 6.0f;               // Slate units.
constexpr float kPaddingY = 4.0f;
} // anonymous namespace

EditableText::EditableText() {
    Text.BindOwner(this, InvalidationFlag::Paint);
    TextColor.BindOwner(this, InvalidationFlag::Paint);
    BackgroundColor.BindOwner(this, InvalidationFlag::Paint);
}

Vec2 EditableText::Measure(Vec2 maxSize) {
    const float size = FontSize.Get();
    const std::string& text = Text.Get();
    float w = std::max(60.0f, static_cast<float>(text.size()) * size * kGlyphAdvanceRatio + 2.0f * kPaddingX);
    float h = size + 2.0f * kPaddingY;
    if (maxSize.x > 0.0f && w > maxSize.x) w = maxSize.x;
    if (maxSize.y > 0.0f && h > maxSize.y) h = maxSize.y;
    return Vec2{w, h};
}

void EditableText::Arrange(Geometry geometry) {
    m_geometry = geometry;
}

void EditableText::Tick(float dt) {
    if (m_focused) {
        const float prev = m_blinkTimer;
        m_blinkTimer = std::fmod(m_blinkTimer + dt, kCaretBlinkPeriod);
        // Invalidate Paint when the blink crosses the half mark, so the
        // caret toggle redraws.
        const bool prevVisible = std::fmod(prev, kCaretBlinkPeriod) < kCaretBlinkPeriod * 0.5f;
        const bool nowVisible  = m_blinkTimer < kCaretBlinkPeriod * 0.5f;
        if (prevVisible != nowVisible) Invalidate(InvalidationFlag::Paint);
    }
}

void EditableText::Paint(SlateRenderer& renderer) {
    const Vec4 bgColor = BackgroundColor.Get();
    const Vec4 borderCol = BorderColor.Get();

    DrawElement bg{};
    bg.kind = DrawKind::Quad;
    bg.rect = Rect{m_geometry.absolutePos, m_geometry.size};
    bg.color = bgColor;
    bg.borderThickness = m_focused ? 1.0f : 0.5f;
    bg.borderColor = borderCol;
    bg.cornerRadius = CornerRadius.Get();
    renderer.PushElement(bg);

    const std::string& text = Text.Get();
    const float fontSize = FontSize.Get();
    const Vec2 textPos = Vec2{
        m_geometry.absolutePos.x + kPaddingX,
        m_geometry.absolutePos.y + kPaddingY};

    DrawElement t{};
    t.kind = DrawKind::Text;
    t.rect = Rect{textPos, Vec2{m_geometry.size.x - 2 * kPaddingX, fontSize}};
    t.color = TextColor.Get();
    t.text = text;
    t.fontSize = fontSize;
    renderer.PushElement(t);

    if (m_focused && m_blinkTimer < kCaretBlinkPeriod * 0.5f) {
        const float caretX = textPos.x +
            static_cast<float>(std::min(m_caret, text.size())) * fontSize * kGlyphAdvanceRatio;
        DrawElement caret{};
        caret.kind = DrawKind::Quad;
        caret.rect = Rect{Vec2{caretX, textPos.y}, Vec2{1.5f, fontSize}};
        caret.color = TextColor.Get();
        renderer.PushElement(caret);
    }
}

EventReply EditableText::OnPointerDown(const PointerEvent& e) {
    if (e.button != PointerButton::Left) return EventReply::Unhandled;
    Focus();
    // Approximate caret placement: chars-from-left-padding.
    const float relX = e.position.x - (m_geometry.absolutePos.x + kPaddingX);
    const float charW = FontSize.Get() * kGlyphAdvanceRatio;
    const std::string& text = Text.Get();
    if (charW > 0.0f) {
        std::size_t hit = static_cast<std::size_t>(std::max(0.0f, relX / charW + 0.5f));
        m_caret = std::min(hit, text.size());
    }
    return EventReply::Handled;
}

EventReply EditableText::OnKeyDown(const KeyEvent& e) {
    if (!m_focused) return EventReply::Unhandled;

    std::string text = Text.Get();
    bool changed = false;

    switch (e.keyCode) {
    case KeyBackspace:
        if (m_caret > 0 && !text.empty()) {
            text.erase(m_caret - 1, 1);
            --m_caret;
            changed = true;
        }
        break;
    case KeyDelete:
        if (m_caret < text.size()) {
            text.erase(m_caret, 1);
            changed = true;
        }
        break;
    case KeyLeft:
        if (m_caret > 0) --m_caret;
        Invalidate(InvalidationFlag::Paint);
        return EventReply::Handled;
    case KeyRight:
        if (m_caret < text.size()) ++m_caret;
        Invalidate(InvalidationFlag::Paint);
        return EventReply::Handled;
    case KeyHome:
        m_caret = 0;
        Invalidate(InvalidationFlag::Paint);
        return EventReply::Handled;
    case KeyEnd:
        m_caret = text.size();
        Invalidate(InvalidationFlag::Paint);
        return EventReply::Handled;
    case KeyEnter:
        CommitIfChanged();
        return EventReply::Handled;
    case KeyEscape:
        Blur();
        return EventReply::Handled;
    default:
        if (e.character != 0) {
            text.insert(m_caret, 1, static_cast<char>(e.character));
            ++m_caret;
            changed = true;
        }
        break;
    }

    if (changed) {
        SetTextInternal(std::move(text));
        return EventReply::Handled;
    }
    return EventReply::Unhandled;
}

void EditableText::Focus() {
    if (!m_focused) {
        m_focused = true;
        m_blinkTimer = 0.0f;
        Invalidate(InvalidationFlag::Paint);
    }
}

void EditableText::Blur() {
    if (m_focused) {
        m_focused = false;
        CommitIfChanged();
        Invalidate(InvalidationFlag::Paint);
    }
}

void EditableText::SetTextInternal(std::string newText) {
    Text = std::move(newText);
    if (OnTextChanged) OnTextChanged(Text.Get());
    Invalidate(InvalidationFlag::Paint);
}

void EditableText::CommitIfChanged() {
    const std::string& current = Text.Get();
    if (current != m_lastCommitted) {
        m_lastCommitted = current;
        if (OnCommitted) OnCommitted(current);
    }
}

} // namespace uz::ui
