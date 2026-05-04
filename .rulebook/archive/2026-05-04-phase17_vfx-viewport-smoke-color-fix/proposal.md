# Proposal: phase17_vfx-viewport-smoke-color-fix

## Why

When `vfx_viewport_app.exe` cycles to the "Smoke" preset (preset
index 2, defined in
`runtime/engine/vfx/components/emitter_presets.h:47`
`ApplyPreset_Smoke`), the rendered particles appear in a color the
user identifies as "wrong". The expected result is a smoke-grey to
dark fade, matching the preset's authored intent. The actual result
visually does not match expectation.

Inspecting the preset:

```cpp
inline void ApplyPreset_Smoke(ParticleEmitter& e) {
    e.maxParticles = 3000;
    e.emissionRate = 200.0f;
    e.lifetime = 4.5f;
    ...
    e.initialColor = {0.5f, 0.5f, 0.5f, 0.7f};       // <-- mid-grey, alpha 0.7
    e.spawnRadius = 20.0f;
    e.emissiveIntensity = 0.0f;
    e.softParticleFadeRange = 50.0f;
    e.sizeOverLifeStart = 0.3f; e.sizeOverLifePeak = 1.5f;
    e.sizeOverLifeEnd = 2.5f; e.sizeOverLifePeakTime = 0.3f;
    e.colorLifeStartR = 0.7f; e.colorLifeStartG = 0.6f;   // <-- WARM start (0.7, 0.6, 0.5)
    e.colorLifeStartB = 0.5f; e.colorLifeStartA = 0.8f;
    e.colorLifeEndR = 0.2f; e.colorLifeEndG = 0.2f;       // <-- DARK GREY end
    e.colorLifeEndB = 0.2f; e.colorLifeEndA = 0.0f;
    e.blendMode = 0;
}
```

There is an apparent contradiction between `initialColor` (mid-grey)
and `colorLifeStart` (warm orange-brown). One of two things is
happening at render time:

1. The `colorLifeStart` is what's actually being applied (warm
   "fire-touched smoke" intent), and the user's visual expectation
   was "neutral grey". In this case the preset is correctly tuned
   and the user wants a different artistic look.

2. `initialColor` is what's being applied (mid-grey at spawn) and
   `colorLifeStart` / `colorLifeEnd` are NOT being interpolated
   over particle lifetime - the color-over-life pipeline is broken
   in `vfx_viewport_app`, so the smoke stays mid-grey forever
   instead of fading from warm to dark. In this case the bug is in
   the VFX renderer's color-over-life path.

The investigation needs to determine which of (1) or (2) is the
actual situation. Diagnostic plan in `tasks.md`.

## What Changes

### Diagnose

1. Read the spawn / update CS shaders to confirm whether
   `initialColor` is written at spawn time and whether
   `colorLife{Start,End}{R,G,B,A}` is interpolated per-frame in the
   update CS based on `age / lifetime`. Check the source files:
   - `runtime/shaders/runtime/vfx/particle_spawn_cs.hlsl`
   - `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`
   - `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl` (or
     wherever final per-vertex / per-pixel color is computed)

2. Run the demo, cycle to Smoke preset, capture frames at t=0.1s,
   t=1s, t=4s of a particle's life (use the existing
   `--screenshot-frame N` mechanic). Verify whether the on-screen
   color changes over time (good: color-over-life works) or stays
   constant (bad: color-over-life broken).

3. If color-over-life is broken: bisect by checking another preset
   that uses color-over-life (e.g., `ApplyPreset_Fire` has dramatic
   warm-to-dark fade). If Fire also fails to fade, the
   color-over-life pipeline is system-wide broken. If Fire works,
   the bug is Smoke-specific (e.g., a field name typo in
   `ApplyPreset_Smoke`).

### Fix

4. Apply the surgical fix matching the diagnosed root cause:
   - Path 1 (artistic mismatch): retune `ApplyPreset_Smoke` to start
     neutral grey and fade darker. Update `colorLifeStart` to
     `(0.5, 0.5, 0.5, 0.8)` and keep `colorLifeEnd` at `(0.15, 0.15,
     0.15, 0.0)`. Validate visually.
   - Path 2 (color-over-life broken in renderer): implement the
     missing interpolation in the update CS or wherever the
     pipeline drops it. Add a regression test (unit test reading
     back per-particle color across lifetime).
   - Path 3 (Smoke-specific field typo): fix the typo in
     `ApplyPreset_Smoke`. Add a static assertion or unit test that
     all `kPresetFns` set `colorLife{Start,End}*` consistently.

5. Re-run the visual A/B - capture a side-by-side screenshot of the
   Smoke preset before / after fix, ensuring visible improvement
   matching the intended look.

## Impact

- Affected specs: `vfx-smoke-preset-color` (NEW spec).
- Affected code: depending on diagnosis - either
  `runtime/engine/vfx/components/emitter_presets.h` (preset retune
  / typo fix), OR `runtime/shaders/runtime/vfx/particle_update_cs.hlsl`
  (renderer fix). May also touch
  `runtime/engine/vfx/components/particle_emitter.h` (CB layout if
  the renderer fix needs new fields).
- Breaking change: NO. Visual change for the Smoke preset only.
- User benefit: the Smoke preset renders with the correct intended
  color, fixing one of the demo's most visible regressions.

## Dependencies

- Independent of other phase17 tasks - the smoke-color bug exists
  regardless of overlay visibility or default-scene composition.
- Recommended to land BEFORE
  `phase17_vfx-viewport-22-preset-validation` so the validation
  baseline screenshots capture the corrected smoke color.
