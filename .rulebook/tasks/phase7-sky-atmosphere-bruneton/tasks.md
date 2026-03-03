## 1. CSkyAtmosphere ECS Component

- [ ] 1.1 Define `CSkyAtmosphere` struct in `src/ecs/components/CSkyAtmosphere.h` with fields: `sun_direction` (float3, normalized), `rayleigh_scale_height` (float, default 8500 m), `mie_scale_height` (float, default 1200 m), `ground_albedo` (float3, default (0.1, 0.1, 0.1)), `mie_anisotropy` (float, 0–1, default 0.8), `sun_angular_diameter_deg` (float, default 0.53), `atmosphere_radius_km` (float, default 6460), `ground_radius_km` (float, default 6360)
- [ ] 1.2 Register `CSkyAtmosphere` in the ECS component registry with full serialization to `.uzscene` format for all fields
- [ ] 1.3 Add a `dirty` bool flag (transient, not serialized) to `CSkyAtmosphere` that is set to `true` whenever any field is mutated; cleared after the precompute pass runs
- [ ] 1.4 Expose all `CSkyAtmosphere` fields in the Inspector panel with appropriate controls: direction gizmo for `sun_direction`, float sliders for scale heights, color picker for `ground_albedo`, 0–1 sliders for `mie_anisotropy`

## 2. Precomputed LUT Textures

- [ ] 2.1 Allocate transmittance LUT as a `Texture2D` (256x64 R11G11B10F) in `SkyAtmospherePass.cpp`; UAV during precompute, SRV during sky render
- [ ] 2.2 Allocate multiple-scattering LUT as a `Texture2D` (32x32 RGBA16F) in `SkyAtmospherePass.cpp`; UAV during precompute, SRV during sky render
- [ ] 2.3 Allocate sky-view LUT as a `Texture2D` (192x108 RGBA16F) in `SkyAtmospherePass.cpp`; UAV during per-frame update, SRV during sky render pass
- [ ] 2.4 Allocate aerial perspective LUT as a `Texture3D` (32x32x32 RGBA16F) in `SkyAtmospherePass.cpp`; UAV during per-frame update, SRV during PBR composite pass

## 3. Precompute Compute Shaders

- [ ] 3.1 Create `shaders/sky/SkyAtmospherePrecompute.hlsl` with compute entry `CS_Transmittance` (dispatch 256x64x1): for each texel encode optical depth along a ray from the given altitude and angle through the atmosphere using Simpson's rule integration with 40 steps; store in transmittance LUT
- [ ] 3.2 Add compute entry `CS_MultiScatter` (dispatch 32x32x1) to `SkyAtmospherePrecompute.hlsl`: compute the second-order scattering contribution by integrating over the hemisphere above each altitude/angle point using the transmittance LUT; store in multiple-scattering LUT
- [ ] 3.3 Create `shaders/sky/SkyAtmospherePass.hlsl` with compute entry `CS_SkyView` (dispatch 192x108x1): integrate inscattering along the view ray from the ground for each sky-dome direction, sampling transmittance and multiple-scattering LUTs; store luminance in sky-view LUT
- [ ] 3.4 Create `shaders/sky/AerialPerspective.hlsl` compute entry (dispatch 32x32x32x1): for each froxel along the view frustum, accumulate transmittance and inscattered light; output (RGB in-scatter, A transmittance) to the aerial perspective 3D LUT
- [ ] 3.5 Trigger `CS_Transmittance` and `CS_MultiScatter` dispatches in `SkyAtmospherePass::update()` only when `CSkyAtmosphere::dirty == true` (parameter change); these are the expensive passes run only on demand
- [ ] 3.6 Trigger `CS_SkyView` and aerial perspective dispatches every frame (cheap update passes) since they depend on the per-frame `sun_direction`

## 4. Sky Atmosphere Render Pass

- [ ] 4.1 Create `src/renderer/passes/SkyAtmospherePass.cpp` implementing `SkyAtmospherePass::render()`: bind transmittance, multiple-scattering, and sky-view LUT SRVs; dispatch a full-screen triangle in `SkyAtmospherePass.hlsl` VS/PS writing HDR sky color to the sky cubemap or direct-to-HDR-backbuffer render target
- [ ] 4.2 Implement sun disk rendering in `SkyAtmospherePass.hlsl` pixel shader: if the view ray angle from `sun_direction` is within `sun_angular_diameter_deg / 2`, apply limb-darkening factor `(1 - cos(angle/half_angle))^0.5` and output the sun disk luminance
- [ ] 4.3 Replace the existing gradient sky background in `RenderGraph.cpp` with the `SkyAtmospherePass`; retain the solid-color fallback path for headless (test) mode when no `CSkyAtmosphere` component exists in the scene

## 5. Aerial Perspective PBR Integration

- [ ] 5.1 In `shaders/pbr/PBR.hlsl`, after computing the lit surface color, sample the aerial perspective LUT using the pixel's reprojected clip-space UVW coordinates: `UVW = float3(screen_uv, z_slice / 32.0)` where `z_slice` uses the same log-depth formula as the cluster grid
- [ ] 5.2 Composite aerial perspective: `final_color = surface_color * lut_sample.a + lut_sample.rgb` (transmittance + inscatter)
- [ ] 5.3 Gate aerial perspective sampling behind `#ifdef AERIAL_PERSPECTIVE_ENABLED` to allow disabling for performance profiling and testing

## 6. Testing

- [ ] 6.1 Write `tests/renderer/test_sky_atmosphere.cpp`: test `CS_Transmittance` output for zenith ray at sea level — verify transmittance at 550 nm wavelength matches the known Rayleigh optical depth value (approximately 0.97) within 1% tolerance using a CPU-side reference integration
- [ ] 6.2 Test `CSkyAtmosphere` dirty flag: mutate `sun_direction`, verify `dirty = true`; run precompute pass, verify `dirty = false`
- [ ] 6.3 Test that `CS_Transmittance` is NOT dispatched when `dirty = false` (verify dispatch count is 0 in a frame where no parameters changed)
- [ ] 6.4 Test `CSkyAtmosphere` serialization round-trip: serialize all eight fields to `.uzscene`, deserialize, compare all values including float3 fields within epsilon
- [ ] 6.5 Test sun disk rendering logic: verify that a view ray exactly aligned with `sun_direction` produces a non-zero sun disk contribution; a ray 2 degrees away produces zero contribution (given 0.53-degree diameter)
- [ ] 6.6 Test aerial perspective LUT sampling UVW clamping: pixel at maximum depth (z_far) maps to `z_slice = 31`, not out of bounds
- [ ] 6.7 Test headless fallback: scene with no `CSkyAtmosphere` component; verify `SkyAtmospherePass::render()` uses the solid-color fallback path and does not access unbound LUT SRVs
- [ ] 6.8 Verify ≥95% line and branch coverage on `SkyAtmospherePass.cpp` including the dirty-flag dispatch gating, fallback path, and aerial perspective compositing code path

## 7. Documentation

- [ ] 7.1 Update `docs/specs/renderer.md`: document `SkyAtmospherePass` position in the render graph, LUT dimensions and formats, per-frame vs. on-demand dispatch breakdown, and aerial perspective integration in PBR
- [ ] 7.2 Update `docs/specs/shader-library.md`: document transmittance LUT, multiple-scattering LUT, sky-view LUT, and aerial perspective LUT SRV register slots
- [ ] 7.3 Update `docs/specs/editor-panels.md`: document `CSkyAtmosphere` Inspector controls and the `sun_direction` gizmo interaction
