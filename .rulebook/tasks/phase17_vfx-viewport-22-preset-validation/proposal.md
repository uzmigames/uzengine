# Proposal: phase17_vfx-viewport-22-preset-validation

## Why

`vfx_viewport_app` exposes 22 in-code presets via Tab / Left/Right
cycling (`kPresetFns[]` in `vfx_viewport_app.cpp:128-136`). Today there
is NO automated validation that any of them render correctly. The
team has discovered (during 2026-05-03 debugging) that:

- Preset 0 ("FireballProj") is broken because it depends on a missing
  `.uzvfxsys` compound (handled by
  `phase17_vfx-viewport-default-scene-no-uzvfxsys`).
- Preset 2 ("Smoke") has a wrong color (handled by
  `phase17_vfx-viewport-smoke-color-fix`).
- The remaining 20 presets have not been visually inspected since
  before the 2026-04 mesh-particle-render-path RC3 work + the
  bindless-attr-read bug. Some may be silently broken too (zero
  particles visible, wrong color, wrong scale, etc.).

This task establishes the validation baseline: capture a reference
screenshot for each of the 22 presets at a fixed frame, store them
as artifacts, and write a regression script that diffs new captures
against the baseline. Without this baseline, future changes (engine
upgrades, preset retunes, render-path refactors) silently break
presets and the team only discovers it visually months later.

## What Changes

### Capture baseline

1. Run `vfx_viewport_app.exe` in default mode (no `--vfx-asset`).
2. For each `i` in `0..21`:
   - Programmatically force `m_currentPreset = i`, trigger
     `m_switchPresetPending = true` in code OR drive the demo via
     a CLI flag `--start-preset N` that sets `m_currentPreset` at
     `OnStart`.
   - Wait until `frame == 120` (covers ~2s of steady-state emission).
   - Capture screenshot to
     `e:/UzmiEngine/uzengine/.sandbox/preset-baseline/<NN>-<name>.png`.

### Add CLI flag

3. Add `--start-preset <name|index>` CLI argument to the demo so
   external scripts can drive specific presets without keyboard
   simulation. Index OR name (e.g., `--start-preset Smoke`) works.

### Add validation script

4. Create `scripts/vfx_validate_presets.ps1` (PowerShell) that:
   - Iterates the 22 preset indices.
   - Launches `vfx_viewport_app.exe` with `--start-preset N
     --headless --screenshot <out>.png --screenshot-frame 120
     --frames 130`.
   - Compares each output against the baseline using PSNR (existing
     `--diff` utility from `DemoApplication`).
   - PSNR >= 35 dB = pass, < 35 dB = fail.
   - Prints a summary table of pass/fail for all 22 presets.

5. Wire the script into the rulebook test workflow (mention in
   `tasks.md` that running it is part of the tail).

### Document the contract

6. Add `docs/engine/25-vfx/85-preset-validation.md` documenting the
   baseline location, how to regenerate, and the PSNR threshold.

## Impact

- Affected specs: `vfx-viewport-22-preset-cycling` (NEW spec).
- Affected code: `runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp`
  (CLI parse + start-preset wire), `scripts/vfx_validate_presets.ps1`
  (NEW), `docs/engine/25-vfx/85-preset-validation.md` (NEW).
  Baseline screenshot artifacts in
  `e:/UzmiEngine/uzengine/.sandbox/preset-baseline/`.
- Breaking change: NO. New CLI flag is additive.
- User benefit: regression detection for all 22 presets at once.
  Future engine refactors (RHI changes, render-graph reordering,
  bindless plumbing) get caught immediately when they break a
  preset, not weeks later when a user reports it.

## Dependencies

- Prerequisite: `phase17_vfx-viewport-default-scene-no-uzvfxsys`
  (cannot baseline preset 0 while it's broken).
- Prerequisite: `phase17_vfx-viewport-smoke-color-fix` (don't want
  to baseline a known-broken Smoke; baseline AFTER the fix lands).
- Recommended after: `phase17_vfx-viewport-overlay-asset-mode` (so
  the validation screenshots show the editor overlay if relevant -
  though baseline can be `--no-overlay` if a CLI flag is added).
