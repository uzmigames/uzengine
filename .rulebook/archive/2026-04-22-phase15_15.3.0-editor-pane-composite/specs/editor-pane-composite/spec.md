# editor-pane-composite

## ADDED Requirements

### Requirement: Tab strip docks above body

`VfxEditorPaneWidget::Render(bounds)` SHALL emit a 28-px
`VfxTabStripWidget` at the top of `bounds` and render the active
sub-panel in the remaining region.

#### Scenario: Body rect has correct geometry

Given `bounds = {640, 50, 640, 500}`
When `Render` runs
Then the tab strip occupies `{640, 50, 640, 28}` and the body
rect handed to the sub-panel equals `{640, 78, 640, 472}`.

### Requirement: ActiveTab defaults to Stack

Newly-constructed `VfxEditorPaneWidget` SHALL report
`ActiveTab() == VfxEditorPaneTab::Stack`.

#### Scenario: Default tab

Given a fresh widget
When `ActiveTab()` is called
Then the return value equals `VfxEditorPaneTab::Stack`.

### Requirement: onTabChanged fires on tab switch

`SetActiveTab(newTab)` MUST fire `onTabChanged(newTab)` when
`newTab != ActiveTab()`. It MUST NOT fire when `newTab ==
ActiveTab()`.

#### Scenario: Same-tab set does not fire callback

Given `ActiveTab() == Stack`
When `SetActiveTab(Stack)` runs
Then `onTabChanged` is NOT invoked.
