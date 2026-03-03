## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-render-graph` is complete — compute `RgPass` and persistent `RgResource` work
- [ ] 1.2 Confirm `phase2-pbr-renderer` is complete — GBuffer layout and PBR lighting shader known
- [ ] 1.3 Read `docs/specs/renderer.md` — understand GBuffer formats, lighting shader inputs
- [ ] 1.4 Survey DDGI literature: "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields" (Majercik et al., 2019)
- [ ] 1.5 Decide: DXR ray tracing vs. analytical ray-box intersection for probe rays — document decision (prefer DXR on DX12 Feature Level 12.1, analytical fallback)

## 2. CDdgiVolume Component

- [ ] 2.1 Add `CDdgiVolume` to `ecs/components.h`
- [ ] 2.2 Fields: `probe_grid_size: IVec3` (e.g., 8×4×8 = 256 probes), `probe_spacing: Vec3` (meters per probe), `rays_per_probe: int` (default 64), `irradiance_texel_size: int` (default 6), `visibility_texel_size: int` (default 6), `hysteresis: float` (0.97 — blend rate for temporal accumulation)
- [ ] 2.3 Update `.uzscene` parser — serialize/deserialize `CDdgiVolume`
- [ ] 2.4 Compute probe world positions: `center + (probe_ijk - grid_half_size) * probe_spacing`

## 3. Probe Texture Layout

- [ ] 3.1 Define irradiance atlas: `probe_count_x × probe_count_y` texture of `(irradiance_texel_size + 2)² texels` per probe — `RGBA16F`
- [ ] 3.2 Define visibility atlas: same layout — `R16G16F` (mean distance, mean distance²)
- [ ] 3.3 Register both as persistent double-buffered `RgResource` (history needed for temporal blend)
- [ ] 3.4 Implement atlas coordinate function: `probe_index → atlas_uv_offset`

## 4. Probe Ray Dispatch Shader

- [ ] 4.1 Create `shaders/ddgi_probe_trace.hlsl` — compute shader (one thread per probe × ray)
- [ ] 4.2 Implement Fibonacci sphere distribution for ray directions (uniform, quasi-random rotation per frame)
- [ ] 4.3 Implement ray-scene intersection:
  - **DXR path**: `TraceRay(TLAS, ...)` with a any-hit and closest-hit shader returning `GBuffer`-like payload
  - **Analytical fallback**: ray-AABB test against `SpatialIndex` (from `phase1-ecs-spatial-query`)
- [ ] 4.4 Compute radiance at hit: sample `GBuffer` albedo/emissive at hit, compute direct lighting contribution using light list
- [ ] 4.5 Miss: sample sky radiance (or ambient constant)
- [ ] 4.6 Store ray radiance + distance into `RWStructuredBuffer<float4>` (temporary per-frame ray buffer)

## 5. Irradiance Blend Shader

- [ ] 5.1 Create `shaders/ddgi_irradiance_blend.hlsl` — compute shader (one thread per probe irradiance texel)
- [ ] 5.2 Implement octahedral irradiance encoding — project all rays' radiance into octahedral map using `encode_oct(dir)` → `(u, v)` texel coordinates
- [ ] 5.3 Implement temporal blend: `new_irradiance = lerp(history_irradiance, ray_irradiance, 1 - hysteresis)`
- [ ] 5.4 Apply probe border updates — copy edge texels to maintain atlas border for bilinear filtering
- [ ] 5.5 Same shader updates visibility atlas: `new_visibility = lerp(history, (mean_dist, mean_dist²), 1-hysteresis)`

## 6. Irradiance Sampling Library

- [ ] 6.1 Create `shaders/include/ddgi_sample.hlsli`
- [ ] 6.2 Implement `SampleDdgiIrradiance(world_pos, world_normal, CDdgiVolumeParams, irradiance_atlas, visibility_atlas) -> float3`:
  - Find 8 nearest probes (trilinear interpolation grid)
  - Per probe: check visibility (compare `dot(probe_to_surface, surface_normal)`)
  - Octahedral decode: convert `world_normal` → atlas UV, sample irradiance atlas
  - Weight by visibility and orientation; weighted sum
  - Return indirect irradiance (multiply by albedo in calling shader)
- [ ] 6.3 Include `ddgi_sample.hlsli` in `shaders/lighting.hlsl`

## 7. PBR Shader Integration

- [ ] 7.1 Add `#ifdef USE_DDGI` guard in `shaders/lighting.hlsl`
- [ ] 7.2 Bind `CDdgiVolumeParams` cbuffer and probe atlases in lighting pass descriptor set
- [ ] 7.3 Add indirect diffuse contribution: `indirect_diffuse = SampleDdgiIrradiance(world_pos, normal, ...) * albedo / PI`
- [ ] 7.4 Final color: `direct_lighting + indirect_diffuse`

## 8. DDGIPass Registration

- [ ] 8.1 Create `DdgiPass` as 2 `RgPass` entries: `ProbeTrace` (compute) → `IrradianceBlend` (compute)
- [ ] 8.2 Register in `RenderGraph` before lighting pass
- [ ] 8.3 `ProbeTrace` reads: GBuffer textures, persistent irradiance atlas (for ray radiance estimation)
- [ ] 8.4 `IrradianceBlend` reads: ray buffer, history irradiance atlas; writes: new irradiance atlas

## 9. Testing

- [ ] 9.1 Unit test: octahedral encode/decode roundtrip — `decode(encode(dir)) ≈ dir` within 0.001
- [ ] 9.2 Unit test: atlas coordinate function — probe `(0,0,0)` maps to correct texel offset
- [ ] 9.3 Unit test: probe weight computation — probe behind surface has weight 0
- [ ] 9.4 Integration test: `CDdgiVolume` placed in scene — `ProbeTrace` and `IrradianceBlend` passes execute without DX12 validation errors
- [ ] 9.5 Integration test: red wall reflects red light onto adjacent grey wall (visible in rendered frame)
- [ ] 9.6 Performance test: 256 probes × 64 rays — ProbeTrace completes in < 2ms on GPU

## 10. Documentation

- [ ] 10.1 Update `docs/specs/renderer.md` — "Global Illumination (DDGI)" section with probe layout, sample API
- [ ] 10.2 Create `docs/specs/global-illumination.md` — full DDGI spec: probe grid, ray dispatch, irradiance encoding
- [ ] 10.3 Update `docs/guides/scene-authoring.md` — "Setting Up Global Illumination" section with `CDdgiVolume` placement guidelines
