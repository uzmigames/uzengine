# Slate UI — Widget Base + Attribute<T> + TextBlock (Phase 18, Step 2)

**Status**: Complete. The widget tree foundation is in place; subsequent
steps add layout containers, input, invalidation, etc.

## Files

- `runtime/engine/ui/slate_attribute.h` — `Attribute<T>` pull binding.
- `runtime/engine/ui/slate_widget.h` — `Geometry`, `Widget` base class.
- `runtime/engine/ui/slate_widget.cpp` — `Widget::Arrange` default impl.
- `runtime/engine/ui/slate_text_block.{h,cpp}` — `TextBlock` widget.
- `runtime/tests/engine/ui/test_slate_widget.cpp` — 12 unit tests.

## Lifecycle

Each frame, every widget in the retained tree goes through:

```
Construct  ──once──▶  Tick(dt)  ──▶  Measure(maxSize)  ──▶  Arrange(geom)  ──▶  Paint(renderer)
                          │              │                        │                    │
                          │              │                        │                    └─▶ DrawElements
                          │              │                        └─▶ stores m_geometry
                          │              └─▶ pure measurement, no allocation
                          └─▶ Attribute<T>::Get() pulls live data
```

- `Construct()` is called exactly once after the widget is constructed.
  No-op for leaf widgets like `TextBlock`. Container widgets in Step 3
  use it to wire parent pointers.
- `Tick(dt)` is the place to pull data via `Attribute<T>::Get()` and
  potentially flag `Invalidate(Paint)` (Step 5). Default no-op.
- `Measure(maxSize)` returns a desired size; pure function. MUST not
  mutate observable child layout state.
- `Arrange(geometry)` accepts the final allocation. Default impl just
  stores `m_geometry`; container widgets override to also arrange
  children. Coordinates are absolute (parent-resolved) Slate units.
- `Paint(renderer)` emits `DrawElement`s via `SlateRenderer::PushElement`.
  MUST NOT call `TextRenderer` directly. Reads from `m_geometry`.

## Geometry

```cpp
struct Geometry {
    Vec2  absolutePos;    // Top-left in screen-space Slate units.
    Vec2  size;           // Allocated width / height in Slate units.
    float dpiScale{1.0f}; // Forwarded for hit-test math.
};
```

The `dpiScale` is forwarded down the tree but the actual physical-pixel
multiplication happens once, in `SlateRenderer::Submit`. Widgets always
work in Slate units.

## Attribute<T>

Pull-style data binding (Decision 3). Two forms:

```cpp
text->Text = "hello";                       // literal: snapshot value
text->Text = []{ return GetUserName(); };   // lambda: re-evaluated per Get
```

Internals:
- Literal assignment stores the value and clears any previously-bound lambda.
- Lambda assignment replaces the getter; the literal field is unchanged
  but unused while the lambda is set.
- `Get()` returns `m_getter()` if bound, else `m_value`.
- `IsBound()` reports whether a lambda is currently active.
- `Unbind()` drops the lambda and falls back to the literal.

Lambda assignment never auto-invalidates the owner widget; the widget
is responsible for snapshot-comparison in `Tick` (formalised in Step 5).

## TextBlock

The simplest possible widget: a leaf that emits one `DrawKind::Text`
element per Paint.

```cpp
auto t = std::make_shared<TextBlock>();
t->Text     = "Hello";
t->TextColor = Color{1.0f, 1.0f, 1.0f, 1.0f};
t->FontSize = 11.5f;
```

- `Measure` returns
  `Vec2{ glyphCount × FontSize × 0.55, FontSize + 2 }` clamped to `maxSize`.
  The 0.55 advance ratio (`kApproxGlyphAdvanceRatio`) is good enough for
  layout boxes in Step 3; Step 7 (composite widgets) switches to real
  bitmap-font advance metrics when caret-precise widths matter.
- `Paint` emits a `DrawKind::Text` element with the widget's geometry,
  the current `Text`/`TextColor`/`FontSize`, and the bitmap path
  (`msdf=false`). MSDF rendering opts in from Step 6.

## Visualization

Code-and-tests for Step 2; the **first standalone visual demo** ships
with **Step 3** (`slate_ui_step3`) where `HBox`/`VBox` layout boxes
let us show 2-3 `TextBlock`s in a meaningful arrangement (e.g.
"FPS: 60 | Frame: 1234 | Slate UI works"). A single TextBlock alone in
a window is visually indistinguishable from a direct `TextRenderer.AddText`
call, while three TextBlocks composed via HBox is the first frame that
exercises the full Slate pipeline.

Behavioral correctness for Step 2 is covered by 12 unit tests in
`test_slate_widget.cpp`:

- Attribute<T>: literal / lambda / IsBound / Unbind / re-evaluation per Get.
- Widget::Arrange: geometry storage + ToRect convenience.
- TextBlock::Measure: linear scaling with length × FontSize, maxSize clamp,
  lambda Text resolved at measure time.
- TextBlock::Paint: emits one Text DrawElement at the arranged geometry
  with the current values; lambda Text is re-evaluated each Paint.

20 Slate tests total (8 from Step 1, 12 new), all passing in 0 ms.

## Build integration

Source already covered by `addCppDir(runtime_mod, b, "engine/ui", ...)`
from Step 1. New `.cpp` files (`slate_widget.cpp`,
`slate_text_block.cpp`) are picked up automatically.
