# ue5-tab-bar

## ADDED Requirements

### Requirement: Active tab merges with panel body

`VfxTabStripWidget::Render` SHALL draw the active tab using the
same fill colour as the panel body directly below it (`cardBg` per
the phase-14.1 UE5 palette), with rounded top corners (radius 3)
and sharp bottom corners (radius 0).

#### Scenario: Active tab fill matches panel body

Given the tab strip rendered above a module panel
When the pixel at the tab bottom-centre and the pixel just below
the tab boundary (inside the panel) are sampled
Then both RGB values equal `cardBg` within 1 LSB.

### Requirement: Active tab accent bar

The active tab SHALL render a 2-px `accentPrimary` rectangle along
its top edge, spanning the full tab width.

#### Scenario: Blue bar on top of active tab

Given the active tab bounds `[x, y, w, h]`
When the strip along `[x, y, w, 2]` is sampled
Then every pixel's RGB equals `accentPrimary` within 1 LSB.

### Requirement: Close button on hover

When a tab is hovered, a `VfxUiIcon::Cross` sprite SHALL render at
the tab's right edge (centred vertically, 10x10 px) tinted with
`textDim`.

#### Scenario: Close icon appears on hover

Given a tab bound at `[100, 0, 120, 24]`
When `HitTest(160, 12)` returns a valid hit AND the hover flag
fires for that tab
Then the layout item list contains an entry of type
`VfxTabItemType::CloseIcon` at roughly `[204, 7, 10, 10]`.
