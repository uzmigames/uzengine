# ue5-palette-typography

## ADDED Requirements

### Requirement: UE5 background ladder

`VfxColorPalette` SHALL expose five discrete background tokens
matching UE5 Slate's grey ladder: `#0B0B0B` outer, `#1C1C1C` panel,
`#242424` bar/header, `#2C2C2C` card/row, `#383838` hover.

#### Scenario: Panel and card reads distinctly

Given the editor rendered at 1280x720
When a screenshot is sampled inside a panel region and inside a
module card within that panel
Then the card pixel RGB differs from the panel pixel RGB by a
Manhattan distance >= 30 (each channel ~= `0x0B` step).

### Requirement: UE5 accent tokens

`VfxColorPalette::accentPrimary` SHALL equal UE5 blue `#0070E0` and
`VfxColorPalette::accentOrange` SHALL equal UE5 orange `#FF7B00`.

#### Scenario: Selection tint matches UE5 blue

Given a selected module card
When the rendered fill is sampled
Then the fill blends `cardBgSelected=#1C3A5E` with a visible blue
bias (channel R < B and G < B).

### Requirement: UE5 radius scale shrunk

`VfxRadiusScale` SHALL publish `none=0, xs=2, sm=3, md=4, lg=6`.
Panels use `none`, buttons use `xs`, tabs use `sm`, dropdowns use
`md`, dialogs use `lg`.

#### Scenario: Panels render with square corners

Given the main editor panel background drawn via `AddRoundedRect`
When the corner pixel is sampled
Then alpha == 1 (no AA falloff), confirming radius == 0.
