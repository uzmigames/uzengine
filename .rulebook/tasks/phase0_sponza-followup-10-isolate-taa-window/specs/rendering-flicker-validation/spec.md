# Rendering Flicker Validation

## Purpose
Window flicker follows jitter, while isolated SDF reflection energy is small. The experiment must locate whether coverage, reconstruction, history clipping or blending introduces the residual instead of assuming the clamp is its cause.

## ADDED Requirements

### Requirement: Stage localization
The system SHALL the report identifies the earliest excessive error relative to the reference without counting expected coverage changes as a defect.

#### Scenario: Stage localization control
Given a static high-contrast edge with known sample positions
When reconstruction, clipping and blending are varied separately
Then the report identifies the earliest excessive error relative to the reference without counting expected coverage changes as a defect.

### Requirement: Temporal robustness
The system SHALL ghosting and recovery are measured and unsafe history reuse cannot qualify as a correction.

#### Scenario: Temporal robustness control
Given a diagnostic variant improves static variance
When the camera reveals new surfaces or cuts
Then ghosting and recovery are measured and unsafe history reuse cannot qualify as a correction.

### Requirement: Reproducible scoped conclusions
The system MUST retain manifests, exact full-resolution measurements, independent repeated runs and scoped hypothesis decisions.

#### Scenario: Confounded or incomplete evidence
Given a control changes a fallback, exposure, geometry coverage or capture alignment
When the hypothesis ledger is evaluated
Then the confound SHALL be recorded and the result MUST NOT be accepted as isolated causal evidence.
