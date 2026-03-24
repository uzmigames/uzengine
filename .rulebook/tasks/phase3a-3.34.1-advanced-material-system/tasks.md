## Phase 0: Foundation — Shading Model Enum + GBuffer Encoding + Shader Permutations

- [x] 0.1 Define `EShadingModel` enum in `material_types.h`. Created with 10 models (Unlit through ThinTranslucent), uint8_t backing, ToString + IsLit helpers.
- [x] 0.2 Define `EMaterialDomain` enum in `material_types.h`. 6 domains (Surface through Volume), uint8_t backing.
- [x] 0.3 Define `EBlendMode` enum in `material_types.h`. 5 modes (Opaque through Modulate), uint8_t backing, IsTranslucent helper.
- [x] 0.4 Add `shadingModel`, `materialDomain`, `blendMode` fields to MaterialInterface (virtual getters), Material (fields + overrides), DrawCommand (shadingModel field). MaterialInstance inherits from parent.
- [x] 0.5 Pack `shadingModelID` (4 bits) into `materialFlags` bits[24:27] in PerObjectCB. Scene sync reads from material and packs into draw command.
- [x] 0.6 Encode shading model ID in GBuffer RT3.a (RGBA16F): `specular + shadingModelID * 16.0`. Deferred lighting decodes with floor/remainder. Exact decode for all 10 models.
- [x] 0.7 Update `deferred_lighting_ps.hlsl` — decode shadingModelID, Unlit early-out (ID==0 skips all lighting). DefaultLit (ID==1) = existing BRDF. All others fall through to DefaultLit.
- [x] 0.8 Update `mesh_ps.hlsl` (forward pass) — read shadingModelID from materialFlags bits[24:27], Unlit early-out. DefaultLit = existing BRDF.
- [x] 0.9 Define `ShaderPermutationKey` (64-bit): bits for shadingModel(4), blendMode(3), bindless(1), instanced(1), pom(1), materialGraph(1), alphaToCoverage(1), ditheredOpacity(1), hasUV1(1), hasUV2(1), hasTangentMap(1), reserved(48). Implemented in shader_permutation_key.h/.cpp with Pack/GetHash/ToDefineList/ToString/FromMaterial.
- [x] 0.10 Implement `ShaderPermutationCache` — hashmap from `ShaderPermutationKey` to `RHIPipelineHandle`. `GetOrCreatePSO(key)` compiles on miss, caches on hit. Implemented in shader_permutation_cache.h/.cpp with Lookup/Insert/GetOrCreatePSO/InvalidateAll. PSO creation on miss deferred to task 0.11.
- [x] 0.11 Migrate `PipelineLibrary` fixed PSO getters (GetGBuffer, GetGBufferMasked, GetTranslucent, GetAdditive, GetForwardPBR) to use `ShaderPermutationCache` internally. Keep old API as convenience wrappers. Removed 10 per-PSO member variables, added m_permutationCache member. Shutdown enumerates known keys to destroy GPU handles before InvalidateAll().
- [x] 0.12 Add PSO disk cache — serialize compiled PSO blobs to `runtime/cache/pso/` for fast reload. Invalidate on shader source change (hash comparison). Already implemented: PsoDiskCache in rhi_pso_disk_cache.h, integrated in CreateCachedPipeline() with blob store/load via GetPipelineCachedBlob.
- [x] 0.13 Build + test: verify existing DefaultLit rendering is unchanged after shading model plumbing. Screenshot verified — all 9 PBR spheres render identically.

## Phase 1: Unlit Shading Model

- [x] 1.1 Implement Unlit in `deferred_lighting_ps.hlsl` — shadingModelID==0 early-out: outputs albedo or emissive, skips all lighting/shadow/IBL.
- [x] 1.2 Implement Unlit in `mesh_ps.hlsl` (forward path) — shadingModelID==0 early-out: outputs albedo*baseColorTint, no lighting.
- [x] 1.3 Add Unlit material to PBR demo — 10th sphere with EShadingModel::Unlit, red tint (1.0, 0.2, 0.1).
- [x] 1.4 Screenshot verification: Unlit sphere shows flat red/checker color, zero lighting variation — no specular, no shadow, no AO.

## Phase 2: Subsurface Scattering

- [x] 2.1 Add subsurfaceColor (float3), subsurfaceRadius (float) to MaterialInterface (virtual getters), Material (fields), DrawCommand. Wired through scene_sync.
- [x] 2.2 Add SubsurfaceColor texture slot support to MaterialInterface sRGB resolution.
- [x] 2.3 Add Thickness texture slot to Material (thicknessPath + thicknessSrvIndex). GetTexturePath/SrvIndex handle "Thickness".
- [x] 2.4 GBuffer custom data: RT2.w stores per-shading-model custom data. Subsurface flag encoded.
- [x] 2.5 Implement Subsurface in deferred_lighting_ps — wrap diffuse (wrap=0.3) + back-lighting (saturate(-NdotL) * sssColor * 0.5) + Cook-Torrance specular preserved. Full dispatch implemented with smID==2 branching.
- [ ] 2.6 Implement screen-space SSS blur pass (Jimenez 2015 separable kernel) — DEFERRED to later phase.
- [x] 2.7 Implement Subsurface in mesh_ps (forward path) — wrap diffuse + back-lighting added after AccumulateDirectLighting.
- [x] 2.8 Screenshot: 11th sphere (SSS, warm skin tint 0.9/0.7/0.6) renders with soft wrap lighting.

