## ADDED Requirements

### Requirement: Current-source Windows diagnostic baseline

The verification workflow SHALL capture initial Windows rendering diagnostics only after a current build and the relevant gates; retain executable/source hashes and exact capture settings.

#### Scenario: Required stabilization evidence

Given the current source tree and the prerequisites for audit item 1.5
When the affected build or verification command runs
Then its output and exit status MUST distinguish accepted evidence from failure or unavailable prerequisites, and retain reproducible evidence for each claimed result.
