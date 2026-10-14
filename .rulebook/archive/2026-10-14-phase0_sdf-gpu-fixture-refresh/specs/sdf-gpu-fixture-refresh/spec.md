# Refresh the SDF GPU fixtures ported in parity-03/04

## Purpose
Refresh the SDF GPU fixtures ported in parity-03/04.

## ADDED Requirements

### Requirement: Refresh the SDF GPU fixtures ported in parity-03/04
The SDF GPU fixtures SHALL bind the layouts the production shaders declare.

#### Scenario: The gate
Given the fixtures and the shaders at HEAD
When the runners execute
Then they pass and -BvhCompare reports the bakes bit-identical.
