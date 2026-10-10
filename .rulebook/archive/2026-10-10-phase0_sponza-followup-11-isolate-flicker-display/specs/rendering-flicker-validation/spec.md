# Rendering Flicker Validation

## Purpose
Turning reflections off changed the scene brightness and automatic exposure, confounding PNG threshold counts. Exposure weighting also reduced dark-side counts while destabilizing bright pixels.

## ADDED Requirements

### Requirement: Presentation isolation
The system SHALL upstream linear temporal error remains unchanged and display threshold changes are reported as presentation effects.

#### Scenario: Presentation isolation control
Given one recorded HDR sequence
When only exposure or bloom is changed
Then upstream linear temporal error remains unchanged and display threshold changes are reported as presentation effects.

### Requirement: Balanced acceptance
The system SHALL a bright-side regression prevents acceptance even if one display metric improves.

#### Scenario: Balanced acceptance control
Given pre-exposed weights reduce dark-side threshold crossings
When bright-side linear stability and detail are measured
Then a bright-side regression prevents acceptance even if one display metric improves.

### Requirement: Reproducible scoped conclusions
The system MUST retain manifests, exact full-resolution measurements, independent repeated runs and scoped hypothesis decisions.

#### Scenario: Confounded or incomplete evidence
Given a control changes a fallback, exposure, geometry coverage or capture alignment
When the hypothesis ledger is evaluated
Then the confound SHALL be recorded and the result MUST NOT be accepted as isolated causal evidence.
