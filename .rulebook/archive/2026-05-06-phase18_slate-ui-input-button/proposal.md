# Proposal: phase18_slate-ui-input-button (Step 4)

## Why

Step 4 do plano-master: "Sistema de input + Button funcional — valida
event routing". Sem input, a UI é apenas decorativa. Sistema de routing
(bubbling/tunneling) precisa ser certo desde o início — adicionar
hover/focus/capture depois é dor.

## What Changes

Camada 4 (early subset — input routing + Button):

```cpp
namespace uz::ui {

struct PointerEvent {
    Vec2  position;       // Slate units, screen-relative
    Vec2  delta;
    enum class Button : uint8_t { None, Left, Right, Middle };
    Button button{Button::None};
    bool   pressed{false};
    bool   released{false};
    int    modifiers{0};  // bitfield: Shift=1, Ctrl=2, Alt=4
};

struct KeyEvent {
    int key;              // VK_ code or scancode
    bool pressed{false};
    int  modifiers{0};
};

enum class EventReply : uint8_t {
    Unhandled,            // Bubble to parent
    Handled,              // Stop bubble
    HandledCapture,       // Stop bubble + capture pointer
};

class Widget {
    // ... existing methods ...
    virtual EventReply OnPointerMove(const PointerEvent&) { return EventReply::Unhandled; }
    virtual EventReply OnPointerDown(const PointerEvent&) { return EventReply::Unhandled; }
    virtual EventReply OnPointerUp(const PointerEvent&) { return EventReply::Unhandled; }
    virtual EventReply OnKey(const KeyEvent&) { return EventReply::Unhandled; }
    virtual void OnHoverEnter() {}
    virtual void OnHoverLeave() {}
};

class InputRouter {
public:
    void SetRoot(SP<Widget> root) { m_root = root; }
    void DispatchPointer(const PointerEvent&);
    void DispatchKey(const KeyEvent&);
private:
    SP<Widget> m_root;
    WP<Widget> m_hoverTarget;     // null if no hover
    WP<Widget> m_pointerCapture;  // captures all PointerMove/Up if set
    WP<Widget> m_focused;         // receives keyboard
};

class Button : public Widget {
public:
    Attribute<Color> NormalColor{Vec4{0.2f, 0.2f, 0.2f, 1.0f}};
    Attribute<Color> HoverColor {Vec4{0.3f, 0.3f, 0.3f, 1.0f}};
    Attribute<Color> PressedColor{Vec4{0.4f, 0.6f, 0.9f, 1.0f}};
    Attribute<float> CornerRadius{4.0f};
    SP<Widget>       Content;        // child (TextBlock, etc.)
    std::function<void()> OnClick;

    Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry) override;
    void Paint(SlateRenderer&) override;
    EventReply OnPointerDown(const PointerEvent&) override;
    EventReply OnPointerUp  (const PointerEvent&) override;
    void OnHoverEnter() override { m_hovered = true; }
    void OnHoverLeave() override { m_hovered = false; m_pressed = false; }

private:
    bool m_hovered{false};
    bool m_pressed{false};
};

} // namespace uz::ui
```

Routing rules:
- `DispatchPointer`: descend tree, find deepest widget whose geometry
  contains pointer. Send `OnPointerDown`. If reply is `HandledCapture`,
  store as `m_pointerCapture`.
- Subsequent `OnPointerMove` / `OnPointerUp` → captured widget if set,
  else hit-test deepest.
- Hover: each pointer move re-hit-tests; if target changed, fire
  `OnHoverLeave` on old + `OnHoverEnter` on new.

## Impact

- Affected specs: `slate-ui-input-button` (NEW).
- Affected code: NEW `runtime/engine/ui/slate_input.{h,cpp}`,
  `slate_button.{h,cpp}`. Modify `slate_widget.h` adding event vmethods.
- Breaking change: NÃO (vmethods novas com default `Unhandled`).
- User benefit: UI fica clicável. Demo Step 4 mostra Button que muda
  cor no hover/press e fires onClick logando "click N".

## Anti-padrões a evitar

```
NÃO FAÇA:
- Hit-test usando geometry NÃO arranged (sempre depois de Arrange).
- Captura de pointer sem release no PointerUp — leak de captura.
- Hover state sem reset quando widget some da árvore (stale weak_ptr OK).

FAÇA:
- HandledCapture só pra drag operations explícitas (Button NÃO captura).
- Hit-test profundo (hits child antes do parent).
- Bubble: parent recebe evento se child retornou Unhandled.
- Event reply é o ÚNICO mecanismo de stop-propagation.
```

## Dependencies

- `phase18_slate-ui-layout-boxes` arquivado (Container widgets existem).
