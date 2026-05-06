/// UzEngine Slate UI — EditableText widget.
///
/// Phase 18 / Step 7. A single-line text input with caret, backspace,
/// and arrow-key navigation. Selection / clipboard / IME / multi-line
/// land later; this widget covers the 80 % of cases the VFX editor
/// needs (emitter rename, parameter typing).
///
/// Public API:
///   - `Text` (Attribute<std::string>)   — current value.
///   - `OnTextChanged(string)`           — fired on every edit.
///   - `OnCommitted(string)`             — fired on Enter or focus loss.
///   - `Focus()` / `Blur()`              — programmatic focus control.
///
/// Render output: a single `DrawKind::Quad` (background) + the text
/// element + a thin caret quad when focused (caret blinks via `Tick`).

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_widget.h"

#include <functional>
#include <string>

namespace uz::ui {

class EditableText : public Widget {
public:
    EditableText();

    Attribute<std::string> Text;
    Attribute<Color>       TextColor{Color{0.92f, 0.93f, 0.95f, 1.0f}};
    Attribute<Color>       BackgroundColor{Color{0.12f, 0.13f, 0.15f, 1.0f}};
    Attribute<Color>       BorderColor{Color{0.27f, 0.30f, 0.36f, 1.0f}};
    Attribute<float>       FontSize{11.5f};
    Attribute<float>       CornerRadius{3.0f};

    /// Fired on every keystroke that modifies the text.
    std::function<void(const std::string&)> OnTextChanged;
    /// Fired on Enter or focus-loss with the most recent committed value.
    std::function<void(const std::string&)> OnCommitted;

    Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry geometry) override;
    void Tick(float dt) override;
    void Paint(SlateRenderer& renderer) override;

    EventReply OnPointerDown(const PointerEvent& e) override;
    EventReply OnKeyDown(const KeyEvent& e) override;

    /// Programmatic focus control. The Step-9 FocusManager wraps these.
    void Focus();
    void Blur();
    [[nodiscard]] bool IsFocused() const noexcept { return m_focused; }
    [[nodiscard]] std::size_t Caret() const noexcept { return m_caret; }

    /// Step 9: opt into the tab cycle.
    [[nodiscard]] bool IsFocusable() const noexcept override { return true; }
    void OnFocusReceived() override { Focus(); }
    void OnFocusLost()     override { Blur(); }

    /// Key-code constants the host platform layer should produce. Keeps
    /// the widget free of OS-specific VK_* defines.
    static constexpr std::uint32_t KeyBackspace = 1;
    static constexpr std::uint32_t KeyDelete    = 2;
    static constexpr std::uint32_t KeyLeft      = 3;
    static constexpr std::uint32_t KeyRight     = 4;
    static constexpr std::uint32_t KeyHome      = 5;
    static constexpr std::uint32_t KeyEnd       = 6;
    static constexpr std::uint32_t KeyEnter     = 7;
    static constexpr std::uint32_t KeyEscape    = 8;

private:
    bool        m_focused{false};
    std::size_t m_caret{0};
    float       m_blinkTimer{0.0f};
    std::string m_lastCommitted;

    /// Sets a new Text value and fires OnTextChanged.
    void SetTextInternal(std::string newText);
    void CommitIfChanged();
};

} // namespace uz::ui
