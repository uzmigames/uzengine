# Rendering Flicker Validation

## Purpose
Previous PNGs were shifted by one render frame and sparse sampling over-counted shadow changes. Trustworthy elimination tests require verified frame identity, readback lifetime, coordinate space and radiometric units.

## ADDED Requirements

### Requirement: Frame integrity
The system SHALL every record matches its rendered frame and incomplete readbacks invalidate the run.

#### Scenario: Frame integrity control
Given a known GPU frame stamp and delayed readback
When consecutive captures are collected under GPU load
Then every record matches its rendered frame and incomplete readbacks invalidate the run.

### Requirement: Measurement validity
The system SHALL known amplitudes and periods are recovered and pure translation is not classified as lighting flicker.

#### Scenario: Measurement validity control
Given constant, periodic and translated synthetic sequences
When the analysis processes them at full resolution
Then known amplitudes and periods are recovered and pure translation is not classified as lighting flicker.

### Requirement: Reproducible scoped conclusions
The system MUST retain manifests, exact full-resolution measurements, independent repeated runs and scoped hypothesis decisions.

#### Scenario: Confounded or incomplete evidence
Given a control changes a fallback, exposure, geometry coverage or capture alignment
When the hypothesis ledger is evaluated
Then the confound SHALL be recorded and the result MUST NOT be accepted as isolated causal evidence.
