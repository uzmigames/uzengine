<!-- doc-meta
title: VFX Preset Validation
description: Baseline + PSNR-diff regression suite for the 22 in-code vfx_viewport_app presets.
date: 2026-05-03
-->

# VFX Preset Validation

Catches visual regressions in the 22 in-code presets (`kPresetFns[]`) of
`runtime/demos/vfx_viewport_app` by comparing each preset's screenshot
against a stored baseline using PSNR.

@ref `phase17_vfx-viewport-22-preset-validation`

## Why

Future engine refactors (RHI changes, render-graph reordering, bindless
plumbing, shader edits) can silently break individual presets without
any test failure. Bugs only surface weeks later when a user reports
"this preset doesn't render anymore". Capturing a per-preset baseline
+ PSNR-diff catches the regression in seconds.

## Components

- `scripts/vfx_validate_presets.ps1` — PowerShell driver. Two modes:
  capture (regenerate baseline) and validate (PSNR-diff vs baseline).
- `runtime/tests/baselines/vfx-presets/<NN>-<name>.png` — baseline screenshots,
  one per preset, captured at frame 120 with default camera.
- `runtime/tests/baselines/vfx-presets/manifest.json` — index of baseline files
  with sha256 hash + byte size for integrity tracking.
- `runtime/demos/vfx_viewport_app/main.cpp` — CLI flag
  `--start-preset <N|name>` lets the script drive each preset
  programmatically.
- `runtime/engine/app/application.cpp` — `--diff` mode prints
  `DIFF_STATS: PSNR=NN.NN dB diff=NN.NN%% maxCh=NNN` to stdout so the
  script can parse PSNR without depending on engine logger
  initialisation.

## Regenerate baseline

After a deliberate visual change to a preset (e.g., the Smoke color
fix in `phase17_vfx-viewport-smoke-color-fix`), regenerate the
baseline so future runs validate against the new look:

```pwsh
cd runtime/demos/vfx_viewport_app && zig build -Doptimize=ReleaseFast
cd ../../..
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/vfx_validate_presets.ps1 -Mode capture
```

This rewrites all 22 PNGs + the manifest. Commit the new baselines
along with the change that altered the preset.

## Validate

```pwsh
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/vfx_validate_presets.ps1 -Mode validate
```

Each preset is recaptured and PSNR-diffed against its baseline. The
script prints a per-preset PASS/FAIL line + summary. Exit code 0 = all
pass, non-zero = at least one fail.

## PSNR threshold

Default: **35 dB** for most presets. The threshold accommodates the
RNG noise inherent to GPU particle simulation (per-particle random
spawn directions, velocity variations, lifetime variation, turbulence
seeds). Identical baseline + current images produce PSNR=999 dB; live
recaptures of well-behaved presets land in the 40-60 dB range.

## Per-preset overrides

Some presets have higher RNG-driven variance and need a relaxed
threshold. The script defines a `$PerPresetThreshold` hashtable for
these:

| Preset    | Threshold | Reason                                              |
|-----------|-----------|-----------------------------------------------------|
| ExpDebris | 25 dB     | Debris explosion: random initial velocities        |
| ExpBurst  | 25 dB     | Burst with random spread                            |
| Sparks    | 25 dB     | Ricochet randomness can exceed default tolerance   |
| LitDebris | 25 dB     | Same as ExpDebris                                   |
| Burst     | 30 dB     | Burst variants tend to PSNR around 36 dB             |

To add a new override, edit `$PerPresetThreshold` in the script + add
a row to this table.

## Wire into CI

The validation script returns non-zero on any preset regression. To
fail CI on regression, add a step:

```yaml
- name: VFX preset regression
  run: powershell -NoProfile -File scripts/vfx_validate_presets.ps1 -Mode validate
```

(The exact GitHub Actions / build-system wiring depends on the project
CI configuration, which is out of scope for this task.)

## Limits

- The validation depends on the engine renderer being deterministic
  modulo per-particle RNG. Hard-coded camera position (1280x720,
  cameraYawDeg=90, cameraPitchDeg=-10, etc.). Changing camera
  defaults invalidates the baseline; regenerate.
- The script assumes the demo writes screenshots reliably at frame
  120. If headless screenshot capture starts failing for a preset,
  add a per-preset opt-out or investigate the screenshot pipeline
  before relaxing the threshold further.
- `FireballProj` (preset 0) animates position via the projectile
  sweep in OnUpdate — its PSNR depends on the exact frame phase. The
  current threshold accommodates this; if it regresses, consider
  driving the animation off a fixed seed or bypassing the position
  sweep in headless mode.
