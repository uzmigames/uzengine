# Task 3.81.0 — VFX Curl Noise Forces

Divergence-free curl noise field for organic swirling particle motion (smoke, fire, dust).
Wires TurbulenceModule's existing FBM noise into a proper curl noise computation and
exposes animated curl parameters via the update constant buffer.

**Reference:** Bridson et al. 2007 "Curl-Noise for Procedural Fluid Flow" — SIGGRAPH 2007.

---

## 1. Shader — Curl Noise HLSLI

- [ ] 1.1 Create `runtime/shaders/runtime/vfx/vfx_curl_noise.hlsli`. Add file-level
  `/// @ref Bridson 2007 "Curl-Noise for Procedural Fluid Flow"` header. Implement
  `float3 CurlNoise3D(float3 p)` that evaluates the curl of a scalar FBM potential:
  sample FBM at `(p + eps.xyy)`, `(p - eps.xyy)`, `(p + eps.yxy)`, etc. for all
  three axis pairs; return `normalize(cross(ddx, ddy, ddz))` via finite-difference
  curl formula (`eps = 0.001`). Re-use the existing `fbm3d` helper already present
  in `vfx_attractors.hlsli` or inline a minimal 3D value-noise FBM (4 octaves max,
  no external dependency).
- [ ] 1.2 Add `float3 AnimatedCurlNoise(float3 p, float time, float speed, float freq)`
  in the same file: evaluates `CurlNoise3D(p * freq + float3(0, 0, time * speed))`.
  Cite: `/// @ref Bridson 2007 — animated potential scrolls along Z at curlSpeed`.
- [ ] 1.3 Write unit comment with `@constants eps=0.001 (finite-difference step)`.

## 2. Constant Buffer — Update CS Extension

- [ ] 2.1 Add three fields to `VFXUpdateCB` in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  `float curlStrength`, `float curlFrequency`, `float curlSpeed`.
  Pad to 16-byte alignment (add `float _padCurl` to fill the float4). Update the
  `static_assert` size check accordingly.
- [ ] 2.2 Mirror the same three fields in the HLSL `VFXUpdateConstants` cbuffer in
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl` after the existing
  `_padShared0` field. Add `float curlStrength`, `float curlFrequency`,
  `float curlSpeed`, `float _padCurl` (16-byte block). Verify total cbuffer size
  matches the C++ static_assert.

## 3. Particle Update CS — Curl Force Integration

- [ ] 3.1 Add `#include "vfx_curl_noise.hlsli"` at the top of
  `runtime/shaders/runtime/vfx/particle_update_cs.hlsl` (after existing includes).
- [ ] 3.2 In `main()`, after the gravity + damping block and before position
  integration, insert the curl force step:
  ```hlsl
  if (curlStrength > 0.0f) {
      float3 curlForce = AnimatedCurlNoise(pos, totalTime, curlSpeed, curlFrequency);
      vel += curlForce * curlStrength * dt;
  }
  ```
  Guard with `curlStrength > 0.0f` to skip the FBM cost when disabled.
  Add `/// @ref Bridson 2007 — curl force accumulation` above the block.
- [ ] 3.3 Expose `totalTime` in `VFXUpdateConstants`: add `float totalTime` to the
  cbuffer (pull from the existing `PerFrameCB.timeParams.x` on the CPU side; add
  it alongside the curl fields added in step 2.2 to stay in the same 16-byte pad).

## 4. CPU — Upload Curl Params

- [ ] 4.1 In `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`, in the function
  that fills `VFXUpdateCB` before dispatch, read `config.curlStrength`,
  `config.curlFrequency`, `config.curlSpeed` from `VFXEmitterConfig` (new fields
  added in 4.2) and write them into `cb.curlStrength`, `cb.curlFrequency`,
  `cb.curlSpeed`. Also set `cb.totalTime = totalElapsedSeconds`.
- [ ] 4.2 Add three fields to `VFXEmitterConfig` in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`:
  ```cpp
  float curlStrength  = 0.0f;   ///< 0 = disabled. Typical smoke: 1.5
  float curlFrequency = 0.5f;   ///< Spatial scale. Higher = tighter swirl.
  float curlSpeed     = 0.2f;   ///< Animation scroll speed (world units/s).
  ```

## 5. TurbulenceModule — Curl Mode Flag

- [ ] 5.1 Add `bool useCurlMode = false` to `TurbulenceModule` in
  `runtime/engine/vfx/modules/turbulence_module.h`. When `true`, the shader snippet
  emitted by `GetShaderCode()` calls `AnimatedCurlNoise` instead of raw FBM noise.
  When `false`, behavior is unchanged (existing FBM path).
- [ ] 5.2 Update `TurbulenceModule::GetParameters()` in
  `runtime/engine/vfx/modules/turbulence_module.cpp` to include a new
  `"use_curl_mode"` Bool parameter definition.
- [ ] 5.3 Update `TurbulenceModule::GetShaderCode()` to return the curl-noise snippet
  when `useCurlMode` is `true`. The snippet must `#include "vfx_curl_noise.hlsli"`
  (or `// NOTE: vfx_curl_noise.hlsli must be included by the compositor`) and call
  `AnimatedCurlNoise(pos, cbTotalTime, cbCurlSpeed, cbCurlFrequency) * cbCurlStrength`.

## 6. Demo — Curl Noise Preset + Toggle

- [ ] 6.1 Add a `"Curl Smoke"` preset to the presets array in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`:
  - `curlStrength = 2.0f`, `curlFrequency = 0.4f`, `curlSpeed = 0.15f`
  - `blendMode = 0` (AlphaBlend for smoke)
  - `spawnVelocity = {0, 1.5, 0}`, `gravity = {0, 0, 0}` (gravity off for smoke)
  - `colorLifeStartR/G/B = 0.7/0.7/0.7`, `colorLifeEndA = 0.0f`
- [ ] 6.2 Add keyboard toggle `'C'` in `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`
  to cycle between curl-noise-off and curl-noise-on on the active emitter config
  (toggle `config.curlStrength` between `0.0f` and `2.0f`).
- [ ] 6.3 Print `[C] Curl noise: ON/OFF  strength=X` to the ImGui overlay when toggled.

## 7. Visual Verification

- [ ] 7.1 Build runtime, then build `runtime/demos/vfx_viewport/`.
  Fix any compilation errors before proceeding.
- [ ] 7.2 Capture screenshot with curl enabled:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 10 --frames 15`
  View via Read tool. Verify particles show swirling/spiraling motion rather than
  straight upward trajectories.
- [ ] 7.3 Capture a second screenshot with curl disabled (`curlStrength=0`).
  Verify no visual difference from the pre-task behavior (no regression).

## 8. Tasks Update

- [ ] 8.1 Mark all checklist items `[x]` in this file as they are completed.
  Add a one-line note describing what was done and any deviations.
