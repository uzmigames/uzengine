# vfx-timeline-redesign

## ADDED Requirements

### Requirement: Transport bar uses icon glyphs

Every transport button (SkipStart, StepBack, PlayPause, StepForward,
SkipEnd, Restart) SHALL render its glyph constant from
`vfx_editor_icons.h`. No text labels are emitted for transport
buttons.

#### Scenario: Play button renders the play glyph when paused

Given a bound clock in `Paused` state
When `Render` runs
Then the `TogglePlayButton` cell emits a text item whose string
equals `VFX_ICON_PLAY`.

### Requirement: Play/Pause icon reflects clock state

The single `TogglePlayButton` SHALL render `VFX_ICON_PLAY` when the
bound clock's state is `Paused` / `Frozen`, and `VFX_ICON_PAUSE`
otherwise.

#### Scenario: Pause icon while playing

Given a bound clock in `Playing` state
When `Render` runs
Then the `TogglePlayButton` cell emits `VFX_ICON_PAUSE`.

### Requirement: Speed dropdown shows current rate

The `SpeedDropdown` trigger SHALL show the current play rate with one
decimal of precision and a suffix `x`.

#### Scenario: 2.0x display when play rate is 2.0

Given a clock with `playRate = 2.0`
When `Render` runs
Then the SpeedDropdown trigger emits the text `"2.0x"`.

### Requirement: Time readout format

`TimeReadout` SHALL emit text in the format `"%.2f / %.2f s"` where
the first value is `clock->GetCurrentTime()` and the second is the
clock's max duration (falling back to `999.99` when unbounded).

#### Scenario: Readout at 1.23s of 5.00s

Given a clock at `currentTime = 1.23`, `maxDuration = 5.00`
When `Render` runs
Then the TimeReadout cell emits text `"1.23 / 5.00 s"`.

### Requirement: Scrub track has filled + unfilled regions

The scrub track SHALL render a filled rectangle spanning from `x=0`
to `x=W*t/maxT` in `accentMuted` and an unfilled rectangle for the
remainder in `cardBg`.

#### Scenario: Half-played track fills half width

Given `currentTime = 2.5`, `maxDuration = 5.0`, track width 400
When `Render` runs
Then the filled rect has `width == 200` (within 1 px tolerance).

### Requirement: Major tick marks every 0.5 s

The scrub track SHALL emit tick marks every 0.5 s as 1-px vertical
lines in `textDim`, plus minor ticks every 0.1 s in `borderSubtle`.

#### Scenario: 10-second track produces 20 major ticks

Given a track with `maxDuration = 10.0`
When `Render` runs
Then at least 20 distinct major-tick rectangles are present.

### Requirement: Burst markers are upward triangles

`BurstMarker` layout items SHALL render as upward-pointing triangles
approximated by stacked rectangles (3-5 rects), with vertical height
scaled by `log2(count + 1)`.

#### Scenario: Higher burst count makes taller marker

Given two bursts at times 1.0 and 2.0 with counts 4 and 64
When `Render` runs
Then the marker at `t=2.0` (count 64) has total height at least 2x
that of the marker at `t=1.0` (count 4).
