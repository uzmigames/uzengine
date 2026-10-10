# Proposal: phase0_sponza-parity-06-qa-harness-validation

## Why

Tasks 01–05 port the renderer changes. This task ports the **evidence infrastructure** SponzaTest
built around them (commit `25cd0c5f`, "quality presets and testing handoff") and closes the loop the
user asked for on 2026-09-07: the manual calibration was removed and backed up
(`sample/reference/sponza-manual-calibration/`) on the condition that we compare and restore it if the
ported pipeline looks worse.

SponzaTest's own handoff states the limits that must be carried over honestly: no Lumen parity,
no Surface Cache, no complete emissive transport, Falcor PSNR ≥ 35 dB unverified, DXR GI evidence
invalid on a Tier 1.0 GPU (`DDGI_ProbeUpdate` failed with 0x80070057), aggregate build failing with
23 pre-existing `test_runtime` errors, ≈1 FPS at maximum quality on a GTX 1660 SUPER.

## What Changes

- QA content: `sample/content/scenes/sponza-gi-camera-c.qa.uzscene`; `scripts/rendering/sponza-lighting-camera-c.uzproject`,
  `scripts/sponza-temporal-camera.{uzproject,uzscene,lua}`, `scripts/rendering/test_sponza_temporal_camera.lua`.
- Presets and tooling: `scripts/rendering/run_sponza_max_quality.bat`, `run_sponza_dxr.bat`,
  `compare_sponza_stationary.ps1`, `verify_sponza_loading.ps1`, `sponza_dxr_capabilities.cpp`,
  `test_editor_navigation.{cpp,ps1}`; `.gitignore` entries for capture/log/hash artifacts.
- Reports (8) into `docs/analysis/rendering/` with an origin header (branch + commit):
  `sponza-gi-correction-2026-09-06.md`, `sponza-surface-lighting-2026-09-07.md`,
  `sponza-loading-exposure-2026-09-07.md`, `sponza-local-reflections-2026-09-07.md`,
  `sponza-sdf-orientation-evidence.md`, `sponza-sun-rotation-evidence.md`,
  `sponza-graph-material-evidence.md`, `sponza-test-handoff.md`.
- `docs/analysis/known-issues-and-partial-implementations.md`: correct the "SDF GI functional" claim,
  add SponzaTest's unresolved list. `docs/analysis/hardcoded/08-scene-specific-code.md` and
  `01-rendering-defaults.md`: re-audit Sponza-tuned defaults after the port.
- Final validation and the keep/restore decision on the manual calibration.
- Supersede `phase1_fix-sponza-lighting-calibration` (its four code items already landed earlier;
  its verification tail is replaced by this task).

Explicitly **out of scope** (not visual on Windows): the Slang/Metal shader pipeline
(`shader_manager.cpp` +123, `metal_*`), macOS thirdparty ignores, FFmpeg/Slang vendor refreshes.

## Impact
- Affected specs: none
- Affected code: scripts, sample QA scenes, docs; one archive
- Breaking change: NO
- User benefit: repeatable A/C/temporal captures, documented evidence, a recorded decision on
  whether the neutral pipeline beats the hand calibration
