# The final gather's energy deficit and gather-side coupling

## Purpose
The final gather's energy deficit and gather-side coupling.

## ADDED Requirements

### Requirement: The final gather's energy deficit and gather-side coupling
The final gather's level SHALL be judged against a reference whose own convergence is measured.

#### Scenario: The gate
Given the reference and the shipping gather
When both are captured on the current build
Then the deficit is reported with the reference's uncertainty.
