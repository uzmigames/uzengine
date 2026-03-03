## 1. CVolumetricFog ECS Component

- [ ] 1.1 Define `CVolumetricFog` struct in `src/ecs/components/CVolumetricFog.h` with fields: `density` (float, default 0.05), `anisotropy` (float, −1 to 1, default 0.0), `height_falloff` (float, meters, default 100), `noise_scale` (float, default 1.0), `noise_speed` (float3, default (0.01, 0, 0.005)), `scatter_color` (float3, default (1.0, 1.0, 1.0)), `fog_start` (float, meters, default 0), `fog_end` (float, meters, default 1000)
- [ ] 1.2 Register `CVolumetricFog` in the ECS component registry with full serialization to `.uzscene` INI format
- [ ] 1.3 Add `enabled` bool field (default `true`) and serialize it; skip fog dispatch in `VolumetricFogPass::dispatch()` when `enabled == false`
- [ ] 1.4 Expose all `CVolumetricFog` fields in the Scene Settings Inspector panel under "Atmosphere → Volumetric Fog" using appropriate sliders, color picker, and checkboxes

## 2. Froxel Volume Allocation

- [ ] 2.1 Allocate the froxel volume `RWTexture3D<float4>` (160x90x64 RGBA16F) in `src/renderer/passes/VolumetricFogPass.cpp` as two ping-pong textures: `FroxelScatter_A` and `FroxelScatter_B`
- [ ] 2.2 Allocate a 3D Perlin/Worley noise texture (64x64x64 R8_UNORM) at engine startup by generating it procedurally on the CPU once and uploading to a static `Texture3D` resource; used as the animated `noise_scale` input
- [ ] 2.3 Bind froxel volume as UAV during compute passes and as SRV during the composite pass; transition resource states correctly in the render graph between the compute and pixel shader phases

## 3. FroxelScatter Compute Shader

- [ ] 3.1 Create `shaders/froxel/FroxelScatter.hlsl` with dispatch `(160/8, 90/8, 64/4)` thread groups: for each froxel cell at slice Z, compute the world-space center position of the cell from its frustum-aligned AABB
- [ ] 3.2 Compute fog density at the cell center using: `density * exp(-max(0, world_y - base_y) / height_falloff)` where `world_y` is the cell's Y coordinate and `base_y` is the camera's Y position, then modulate by the 3D noise texture sampled at `world_pos * noise_scale + noise_speed * time_seconds`
- [ ] 3.3 Compute the Henyey-Greenstein phase function value at the cell using `anisotropy` for the directional scatter term
- [ ] 3.4 Write `float4(scatter_rgb, density)` to the froxel UAV where `scatter_rgb = density * scatter_color * phase_function`

## 4. FroxelIntegrate Compute Shader

- [ ] 4.1 Create `shaders/froxel/FroxelIntegrate.hlsl` with dispatch `(160/8, 90/8, 1)` thread groups, iterating over Z slices from near to far within each thread:
- [ ] 4.2 Implement ray-march accumulation loop: for each Z slice, read scatter + density from `FroxelScatter`; compute transmittance contribution `dt_transmittance = exp(-density * slice_thickness)`; accumulate `in_scatter += in_scatter_prev * dt_transmittance + scatter_rgb * (1 - dt_transmittance)`; update running transmittance `T *= dt_transmittance`
- [ ] 4.3 Write the integrated result `float4(in_scatter, T)` to the integrated froxel volume (second RGBA16F `Texture3D`, same dimensions as scatter volume)

## 5. Shadow Map Lookup for Light Shafts

- [ ] 5.1 In `FroxelScatter.hlsl`, for each froxel cell, perform a shadow map lookup against the primary directional light's shadow map using the cell's world-space center position: sample `shadow_map.SampleCmpLevelZero(shadow_sampler, shadow_uv, shadow_depth)` and multiply the scatter color by the shadow visibility result
- [ ] 5.2 Apply a soft shadow filter (2x2 PCF) at the froxel shadow lookup to avoid aliased shaft edges
- [ ] 5.3 Gate the shadow lookup behind `#ifdef VOLUMETRIC_SHADOWS_ENABLED` compile-time flag so it can be disabled for performance testing

## 6. VolumetricFogComposite Pass

