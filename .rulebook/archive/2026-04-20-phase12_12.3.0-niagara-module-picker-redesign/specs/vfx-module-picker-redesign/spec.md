# vfx-module-picker-redesign

## ADDED Requirements

### Requirement: Search bar hit target

The dialog SHALL emit exactly one `SearchInput` layout item at the top
of the layout, spanning the full dialog width minus padding.

#### Scenario: SearchInput precedes tabs and tiles

Given an open dialog
When `ComputeLayout` runs
Then the first layout item has `type == SearchInput`, and every
subsequent tab or tile item comes after it.

### Requirement: Category tabs carry count badges

Each `CategoryTab` layout item SHALL render the category name
followed by a middle-dot separator and the number of modules whose
`category == tab.label`.

#### Scenario: Tab label includes count

Given a dialog opened on a stage with 4 velocity modules and 2 color
modules in the metadata table
When the Velocity tab is laid out
Then its label text contains "Velocity" and "4".

### Requirement: Tile grid adapts to dialog width

The dialog SHALL lay out tiles in 3 columns when `m_boundsWidth < 600`
and in 4 columns when `m_boundsWidth >= 600`.

#### Scenario: 4-column layout at 720 width

Given a dialog with `m_boundsWidth = 720`
When `ComputeLayout` runs
Then adjacent `ModuleTile` items on the same row have `rect.x` values
indicating 4 columns.

### Requirement: Selected tile has accent border

The selected tile SHALL receive a 2-px accent border in addition to
the usual selected background shade.

#### Scenario: Selected border rendered

Given a dialog with one selected tile
When `Render` runs against a capture-enabled mock renderer
Then 4 accent-colored rectangles (one per edge) bounding the selected
tile are present in the emitted rect list.

### Requirement: Create button disabled when no selection

The `Footer` `ButtonCreate` sub-rect SHALL render with `textDisabled`
color and ignore clicks when `m_selectedIndex < 0`.

#### Scenario: Disabled button swallows clicks

Given a dialog with no tile selected
When a click is dispatched on the Create button region
Then `onModuleChosen` is not invoked.

### Requirement: Metadata description + icon glyph are populated

Every entry in the built-in `VfxModuleMetadata` table SHALL have a
non-empty `description` string (>= 10 characters) and a non-empty
`iconGlyph` string.

#### Scenario: Metadata completeness at init

Given the built-in metadata table
When any entry is inspected
Then both `description.length() >= 10` and `iconGlyph.empty() == false`
hold.
