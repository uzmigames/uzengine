## 1. Critical Fixes — Immediate Visual Impact (UNIQUE to 0.9.0)

- [x] 1.1 **Fix Mie Absorption coefficient** — changed `4.4e-3f` to `4.44e-4f`. UE5 ref: `SkyAtmosphereComponent.cpp:114`.
- [x] 1.2 **Fix Directional Light default intensity** — changed default from `1.0f` to `10.0f`. UE5 ref: `DirectionalLightComponent.cpp:1004`. PBR demo explicitly sets all intensities, no demo changes needed.
- [x] 1.3 **Update PBR demo light/sky params** — verified demo explicitly sets all light/sky params. No changes needed.

## 2. PBR BRDF + Tonemap — DELEGATED to phase3a-3.27.0

> **All PBR BRDF and tonemap work is tracked in `phase3a-3.27.0-pbr-brdf-ue5-parity/tasks.md`.**
> That task is a superset: Burley diffuse, SmithJointApprox, k-remapping, multiscatter,
> MIN_ROUGHNESS clamp, FilmToneMap, sRGB curve, debug comparison mode, PBR parity demo.
> Mark items here as done when 3.27.0 completes.

- [x] 2.1 ~~Burley Diffuse~~ → implemented directly: `DiffuseBurley()` in pbr_common.hlsli + lighting.hlsli + EvaluateBRDF
- [x] 2.2 ~~SmithJointApprox~~ → implemented directly: `Vis_SmithJointApprox()` in pbr_common.hlsli + lighting.hlsli, removed old GeometrySmith/GeometrySchlickGGX
- [x] 2.3 ~~k-remapping IBL vs direct~~ → verified: IBL uses brdf_lut_cs.hlsl `GeometrySmithIBL` with `k=a^2/2`, direct uses Vis_SmithJointApprox (no k-remapping needed)
- [x] 2.4 ~~Multi-scatter energy compensation~~ → implemented: Karis 2013 Ems/Favg compensation in deferred_lighting_ps.hlsl + mesh_ps.hlsl (both bindless and SM5 paths)
- [x] 2.5 ~~MIN_ROUGHNESS clamp~~ → verified: `max(roughness*roughness, MIN_ROUGHNESS*MIN_ROUGHNESS)` applied in both deferred and forward paths, MIN_ROUGHNESS=0.04f
- [x] 2.6 ~~FilmToneMap~~ → **3.27.0 item 1.6** ✓ DONE — film_tonemap.hlsli + tonemap_ps.hlsl + PerFrameCB params
- [ ] 2.7 ~~Replace Narkowicz in deferred/forward~~ → **3.27.0 items 1.4, 1.5**

## 3. GTAO — DELEGATED to phase3a-3.15.0

> **All GTAO work is tracked in `phase3a-3.15.0-gtao-ambient-occlusion/tasks.md`.**
> That task is a superset: depth mip chain, GTAO compute, denoise, upsample, temporal
> accumulation, distance fade, power curve, multi-bounce, render graph integration.

- [ ] 3.1 ~~GTAO compute shader~~ → **3.15.0 item 1.2**
- [ ] 3.2 ~~GTAO spatial filter~~ → **3.15.0 item 1.4**
- [ ] 3.3 ~~GTAO render graph pass~~ → **3.15.0 item 1.11**
- [ ] 3.4 ~~AO integration in lighting~~ → **3.15.0 item 1.12**
- [ ] 3.5 ~~AO configuration~~ → **3.15.0 item 1.10**

## 4. Bloom — DELEGATED to phase3a-3.28.0

> **All bloom pyramid work is tracked in `phase3a-3.28.0-bloom-pyramid-upgrade/tasks.md`.**
> That task is a superset: Karis average, soft threshold knee, 6-stage downsample/upsample,
> per-stage tints, composite, render graph integration.

