# widget-integration

## ADDED Requirements

### Requirement: Toolbar docks below emitter bar

`VfxEditorOverlay` SHALL compose a 28-px `VfxToolbarWidget` strip
directly below the emitter tab bar, above the stack panel + center
panel + right panel.

#### Scenario: Toolbar strip exists in composition

Given the overlay's composition layout is computed for a 1280x720 screen
When the host reads `VfxEditorComposition::toolbarBar`
Then its height equals 28 px and its y-origin equals
`menuBar.y + menuBar.height + emitterBar.height`.

### Requirement: Status bar docks above timeline

`VfxEditorOverlay` SHALL compose a 22-px `VfxStatusBarWidget` strip
directly above the timeline scrubber.

#### Scenario: Status bar sits above timeline

Given the composition layout
When the host reads `VfxEditorComposition::statusBar`
Then its height equals 22 px and its y-origin equals
`timelineBar.y - 22`.

### Requirement: Toolbar play-pause button toggles

The toolbar's Play-Pause button MUST be a `Toggle` kind; clicking it
flips the widget's internal `toggled` flag.

#### Scenario: Toggle flip on click

Given the toolbar renders with the Play-Pause button not toggled
When the host dispatches a click to the button id
Then `VfxToolbarWidget::IsToggled(id)` returns `true` on the next
read.
