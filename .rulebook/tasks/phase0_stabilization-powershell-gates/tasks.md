- [x] 1.1 Centralize repository discovery and preserve the caller's working directory and PATH.
- [x] 1.2 Cover engine/editor/platform/tools/tests sources; use compilation databases for native lint and fail missing prerequisites.
- [x] 1.3 Verify root/unrelated working directories and checkout paths with spaces, including a controlled failing source and failing command.
- [x] 2.1 Update documentation with implemented behavior, commands, evidence and remaining limits.
- [x] 2.2 Write meaningful regression tests and negative controls for the changed behavior.
- [x] 2.3 Run tests and confirm they pass; record actual outcomes before closure.

## Implementation evidence — 2026-10-10

The five controlled-tool regressions pass for repository discovery, all four entry points, paths with spaces, empty/invalid compilation databases and native/TypeScript failures. This closes script semantics; real clang tool acceptance is not claimed.

[Implementation, commands and evidence](../../../docs/analysis/project-stabilization/07-baseline-implementation.md). Logs and machine-readable artifacts: `.sandbox/stabilization/`.
