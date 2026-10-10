# vfx-system-overview-panel

## ADDED Requirements

### Requirement: One row per emitter

The widget SHALL emit exactly one `EmitterRow` layout item per bound
emitter + one terminal `AddEmitterRow` item.

#### Scenario: 3-emitter system yields 4 items

Given a widget bound to 3 emitters
When `ComputeLayout` runs
Then the result is 3 `EmitterRow` items followed by 1
`AddEmitterRow` item.

### Requirement: Status dot reflects runtime state

Each `StatusDot` sub-rect SHALL render with `stateSuccess` when the
emitter is `emitting && aliveParticles > 0`, `stateWarning` when
`emitting == false && aliveParticles > 0` (particles decaying),
`textDim` otherwise.

#### Scenario: Running emitter has green dot

Given an emitter with `emitting = true`, `aliveParticles = 42`
When `Render` runs
Then the emitted dot rect color equals `VfxTheme().stateSuccess`.

### Requirement: Particle count readout

Each row SHALL emit a `ParticleCount` text item right-aligned within
the row, showing `aliveParticles` as a decimal.

#### Scenario: Count of 1234 displays

Given an emitter with `aliveParticles = 1234`
When `Render` runs
Then the row's ParticleCount cell emits text equal to `"1234"`.

### Requirement: Reorder callback fires on drag

A drag from row index `srcIdx` ending at row index `dstIdx` SHALL
fire `onEmitterMoved(srcIdx, dstIdx)` exactly once.

#### Scenario: Drag from 0 to 2 fires callback with (0, 2)

Given a widget with 3 emitters and `onEmitterMoved` bound
When a drag-end is dispatched at row 2 after `BeginDrag(0)`
Then the callback is invoked with arguments `(0, 2)`.

### Requirement: Add button fires callback

Clicking the `AddEmitterRow` SHALL fire `onAddEmitterRequested`
exactly once.

#### Scenario: Add click fires callback

Given a widget with `onAddEmitterRequested` bound
When a click is dispatched on the AddEmitterRow rect
Then the callback is invoked once.

### Requirement: Caller-applied mutation contract

The widget SHALL NOT mutate the emitter list directly. All mutations
happen inside the host-provided callbacks.

#### Scenario: Delete callback not executed - no removal

Given a widget with `onEmitterDeleted` bound but no handler logic
When the user clicks the Delete button on row 1
Then `ComputeLayout` still produces the same number of rows (widget
does not shrink the list itself).
