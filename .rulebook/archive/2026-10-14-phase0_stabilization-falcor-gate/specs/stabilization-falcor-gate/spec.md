## ADDED Requirements

### Requirement: Strict Falcor acceptance gate

The verification workflow SHALL require matching image dimensions and capture metadata, preserve reference images, and accept only valid PSNR scores at or above 35 dB.

#### Scenario: Required stabilization evidence

Given the current source tree and the prerequisites for audit item 1.4
When the affected build or verification command runs
Then its output and exit status MUST distinguish accepted evidence from failure or unavailable prerequisites, and retain reproducible evidence for each claimed result.
