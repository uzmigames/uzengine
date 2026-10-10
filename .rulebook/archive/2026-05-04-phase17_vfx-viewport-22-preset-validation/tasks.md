## 1. Implementation

- [x] 1.1 Added `--start-preset <N|name>` CLI flag in `runtime/demos/vfx_viewport_app/main.cpp`. Accepts integer 0..21 OR case-insensitive name (e.g., `Smoke`, `Tornado`). Stored in `m_startPresetIdx`. Name resolution via new static helper `VFXViewportApp::PresetIndexFromName()` exposed in `vfx_viewport_app.h`.
- [x] 1.2 `OnStart` now applies `kPresetFns[m_startPresetIdx]` to the standalone emitter when `m_startPresetIdx >= 0`, with fallback to Fire (1) on out-of-range. Logs a `[App] [warning]` if invalid.
- [x] 1.3 Verified — `vfx_viewport_app.exe --headless --start-preset Tornado --frames 5 --verbose` logs `started (... preset: Tornado)`.
- [x] 1.4 Created `.sandbox/preset-baseline/`.
- [x] 1.5 Wrote `scripts/vfx/vfx_validate_presets.ps1` with `-Mode capture` that iterates 0..21, launches the demo with `--start-preset N --headless --screenshot --screenshot-frame 120 --frames 130`, and writes `manifest.json` listing each PNG + sha256 hash + byte size.
- [x] 1.6 Ran capture once. All 22 baselines saved at consistent ~187-193 KB sizes (none black/empty). Visually inspected `02-Smoke.png` and `15-Tornado.png` — sky+ground composition with editor overlay parameter inspector visible on the right, matching the expected default-mode layout. No rejected baselines.
- [x] 1.7 Added `-Mode validate` to the script. Diffing was wired through the engine's `--diff` utility: enhanced `Application::TryDiffImages` to print `DIFF_STATS: PSNR=NN.NN dB diff=NN.NN%% maxCh=NNN` to STDOUT (independent of the engine logger so the script can parse it without a `--verbose` flag). Added per-preset PSNR threshold overrides for 5 RNG-noisy presets (ExpDebris, ExpBurst, Sparks, LitDebris, Burst). Final result: 22/22 PASS.

## 2. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation — created `docs/engine/25-vfx/85-preset-validation.md` documenting baseline location, capture/validate procedures, default PSNR threshold (35 dB), per-preset overrides (table with reasons), CI wiring guidance, and known limits (camera-defaults invariant, FireballProj position-sweep caveat).
- [x] 2.2 Write tests covering the new behavior — the validation script `scripts/vfx/vfx_validate_presets.ps1` IS the test. 22 baselines committed at `runtime/tests/baselines/vfx-presets/` with `manifest.json` (sha256 + byte size). CI wiring sketched in the doc but not added — depends on CI configuration which is out of scope for this surgical task.
- [x] 2.3 Run tests and confirm they pass — `scripts/vfx/vfx_validate_presets.ps1 -Mode validate` returns 22/22 PASS on the freshly captured baseline (verified post-move-to-`runtime/tests/baselines/`).
