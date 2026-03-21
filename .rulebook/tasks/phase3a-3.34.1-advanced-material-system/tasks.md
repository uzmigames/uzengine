## Phase 0: Foundation — Shading Model Enum + GBuffer Encoding + Shader Permutations

- [ ] 0.1 Define `EShadingModel` enum in `material_types.h`: Unlit(0), DefaultLit(1), Subsurface(2), SubsurfaceProfile(3), ClearCoat(4), TwoSidedFoliage(5), Hair(6), Cloth(7), Eye(8), ThinTranslucent(9).
- [ ] 0.2 Define `EMaterialDomain` enum in `material_types.h`: Surface(0), Decal(1), PostProcess(2), LightFunction(3), UI(4), Volume(5).
- [ ] 0.3 Define `EBlendMode` enum in `material_types.h`: Opaque(0), Masked(1), Translucent(2), Additive(3), Modulate(4).
- [ ] 0.4 Add `shadingModel`, `materialDomain`, `blendMode` fields to `MaterialInterface`, `Material`, `MaterialInstance`, and `DrawCommand`.
- [ ] 0.5 Add `shadingModelID` (uint, 4 bits) to `PerObjectCB` — GBuffer pass writes it per draw.
- [ ] 0.6 Encode shading model ID in GBuffer: pack 4-bit ID into stencil buffer (D32_Float_S8 → use stencil bits) or into RT1.a upper nibble. Research best approach per published references.
- [ ] 0.7 Update `deferred_lighting_ps.hlsl` to read shading model ID and dispatch via `switch(shadingModelID)`. DefaultLit(1) = existing BRDF. All others initially fall through to DefaultLit.
- [ ] 0.8 Update `mesh_ps.hlsl` (forward pass) to read `shadingModel` from PerObjectCB and dispatch. DefaultLit = existing BRDF. Others initially fall through.
- [ ] 0.9 Define `ShaderPermutationKey` (64-bit): bits for shadingModel(4), blendMode(3), bindless(1), instanced(1), pom(1), materialGraph(1), alphaToCoverage(1), ditheredOpacity(1), hasUV1(1), hasUV2(1), hasTangentMap(1), reserved(48).
- [ ] 0.10 Implement `ShaderPermutationCache` — hashmap from `ShaderPermutationKey` to `RHIPipelineHandle`. `GetOrCreatePSO(key)` compiles on miss, caches on hit.
- [ ] 0.11 Migrate `PipelineLibrary` fixed PSO getters (GetGBuffer, GetGBufferMasked, GetTranslucent, GetAdditive, GetForwardPBR) to use `ShaderPermutationCache` internally. Keep old API as convenience wrappers.
- [ ] 0.12 Add PSO disk cache — serialize compiled PSO blobs to `runtime/cache/pso/` for fast reload. Invalidate on shader source change (hash comparison).
- [ ] 0.13 Build + test: verify existing DefaultLit rendering is unchanged after shading model plumbing.

## Phase 1: Unlit Shading Model

- [ ] 1.1 Implement Unlit in `deferred_lighting_ps.hlsl` — when shadingModelID == 0, output = GBuffer albedo * emissiveIntensity. Skip all diffuse, specular, shadow, AO, IBL evaluation.
- [ ] 1.2 Implement Unlit in `mesh_ps.hlsl` (forward path) — output = albedo * emissiveIntensity, no lighting.
- [ ] 1.3 Add Unlit material to PBR demo or create test scene — flat-colored plane with no lighting response.
- [ ] 1.4 Screenshot verification: Unlit surface shows flat color, zero lighting variation when light direction changes.

## Phase 2: Subsurface Scattering

