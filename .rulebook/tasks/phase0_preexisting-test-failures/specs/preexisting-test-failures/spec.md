# Own and fix the pre-existing test_runtime failures

## Purpose
Own and fix the pre-existing test_runtime failures.

## ADDED Requirements

### Requirement: Own and fix the pre-existing test_runtime failures
The test suite SHALL report no failure without a named owner, and every previously "pre-existing" failure SHALL be fixed or carry a recorded reason.

#### Scenario: The gate
Given a full test_runtime run
When its failures are listed
Then each one is either fixed or listed with an owner and a reason.
