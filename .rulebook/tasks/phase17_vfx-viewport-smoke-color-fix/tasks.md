## 1. Diagnose

- [ ] 1.1 Read `runtime/shaders/runtime/vfx/particle_spawn_cs.hlsl` and confirm whether `initialColor` (from `ParticleEmitter.initialColor`) is written to the per-particle `colors[]` buffer at spawn time. Note the exact line + variable name.
- [ ] 1.2 Read `runtime/shaders/runtime/vfx/particle_update_cs.hlsl` and confirm whether `colorLifeStart{R,G,B,A}` and `colorLifeEnd{R,G,B,A}` are read from the constant buffer and interpolated based on `age / lifetime` per particle. If the interpolation is missing entirely, root cause is "color-over-life pipeline broken in renderer".
- [ ] 1.3 Run the demo, cycle to Smoke preset (preset index 2), capture screenshots at frame 30, frame 90, frame 240 (covers ~0.5s, 1.5s, 4s of particle life at 60 fps). Use `--screenshot-frame N --frames N+10` and capture three separate runs OR a single run with multi-frame screenshot if available. Visually compare colors across frames - if they are identical, color-over-life is broken.
- [ ] 1.4 Bisect against `ApplyPreset_Fire` (preset index 1) which has dramatic warm-to-dark color-over-life. Run the demo with Fire preset, capture frames 30 / 90 / 240. If Fire colors evolve correctly but Smoke does not, the bug is Smoke-specific (typo in `ApplyPreset_Smoke`). If Fire ALSO does not evolve, color-over-life is system-wide broken.
- [ ] 1.5 Write down which path the diagnosis converged on (1, 2, or 3 from the proposal) - acceptable form is "1.X result Y rules in / rules out path Z", NOT "I think it's path Z".

## 2. Fix

- [ ] 2.1 Apply the surgical fix matching the diagnosed path:
   - Path 1 (artistic mismatch): retune `ApplyPreset_Smoke` color values in `runtime/engine/vfx/components/emitter_presets.h:47`. Change `colorLifeStart` to `(0.5, 0.5, 0.5, 0.8)` (neutral grey start) and keep `colorLifeEnd` at `(0.15, 0.15, 0.15, 0.0)` (dark fade). Also align `initialColor` to `(0.5, 0.5, 0.5, 0.8)`.
   - Path 2 (color-over-life broken in renderer): implement the missing per-particle color interpolation in `particle_update_cs.hlsl`. Add a regression test asserting per-particle color reaches `colorLifeEnd` by `age == lifetime`.
   - Path 3 (Smoke-specific typo): fix the typo in `ApplyPreset_Smoke`.

- [ ] 2.2 Build runtime + demo (`zig build -Ddll=true -Doptimize=ReleaseFast` then `runtime/demos/vfx_viewport_app/`).

- [ ] 2.3 Capture before/after screenshots at frame 90 of the Smoke preset. Save to `e:/UzmiEngine/uzengine/.sandbox/smoke-before.png` and `smoke-after.png`. Visually verify the smoke color matches the intended grey-to-dark fade.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 3.1 Update or create documentation covering the implementation - add a "Smoke preset color recipe" subsection to `docs/engine/25-vfx/` (file TBD - whichever doc covers preset authoring). Document the discovered color-over-life behavior so future preset authors know whether `initialColor` or `colorLifeStart` wins at spawn.
- [ ] 3.2 Write tests covering the new behavior - if Path 2 was the root cause, add a CS regression test that verifies per-particle color interpolation matches expected formula `lerp(colorLifeStart, colorLifeEnd, age/lifetime)` within tolerance. If Path 1 or 3, add a unit test asserting `ApplyPreset_Smoke` produces the corrected color values.
- [ ] 3.3 Run tests and confirm they pass - `runtime/build/bin/test_runtime.exe --gtest_filter="*Smoke*:*ColorOverLife*"` 100% pass.