- [ ] 6.1 Create `shaders/froxel/VolumetricFogComposite.hlsl` as a full-screen pixel shader: for each pixel, compute the clip-space UVW = `float3(screen_uv, depth_to_fog_slice(linear_depth))` where `depth_to_fog_slice` maps the pixel's linear depth to [0, 1] over the fog range [fog_start, fog_end]
- [ ] 6.2 Sample the integrated froxel volume at UVW using trilinear filtering: `float4 fog = FroxelIntegrated.SampleLevel(sampler, UVW, 0)`
- [ ] 6.3 Composite: `final_color = hdr_scene_color * fog.a + fog.rgb` (transmittance * scene + inscatter)
- [ ] 6.4 Insert `FroxelScatter` → `FroxelIntegrate` → `VolumetricFogComposite` passes into `RenderGraph.cpp` after the PBR lighting pass and before bloom/tonemapping

## 7. .uzscene Format Integration

- [ ] 7.1 Add `[VolumetricFog]` section to the `.uzscene` INI format documentation with keys: `enabled`, `density`, `anisotropy`, `height_falloff`, `noise_scale`, `noise_speed_x/y/z`, `scatter_r/g/b`, `fog_start`, `fog_end`
- [ ] 7.2 Implement `CVolumetricFog` INI serializer/deserializer in `src/scene/scene_serializer.cpp` reading and writing all above keys
- [ ] 7.3 Skip fog serialization entirely when `CVolumetricFog` is not present on any entity (backwards compatible with scenes that predate this feature)

## 8. Testing

- [ ] 8.1 Write `tests/renderer/test_volumetric_fog.cpp`: test `CVolumetricFog` serialization round-trip — write all fields to `.uzscene`, reload, verify each field matches within float epsilon
- [ ] 8.2 Test `enabled = false` skips dispatch: verify `VolumetricFogPass::dispatch()` does not submit any compute commands when `CVolumetricFog::enabled == false` (use dispatch-count mock)
- [ ] 8.3 Test height-based density formula: at `world_y = base_y` → density equals `CVolumetricFog::density`; at `world_y = base_y + height_falloff` → density equals `density / e` (within 1% tolerance)
- [ ] 8.4 Test ray-march integration: integrate over 8 froxel slices with known constant scatter and density values; verify the analytically computed transmittance matches the shader output within 0.1%
- [ ] 8.5 Test Henyey-Greenstein phase function at forward scatter (`cos_theta = 1.0`, `anisotropy = 0.5`) matches the known analytical value `(1 + g²) / (2 * (1 - g)²)` = 4.5
- [ ] 8.6 Test `VolumetricFogComposite` depth-to-fog-slice mapping: pixel at `linear_depth = fog_start` → UVW.z = 0; at `fog_end` → UVW.z = 1; at `fog_start + 0.5 * (fog_end - fog_start)` → UVW.z ≈ 0.5
- [ ] 8.7 Test `VolumetricFogComposite` compositing: set froxel sample to `(1, 0, 0, 0.5)` (red inscatter, 50% transmittance); scene color `(0, 1, 0)`; verify output = `(1.0, 0.5, 0.0)` = `(1,0,0) + (0,1,0)*0.5`
- [ ] 8.8 Test shadow-lookup path: disable `VOLUMETRIC_SHADOWS_ENABLED` and verify scatter output is identical to the no-shadow baseline (regression test)
- [ ] 8.9 Verify ≥95% line and branch coverage on `VolumetricFogPass.cpp`, the `CVolumetricFog` serialization code, and the composite depth-mapping function

## 9. Documentation

- [ ] 9.1 Update `docs/specs/renderer.md`: document the froxel volume dimensions, scatter/integrate/composite pass order in the render graph, and the shadow map lookup path for light shafts
- [ ] 9.2 Update `docs/specs/screen-space-effects.md`: document `CVolumetricFog` component parameters, height falloff formula, noise animation, Henyey-Greenstein anisotropy range and effect on scatter direction
- [ ] 9.3 Update `docs/specs/shader-library.md`: document froxel volume UAV/SRV register slots, the integrated froxel RT format, and the `VOLUMETRIC_SHADOWS_ENABLED` compile define
- [ ] 9.4 Update `docs/specs/point-spot-light-shadows.md`: document that the directional light shadow map is also consumed by the volumetric fog scatter pass for light shaft computation
