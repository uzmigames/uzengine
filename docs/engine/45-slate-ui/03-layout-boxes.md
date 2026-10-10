# Slate UI — Layout Boxes (Phase 18, Step 3)

**Status**: Complete. The three foundational containers are in place
and exercised by 16 unit tests.

## Files

- `runtime/engine/ui/slate_slot.h` — `SlotBase`, `HAlign`, `VAlign`, `SizeRule`.
- `runtime/engine/ui/slate_box.{h,cpp}` — `HorizontalBox` / `VerticalBox` / `Overlay`.
- `runtime/engine/ui/slate_spacer.h` — `Spacer` (declared sizes / Fill consumer).
- `runtime/tests/engine/ui/test_slate_box.cpp` — 16 unit tests.

## Two-pass layout

Slate uses a **Measure → Arrange** model. The same convention as Unreal
Slate / WPF / Avalonia: layout is computed in two passes per frame.

```
Pass 1 — Measure(maxSize):
  HBox: total.x = Σ child.x + slot.padX
        total.y = max(child.y + slot.padY)
  VBox: total.y = Σ child.y + slot.padY
        total.x = max(child.x + slot.padX)
  Overlay: max child.x + slot.padX (and y)
  → clamp to maxSize

Pass 2 — Arrange(geometry):
  Sum the Auto allocations; the rest is the Fill pool.
  Each Fill slot gets `pool * (fillFactor / Σ fillFactor)`.
  Padding is subtracted from the slot before the child sees its rect.
  HAlign / VAlign positions the child inside the cell when the cell is
  larger than the child's measure.
```

## Slot

```cpp
struct SlotBase {
    SP<Widget> widget;
    Vec4       padding;       // Slate units: L, T, R, B.
    HAlign     hAlign;        // Left / Center / Right / Fill.
    VAlign     vAlign;        // Top / Center / Bottom / Fill.
    SizeRule   sizeRule;      // Auto / Fill.
    float      fillFactor;    // Default 1.0; ratio among Fill siblings.
};
```

Fluent setters return `*this`:

```cpp
hbox->AddSlot(text)
   .HAlign_(HAlign::Right)
   .VAlign_(VAlign::Center)
   .Padding_(Vec4{8, 4, 8, 4})
   .FillSize(2.0f);
```

`HAlign_` and `VAlign_` carry trailing underscores because `HAlign` /
`VAlign` are also enum names — the underscore avoids the conflict at
the call-site without forcing a different identifier in the type.

## HBox / VBox / Overlay

`HorizontalBox` (alias `HBox`) and `VerticalBox` (alias `VBox`) lay
children out left-to-right and top-to-bottom respectively. `Overlay`
stacks children on the same rect (z-order = slot index, last on top),
each aligned independently inside the parent's allocation — useful for
tooltips, modal dimmers, badge overlays.

All three implement `Measure` / `Arrange` / `Paint`. Paint is just an
in-order walk of the slots calling each child's `Paint`.

## Spacer

Tiny widget for explicit gaps:

```cpp
auto sp = std::make_shared<Spacer>(Vec2{0, 12}); // 12px tall horizontal rule
auto fl = std::make_shared<Spacer>();            // collapsible
hbox->AddSlot(left);
hbox->AddSlot(fl).FillSize(1.0f);                // shoves `right` to the right edge
hbox->AddSlot(right);
```

Paint is a no-op.

## Visualization

A standalone visual demo is rolled forward to **Step 4** where `Button`
+ click events let the screenshot show interaction (a button row is the
most informative shot for a layout system, since static text could just
as easily come from `printf`). The behavioral correctness of layout is
covered by 16 unit tests at this layer.

The Slate test suite is now **36/36** passing in 0 ms total:

| Suite          | Tests | Description |
|----------------|-------|-------------|
| SlateRect      | 3     | Step 1 |
| SlateRenderer  | 3     | Step 1 |
| SlateDrawElement | 2   | Step 1 |
| SlateAttribute | 5     | Step 2 |
| SlateWidget    | 2     | Step 2 |
| SlateTextBlock | 5     | Step 2 |
| SlateBox       | 9     | Step 3 |
| SlateOverlay   | 3     | Step 3 |
| SlateSpacer    | 2     | Step 3 |

## Design choices

- **Padding is per-slot, not per-widget.** Widgets are reusable; slot
  padding is the layout decision. This matches Slate (`SBoxPanel::Slot`),
  Avalonia (`StackPanel.Spacing`), and WPF (`Margin` is the same idea
  in the parent's coordinate space).
- **Auto + Fill mix.** Sidebars are `Auto`; the centre body is
  `FillSize(1.0f)`. Two Fill siblings with `factor=1, factor=3` split
  1:3, matching CSS's `flex-grow`.
- **Cross-axis Fill stretches.** A child's cross-axis size defaults to
  the slot's full cross-axis allocation when `VAlign::Fill` (HBox) or
  `HAlign::Fill` (VBox) is set — that's the default, so it just works.
