# split-layout-mode

## ADDED Requirements

### Requirement: SplitHorizontal computes viewport + editorPane

When `cfg.layoutMode == SplitHorizontal`, `VfxComputeEditorLayout`
SHALL emit a left `viewportReserve` rect and a right `editorPane`
rect whose combined width equals the screen width and whose
heights equal the middle content band (screen minus menu /
toolbar / status / timeline strips).

#### Scenario: 50/50 split at 1280x720

Given `screenW=1280`, `screenH=720`, `splitFraction=0.5`,
`layoutMode=SplitHorizontal`, default strip heights
When `VfxComputeEditorLayout` runs
Then `viewportReserve.width == 640`, `editorPane.width == 640`,
`viewportReserve.x + viewportReserve.width == editorPane.x`,
and both heights equal the middle content band.

### Requirement: splitFraction clamped to [0.2, 0.8]

Values outside `[0.2, 0.8]` MUST be clamped so neither side
collapses.

#### Scenario: splitFraction=0.05 clamps to 0.2

Given `splitFraction=0.05`, `screenW=1000`
When `VfxComputeEditorLayout` runs
Then `viewportReserve.width` equals `200` (0.2 * 1000).

### Requirement: Niagara mode unchanged

`cfg.layoutMode == Niagara` MUST produce identical rects to the
phase-14 composition — adding the new enum value must not change
the existing call-site behaviour.

#### Scenario: Niagara default emits the phase-14 rects

Given default config (`layoutMode=Niagara`), `screenW=1280`,
`screenH=720`
When `VfxComputeEditorLayout` runs
Then the `stackPanel`, `centerPanel`, `rightPanel`, `timelineBar`
values equal the phase-14.10 reference values within 0.01 px.
