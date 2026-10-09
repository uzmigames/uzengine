# vfx-menu-bar-redesign

## ADDED Requirements

### Requirement: Menu command dispatch is a single entry point

All menu item activations SHALL fire `onMenuCommand(VfxMenuCommand)`
with a single command enum value. No direct state mutation happens
inside the overlay; the host translates commands to actions.

#### Scenario: Save menu item fires File_Save

Given an overlay with `onMenuCommand` bound and the File menu open
When the user clicks the "Save" item
Then the callback is invoked exactly once with `VfxMenuCommand::File_Save`.

### Requirement: Dropdown items include hotkey hint column

Every non-separator dropdown item SHALL reserve a right-aligned
column for its hotkey hint string. The column width SHALL be
consistent across items within a single dropdown so hints align
vertically.

#### Scenario: Undo and Redo hints align

Given the Edit menu open with Undo ("Ctrl+Z") and Redo ("Ctrl+Y")
When `Render` runs
Then the x coordinate of the first character of both "Ctrl+Z" and
"Ctrl+Y" is identical.

### Requirement: Separator rows render as 1-px dividers

A `VfxMenuItem` flagged as separator SHALL render as a 1-px
`borderSubtle` horizontal line across the dropdown width, with
6-px vertical padding above and below.

#### Scenario: Separator rect is 1-px tall

Given an Edit menu with a separator between Cut and Paste
When `Render` runs
Then at least one rect in the emitted list has `height == 1.0` and
`color == borderSubtle`.

### Requirement: Disabled items are not hit-testable

An item marked with `enabled == false` SHALL render with
`textDisabled` color and clicking its rect SHALL NOT fire
`onMenuCommand`.

#### Scenario: Disabled Undo swallows click

Given a disabled Undo item (no undoable commands)
When a click is dispatched on the Undo row
Then `onMenuCommand` is not invoked.

### Requirement: Emitter tab bar shows per-emitter tabs

The emitter bar SHALL emit exactly one `EmitterTab` layout item per
emitter in the bound system, plus one `AddEmitterButton` at the end.

#### Scenario: 3 emitters yield 4 items

Given an overlay bound to a system with 3 emitters
When the emitter bar layouts
Then the emitted items are 3 `EmitterTab` items + 1 `AddEmitterButton`
in that order.

### Requirement: Active emitter tab has accent underline

The tab at `m_activeEmitterIdx` SHALL render a 2-px `accentPrimary`
rectangle along its bottom edge.

#### Scenario: Active tab underline emitted

Given `m_activeEmitterIdx = 1` with 3 emitters
When `Render` runs
Then the rect list contains a 2-px-tall `accentPrimary` rect whose
x spans the second emitter tab only.
