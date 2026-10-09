# menu-accelerator-input

## ADDED Requirements

### Requirement: Alt+letter opens matching top menu

`VfxMatchAccelerator(ascii)` SHALL return the index of the first
top menu whose `acceleratorIndex` character equals `ascii`
(case-insensitive), or -1 when no menu matches.

#### Scenario: Alt+F matches File

Given the shipped menu bar (File/Edit/View/Presets/Help)
When `VfxMatchAccelerator('F')` is called
Then the return value equals 0 (File menu index).

#### Scenario: Unknown letter returns -1

Given the shipped menu bar
When `VfxMatchAccelerator('Z')` is called
Then the return value equals -1.

### Requirement: Bare letter fires dropdown item

`VfxMatchAcceleratorInMenu(ascii, items, count)` SHALL return the
first item index whose `acceleratorIndex` character equals `ascii`
AND whose `enabled == true` AND whose `isSeparator == false`.

#### Scenario: `A` matches Save As in File menu

Given the File menu item list
When `VfxMatchAcceleratorInMenu('A', fileItems, count)` is called
Then the return value equals the index of the "Save As" row
(acceleratorIndex == 5).

#### Scenario: Separator rows never match

Given a menu item list containing only separators + disabled rows
When any `VfxMatchAcceleratorInMenu` call runs
Then the return value equals -1.
