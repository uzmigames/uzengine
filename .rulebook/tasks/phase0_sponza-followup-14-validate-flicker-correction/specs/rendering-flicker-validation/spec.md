# Rendering Flicker Validation

## Purpose
A correction must address a demonstrated source while preserving brightness, sharpness, temporal response and rendering performance. Darkening shadows or suppressing jitter is not a sufficient success criterion.

## ADDED Requirements

### Requirement: Correction acceptance
The system SHALL all preregistered gates must pass before defaults change.

#### Scenario: Correction acceptance control
Given a candidate reduces the demonstrated flicker
When energy, detail, bright-side stability, motion recovery and cost are checked
Then all preregistered gates must pass before defaults change.

### Requirement: Honest completion
The system SHALL implementation is not marked complete and the next evidence-producing step remains tracked.

#### Scenario: Honest completion control
Given a candidate fails a gate or attribution remains inconclusive
When the task status is updated
Then implementation is not marked complete and the next evidence-producing step remains tracked.

### Requirement: Reproducible scoped conclusions
The system MUST retain manifests, exact full-resolution measurements, independent repeated runs and scoped hypothesis decisions.

#### Scenario: Confounded or incomplete evidence
Given a control changes a fallback, exposure, geometry coverage or capture alignment
When the hypothesis ledger is evaluated
Then the confound SHALL be recorded and the result MUST NOT be accepted as isolated causal evidence.
