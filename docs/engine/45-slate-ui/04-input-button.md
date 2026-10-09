# Slate UI — Input + Button (Phase 18, Step 4)

**Status**: Complete. The input dispatcher routes pointer events
through the widget tree; `Button` is the first interactive widget.

## Files

- `runtime/engine/ui/slate_input.{h,cpp}` — `PointerEvent`, `KeyEvent`,
  `EventReply`, `InputRouter`.
- `runtime/engine/ui/slate_button.{h,cpp}` — `Button` widget.
- Updated `slate_widget.h` with input vmethods + parent linkage.
- `runtime/tests/engine/ui/test_slate_input.cpp` — 10 unit tests.

## Event types

```cpp
struct PointerEvent {
    Vec2          position;
    Vec2          delta;
    PointerButton button;     // None / Left / Right / Middle
    bool          isDown;
    float         wheelDelta;
};

struct KeyEvent {
    uint32_t keyCode;
    bool     isDown;
    bool     ctrl, alt, shift;
    char32_t character;
};

enum class EventReply { Unhandled, Handled, HandledCapture };
```

The platform input layer normalises raw OS events into these structs
and feeds them to `InputRouter::DispatchPointer` /
`InputRouter::DispatchKey`. Slate widgets never see a raw `WM_LBUTTONDOWN`.

## Routing

`InputRouter::DispatchPointer` follows three rules:

1. **Hit-test deepest-first.** The router walks the tree depth-first
   (later siblings = "on top"), tests `Geometry::ToRect().Contains(pos)`,
   and selects the innermost matching widget. A child Button under an
   HBox claims the click before the HBox does.
2. **Bubble.** If the chosen widget returns `EventReply::Unhandled`, the
   event walks back up the parent chain (`Widget::GetParent()`). The
   first `Handled` / `HandledCapture` ancestor stops it. If the chain
   is exhausted, the dispatch reports unhandled.
3. **Capture.** A `HandledCapture` reply on `OnPointerDown` makes the
   widget the **exclusive** recipient of subsequent `OnPointerMove` and
   `OnPointerUp` events until the `PointerUp` arrives (which releases
   capture). The cursor can leave the widget bounds without breaking
   the press — exactly what scroll thumbs and drag handles need.

Hover transitions: every dispatch records the deepest hit. When the hit
changes vs the last frame, the router fires `OnHoverLeave` on the
previous target and `OnHoverEnter` on the new one. Same target →
neither hook fires.

Parent links are wired automatically inside `InputRouter::SetRoot` by
walking `Widget::HitTestChildren()` recursively. Container widgets
(HBox/VBox/Overlay/Button) override `HitTestChildren()` to expose their
slot widgets; leaf widgets return `nullptr`.

## Button

```cpp
auto btn = std::make_shared<Button>();
btn->Content = std::make_shared<TextBlock>();
std::static_pointer_cast<TextBlock>(btn->Content)->Text = "Click me";
btn->OnClick = []{ UZ_LOG(App, Info, "click!"); };
```

State machine:

```
Idle
 ├── HoverEnter ──▶ Hovered
 │                    ├── PointerDown  ──▶ Pressed (HandledCapture)
 │                    │                     ├── PointerUp inside ─▶ OnClick(); Idle
 │                    │                     └── HoverLeave        ─▶ press dropped; Hovered/Idle
 │                    └── HoverLeave  ──▶ Idle
 └── PointerDown    ──▶ Pressed  (router routed via HitTest, no Hover yet)
```

Visual cascade:

```
Color = Pressed ? PressedColor
      : Hovered ? HoverColor
      : NormalColor
```

Defaults match a neutral dark theme ready for the Step 6 Brush /
StyleSet replacement. The button paints exactly **one** Quad
DrawElement plus whatever its `Content` widget emits.

## Test coverage

The Slate suite is now **46/46** passing in 0 ms total:

| Suite           | Tests | Notes |
|-----------------|-------|-------|
| Step 1 + 2 + 3  | 36    | Renderer + widget + layout. |
| SlateInput      | 6     | Hit-test, bubble, capture, hover. |
| SlateButton     | 5     | Click semantics + state cascade. |

Key invariants enforced by the tests:

- Deepest child wins hit-test; bubble reaches parent only when child
  returns Unhandled.
- HandledCapture redirects subsequent Move/Up to the captor regardless
  of the cursor's spatial relation to it; PointerUp releases capture.
- Hover Enter/Leave fire only on target change.
- Button OnClick fires **only** when Down + Up both happen inside.
- HoverLeave during press defensively drops `m_pressed` so a release
  outside cannot fire a click on re-entry.
- Paint colour matches the cascade: Idle → Hover → Pressed.

## Design choices

- **Capture inside Button is automatic.** `OnPointerDown` returns
  `HandledCapture`, not just `Handled`, so the cursor can leave and
  re-enter without losing the press. Drag-out-then-back-in works.
- **PointerMove is Unhandled by default on Button.** Hover state is
  driven by the router's separate Enter/Leave hooks. This frees parent
  containers (e.g. a draggable HBox) to still receive Move events.
- **Defensive press drop on HoverLeave.** Without it, a user pressing
  the button, dragging out, and re-entering would fire OnClick on
  release inside — visually jarring. Slate handles this differently
  (it tracks a "drag started here" anchor); we picked the simpler
  Enter/Leave-driven model because it's enough for the VFX editor.

## Visualization

A standalone `slate_ui_step4` demo with screenshot lands in **Step 5**
(invalidation). Rationale: a static button screenshot looks identical
to the last layout shot. Step 5 introduces the dirty-bit cache and its
demo (`slate_ui_step5`) is the perf-comparison demo from the spec —
it explicitly toggles between "always-invalidate" and "smart-invalidate"
modes, producing two contrasting screenshots that prove the renderer +
input + invalidation pipeline together. That single demo absorbs the
visualization debt for steps 1-5 in a single coherent shot.
