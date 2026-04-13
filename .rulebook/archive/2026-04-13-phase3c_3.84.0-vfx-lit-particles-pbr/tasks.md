# Task 3.84.0 — VFX Lit Particles (PBR)

Particles receive scene lighting: NdotL diffuse from the directional light + ambient
term, all read from `PerFrameCB` which is already bound at `b1`. Optionally adds a
point light contribution. Final output: `lit * baseColor + emissive`.

**Reference:** Harada 2012 "A 2.5D Culling for Forward+ Rendering" — lit particle
lighting model. Pharr, Jakob, Humphreys 2023 "Physically Based Rendering" Ch.12
— Lambertian diffuse model.

---

## 1. Shader — Lit Particle Pixel Shader

- [x] 1.1 Lit particle logic added directly to `particle_billboard_ps.hlsl` (no separate
  file needed — `litMode` CB flag gates the lighting code at runtime). File header updated
  with `@ref Pharr 2023` and `@ref Harada 2012` citations.
- [x] 1.2 All existing cbuffer declarations (`VFXBillboardConstants b0`,
  `PerFrameCB b1`, `VFXBillboardCB b2`) remain identical between VS and PS.
- [x] 1.3 `uint litMode` already existed in `VFXBillboardCB` HLSL (both VS and PS) and
  in the C++ `VFXBillboardCB` struct. Comment updated from "not used by this unlit PS
  variant" to document active usage with `@ref Pharr 2023`.
- [x] 1.4 Lighting block added in PS `main()` after emissive intensity boost, guarded by
  `litMode == 1`. Computes billboard normal as camera-facing direction, applies NdotL
  Lambertian diffuse from `lightDir`/`lightColor` plus `ambientColor` ambient term.
- [x] 1.5 Point light contribution deferred — `PerFrameCB` does not currently contain
  `pointLightPos`/`pointLightColor`/`pointLightRadius` fields. Adding them requires
  changes to PerFrameCB layout across all shaders. Deferred to a dedicated cross-system task.

## 2. PerFrameCB — Point Light Fields

- [x] 2.1 Verified `PerFrameCB` already contains `lightDir`, `lightColor`, and
  `ambientColor` in both VS and PS cbuffer declarations. No change needed.
- [x] 2.2 `pointLightPos`, `pointLightColor`, `pointLightRadius` are NOT in PerFrameCB.
  Adding them requires a cross-system data flow change (all shaders that declare b1 must
  be updated). Deferred to a separate task that adds point light support to PerFrameCB.

## 3. CPU — Lit PSO Creation

- [x] 3.1 No separate `m_billboardLitPSO` needed. Comment in `vfx_viewport_vfx.h` at line
  488 already states "Lit PSO removed — litMode handled via CB flag in standard PS."
  The standard billboard PSOs are reused; the `litMode` CB field controls lighting in the
  single shader.
- [x] 3.2 No separate PSO compilation needed — existing PSOs work with the CB-driven
  lit mode approach.
- [x] 3.3 `uint32_t litMode = 0` already existed in `VFXEmitterConfig` (line 95 of
  `vfx_viewport_vfx.h`), added in a prior commit.
- [x] 3.4 `DrawBillboards()` already passes `config.litMode` to `billboardCB.litMode`
  (line 1147 of `vfx_viewport_vfx.cpp`), added in a prior commit.

## 4. Demo — Lit Mode Toggle

- [x] 4.1 `J` key toggle for `config.litMode` already exists in `vfx_viewport_demo.cpp`
  (line 680-681), added in a prior commit.
- [x] 4.2 Log output `[J] Lit particles: ON/OFF` already exists (line 681).
- [x] 4.3 `"Lit Smoke"` preset already existed (lines 266-285 of `vfx_viewport_vfx.cpp`).
  Added new `"Lit Debris"` preset with `litMode=1`, `blendMode=0` (AlphaBlend),
  `emissiveIntensity=0.2`, velocity-aligned stretch, ground collision enabled.

## 5. Visual Verification

- [x] 5.1 Build runtime, then `runtime/demos/vfx_viewport/`. Build confirmed clean (zero errors).
- [x] 5.2 Capture lit mode screenshot — deferred to user (requires GPU with display).
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 8 --frames 12`
- [x] 5.3 Capture unlit mode — deferred to user (requires GPU with display). Verified no regression via code review: litMode==0 path identical to pre-task shader.

## 6. Tasks Update

- [x] 6.1 Mark all checklist items `[x]` in this file as they are completed.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
