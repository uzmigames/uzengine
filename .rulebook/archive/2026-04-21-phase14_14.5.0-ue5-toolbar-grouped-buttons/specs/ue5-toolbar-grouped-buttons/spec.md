# ue5-toolbar-grouped-buttons

## ADDED Requirements

### Requirement: Icon-plus-label button layout

Each toolbar button SHALL render a 16x16 icon sprite followed by a
10-px MSDF label, with 4-px inner padding on every side and 2-px
gap between adjacent buttons.

#### Scenario: Button width matches icon + label + padding

Given a button with icon 16 px + label "Save" (measured ~28 px)
When the widget renders with `kPadding = 4`
Then the button rect width == `4 + 16 + 4 + 28 + 4 = 56 px`.

### Requirement: Group separator geometry

`AddSeparator()` between two button runs SHALL emit a 1-px
`borderSubtle` vertical rect at the midpoint of an 8-px combined
padding gap.

#### Scenario: Separator sits in 8-px gap

Given button run ending at `x = 120` then a separator then a
button run starting at `x = 128`
When the separator renders
Then the separator rect is `[124, toolbar.y, 1, toolbar.h]`.

### Requirement: Toggle button sticks on click

A `VfxToolbarButton` with `kind = Toggle` SHALL keep its fill as
`cardBgSelected` while `toggled == true` and revert to `barBg` on
the next click.

#### Scenario: Play-pause stays highlighted while playing

Given a toggle button initially not toggled
When `Click(id)` fires once
Then `toggled == true` and the render fill == `cardBgSelected`.
When `Click(id)` fires a second time
Then `toggled == false` and the fill reverts to `barBg`.
