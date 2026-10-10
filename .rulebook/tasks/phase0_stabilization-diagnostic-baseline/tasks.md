- [x] 1.1 Build current runtime and diagnostic consumer with identified dependency inputs after baseline build prerequisites.
- [x] 1.2 Capture first frames and a representative scene with supported diagnostic flags, recording GPU/driver, scene/assets, camera, dimensions, CVars and frames.
- [x] 1.3 Retain diagnostic artifacts and explicit unavailable cases; do not accept stale binaries or missing matched Falcor references.
- [x] 2.1 Update documentation with implemented behavior, commands, evidence and remaining limits.
- [x] 2.2 Write meaningful regression tests and negative controls for the changed behavior.
- [x] 2.3 Run tests and confirm they pass within this task's scope: capture regressions and CLI negative controls; verify actual PNGs and record full native outcomes separately before baseline closure.

## Implementation evidence — 2026-10-10

Current-source DX12 PBR Showcase capture produced all nine requested frames 0–8,
including five preload frames, with exit 0 on RTX 4090. A control without preload
opt-in produced only frames 5–8. Executable/source/dependency hashes, GPU/driver,
camera, effective CVars, asset hashes and per-image dimensions/integrity are retained.
All four capture regressions and both real CLI negative controls pass.

[Implementation, commands and evidence](../../../docs/analysis/project-stabilization/07-baseline-implementation.md). Logs and machine-readable artifacts: `.sandbox/stabilization/`.

The locked source-only runtime/DLL and addon compile. The final full native suite
has 84 failures and 12 skips owned by phase0_preexisting-test-failures. Render-state
warnings, WARN_ZERO targets and unsupported/disabled diagnostic targets remain
observations, not verified-OK rendering. Stage 0 requires this initial diagnostic
baseline; final matched Falcor and sequence acceptance remains umbrella item 4.1.
The earlier closure note conflated these scopes. Claude's read-only review agreed
with this reconciliation. See [final evidence and limits](../../../docs/analysis/project-stabilization/09-test-coverage-and-first-frames.md).
