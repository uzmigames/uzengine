## ADDED Requirements

### Requirement: Required test and fixture integrity

The verification workflow SHALL separate unit tests from editor/native integration harnesses, fail empty required suites and absent fixtures, repair lint, and install coverage providers without reducing thresholds.

#### Scenario: Required stabilization evidence

Given the current source tree and the prerequisites for audit item 1.3
When the affected build or verification command runs
Then its output and exit status MUST distinguish accepted evidence from failure or unavailable prerequisites, and retain reproducible evidence for each claimed result.
