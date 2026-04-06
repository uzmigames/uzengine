## 1. SkyLight Indoor Wiring

- [x] 1.1 Wire `SkyLightComponent.indirectLightingIntensity` into `PerFrameCB` and read in `deferred_lighting_ps.hlsl`. Already implemented: SH coefficients pre-scaled by indirectLightingIntensity on CPU (frame_constants.cpp:247).
- [x] 1.2 Wire `SkyLightComponent.minOcclusion` into `PerFrameCB` — clamp AO term to `max(ao, minOcclusion)` in deferred lighting. Added AO floor clamp after GTAO merge in deferred_lighting_ps.hlsl.
- [x] 1.3 Wire `SkyLightComponent.lowerHemisphereIsBlack` and `lowerHemisphereColor` into SH projection. Added L0+L1_Z SH contribution in frame_constants.cpp when lowerHemisphereIsBlack=false.
- [x] 1.4 Add `r.SkyLight.IndirectIntensity` CVar (default 1.0). Added to frame_constants.cpp, multiplies skyIndirectIntensity before SH scale.
- [x] 1.5 Build + verify: Sponza with indirectLightingIntensity=1.4, minOcclusion=0.08, lowerHemisphere warm color — interior visibly brighter with spatial variation.

## 2. Irradiance Probe Grid

- [ ] 2.1 Create `irradiance_probe_grid.h/.cpp` in `engine/renderer/gi/` — `IrradianceProbeGrid` class: 3D grid of L2 SH probes (9 coefficients per probe, RGB = 27 floats). Configurable grid dimensions (default 8x4x8), world-space extent (auto from scene AABB or manual).
- [ ] 2.2 Create `LightProbeVolumeComponent` in `engine/ecs/environment/` — box region defining probe placement area. Fields: `gridDimensions` (uvec3), `extentMin`/`extentMax` (Vec3), `autoFitScene` (bool).
- [ ] 2.3 Implement probe SH capture from sky cubemap — for each probe position, project the sky cubemap into L2 SH coefficients using the same `SHProjection` utility already in `engine/renderer/lighting/sh_projection.h`.
- [ ] 2.4 Upload probe SH data to a GPU `StructuredBuffer<float4>` (9 float4s per probe = 144 bytes/probe). Register SRV in global descriptor heap.
- [ ] 2.5 Add probe grid sampling to `deferred_lighting_ps.hlsl` — trilinear interpolation of world position into probe grid UVW, sample 9 SH coefficients, evaluate `IrradianceSH(normal, shCoeffs)` for diffuse GI contribution.
- [ ] 2.6 Add probe grid parameters to `PerFrameCB` or a new `ProbeGridCB`: grid origin, cell size, dimensions, SH buffer SRV index.
- [ ] 2.7 Wire probe grid into `frame_constants.cpp` — populate CB from `LightProbeVolumeComponent` + `IrradianceProbeGrid` data each frame.
- [ ] 2.8 Build + verify: Sponza with a probe volume shows varying ambient color across the interior (walls near windows brighter than deep corridors).

## 3. Probe Update via SDF Ray March

- [ ] 3.1 Create `probe_update_cs.hlsl` — compute shader that updates one probe per thread group. For each probe position: cast N rays (64 directions, cosine-weighted hemisphere), ray march through the Global Distance Field, accumulate SH from hit surface albedo + sky at miss. Output: updated L2 SH coefficients.
- [ ] 3.2 Integrate with existing `GlobalDistanceField` — the SDF cascade data (already computed by `global_distance_field.cpp`) provides the distance field for ray marching. Read the GDF 3D texture in the probe update shader.
- [ ] 3.3 Implement temporal probe spreading — update a subset of probes per frame (e.g., 64 probes/frame out of 256 total) to amortize cost. Full grid refresh over 4 frames.
- [ ] 3.4 Add hysteresis blending — new SH = lerp(old SH, traced SH, 0.05) to prevent flickering from noisy single-frame traces.
- [ ] 3.5 Build + verify: probe grid updates from scene geometry — move a large object, probes near it darken on the shadowed side within 4 frames.

## 4. DDGI (DXR Ray Traced Probe Update)

