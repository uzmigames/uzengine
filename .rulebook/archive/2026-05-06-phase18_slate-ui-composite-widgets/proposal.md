# Proposal: phase18_slate-ui-composite-widgets (Step 7)

## Why

Step 7 do plano-master: "Composite widgets — ScrollBox, EditableText,
ComboBox". Sem estes, o VFX editor não monta: Details panel é scroll,
nome de emitter é EditableText, dropdown de "Mesh / Sprite / Mesh
Renderer" é ComboBox. São widgets compostos que reusam HBox/VBox/Button
+ adicionam estado interno (scroll offset, caret, dropdown open).

## What Changes

```cpp
namespace uz::ui {

// ScrollBox — content maior que viewport com clip + roda do mouse.
class ScrollBox : public Widget {
public:
    void SetContent(SP<Widget> child);
    Attribute<float> ScrollY{0.0f};   // 0..maxScroll
    Attribute<bool>  ShowScrollbar{true};

    Vec2 MeasureImpl(Vec2 maxSize) override;
    void Arrange(Geometry g) override;
    void PaintImpl(std::vector<DrawElement>& list) override;
    EventReply OnPointerWheel(const PointerEvent& e) override;

private:
    SP<Widget> m_content;
    float      m_maxScroll{0.0f};
};

// EditableText — input field com caret + selection + IME hooks futuros.
class EditableText : public Widget {
public:
    Attribute<std::string> Text;
    Attribute<Color>       TextColor;
    std::function<void(const std::string&)> OnTextChanged;
    std::function<void(const std::string&)> OnCommitted; // Enter / blur

    Vec2 MeasureImpl(Vec2 maxSize) override;
    void PaintImpl(std::vector<DrawElement>& list) override;
    EventReply OnPointerDown(const PointerEvent& e) override;
    EventReply OnKeyDown(const KeyEvent& e) override;
    EventReply OnFocusReceived() override;
    EventReply OnFocusLost() override;

private:
    size_t m_caret{0};
    size_t m_selStart{0};
    size_t m_selEnd{0};
    bool   m_focused{false};
    float  m_caretBlinkTimer{0.0f};
};

// ComboBox<T> — dropdown selector.
template <typename T>
class ComboBox : public Widget {
public:
    void SetOptions(std::vector<T> opts, std::function<std::string(const T&)> labeler);
    Attribute<int>           SelectedIndex{0};
    std::function<void(int)> OnSelectionChanged;

    Vec2 MeasureImpl(Vec2 maxSize) override;
    void PaintImpl(std::vector<DrawElement>& list) override;
    EventReply OnPointerDown(const PointerEvent& e) override;

private:
    std::vector<T> m_options;
    std::function<std::string(const T&)> m_labeler;
    bool m_open{false};
    SP<Widget> m_popup;  // built lazily on open
};

} // namespace uz::ui
```

ScrollBox aplica ScissorPush no PaintImpl do conteúdo e desenha
scrollbar à direita (8px wide track + thumb proporcional). Wheel
event ajusta ScrollY com clamp.

EditableText renderiza Text com caret pipe `|` no índice m_caret
(blink 500ms via Tick). Seleção destaca background. Ctrl+A/C/V via
KeyEvent.modifiers.

ComboBox no closed state mostra label do option atual + chevron à
direita. No open state, monta popup overlay com lista de TextBlock
clickable (cada item = botão fakeado).

## Impact

- Affected specs: `slate-ui-composite-widgets` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_scrollbox.{h,cpp}`,
  `slate_editable_text.{h,cpp}`, `slate_combobox.h` (template, header-only).
- Breaking change: NO.
- User benefit: Details panel do VFX editor finalmente monta. Demo
  Step 7 mostra um form com 3 EditableText + 2 ComboBox + ScrollBox
  com 50 rows.

## Anti-padrões a evitar

```
NÃO FAÇA:
- ScrollBox sem ScissorPush — content vaza fora da viewport.
- EditableText sem focus — clicks fora não desfocam (caret persiste).
- ComboBox popup como child da árvore — fica clipped pelo parent.
- Caret blink via real-time clock global — usa m_caretBlinkTimer local.

FAÇA:
- ScrollBox: ScissorPush antes do content, ScissorPop depois.
- EditableText: focus capture via InputRouter (HandledFocusable).
- ComboBox popup como Overlay layer no root (TopMost) com z=999.
- Caret blink via Tick(dt) — fmod(timer, 1.0) > 0.5 → invisible.
```

## Dependencies

- `phase18_slate-ui-style-brush` arquivado.
