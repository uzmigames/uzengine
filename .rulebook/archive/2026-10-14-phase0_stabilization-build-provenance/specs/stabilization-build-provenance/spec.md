## ADDED Requirements

### Requirement: Reproducible build and source provenance

The verification workflow SHALL align the C++23/Zig contract, retain npm lockfiles, inventory native dependency inputs and record current-source build provenance.

#### Scenario: Required stabilization evidence

Given the current source tree and the prerequisites for audit item 1.1
When the affected build or verification command runs
Then its output and exit status MUST distinguish accepted evidence from failure or unavailable prerequisites, and retain reproducible evidence for each claimed result.
