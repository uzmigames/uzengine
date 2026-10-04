# Rendering Flicker Validation

## Purpose
The instrumented window does not represent all reported shadows. Separate regional measurements must distinguish reflection noise, diffuse gather, probe updates, AO and geometry instability.

## ADDED Requirements

### Requirement: Independent shadow attribution
The system SHALL each ROI has its own evidence-backed attribution or an explicit inconclusive result.

#### Scenario: Independent shadow attribution control
Given the rear door and right shadow have registered world-space ROIs
When individual lighting components are instrumented and controlled
Then each ROI has its own evidence-backed attribution or an explicit inconclusive result.

### Requirement: Fallback awareness
The system SHALL the fallback, brightness and exposure changes are recorded and cannot be misreported as isolated noise reduction.

#### Scenario: Fallback awareness control
Given a GI or reflection switch changes the active lighting path
When before/after statistics are compared
Then the fallback, brightness and exposure changes are recorded and cannot be misreported as isolated noise reduction.

### Requirement: Reproducible scoped conclusions
The system MUST retain manifests, exact full-resolution measurements, independent repeated runs and scoped hypothesis decisions.

#### Scenario: Confounded or incomplete evidence
Given a control changes a fallback, exposure, geometry coverage or capture alignment
When the hypothesis ledger is evaluated
Then the confound SHALL be recorded and the result MUST NOT be accepted as isolated causal evidence.
