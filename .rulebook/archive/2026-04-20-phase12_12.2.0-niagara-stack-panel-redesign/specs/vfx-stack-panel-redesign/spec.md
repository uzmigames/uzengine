# vfx-stack-panel-redesign

## ADDED Requirements

### Requirement: Emitter header card at top of layout

The stack panel SHALL emit exactly one `EmitterHeader` layout item
before any stage header when a bound emitter is present. The emitter
header SHALL show the emitter name, an enable checkbox whose state
reflects `emitter->emitting`, and an edit action for the name.

#### Scenario: Emitter header precedes all stages

Given a widget bound to an emitter with a populated stack
When `ComputeLayout` runs
Then the first layout item has `type == EmitterHeader`, and every
subsequent `StageHeader` item comes after it.

### Requirement: Stage header has inline add-module button

Each `StageHeader` layout item SHALL include a 20-px `StageAddButton`
sub-rect on its right edge. Clicking the button fires
`onAddModuleRequested(stage)`. No standalone "+ Add Module" row is
emitted below each stage.

#### Scenario: No AddRow items in the layout

Given a widget bound to any emitter
When `ComputeLayout` runs
Then no layout item has `type == AddRow` (the sentinel kind is retained
for ABI stability but no item is emitted for it).

#### Scenario: Stage add button dispatches the callback

Given a widget bound to an emitter with an empty EmitterSpawn stage
When `HitTest` is called on the coordinates of the stage's add button
Then the returned hit has `type == StageAddButton` and
`stage == EmitterSpawn`.

### Requirement: Module card sub-rects are hit-testable independently

Every `ModuleCard` layout item SHALL also emit `CardDragHandle`,
`CardCheckbox`, and `CardOverflowMenu` sub-items. Each sub-item has
its own rect that `HitTest` returns distinctly.

#### Scenario: Hit-test on overflow menu returns CardOverflowMenu

Given a module card at a known position
When `HitTest` is called on the 20-px region at the card's right edge
Then the result has `type == CardOverflowMenu` and the card's
`instanceId`.

### Requirement: Selected card has accent stripe

When a module card's `instanceId == m_selectedInstanceId`, `Render`
SHALL draw an `accentPrimary` rectangle 3 px wide along the card's
left edge in addition to the usual selected background shade.

#### Scenario: Selected stripe is drawn

Given a widget with one selected module
When `Render` executes against a capture-enabled text renderer mock
Then the rectangle list includes a 3-px-wide rect at the card's left
edge colored with `VfxTheme().accentPrimary`.

### Requirement: Context menu callback fires on overflow menu click

The widget SHALL expose a new callback
`onModuleContextMenuRequested(const std::string& instanceId, float x, float y)`
that fires when the overflow menu sub-rect is clicked.

#### Scenario: Overflow click fires context menu callback

Given a widget with `onModuleContextMenuRequested` bound
When the host dispatches a `CardOverflowMenu` hit
Then the callback receives the module's `instanceId` and the click
coordinates.
