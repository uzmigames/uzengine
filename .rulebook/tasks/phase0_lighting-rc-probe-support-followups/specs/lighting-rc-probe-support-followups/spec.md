# Radiance-cache probe support in the shipped configuration

## Purpose
Radiance-cache probe support in the shipped configuration.

## ADDED Requirements

### Requirement: Radiance-cache probe support in the shipped configuration
The shipped radiance-cache configuration SHALL have a pass/fail regression harness.

#### Scenario: The gate
Given sample/projects/sponza.uzproject's probe settings
When the closing sweep runs
Then the configuration's registered gates are checked.
