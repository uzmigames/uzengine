# Task 3.84.0 — VFX Lit Particles (PBR)

Particles receive scene lighting: NdotL diffuse from the directional light + ambient
term, all read from `PerFrameCB` which is already bound at `b1`. Optionally adds a
point light contribution. Final output: `lit * baseColor + emissive`.

**Reference:** Harada 2012 "A 2.5D Culling for Forward+ Rendering" — lit particle
lighting model. Pharr, Jakob, Humphreys 2023 "Physically Based Rendering" Ch.12
— Lambertian diffuse model.

---

## 1. Shader — Lit Particle Pixel Shader

- [ ] 1.1 Create `runtime/shaders/runtime/vfx/particle_billboard_lit_ps.hlsl` as a
  new file. It is a copy-variant of `particle_billboard_ps.hlsl` with lighting added.
  Add file header:
  ```hlsl
  /// @ref Pharr 2023 "Physically Based Rendering" Ch.12 — Lambertian diffuse NdotL
  /// @ref Harada 2012 "2.5D Culling for Forward+" — lit particle integration
  ```
- [ ] 1.2 Keep all existing cbuffer declarations (`VFXBillboardConstants b0`,
  `PerFrameCB b1`, `VFXBillboardCB b2`) identical to the unlit PS.
- [ ] 1.3 Add `uint litMode` to `VFXBillboardCB` (replace one existing `_pad*` field
  in the HLSL layout). `litMode = 0` = unlit (existing behavior), `litMode = 1` = lit.
  Update `VFXBillboardCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h` to
  match (replace matching pad field with `uint32_t litMode = 0`). Static assert must
  still pass (size unchanged).
- [ ] 1.4 In the lit PS `main()`, after computing `emissive` and `opacity`, insert the
  lighting block guarded by `litMode == 1`:
  ```hlsl
  if (litMode == 1) {
      // Approximate billboard normal as camera-facing (toward camera direction).
      // For camera-facing billboards the surface normal IS the view direction.
      float3 normalWS = normalize(cameraPos.xyz - input.positionWS);
      // Directional light NdotL (Lambertian)
      float NdotL = saturate(dot(normalWS, -lightDir.xyz));
      // Ambient + diffuse
      float3 litColor = ambientColor.rgb + lightColor.rgb * NdotL;
      emissive = emissive * litColor;
      // @ref Pharr 2023 Ch.12 — Lambertian diffuse: L = kd * (N·L) * lightColor
  }
  ```
- [ ] 1.5 Add the optional point light contribution block (reads `pointLightPos`,
  `pointLightColor`, `pointLightRadius` from `PerFrameCB` — see step 2.2):
  ```hlsl
  if (litMode == 1) {
      float3 toLight    = pointLightPos.xyz - input.positionWS;
      float  distSq     = dot(toLight, toLight);
      float  radiusSq   = pointLightRadius * pointLightRadius;
      float  attenuation = saturate(1.0 - distSq / radiusSq);
      attenuation *= attenuation; // quadratic falloff
      float  NdotLPt    = saturate(dot(normalWS, normalize(toLight)));
      emissive += emissive * pointLightColor.rgb * NdotLPt * attenuation;
      // @ref Lagarde 2014 "Moving Frostbite to PBR" — windowed attenuation
  }
  ```

## 2. PerFrameCB — Point Light Fields

- [ ] 2.1 Verify that `PerFrameCB` already contains `lightDir` and `lightColor` and
  `ambientColor` by reading `runtime/shaders/runtime/include/per_frame_cb.hlsli`
  (or equivalent). These are already in the cbuffer based on current billboard_ps.hlsl
  declarations. No change needed if present.
- [ ] 2.2 Check if `PerFrameCB` includes point light fields. If `pointLightPos`,
  `pointLightColor`, and `pointLightRadius` are absent:
  - Add `float4 pointLightPos`, `float4 pointLightColor`, `float pointLightRadius`,
    `float3 _padPL` to the C++ `PerFrameCB` struct (maintain 16-byte alignment).
  - Mirror in HLSL `PerFrameCB` cbuffer in `particle_billboard_lit_ps.hlsl`.
  - In the demo, fill these fields from the active scene light or a hardcoded
    debug value `{2.0f, 1.5f, 0.0f, 1.0f}`, `{1.0f, 0.8f, 0.6f, 0.0f}`, radius `5.0f`.
  - If they already exist, skip addition and just use them.

## 3. CPU — Lit PSO Creation

- [ ] 3.1 In `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`, add
  `RHIPipelineHandle m_billboardLitPSO` to `VFXParticleSystem` (alongside existing
  blend-mode PSOs).
- [ ] 3.2 In `VFXParticleSystem::Init()` in `vfx_viewport_vfx.cpp`, compile and create
  a fourth PSO using `particle_billboard_vs.hlsl` + `particle_billboard_lit_ps.hlsl`
  with `PremulAlpha` blend state (same as the default PSO). Store as `m_billboardLitPSO`.
- [ ] 3.3 Add `uint32_t litMode = 0` to `VFXEmitterConfig` in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`.
- [ ] 3.4 In `VFXParticleSystem::DrawBillboards()`, when `config.litMode == 1`, bind
  `m_billboardLitPSO` instead of the standard PSO and set
  `billboardCB.litMode = 1` before the draw call.

## 4. Demo — Lit Mode Toggle

- [ ] 4.1 Add keyboard toggle `'L'` in `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`
  to toggle `config.litMode` between 0 and 1.
- [ ] 4.2 Print `[L] Lit particles: ON/OFF` to the ImGui overlay.
- [ ] 4.3 Add a `"Lit Smoke"` preset in `vfx_viewport_vfx.cpp`:
  - `litMode = 1`, `blendMode = 0` (AlphaBlend)
  - `emissiveIntensity = 0.0f`, `gravity = {0, 0, 0}`
  - `colorLifeStart = {0.8, 0.8, 0.8, 1.0}`, `colorLifeEnd = {0.3, 0.3, 0.3, 0.0}`

## 5. Visual Verification

- [ ] 5.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 5.2 Capture lit mode screenshot:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 8 --frames 12`
  View via Read tool. Verify particles show darker shading on the side facing
  away from `lightDir` and brighter shading on the lit side. Compare against unlit.
- [ ] 5.3 Capture unlit mode. Verify no regression.

## 6. Tasks Update

- [ ] 6.1 Mark all checklist items `[x]` in this file as they are completed.