- [ ] 2.1 Add `subsurfaceColor` (float3, default 1.0,0.2,0.1) and `subsurfaceRadius` (float, default 1.0) fields to `MaterialInterface`/`Material`/`DrawCommand`/`PerObjectCB`.
- [ ] 2.2 Add `SubsurfaceColor` texture slot to MaterialInterface (tangent-space RGB map for spatially-varying SSS tint).
- [ ] 2.3 Add `Thickness` texture slot to MaterialInterface (R channel, 0=thick/no transmission, 1=thin/full transmission).
- [ ] 2.4 Encode subsurface params in GBuffer custom data channel — pack subsurfaceColor into RT unused bits or add 5th RT (R8G8B8A8) for shading-model-specific data.
- [ ] 2.5 Implement Subsurface shading in `deferred_lighting_ps.hlsl` — wrap diffuse: `NdotL_wrap = (NdotL + wrap) / (1 + wrap)` with wrap derived from subsurfaceRadius. Add back-lighting: `subsurfaceColor * saturate(-NdotL) * thickness`.
- [ ] 2.6 Implement screen-space SSS blur pass (Jimenez 2015 separable kernel) — 11-tap Gaussian in screen space, weighted by depth difference. Two passes: horizontal + vertical.
- [ ] 2.7 Implement Subsurface in `mesh_ps.hlsl` (forward path) — same wrap diffuse + back-lighting.
- [ ] 2.8 Screenshot verification: subsurface sphere shows soft wrap lighting and colored light bleed-through on thin areas.

## Phase 3: Clear Coat

- [ ] 3.1 Add `clearCoat` (float, default 1.0) and `clearCoatRoughness` (float, default 0.04) fields to material chain.
- [ ] 3.2 Add `ClearCoatNormal` texture slot to MaterialInterface (optional separate normal for coating layer).
- [ ] 3.3 Implement ClearCoat in `deferred_lighting_ps.hlsl` — dual-layer specular: base layer = standard GGX at material roughness, coat layer = GGX at clearCoatRoughness. Combine: `coatFresnel * coatSpec + (1 - coatFresnel) * baseSpec`. Coat F0 = 0.04 (dielectric IOR ~1.5).
- [ ] 3.4 Implement ClearCoat in `mesh_ps.hlsl` (forward) — same dual-layer specular.
- [ ] 3.5 Encode clearCoat params in GBuffer custom data channel.
- [ ] 3.6 Screenshot verification: car paint sphere shows two distinct specular highlights (soft base + sharp coat).

## Phase 4: Two-Sided Foliage

- [ ] 4.1 Implement TwoSidedFoliage in deferred lighting — transmittance: `backLighting = saturate(-NdotL) * subsurfaceColor * transmittanceStrength`. Light passes through thin leaf surfaces.
- [ ] 4.2 Implement TwoSidedFoliage in forward pass — same transmittance term added to diffuse.
- [ ] 4.3 Screenshot verification: leaf quad backlit by sun shows colored translucency.

## Phase 5: Cloth (Ashikhmin/Charlie Sheen)

- [ ] 5.1 Add `sheenColor` (float3, default 0.04,0.04,0.04) and `sheenRoughness` (float, default 0.5) to material chain.
- [ ] 5.2 Implement Cloth shading in deferred lighting — replace GGX specular with Charlie/Ashikhmin sheen NDF. Add Fresnel-weighted sheen: `sheenColor * D_Charlie(roughness, NdotH) * V_Neubelt(NdotV, NdotL)`.
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
- [ ] 8.2 Implement ThinTranslucent — single-layer glass/leaf: compute transmittance via `exp(-thickness / transmittanceColor)`. Dual-sided lighting (front + back).
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

- [ ] 12.1 Add `OpacityMask` texture slot — separate from albedo alpha. R channel, used for masked blend mode.
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

## 14. Documentation

- [ ] 14.1 Update `docs/specs/rendering/shader-variants.md` with ShaderPermutationKey layout and PSO cache API.
- [ ] 14.2 Create `docs/guides/shading-models.md` — per-model BRDF formulas, parameter reference, artist selection guide.
- [ ] 14.3 Create `docs/guides/decal-system.md` — decal blend modes, projection setup, sorting rules.
- [ ] 14.4 Update `docs/specs/rendering/shader-library.md` with new HLSLI includes (cloth_brdf.hlsli, hair_brdf.hlsli, subsurface.hlsli, decal_common.hlsli).
- [ ] 14.5 Document GBuffer layout change (shading model ID encoding) in `docs/engine/12-rendering/`.
- [ ] 14.6 Document material domain system and valid domain+blendMode combinations.
