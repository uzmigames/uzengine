## 1. Scene Light Data Pipeline Fix

- [x] 1.1 Update `StaticMeshRenderer::draw()` signature to accept light CBV buffer handle
- [x] 1.2 Replace hardcoded `LightFrameCB` values with data from `LightState` (direction, intensity, color)
- [x] 1.3 Replace hardcoded `cameraPos` with actual camera position from `camState`
- [x] 1.4 Update call site in `renderer.cpp` to pass `cachedLight_` and camera position
- [x] 1.5 Verify specular highlight moves correctly when Sun node rotation changes
- [x] 1.6 Verify `light_intensity = 3.0` and `light_color = 1.0 0.95 0.88` from scene are applied

## 2. Material Format Extension

- [x] 2.1 Material system supports displacement via generic `textures` / `texture_handles` / `parameters` maps
- [x] 2.2 Parse `displacement_texture` and `parallax_scale` keys in material loader
- [x] 2.3 MeshProxy caches `cached_displacement_tex` and `cached_parallax_scale` from material
- [x] 2.4 `assets/materials/stonebricks.uzmat` has `displacement_texture` and `parallax_scale = 0.05`
- [x] 2.5 Wire up `t5` displacement texture binding in `mesh_proxy.cpp` draw
- [x] 2.6 Root signature exposes t0-t9 (10 SRV slots) to pixel shader

## 3. Parallax Occlusion Mapping Shader

- [x] 3.1 `Texture2D displacementTex : register(t5)` in `pbr.ps.hlsl`
- [x] 3.2 `parallaxScale` in `MaterialCB` (replaced `_matPad`)
- [x] 3.3 Implement `ParallaxOcclusionMapping()`: steep POM loop (8-32 layers) + linear interp
- [x] 3.4 Gate POM on `parallaxScale > 0.0` — no-op for materials with no displacement
- [x] 3.5 Apply offset UV to all texture samples (albedo, normal, roughness, ao)
- [x] 3.6 Rebuild embedded shader headers and verify no HLSL compile errors

## 4. IBL Pipeline (added during implementation)

- [x] 4.1 BRDF LUT: 256x256 RG16F split-sum integration at t7
- [x] 4.2 Irradiance cubemap: 32x32 per face, cosine-weighted hemisphere (1024 samples) at t8
- [x] 4.3 Prefiltered specular cubemap: 128x128, 5 mip levels, GGX importance sampling at t9
- [x] 4.4 Root CBV refactor: b2 from 16 root constants to root CBV (saved 14 DWORDs)
- [x] 4.5 PBR shader: cubemap irradiance (diffuse) + prefiltered env + BRDF LUT (specular split-sum)
- [x] 4.6 envIntensity tuned (1.2 → 2.5 after metallic fix, balanced for metals+dielectrics)
- [x] 4.7 `texture_upload_subresource` RHI function for per-face/mip cubemap upload
- [x] 4.8 Sky cubemap generation (CPU-side): source env + irradiance convolution + GGX prefilter

## 5. POM Quality Fixes

- [x] 5.1 Cavity occlusion reduced 50% -> 25% (mortar joint darkening)
- [x] 5.2 AO softened: linear for diffuse, squared for specular
- [x] 5.3 Tangent smoothstep widened (0.03-0.30) for gradual seam fade
- [x] 5.4 Grazing angle UV blend prevents silhouette artifacts

## 6. Material System Expansion

- [x] 6.1 Emission support: emissionColor[3] + emissionEnergy in MaterialCB (12 DWORDs, was 8)
- [x] 6.2 Root sig b1 expanded from 8 to 12 DWORDs (56/64 total budget)
- [x] 6.3 PBR shader: additive emission pre-tonemap
- [x] 6.4 StaticMeshRenderer MaterialCB layout fixed to match shader

## 7. PBR Quality Improvements

- [x] 7.1 Horizon occlusion for specular IBL (prevents light leak below surface)
- [x] 7.2 Multi-scattering energy compensation (Kulla-Conty 2017 / Fdez-Aguera 2019)

## 8. Per-Material Shader Parameters

- [x] 8.1 MaterialCB expanded to 16 DWORDs: added envMultiplier, normalStrength, specularMultiplier
- [x] 8.2 Root sig b1 expanded from 12 to 16 DWORDs (60/64 total budget)
- [x] 8.3 PBR shader uses envMultiplier (IBL), normalStrength (normal map), specularMultiplier (direct spec)
- [x] 8.4 MeshProxy caches + uploads 16-DWORD matCB with new fields
- [x] 8.5 Material loader already handles new params via generic `parameters[key]` map

## 9. Metallic Fallback Texture Fix (critical bug)

- [x] 9.1 Fixed metallic fallback from black_texture → white_texture in mesh_proxy.cpp
- [x] 9.2 Fixed same bug in static_mesh_renderer.cpp
- [x] 9.3 Root cause: `metallic = materialMetallic * metallicTex.Sample()` with black tex = 0 always

## 10. IBL Cubemap Tuning

- [x] 10.1 Sky cubemap: dark ground (0.06,0.05,0.04) for metal contrast
- [x] 10.2 Sky cubemap: brighter zenith (0.18,0.40,0.95) + horizon (0.75,0.85,1.05)
- [x] 10.3 Sun intensity in cubemap: 3.0 → 5.0
- [x] 10.4 Global envIntensity tuned to 2.5 (balanced metals+dielectrics)
- [x] 10.5 Per-material envMultiplier/specularMultiplier tuned for iron, copper, gold

## Deferred

- [x] D1. Unit tests for material loading, LightFrameCB, POM — 12 new PBRMaterialTest cases added
- [x] D2. Documentation updates (renderer.md, material format) — renderer.md fully rewritten: actual forward pipeline (not deferred), root sig layout (64/64 DWORDs), MaterialCB layout, .uzmat format reference, IBL pipeline, shadow system, post-process stack, fallback textures, BRDF model
- [x] D3. GPU compute IBL pipeline (replacing CPU-side) — IblBaker class with 3 compute shaders (BRDF LUT, irradiance convolution, GGX prefilter), per-mip UAV support, integrated into renderer.cpp with CPU fallback
- [x] D4. Scene-driven IBL cubemap regeneration (sync with SkyAtmosphere component) — dirty-flag in update()
- [x] D5. Emission texture support — BLOCKED: root sig at 64/64, all SRV slots (t0-t9) used. Requires phase3-ultra-realistic-rendering prerequisite 0.1 (move MaterialCB to root CBV) to free DWORD budget for additional SRV slot