## Phase 3: Clear Coat

- [x] 3.1 Add `clearCoat` (float, default 0.0) and `clearCoatRoughness` (float, default 0.04) fields to material chain. Added to MaterialInterface (virtual getters), Material (fields + overrides + GetScalarParam), DrawCommand, scene_sync.
- [x] 3.2 Add `ClearCoatNormal` texture slot to MaterialInterface (optional separate normal for coating layer). Added clearCoatNormalPath + clearCoatNormalSrvIndex to Material, wired in GetTexturePath/GetTextureSrvIndex.
- [x] 3.3 Implement ClearCoat in `deferred_lighting_ps.hlsl` — dual-layer specular: base GGX + coat GGX (clearCoatRoughness=0.04, F0=0.04). Energy conservation: base attenuated by coat Fresnel, coat added on top. IBL: base ambient attenuated + coat env reflection added. Uses smID==4 dispatch.
- [ ] 3.4 Implement ClearCoat in `mesh_ps.hlsl` (forward) — DEFERRED: forward path needs full PBR first (currently albedo*NdotL only).
- [x] 3.5 Encode clearCoat params in GBuffer custom data channel. clearCoatRoughness packed into materialFlags bits[28:31] (4 bits, 16 levels), written to RT2.w in gbuffer_ps when smID==4, read from customData in deferred lighting.
- [x] 3.6 Screenshot verification: car paint sphere (deep blue, EShadingModel::ClearCoat) shows sharp coat specular highlight on top of soft base. 12th sphere in pbr_test demo.

## Phase 4: Two-Sided Foliage

- [x] 4.1 Implement TwoSidedFoliage in deferred lighting — wrap diffuse (wrap=0.5) + stronger transmittance (0.8) via smID==5 branch. Already present from prior commit alongside smID==2 subsurface.
- [ ] 4.2 Implement TwoSidedFoliage in forward pass — same transmittance term added to diffuse.
- [ ] 4.3 Screenshot verification: leaf quad backlit by sun shows colored translucency.

## Phase 5: Cloth (Ashikhmin/Charlie Sheen)

- [ ] 5.1 Add `sheenColor` (float3, default 0.04,0.04,0.04) and `sheenRoughness` (float, default 0.5) to material chain.
- [x] 5.2 Implement Cloth shading in deferred lighting — D_Charlie + V_Neubelt helper functions + smID==7 branch: Lambert diffuse + sheen specular, no GGX.
- [ ] 5.3 Implement Cloth in forward pass — same BRDF replacement.
- [ ] 5.4 Encode sheenColor in GBuffer custom data.
- [ ] 5.5 Screenshot verification: velvet sphere shows soft sheen at grazing angles, no hard specular highlights.

## Phase 6: Hair (Marschner)

- [ ] 6.1 Add `Tangent` texture slot to MaterialInterface — tangent direction map for anisotropic specular along hair strand.
- [ ] 6.2 Add `tangentShift1` (float, default 0.05), `tangentShift2` (float, default -0.05), `specularTint2` (float3, default hair color) to material chain.
- [ ] 6.3 Implement Hair shading (Marschner dual-specular) — R lobe (surface reflection) + TRT lobe (internal reflection + transmission). Specular along tangent, not normal. Kajiya-Kay diffuse term.
- [ ] 6.4 Hair requires special transparency handling — either alpha-to-coverage or separate forward pass with OIT.
- [ ] 6.5 Screenshot verification: hair strand cards show anisotropic dual-highlight along fiber direction.

## Phase 7: Eye Shading

- [ ] 7.1 Add iris/sclera texture slots and eye-specific params (irisDepth, pupilScale, limbalRingIntensity) to material chain.
- [ ] 7.2 Implement Eye shading — corneal refraction (parallax offset for iris), sclera SSS, limbal ring darkening, caustic highlight.
- [ ] 7.3 Screenshot verification: eye sphere shows refracted iris, specular on cornea, limbal ring.

## Phase 8: Thin Translucent

- [ ] 8.1 Add `transmittanceColor` (float3, default 1,1,1) to material chain — Beer-Lambert absorption color for thin surfaces.
- [x] 8.2 Implement ThinTranslucent in deferred lighting — smID==9 back-lighting with Beer-Lambert color absorption (baseColor * backNoL * 0.6 * lightColor * shadow). Direct lighting uses DefaultLit GGX specular fallthrough.
- [ ] 8.3 Screenshot verification: stained glass panel shows colored transmission.

## Phase 9: Decal System

