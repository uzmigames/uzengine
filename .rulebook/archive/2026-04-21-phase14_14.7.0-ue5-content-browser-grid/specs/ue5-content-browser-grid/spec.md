# ue5-content-browser-grid

## ADDED Requirements

### Requirement: Tile geometry

Each asset tile SHALL occupy 96 px (thumbnail) + 24 px (label
strip) for a total tile height of 120 px, with a 96 px width.
Tiles SHALL be laid out left-to-right with an 8 px gap, wrapping
to a new row when the panel's remaining width cannot fit another
tile.

#### Scenario: Grid lays out 3 columns at 320 px panel

Given a panel of width 320 px and 6 asset tiles
When `Render` runs with `gap = 8` and `tileW = 96`
Then `cols = floor((320 - 8) / (96 + 8)) = 3` and the 4th tile
starts a new row at `y = 120 + 8`.

### Requirement: Type-coloured top border

Each tile SHALL render a 2-px rectangle along its top edge using
the `kindTint` colour supplied by the host, so asset type reads
at a glance.

#### Scenario: Purple band on a Blueprint tile

Given a tile with `kindTint = {0.6, 0.3, 0.85, 1.0}`
When the strip along `[tile.x, tile.y, 96, 2]` is sampled
Then every pixel matches that colour within 1 LSB.

### Requirement: Drag threshold

`onDragStart` SHALL only fire after the cursor moves > 6 px from
the mouse-down position while the button is still held — click
followed by immediate release MUST fire `onSelect`, not
`onDragStart`.

#### Scenario: Click without drag

Given mouse-down at `(10, 10)` and mouse-up at `(11, 11)` without
any intermediate moves beyond threshold
When the widget processes the click sequence
Then `onSelect(tileId)` fires exactly once and
`onDragStart(tileId)` does NOT fire.
