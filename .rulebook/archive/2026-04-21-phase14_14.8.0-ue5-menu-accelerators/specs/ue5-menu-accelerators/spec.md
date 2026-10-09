# ue5-menu-accelerators

## ADDED Requirements

### Requirement: Underline the accelerator character

When a menu item's `acceleratorIndex >= 0`, the render SHALL emit
a 1-px `textPrimary` rectangle below the baseline of that
character, spanning exactly its advance width.

#### Scenario: File label renders with `F` underlined

Given the menu entry `{ label = "File", acceleratorIndex = 0 }`
When the widget renders
Then the emitted 1-px rect has `x = label.x`, `y = label.y + h`
and `w = glyphAdvanceWidth('F')` within 1 px tolerance.

### Requirement: Alt+key opens the matching menu

`HandleKey(ascii, Alt)` SHALL open the top-level menu whose
accelerator character matches `ascii` (case-insensitive).

#### Scenario: Alt+F opens the File menu

Given the menu bar is not currently open
When `HandleKey('F', Alt)` is invoked
Then `m_openMenu` equals the File menu index and the key event
is marked as swallowed.

### Requirement: In-dropdown letter key fires item

While a dropdown is open, `HandleKey(letter)` without Alt SHALL
fire the first item whose `acceleratorIndex` points to the
matching character and close the dropdown.

#### Scenario: `A` in the File menu fires "Save As"

Given the File menu is open with Save As at
`acceleratorIndex = 5` over label "Save As"
When `HandleKey('A', NoMods)` is invoked
Then `onMenuCommand(Cmd_SaveAs)` fires once and the dropdown
closes.
