# Rendering Flicker Validation

## Purpose
GDF events coincide with some lighting steps, but fixed screen rectangles and old screenshot naming confound attribution. A world-surface comparison with explicit event causes is required.

## ADDED Requirements

### Requirement: Boundary intervention
The system SHALL a causal claim requires the lighting discontinuity to follow the crossing rather than the original frame.

#### Scenario: Boundary intervention control
Given the same camera path can run with different grid phases
When the clipmap crossing moves to another render frame
Then a causal claim requires the lighting discontinuity to follow the crossing rather than the original frame.

### Requirement: Valid motion metric
The system SHALL invalid samples are excluded and insufficient coverage invalidates the comparison.

#### Scenario: Valid motion metric control
Given a tracked surface is occluded or exits clipmap coverage
When event residuals are computed
Then invalid samples are excluded and insufficient coverage invalidates the comparison.

### Requirement: Reproducible scoped conclusions
The system MUST retain manifests, exact full-resolution measurements, independent repeated runs and scoped hypothesis decisions.

#### Scenario: Confounded or incomplete evidence
Given a control changes a fallback, exposure, geometry coverage or capture alignment
When the hypothesis ledger is evaluated
Then the confound SHALL be recorded and the result MUST NOT be accepted as isolated causal evidence.
