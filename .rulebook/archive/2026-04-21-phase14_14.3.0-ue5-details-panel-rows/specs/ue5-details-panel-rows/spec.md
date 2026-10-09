# ue5-details-panel-rows

## ADDED Requirements

### Requirement: Two-column label/value split

Every editor row (`EditorFloat / Vec3 / Color / Bool / Enum / Curve
/ Ramp / String`) SHALL emit a label column at 40 % of the row
width and a value-editor column at 60 %, divided by a 1-px
`borderSubtle` vertical hairline.

#### Scenario: Float row renders split columns

Given a parameter inspector row of type `EditorFloat` with
`row.w = 200`
When the widget renders
Then the label draws inside `[row.x, row.x+80]` and the value
editor draws inside `[row.x+81, row.x+200]`, with a 1-px vertical
`borderSubtle` rect at `x = row.x+80`.

### Requirement: Zebra striping

Property rows SHALL alternate `panelBg` (even index) and
`panelBgAlt` (odd index) fill colours. Section-header rows are
excluded from zebra striping.

#### Scenario: Adjacent rows alternate fills

Given three property rows rendered in order
When the fills are sampled
Then row 0 == `panelBg`, row 1 == `panelBgAlt`, row 2 == `panelBg`.

### Requirement: Section header chevron

Section headers SHALL render a `ChevronDown` sprite when expanded
and `ChevronRight` when collapsed, positioned at `row.x + 6 px`
vertically centred.

#### Scenario: Collapsed section shows right chevron

Given a section header where `IsSectionExpanded == false`
When the widget renders
Then the emitted sprite at `row.x + 6` has UV matching
`VfxGetIconUv(VfxUiIcon::ChevronRight)`.