- [x] 4.1 ~~Bloom threshold~~ → **3.28.0 items 1.1, 1.2** ✓ DONE — bloom_threshold_cs.hlsl with Karis average
- [x] 4.2 ~~Bloom downsample chain~~ → **3.28.0 item 1.3** ✓ DONE — bloom_downsample_cs.hlsl 13-tap Jimenez
- [x] 4.3 ~~Bloom upsample chain~~ → **3.28.0 items 1.4, 1.5** ✓ DONE — bloom_upsample_cs.hlsl 9-tap tent
- [ ] 4.4 ~~Bloom composite~~ → **3.28.0 item 1.7**
- [ ] 4.5 ~~Bloom in PBR demo~~ → **3.28.0 item 1.11**

## 5. Shadow Quality Fixes (UNIQUE to 0.9.0)

- [ ] 5.1 **PCF adaptive radius per cascade** — in `shadow_common.hlsli`: scale PCF filter radius by cascade index. Near cascades: tight kernel (2-4 texels). Far cascades: wider kernel (8-16 texels). UE5 ref: `ShadowFilteringCommon.ush`.
- [ ] 5.2 **Shadow bias per cascade resolution** — in `shadow_renderer.cpp`: compute `depthBias = baseDepthBias * cascadeResolution / 2048.0f`. UE5 ref: DirectionalLightComponent.cpp bias formula.
- [ ] 5.3 **Verify shadow sharpen + overblur correction active** — check that `saturate((S-0.5)*sharpen+0.5)` and `S*S` are applied in shadow_common.hlsli. Already implemented but verify with debug grid.

## 6. Visual Verification

- [ ] 6.1 **Before/after screenshots** — capture PBR demo DX12 before and after ALL fixes applied. Store in `.screenshots/parity_before.png` and `.screenshots/parity_after.png`.
- [ ] 6.2 **UE5 reference screenshot** — capture equivalent UE5 scene (sphere on checkerboard, default directional light + sky light + sky atmosphere + height fog) for side-by-side comparison.
- [ ] 6.3 **3-backend verification** — confirm DX12, DX11, Vulkan render identically after all fixes.
- [ ] 6.4 **Shadow debug grid comparison** — verify shadow quality improvement on all 3 backends.

## 7. Testing

- [ ] 7.1 **Unit tests for Burley diffuse** — verify output matches UE5 reference values for specific NdotL/NdotV/roughness combinations. (runs after 3.27.0)
- [ ] 7.2 **Unit tests for SmithJointApprox** — verify output matches UE5 reference values. (runs after 3.27.0)
- [ ] 7.3 **Unit tests for FilmToneMap** — verify curve matches UE5 for specific input values with default params. (runs after 3.27.0)
- [ ] 7.4 **Build passes on all 3 backends** — `cd runtime && zig build` clean with zero warnings.
- [ ] 7.5 **56+ shader parameter tests still pass** — no regressions from CB layout changes.

## 8. Documentation

- [ ] 8.1 **Update CHANGELOG** — document all parity fixes with UE5 citations.
- [ ] 8.2 **Update rendering parity analysis** — mark fixed items in `.claude/analysis/ue5-uzengine-rendering-parity-comparison.md`.
- [ ] 8.3 **Document artist tonemap controls** — add FilmSlope/FilmToe/FilmShoulder to shader authoring guide.

## Dependency Map

```
Stream A: §1 Critical Fixes (unique) — NO DEPS, can start immediately
Stream B: §2 PBR BRDF + Tonemap — delegated to 3.27.0 (touches pbr_common.hlsli, tonemap)
Stream C: §3 GTAO — delegated to 3.15.0 (needs Hi-Z from 3.36.0 first)
Stream D: §4 Bloom — delegated to 3.28.0 (touches bloom_*.hlsl, independent)
Stream E: §5 Shadow Quality (unique) — NO DEPS, can start immediately
Stream F: §6-8 Verification/Testing/Docs — runs AFTER all above complete
```
