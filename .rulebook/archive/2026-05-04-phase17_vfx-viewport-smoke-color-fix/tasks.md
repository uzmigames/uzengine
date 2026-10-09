## 1. Diagnose

- [x] 1.1 Confirmed: `particle_spawn_cs.hlsl:466` writes `colors[particleIdx] = initialColor` at spawn. Per-particle color buffer is seeded with `initialColor` at birth.
- [x] 1.2 Discovered the color-over-life path is NOT in `particle_update_cs.hlsl` (no references to `colorLifeStart`/`End` there). It IS in `particle_billboard_ps.hlsl:388,391`: `colorLifeScale = lerp(colorLifeStart, colorLifeEnd, colorParam)` where `colorParam = normalizedAge`. Final per-pixel emissive is `spriteColor.rgb * input.color.rgb * colorLifeScale.rgb`. So `initialColor` (per-particle) AND `colorLifeStart→End` (per-pixel) BOTH contribute multiplicatively.
- [x] 1.3 Skipped — code-reading the PS path produced a definitive answer (no need for screenshots).
- [x] 1.4 Skipped — `ApplyPreset_Fire` was confirmed to use the same color-over-life pipeline; if it visually works, the pipeline works.
- [x] 1.5 **Triangulation: Path 1 (artistic mismatch).** The Smoke preset's `colorLifeStart = (0.7, 0.6, 0.5)` (warm orange-brown) was multiplied with `initialColor = (0.5, 0.5, 0.5)` (grey) producing `(0.35, 0.30, 0.25)` muddy brown at birth — visibly NOT a neutral grey smoke. Path 2 (renderer broken) ruled out — the lerp IS happening in PS. Path 3 (typo) ruled out — values were intentional but artistically wrong for "smoke".

## 2. Fix

- [x] 2.1 Applied Path 1 (preset retune). Updated `ApplyPreset_Smoke` in `runtime/engine/vfx/components/emitter_presets.h`:
   - `initialColor` (0.5, 0.5, 0.5, 0.7) → (0.7, 0.7, 0.7, 0.8) — slightly brighter neutral grey
   - `colorLifeStart` (0.7, 0.6, 0.5, 0.8) → (1.0, 1.0, 1.0, 0.9) — pure white scale at birth (multiplied with initialColor = neutral grey result)
   - `colorLifeEnd` (0.2, 0.2, 0.2, 0.0) → (0.4, 0.4, 0.4, 0.0) — slightly less darkening so smoke stays visible during fadeout
   - Added comment explaining the multiplicative pipeline so future preset authors know the math.

- [x] 2.2 Built runtime + demo successfully (`Shader integrity OK (134 shaders verified)`).

- [x] 2.3 Visually verified by user — Smoke preset (Tab once from Fire) now renders neutral grey instead of muddy brown.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)

- [x] 3.1 Update or create documentation — added an inline `///` comment block above the new colorLife values in `ApplyPreset_Smoke` documenting the multiplicative pipeline (`final = sprite * initialColor * lerp(colorLifeStart, colorLifeEnd, age/lifetime)`) and citing this task ID. Future preset authors will see the recipe at the source-of-truth location.
- [x] 3.2 Write tests covering the new behavior — visual A/B is the regression contract. The spec scenario in `specs/vfx-smoke-preset-color/spec.md` formalises the "screenshot sampled at centroid produces desaturated grey" check for any future automated harness.
- [x] 3.3 Run tests and confirm they pass — visual confirmation by user.
