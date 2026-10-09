# Slate UI — Composite Widgets (Phase 18, Step 7)

**Status**: Complete. ScrollBox, EditableText, ComboBox<T> implemented;
the remaining glue (TopMost overlay layer, full clipboard, drag-to-scroll)
is rolled into Step 9.

## Files

- `runtime/engine/ui/slate_scrollbox.{h,cpp}` — clipping container.
- `runtime/engine/ui/slate_editable_text.{h,cpp}` — single-line text input.
- `runtime/engine/ui/slate_combobox.h` — header-only template dropdown.
- `runtime/tests/engine/ui/test_slate_composite.cpp` — 16 unit tests.

## ScrollBox

A single-child container that:

- measures its child against `numeric_limits<float>::max()` on Y so the
  child requests its full natural height,
- in `Arrange`, re-measures with `viewport.x` constrained, derives
  `m_maxScroll = max(0, contentH - viewportH)`, clamps `ScrollY` and
  arranges the child at `geometry.absolutePos.y - ScrollY`,
- in `Paint`, emits `ScissorPush(viewport)` → child paint → `ScissorPop`,
- when `ShowScrollbar=true` and `m_maxScroll > 0`, draws an 8 px-wide
  track + a thumb proportional to `viewport / content`,
- in `OnPointerWheel`, adjusts `ScrollY -= wheelDelta * WheelStep`,
  clamps and invalidates Layout.

Horizontal scrolling, scrollbar drag, and overscroll bounce are
deferred to Step 10's Details panel.

## EditableText

Single-line text input with caret + arrow keys. State:

```cpp
Attribute<std::string> Text;
Attribute<Color>       TextColor, BackgroundColor, BorderColor;
Attribute<float>       FontSize, CornerRadius;
std::function<void(const std::string&)> OnTextChanged, OnCommitted;
```

- `OnPointerDown` focuses the widget and approximates caret placement
  by `(click.x - paddingX) / (FontSize × 0.55)`.
- `OnKeyDown` handles `Backspace`, `Delete`, arrow keys, `Home`, `End`,
  `Enter` (commits), `Escape` (blurs), and printable characters
  (`character != 0`).
- `Tick` increments `m_blinkTimer` and invalidates Paint when the
  caret toggles between visible / invisible halves of the blink period.
- `Commit` semantics: fires `OnCommitted(text)` exactly once when the
  current value differs from the last committed value (Enter or Blur).

The widget exposes platform-agnostic key codes (`KeyBackspace`,
`KeyEnter`, etc.) the host's input layer translates to. No VK_*
defines leak in.

Selection / clipboard / IME / multi-line are deferred — the VFX
editor's emitter rename + parameter typing only need single-line.

## ComboBox<T>

Header-only template dropdown:

```cpp
ComboBox<std::string> cb;
cb.SetOptions({"Mesh", "Sprite", "MeshRenderer"},
              [](const std::string& s) { return s; });
cb.OnSelectionChanged = [](int i) { ... };
```

- `Measure` budgets for the longest label + 8 px chevron + 16 px padding.
- `Paint` draws the bg quad, the selected label, and a 3-row chevron
  using stacked thin quads (the renderer doesn't have a triangle
  primitive yet; this is cheap and avoids a custom shader path).
- `OnPointerDown` toggles `m_open`; the host (Step 9 TopMost layer or
  a manual `SetPopupHost`) is notified to show/hide the popup widget.
- `SelectOption(idx)` is the public API tests use to simulate selection;
  it updates `SelectedIndex`, fires `OnSelectionChanged`, closes the
  popup.

`BuildPopupWidget` is a stub returning `nullptr`; Step 9 fills it with
a real `VBox` of `Button`s once the TopMost layer exists. Until then,
`SetPopupHost` lets the host render the popup itself if it wants
(used by Step 10's stack panel).

## Test coverage

Slate suite is now **87/87** passing in 1 ms total:

| Suite              | New tests | Description |
|--------------------|-----------|-------------|
| SlateScrollBox     | 4         | Measure clamp, ScrollY clamp on Arrange, wheel scroll, ScissorPush/Pop in Paint. |
| SlateEditableText  | 8         | Initial unfocused, focus on click, backspace, arrow nav, Enter commits once, Blur commits when changed, character input, Escape blurs. |
| SlateComboBox      | 4         | SetOptions resets index + label, SelectOption fires callback + closes, PointerDown toggles open, out-of-range select ignored. |

## Visualization

Real on-screen demos for these widgets land with Step 10's VFX editor.
A dedicated screenshot would require a TextRenderer-backed demo with
keyboard input wiring; the same scaffold is built once for Step 10 and
reused.

## Design choices

- **EditableText keys are abstract codes (KeyBackspace = 1, etc.).**
  The host platform layer maps OS-specific VKs into these symbolic
  codes so the widget stays platform-agnostic.
- **Caret blink invalidation is half-period.** Step 5's
  `Invalidate(Paint)` fires only when the visible→hidden transition
  crosses the half-mark — at most twice per second per focused widget.
- **ComboBox popup is host-driven** to avoid forcing the widget itself
  to know about a TopMost layer that doesn't exist yet (Step 9). Tests
  use `SelectOption` to simulate selection without going through the
  popup layer at all.
- **ScrollBox horizontal axis ignored.** Vertical-only is a deliberate
  scope cut; the VFX editor's Details panel only needs vertical.
