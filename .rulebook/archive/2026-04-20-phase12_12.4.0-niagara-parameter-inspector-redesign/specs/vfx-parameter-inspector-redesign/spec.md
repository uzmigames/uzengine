# vfx-parameter-inspector-redesign

## ADDED Requirements

### Requirement: Parameters grouped by section

The inspector SHALL emit one `Section` layout item for every distinct
`sectionName` found in the bound store, followed by the `ParamRow`
items of that section. Parameters with an empty / missing section
name group under "General".

#### Scenario: Distinct sections are emitted once

Given a store with 3 parameters (sections: "Spawn", "Spawn", "Render")
When `ComputeLayout` runs
Then exactly 2 `Section` items appear ("Spawn" and "Render"), each
followed by its own `ParamRow` items.

### Requirement: Section collapse hides rows

When a section's expanded state is `false`, `ComputeLayout` SHALL omit
all `ParamRow` items belonging to that section.

#### Scenario: Collapsed section has no rows

Given a section "Spawn" with 4 parameters and collapse state `false`
When `ComputeLayout` runs
Then the layout contains the `Section` item for "Spawn" but no
`ParamRow` items whose section equals "Spawn".

### Requirement: Two-column layout with 40/55/5 split

Each `ParamRow` SHALL split its width into `LabelRect` ~40%, `EditorRect`
~55%, and `ActionsRect` ~5%. Tolerance is +/- 2 pixels.

#### Scenario: Layout split at 400-wide row

Given a `ParamRow` with total width 400
When layout runs
Then `LabelRect.width` is in `[158, 162]`, `EditorRect.width` is in
`[218, 222]`, and `ActionsRect.width` is in `[18, 22]`.

### Requirement: Type-specific editor kinds emitted

The inspector SHALL emit a concrete editor layout item matching the
parameter's declared type (`EditorFloat` for float parameters,
`EditorVec3` for vec3, `EditorColor` for color, `EditorBool` for bool,
`EditorEnum` for enum, `EditorCurve` for curve binding, `EditorRamp`
for ramp binding, `EditorString` for string).

#### Scenario: Float parameter emits EditorFloat

Given a bound store with a float parameter named "lifetime"
When `ComputeLayout` runs
Then the layout contains exactly one `EditorFloat` item whose
`paramName` equals "lifetime".

### Requirement: Reset button fires callback

Clicking the `ResetToDefault` sub-rect SHALL fire
`onParameterReset(scope, name)` with the parameter's scope and name.

#### Scenario: Reset click fires callback

Given an inspector with `onParameterReset` bound
When the host dispatches a `ResetToDefault` hit
Then the callback receives the hit parameter's scope + name.

### Requirement: Out-of-range values render error border

When a numeric parameter's current value falls outside the registered
`[min, max]` range, `Render` SHALL draw a 1-px `stateError` border
around that parameter's `EditorRect`.

#### Scenario: Out-of-range renders red border

Given a float parameter "x" with range `[0, 1]` and value `1.5`
When `Render` runs against a capture-enabled mock
Then 4 `stateError` rects matching the editor's edges are present.
