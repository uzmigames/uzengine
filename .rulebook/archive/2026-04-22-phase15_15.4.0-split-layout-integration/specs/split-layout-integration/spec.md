# split-layout-integration

## ADDED Requirements

### Requirement: Splitter drag updates splitFraction

When the user drags the gutter by `dx` pixels,
`VfxEditorOverlay` MUST update
`layoutConfig.splitFraction += dx / screenW` and clamp the
result to `[0.2, 0.8]`.

#### Scenario: 64-px drag at 1280 px width

Given `splitFraction = 0.50`, `screenW = 1280`
When the splitter drag delta is +64 px
Then `splitFraction` equals `0.55` (0.50 + 64/1280).

### Requirement: Mode toggle via View menu

`VfxMenuCommand::View_SplitLayout` MUST flip between the two
layout modes and push a status-bar notification reflecting the
new mode.

#### Scenario: Toggle from Niagara to SplitHorizontal

Given the overlay starts in `Niagara` mode
When the host dispatches `View_SplitLayout`
Then the layout mode switches to `SplitHorizontal` and the
status-bar notification queue gains an entry with a message
matching the new mode label.

### Requirement: Niagara path unchanged

Building the overlay in `Niagara` mode MUST produce identical
widget dispatch as phase 14.16 — the new split-mode branch must
not alter the existing render path.

#### Scenario: Niagara mode preserves phase-14.16 layout

Given the overlay is in `Niagara` mode
When `BuildNiagaraUI` runs once
Then the stack panel / center panel / right panel rects match
the phase-14.16 reference values.