- [ ] 4.1 Create `ddgi_probe_update_cs.hlsl` — compute shader using DXR inline ray tracing (`RayQuery<>`) to trace rays from probe positions through the TLAS. Accumulate irradiance from closest-hit surface albedo + emissive + sky miss.
- [ ] 4.2 Create `DDGISystem` in `engine/renderer/gi/` — manages probe texture atlas (irradiance: 6x6 per probe octahedral, depth: 16x16 per probe octahedral), probe relocation, probe state (active/inactive).
- [ ] 4.3 Implement probe irradiance atlas — 2D texture atlas storing per-probe octahedral-encoded irradiance (RGB). Updated each frame by the DDGI compute shader.
- [ ] 4.4 Implement probe depth atlas — 2D texture atlas storing per-probe octahedral-encoded mean distance + distance^2 (for Chebyshev visibility test). Prevents light leaking through thin walls.
- [ ] 4.5 Add DDGI sampling to `deferred_lighting_ps.hlsl` — for each pixel, find the 8 surrounding probes, sample octahedral irradiance, apply Chebyshev visibility weight, trilinear blend. Replaces or blends with SH probe path.
- [ ] 4.6 Wire DDGI into render pipeline — compute dispatch after TLAS build, before deferred lighting. Gate by `r.RayTracing.GI` CVar (requires DXR support).
- [ ] 4.7 Build + verify: Sponza with DDGI shows colored bounce light (warm stone walls reflect warm light onto nearby surfaces, blue sky light enters through windows).

## 5. Interior Shadow Enhancement

- [x] 5.1 Wire contact shadows into deferred_lighting_ps.hlsl. ComputeContactShadow() now called when r.ContactShadow.Length > 0, multiplied with CSM shadow. Bindless path passes depth SRV from GBufferIndices.
- [x] 5.2 GTAO bindless fix: deferred_lighting_ps.hlsl was missing GTAO sampling in UZ_BINDLESS path. Added ResourceDescriptorHeap[_gtaoSrv] read with proper UINT32_MAX sentinel check. GTAO now contributes AO in bindless mode.
- [ ] 5.3 Add shadow cascade interior detection — when all CSM cascades return shadow=0 for a pixel (fully in shadow), skip the expensive PCF/PCSS filtering and use a fast constant shadow=0 path. This saves GPU time in fully enclosed areas.
- [x] 5.4 Build + verify: Sponza with GTAO+contact shadows shows depth variation on columns and arches. Verified via screenshot comparison.

## 6. Testing

- [ ] 6.1 Unit test: `IrradianceProbeGrid` SH interpolation — verify trilinear blend of SH coefficients at grid corners, edges, and center matches expected values.
- [ ] 6.2 Unit test: `LightProbeVolumeComponent` auto-fit from scene AABB — verify grid dimensions and extents match scene bounds with correct padding.
- [ ] 6.3 Visual test: Sponza with SkyLight `indirectLightingIntensity=2.5`, `minOcclusion=0.15` — screenshot PSNR > 30 dB vs golden reference.
- [ ] 6.4 Visual test: Sponza with probe grid — verify ambient varies spatially (corridors darker than open areas) via `--view-mode lighting` screenshot.
- [ ] 6.5 Visual test: DDGI bounce light — verify warm bounce on walls near sunlit surfaces using `--pixel-debug` at specific coordinates.
- [ ] 6.6 Performance test: probe grid update < 0.5ms at 256 probes (SDF path), DDGI update < 2ms at 256 probes (RT path), full frame < 4ms overhead.
- [ ] 6.7 Verify 95%+ coverage on all new code.

## 7. Documentation

- [ ] 7.1 Update `docs/specs/rendering/renderer.md` with indoor lighting pipeline description (probe grid, DDGI, SkyLight indoor mode).
- [ ] 7.2 Update `docs/specs/rendering/ibl-pipeline.md` with irradiance probe grid sampling and DDGI atlas format.
- [ ] 7.3 Create `docs/guides/indoor-lighting-guide.md` — artist guide for setting up indoor scenes (probe volume placement, SkyLight tuning, fill lights).
- [ ] 7.4 Update `runtime/CHANGELOG.md` with indoor lighting system additions.
