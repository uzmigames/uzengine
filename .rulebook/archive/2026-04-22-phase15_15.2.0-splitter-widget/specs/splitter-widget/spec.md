# splitter-widget

## ADDED Requirements

### Requirement: HitTest matches the 6-px gutter rect

`VfxSplitterWidget::HitTest(x, y, bounds)` SHALL return true iff
`(x, y)` lies inside `bounds` (width == 6 px by construction).

#### Scenario: Cursor in bar hits

Given `bounds = { 640, 30, 6, 500 }`
When `HitTest(642, 200, bounds)` is called
Then the return value equals `true`.

#### Scenario: Cursor outside bar misses

Given the same bounds
When `HitTest(700, 200, bounds)` is called
Then the return value equals `false`.

### Requirement: UpdateDrag returns delta from the anchor

`BeginDrag(x0)` SHALL store `x0`; a subsequent `UpdateDrag(x1)`
MUST return `x1 - x0` and reset the anchor to `x1` so
back-to-back calls return frame-local deltas.

#### Scenario: 10-px drag returns 10

Given `BeginDrag(100.0f)` was called
When `UpdateDrag(110.0f)` is called
Then the return value equals `10.0f` and a second
`UpdateDrag(115.0f)` returns `5.0f`.

### Requirement: IsDragging mirrors begin/end

After `BeginDrag`, `IsDragging()` MUST return true until
`EndDrag()` is called.

#### Scenario: Drag lifecycle

Given a fresh `VfxSplitterWidget`
When `BeginDrag(100.0f)` runs
Then `IsDragging()` returns `true`.
When `EndDrag()` runs
Then `IsDragging()` returns `false`.
