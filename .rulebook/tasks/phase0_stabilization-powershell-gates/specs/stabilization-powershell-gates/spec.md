## ADDED Requirements

### Requirement: Reliable PowerShell quality gates

The verification workflow SHALL resolve all gate paths from the script, inspect current engine sources using actual compilation configuration, and propagate required-tool and command failures.

#### Scenario: Required stabilization evidence

Given the current source tree and the prerequisites for audit item 1.2
When the affected build or verification command runs
Then its output and exit status MUST distinguish accepted evidence from failure or unavailable prerequisites, and retain reproducible evidence for each claimed result.
