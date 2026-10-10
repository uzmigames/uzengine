- [x] 1.1 Repair extension lint and add meaningful unit coverage for existing testable behavior; classify manual native harnesses explicitly.
- [x] 1.2 Fail empty required suites and absent documentation fixtures; generate required MCP fixtures in CI.
- [x] 1.3 Install compatible coverage providers, execute type checks/lint/tests/coverage and record native failures under their existing owner.
- [x] 2.1 Update documentation with implemented behavior, commands, evidence and remaining limits.
- [x] 2.2 Write meaningful regression tests and negative controls for the changed behavior.
- [x] 2.3 Run tests and confirm they pass within this task's scope: complete extension/MCP type, lint, format and coverage gates plus changed validation regressions; record native failures under their existing owner before closure.

## Implementation evidence — 2026-10-10

Extension/MCP type checks, builds, lint, format and complete coverage gates pass:
116 extension and 88 MCP tests. All four global coverage metrics exceed the
unchanged 95% thresholds. Empty-suite and missing-fixture negative controls fail
as required. The current generated docs fixture and compiled stdio server were
also verified. Only test programs/declarations are excluded from extension
production coverage; every production module remains included.

[Implementation, commands and evidence](../../../docs/analysis/project-stabilization/07-baseline-implementation.md). Logs and machine-readable artifacts: `.sandbox/stabilization/`.

The final native follow-up finished: 7,545 passed, 84 failed, 12 skipped out of
7,641 tests. Two environment-dependent test assertions were corrected; all 29
targeted capture/VFX tests pass. The existing native-failure owner retains the
remaining failures; this task closes test-integrity implementation, not native
suite acceptance. Claude reviewed the limited closure scope read-only.
See [final results](../../../docs/analysis/project-stabilization/09-test-coverage-and-first-frames.md).
