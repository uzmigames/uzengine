## 1. Implementation

- [x] 1.0 MaterialInterface/Material/MaterialInstance hierarchy — Created `material_interface.h` (abstract base with virtual GetScalarParam/GetVectorParam/GetTexturePath/GetTextureSrvIndex + PBR convenience getters). Updated `material_asset.h` (Material inherits MaterialInterface, implements all virtuals from fixed fields). Rewrote `material_instance.h` (inherits MaterialInterface, parent is MaterialInterface* for multi-level chaining, private override fields with trailing underscore). Updated `static_mesh_component.h` with `overrideMaterials` vector + `GetMaterial(slot)` + `SetMaterial(slot, mat)`. Updated `scene_renderer.cpp` to read material params from MaterialInterface when available, with legacy component path fallback. Updated PBR demo to use Material/MaterialInstance objects.
- [x] 1.1 Add emissive texture slot — register t5 in GBuffer PS (bindless via emissiveIndex). Emissive written to GBuffer RT3 (RGBA16Float), added to final color in deferred lighting pass. Default = black (0,0,0) = no emission. HDR values > 1.0 drive bloom. Implemented in gbuffer_ps.hlsl + deferred_lighting_ps.hlsl.
- [x] 1.2 Add emissive parameters to PerObjectCB — `uint32_t emissiveIndex` at offset 20, `float emissiveColorR/G/B` + `float emissiveIntensity` at offset 160. Updated GBufferPerObjectParams, DrawCommand, InstanceBatch to carry emissive data.
- [x] 1.3 Update gbuffer_ps.hlsl + deferred_lighting_ps.hlsl for emissive — GBuffer pass samples emissive texture, applies color*intensity, writes to RT3. Lighting pass reads GBuffer emissive and adds to final color: `color = ambient + Lo + emissiveContrib`. Emissive bypasses all lighting (self-lit). @source UE5 BasePassPixelShader.usf:1480,1541.
- [ ] 1.4 Add opacity texture slot — register t6 (or bindless). R channel = opacity [0,1]. Default = white (1.0 = fully opaque).
- [x] 1.5 Implement alpha test (masked blend mode) — Added `blendMode` field to PerObjectCB (offset 252) and both HLSL shaders (gbuffer_ps, mesh_ps). When blendMode==1, shader discards fragments where opacity < alphaClipThreshold (default 0.3333, UE5 Material.h:1891). Removed [earlydepthstencil] from both shaders since discard and early-Z are mutually exclusive. GBuffer pass skips masked draws when no maskedPipeline is provided.
- [x] 1.6 Implement translucent blend mode — Added GetTranslucent(bool) to PipelineLibrary: SrcAlpha/InvSrcAlpha blend, depth read no write, CullMode::None, uses forward mesh_ps shader into RGBA16Float HDR target. GBuffer pass skips blendMode >= 2 draws. Forward shader outputs matOpacity in alpha channel for translucent mode.
- [x] 1.7 Implement additive blend mode — Added GetAdditive(bool) to PipelineLibrary: One/One blend, depth read no write, CullMode::None, uses forward mesh_ps shader. No sorting required (additive is commutative).
- [x] 1.8 Add blend mode to PerObjectCB — Added `uint32_t blendMode` at offset 252 (replaced _pad2), `float alphaClipThreshold` at offset 24, `uint32_t twoSided` at offset 28 (replaced _pad[2]). Mirrored in GBufferPerObjectParams. Both HLSL cbuffers updated. DrawCommand carries blendMode, alphaClipThreshold, twoSided, opacitySrvIndex. GBuffer pass selects masked PSO and propagates fields to CB. Forward PBR pass (pbr_pass.cpp) also propagates.
- [x] 1.9 Add two-sided rendering flag — `uint32_t twoSided` at offset 28 in PerObjectCB. GBufferMasked PSO uses CullMode::None. Translucent and Additive PSOs also use CullMode::None by default. DrawCommand carries twoSided field. PSO selection driven by blendMode in GBuffer pass.
- [ ] 1.10 Add displacement/heightmap texture slot — register t7 (or bindless). R channel = height [0,1]. Default = black (0.0 = no displacement). Reserved for POM task (3.13.0).
- [x] 1.11 Update G-Buffer pass for emissive — Added GBuffer RT3 (RGBA16Float) for HDR emissive. GBuffer::Create allocates emissive texture. GBufferPassDesc includes emissiveTarget. Pipeline library creates PSOs with 4 color attachments. Lighting pass reads emissive via bindless or SM5 t10 register. Scene renderer wires emissive through entire pipeline (StaticMeshComponent -> DrawCommand -> GBufferPerObjectParams -> HLSL).
- [ ] 1.12 Create material expansion demo — scene with: emissive neon signs (glow + bloom), masked chain-link fence, translucent colored glass, additive hologram. All material types in one scene.

## 2. Visual Verification

- [ ] 2.1 Screenshot: emissive surface glowing and contributing to bloom
- [ ] 2.2 Screenshot: masked foliage — clean alpha cutout, no fringing
- [ ] 2.3 Screenshot: translucent glass — background visible through tinted surface
- [ ] 2.4 Screenshot: additive hologram — bright overlay blended with scene
- [ ] 2.5 Screenshot: emissive intensity=10 — very bright, large bloom halo
- [ ] 2.6 Regression: existing opaque materials unchanged

## 3. Documentation

- [ ] 3.1 Document all material texture slots and default values with UE5 equivalents
- [ ] 3.2 Add blend mode guide (opaque vs masked vs translucent vs additive)
- [ ] 3.3 Document per-material parameter system and PerObjectCB layout