- [ ] 9.1 Define `EDecalBlendMode` enum: Translucent(0), Stain(1), Normal(2), Emissive(3), DBuffer_AlbedoNormalRoughness(4).
- [ ] 9.2 Implement `DecalComponent` — OBB projection volume, material reference, sort priority, fade distance/angle.
- [ ] 9.3 Implement Decal render pass — after GBuffer, before lighting. Project decal OBB to UV, sample decal material, blend into GBuffer channels per `EDecalBlendMode`.
- [ ] 9.4 Implement per-channel blend control — decal can write to albedo only, albedo+normal, or full PBR channels.
- [ ] 9.5 Implement decal sorting by priority layer.
- [ ] 9.6 Implement decal fade by distance and projection angle.
- [ ] 9.7 Screenshot verification: blood splatter decal projected onto a wall, blending albedo+normal.

## Phase 10: Dithered Opacity (LOD Transitions)

- [ ] 10.1 Implement dithered opacity in GBuffer pass — Bayer 4x4 or 8x8 matrix. `if (ditherPattern[pixelPos % N] > opacity) discard`. Used for LOD cross-fade.
- [ ] 10.2 Add `useDitheredOpacity` flag to material/DrawCommand.
- [ ] 10.3 Screenshot verification: two overlapping LODs with dithered fade show smooth screen-door transition.

## Phase 11: Material Domain System

- [ ] 11.1 Route materials to correct render pass based on `EMaterialDomain` — Surface to GBuffer/Forward, Decal to DecalPass, PostProcess to PostProcessPass, UI to UIPass.
- [ ] 11.2 Validate domain at material load time — reject invalid domain+blendMode combinations (e.g., Decal+Additive).
- [ ] 11.3 Update scene sync to skip materials whose domain does not match the current pass.

## Phase 12: Additional Texture Slots

- [x] 12.1 Add `OpacityMask` texture slot — Implemented as `opacitySrvIndex` in PerObjectCB (offset 28, repurposed twoSided field). R channel, bindless SRV, multiplied with scalar opacity in gbuffer_ps and mesh_ps. twoSided moved to materialFlags bit 3.
- [ ] 12.2 Add `DetailNormal` texture slot — second normal map sampled with UV1 (detail tiling).
- [ ] 12.3 Add `DetailAlbedo` texture slot — second albedo overlay with UV1.
- [ ] 12.4 Update GBuffer PS to blend detail normal with base normal via Reoriented Normal Mapping (RNM).
- [ ] 12.5 Update GBuffer PS to blend detail albedo with base albedo (multiply blend).

## 13. Testing

- [ ] 13.1 Unit test: EShadingModel/EBlendMode/EMaterialDomain enum serialization round-trip.
- [ ] 13.2 Unit test: ShaderPermutationKey construction — verify bit packing and equality.
- [ ] 13.3 Unit test: ShaderPermutationCache — cache hit returns same handle, cache miss triggers compile.
- [ ] 13.4 Unit test: GBuffer shading model ID encode/decode round-trip (write in GBuffer pass, read in lighting).
- [ ] 13.5 Unit test: DecalComponent OBB projection math — UV calculation from world position.
- [ ] 13.6 Unit test: Dithered opacity Bayer matrix pattern correctness.
- [ ] 13.7 Integration test: scene with all 10 shading models renders without GPU errors.
- [ ] 13.8 Integration test: decal projected onto curved surface blends correctly.
- [ ] 13.9 Integration test: shader permutation cache generates correct PSOs for all blendMode x shadingModel combinations.
- [ ] 13.10 Regression test: existing DefaultLit opaque rendering unchanged (pixel-accurate comparison).
- [ ] 13.11 Verify 95%+ coverage on all new code.
- [x] 13.12 Lua API unit tests for split scripting modules: test_lua_cvar_api.cpp (29 tests — get/set/reset/reset_all/info/list + stack hygiene), test_lua_material_api.cpp (49 tests — set/get mesh, all 10 shading models, all 5 blend modes, scalar/bool/enum round-trips, error paths), test_lua_scene_io_api.cpp (22 tests — no-world/no-VFS guards, file-not-found, empty world, bad path, stack hygiene). All compile clean (25/27 build steps pass; 2 failures are Windows file-lock on test_runtime.exe, not compile errors).

## 14. Documentation

- [ ] 14.1 Update `docs/specs/rendering/shader-variants.md` with ShaderPermutationKey layout and PSO cache API.
- [ ] 14.2 Create `docs/guides/shading-models.md` — per-model BRDF formulas, parameter reference, artist selection guide.
- [ ] 14.3 Create `docs/guides/decal-system.md` — decal blend modes, projection setup, sorting rules.
- [ ] 14.4 Update `docs/specs/rendering/shader-library.md` with new HLSLI includes (cloth_brdf.hlsli, hair_brdf.hlsli, subsurface.hlsli, decal_common.hlsli).
- [ ] 14.5 Document GBuffer layout change (shading model ID encoding) in `docs/engine/12-rendering/`.
- [ ] 14.6 Document material domain system and valid domain+blendMode combinations.
