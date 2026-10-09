# vfx-curves-editor-redesign

## ADDED Requirements

### Requirement: Grid rendering emits major + minor lines

`Render` SHALL draw a grid of vertical + horizontal lines covering the
visible viewport. Major gridlines appear at integer time (X) and at
value 0.0 / 0.5 / 1.0 (Y). Minor gridlines subdivide each major
interval into 4 parts.

#### Scenario: Major grid at integer seconds

Given a viewport spanning `time in [0, 3]` and `value in [0, 1]`
When `Render` runs against a capture-enabled mock
Then at least 4 vertical lines are emitted at `time = 0, 1, 2, 3` each
colored with a grid-major token (a distinct shade from minor).

### Requirement: Curve stroke uses 2-px rectangles

The curve line SHALL be drawn by sampling the bound curve every
1-2 px of X and emitting connecting rectangles 2 px wide along each
segment.

#### Scenario: Dense sampling on a wide canvas

Given a canvas `800 x 300` bound to a curve with 3 keys
When `Render` runs
Then the emitted rect list contains at least 300 segments for the
curve stroke (one per 2-3 px of X).

### Requirement: Selected key has tangent handles

When a key is selected, `ComputeLayout` SHALL emit two additional
`TangentHandleLeft` and `TangentHandleRight` items positioned at the
key's computed left / right tangent vectors.

#### Scenario: Tangent handles appear for selection

Given a curve with 3 keys and `m_selectedKeyIndex == 1`
When `ComputeLayout` runs
Then exactly 2 items with types `TangentHandleLeft` and
`TangentHandleRight` are present, both tied to key index 1.

### Requirement: Toolbar buttons emit distinct hit kinds

A toolbar strip at the top of the canvas SHALL host separate hit
items: `ButtonAddKey`, `ButtonDeleteKey`, `ButtonZoomFit`,
`ButtonSnapToggle`, `InterpPicker`.

#### Scenario: Each button is independently hit-testable

Given a toolbar at a known position
When `HitTest` is called on each button's rect
Then the returned hit type matches the button's enum value.

### Requirement: Scroll scales viewport around cursor

`HandleScroll(dx, dy, cursorX, cursorY)` SHALL scale the viewport so
the world-space point under the cursor stays fixed on screen.

#### Scenario: Zoom keeps cursor point fixed

Given a viewport and a cursor over world-space `(time=1.0, value=0.5)`
When `HandleScroll(0, 1, cursorX, cursorY)` is called to zoom in
Then the viewport-to-cursor mapping for `(time=1.0, value=0.5)` is
within 1 pixel of the original cursor position.

### Requirement: Color ramp mode draws a gradient strip

When `m_mode == Ramp`, `Render` SHALL draw a horizontal gradient
sampled from the bound ramp across the full canvas width, with
triangular stop markers below the strip at each stop's `t`.

#### Scenario: Gradient has sampled stops

Given a ramp with 2 stops (red at t=0, blue at t=1) on a 400-wide
canvas
When `Render` runs
Then at least 50 gradient rectangles are emitted, colors interpolated
between the stops.
