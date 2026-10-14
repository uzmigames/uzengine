- [x] 1.1 Reject missing/unreadable images, incompatible or missing metadata and dimension mismatches.
- [x] 1.2 Reject every score below 35 dB and invalid scores; propagate capture failure and required-comparison failure to process exit.
- [x] 1.3 Verify 24.9, 25, 34.9, 35 dB, identical images and missing/mismatched/stale evidence with negative controls.
- [x] 2.1 Update documentation with implemented behavior, commands, evidence and remaining limits.
- [x] 2.2 Write meaningful regression tests and negative controls for the changed behavior.
- [x] 2.3 Run tests and confirm they pass; record actual outcomes before closure.

## Implementation evidence — 2026-10-10

All 13 gate regressions pass, including thresholds before rounding, invalid/missing/stale metadata, mismatched dimensions, real CLI exit codes and protection against report overwrites. No matched GPU pair or Falcor PSNR pass is claimed.

[Implementation, commands and evidence](../../../docs/analysis/project-stabilization/07-baseline-implementation.md). Logs and machine-readable artifacts: `.sandbox/stabilization/`.
