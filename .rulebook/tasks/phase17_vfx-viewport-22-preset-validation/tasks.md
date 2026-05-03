## 1. Implementation

- [ ] 1.1 Add `--start-preset <name|index>` CLI flag parsing in `runtime/demos/vfx_viewport_app/main.cpp` (or wherever args are parsed). Accept either an integer 0..21 or a string matching `kPresetNames[i]`. Store in a new `m_startPresetIdx` member of `VFXViewportApp`.
- [ ] 1.2 In `OnStart` after the standalone emitter is created (default mode), if `m_startPresetIdx >= 0` apply that preset immediately via `kPresetFns[m_startPresetIdx](rt)` instead of the hardcoded Fire default.
- [ ] 1.3 Build runtime + demo. Verify `--start-preset 5` (MagicOrb) launches with that preset active and the overlay shows "MagicOrb" as the current preset name.
- [ ] 1.4 Create directory `e:/UzmiEngine/uzengine/.sandbox/preset-baseline/`.
- [ ] 1.5 Write `scripts/vfx_validate_presets.ps1` that iterates `0..21`, launches the demo with `--start-preset N --headless --screenshot .sandbox/preset-baseline/NN-name.png --screenshot-frame 120 --frames 130`, and writes a `manifest.json` listing the captured baseline files + their byte sizes + sha256 hash.
- [ ] 1.6 Run the script ONCE to generate the baseline. Manually inspect ALL 22 screenshots with the Read tool. Reject any that show fully-black, camera-engulfed, or obviously broken renders. For each rejected preset, file a bug task `phase17_vfx-viewport-preset-<name>-broken` and exclude that preset from the baseline manifest until fixed.
- [ ] 1.7 Add a `--mode validate` flag to `scripts/vfx_validate_presets.ps1` that re-captures all 22 and PSNR-diffs against the baseline manifest. PSNR < 35 dB = fail. Print a summary table.

## 2. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation - create `docs/engine/25-vfx/85-preset-validation.md` documenting the baseline location, the regeneration procedure (`scripts/vfx_validate_presets.ps1 -Mode capture`), the validation procedure (`-Mode validate`), the PSNR threshold (35 dB), and the per-preset bypass list (any preset that legitimately cannot pass PSNR validation, e.g., due to RNG-driven motion).
- [ ] 2.2 Write tests covering the new behavior - the validation script ITSELF is the test. Add an entry to the rulebook task workflow (or `.github/workflows/` if CI exists) that runs `scripts/vfx_validate_presets.ps1 -Mode validate` and fails the build on any preset regression.
- [ ] 2.3 Run tests and confirm they pass - executing `scripts/vfx_validate_presets.ps1 -Mode validate` against the freshly captured baseline returns 22/22 pass.
