# vfx-context-menu-system

## ADDED Requirements

### Requirement: Open/Close lifecycle is idempotent

`VfxContextMenuWidget::Open` SHALL set `IsOpen() == true` regardless
of prior state; `Close` SHALL set it to `false`. Opening while open
replaces the current anchor + items.

#### Scenario: Re-open replaces items

Given an open menu with 3 items
When `Open(x, y, newItems={1, 2})` is called
Then `IsOpen() == true` and layout reports exactly 2 rows.

### Requirement: Anchor flip on overflow

When the computed bottom-right corner of the menu would exceed the
screen, `Open` SHALL flip the menu to anchor at the opposite edge.

#### Scenario: Right edge overflow flips menu left

Given `screenW = 1280`, anchor at `x = 1260`, menu width 200
When `Open` runs
Then the menu's final `rect.x < 1260` (flipped left of anchor).

### Requirement: Escape dismisses

The handler SHALL expose a `ProcessKey(Key)` method that closes the
menu when `Key::Escape` is passed.

#### Scenario: Escape closes menu

Given an open menu
When `ProcessKey(Key::Escape)` is called
Then `IsOpen() == false`.

### Requirement: Submenu opens on hover timeout

Hovering an item whose `submenu` is non-empty for >= 200 ms SHALL
cause the widget to render a nested menu anchored at the item's right
edge.

#### Scenario: Submenu renders after 200ms hover

Given an item with a 2-entry submenu
When `Tick(0.25)` is called while the item is hovered
Then the nested menu is part of `ComputeLayout`'s returned items.

### Requirement: Separator rows are 1-px horizontal lines

`VfxContextMenuItem::isSeparator == true` SHALL render as a 1-px
`borderSubtle` line.

#### Scenario: Separator rect emission

Given a menu with one separator between two items
When `Render` runs
Then at least one rect with `height == 1.0` and `color == borderSubtle`
is emitted.

### Requirement: Disabled items swallow clicks

Items with `enabled == false` SHALL render with `textDisabled` and
NOT invoke `onActivate` when clicked.

#### Scenario: Disabled click noops

Given a menu with a disabled "Delete" item (counter starts at 0)
When a click is dispatched on the "Delete" row
Then the counter remains 0 and the menu stays open.
