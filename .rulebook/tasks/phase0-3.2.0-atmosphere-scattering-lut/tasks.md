## Phase 1: Multi-Scatter in AP Volume (P0 — trivial fix)
- [x] 1.1 Read `aerial_perspective_volume_cs.hlsl`, identify the per-voxel ray march loop. Identified 8-step march loop at line 180.
- [x] 1.2 Add `MultiScatterLUT.SampleLevel(linearClamp, multiScatterUV, 0)` per step. Added `AP_SampleMultiScatteringLut()` in aerial_perspective.hlsli + sampling in march loop via `ResourceDescriptorHeap[multiScatterLutIdx]`.
- [x] 1.3 Accumulate multi-scattered contribution: `singleScatter = sunT * phaseScatter` + `multiScatter = multiScatteredLum * totalScatter`, combined into Frostbite analytical integral.
- [x] 1.4 Bind multi-scatter LUT SRV in `atmosphere_renderer.cpp`. Added `multiScatterLutIdx` to APVolumeCB (replacing _pad0), set from `ctx.multiScatterLutSrv`. Added `multiScatterLutDep` to APVolumePassDesc with RG read dependency.
- [x] 1.5 Build passes (zig build — zero errors). Visual verification pending demo run.

## Phase 2: Bruneton 3D Scattering LUT Precomputation (P1 — core)
- [ ] 2.1 Create `runtime/shaders/runtime/atmosphere/single_scattering_lut_cs.hlsl`: compute shader that writes to a 256x128x32 3D UAV. For each texel, decode (r, mu, mu_s, nu) from the 4D→3D packing, integrate single Rayleigh+Mie scattering along the view ray using transmittance LUT. Output RGBA: Rayleigh.rgb + Mie.r (combined scattering packing).
- [ ] 2.2 Implement 4D↔3D texture coordinate mapping functions in `atmosphere_common.hlsli`: `GetRMuMuSNuFromScatteringTextureUVWZ()`, `GetScatteringTextureUVWZFromRMuMuSNu()`. Follow Bruneton 2017 non-linear parameterization exactly.
- [ ] 2.3 Create `runtime/shaders/runtime/atmosphere/scattering_density_cs.hlsl`: compute density of N-th order scattered light. Reads single scattering + previous multiple scattering + irradiance textures. Writes to temporary 3D density texture.
- [ ] 2.4 Create `runtime/shaders/runtime/atmosphere/multiple_scattering_lut_cs.hlsl`: integrate scattering density to compute N-th order multiple scattering. Accumulate additively into the main scattering 3D texture.
- [ ] 2.5 Create `runtime/shaders/runtime/atmosphere/irradiance_lut_cs.hlsl`: 64x16 compute shader. Pass 1: direct irradiance from transmittance LUT. Pass 2+: indirect irradiance from scattering textures, accumulated per order.
- [ ] 2.6 Implement precomputation orchestration in `atmosphere_renderer.cpp`: single_scatter → (density → indirect_irradiance → multiple_scatter) × 4 orders. Barrier between each pass. Dirty-flag driven (recompute when params change).
- [ ] 2.7 Allocate GPU resources: scattering 3D texture (256x128x32 RGBA16F), irradiance 2D texture (64x16 RGBA16F), temporary density 3D texture (same dims, can alias with scattering after precompute).
- [ ] 2.8 Build + verify precomputation runs without errors or GPU hangs. Check scattering LUT is non-zero and visually reasonable via buffer visualization.

## Phase 3: Sky Rendering via LUT Lookup (replaces per-pixel march)
- [ ] 3.1 Implement `GetSkyRadiance(r, mu, mu_s, nu)` in `atmosphere_common.hlsli`: single 4D→3D texture lookup into scattering LUT. Decompose Mie from combined packing using `mie.rgb = mie.r * (rayleigh.rgb / max(rayleigh.r, 1e-6))` extrapolation.
- [ ] 3.2 Implement `GetSkyRadianceToPoint(camera, point, sunDir)` for aerial perspective: dual lookup at camera and point positions, subtract attenuated point scattering.
- [ ] 3.3 Replace the 32-step ray march loop in `sky_atmosphere_ps.hlsl` with a call to `GetSkyRadiance()`. Keep sun disk rendering (analytical, not from LUT).
- [ ] 3.4 Add phase function modulation: multiply Rayleigh by `RayleighPhase(nu)` and Mie by `CornetteShanksPhase(g, nu)` at lookup time (Bruneton runtime.glsl pattern).
- [ ] 3.5 Handle ground intersection: if view ray hits ground, compute ground radiance from irradiance LUT + transmittance + ground albedo.
- [ ] 3.6 Bind scattering 3D LUT SRV in sky atmosphere pass descriptor.
- [ ] 3.7 Build + visual verification: A/B compare old (32-step march) vs new (LUT lookup). Should be visually identical but ~32x faster.

## Phase 4: Irradiance LUT Integration
- [ ] 4.1 Use irradiance LUT (from Phase 2) for ground lighting in sky PS: `GetIrradiance(r, mu_s)` lookup replaces ground color approximation.
- [ ] 4.2 Expose irradiance LUT SRV for height fog system: use ground irradiance for fog inscattering color instead of hardcoded sky color.
- [ ] 4.3 Optional: CPU readback of irradiance at camera position for sun/sky color. 1x1 compute dispatch that samples the LUT at (cameraAltitude, sunZenith), readback via staging buffer.
- [ ] 4.4 Build + visual verification: ground color should match sky at horizon. Fog inscattering should be physically derived.

## Phase 5: AP Volume Upgrade (use scattering LUT)
- [ ] 5.1 Replace the 8-step per-voxel march in `aerial_perspective_volume_cs.hlsl` with `GetSkyRadianceToPoint()` LUT lookup (dual 4D lookup per voxel).
- [ ] 5.2 This eliminates the per-voxel march entirely — each voxel becomes two 3D texture samples.
- [ ] 5.3 Multi-scattering is now implicit in the scattering LUT (all orders baked), so the Phase 1 fix becomes redundant (but harmless).
- [ ] 5.4 Build + visual verification: aerial perspective quality should improve (exact multi-scatter vs approximation). Performance should be similar or better.

## Phase 6: Cleanup + Optimization
- [ ] 6.1 Remove the now-unused 32-step ray march code from `sky_atmosphere_ps.hlsl` (keep behind `#ifdef FALLBACK_RAYMARCH` for debugging).
- [ ] 6.2 Consider removing the Hillaire 2020 multi-scatter 2D LUT if the Bruneton 3D LUT fully replaces it. Keep if AP volume fallback needs it.
- [ ] 6.3 Update `sky_cubemap_cs.hlsl` to use `GetSkyRadiance()` LUT lookup instead of its current 16-step march. Cubemap bake becomes ~16x faster.
- [ ] 6.4 Profile: measure frame time reduction for sky pass and AP volume pass.
- [ ] 6.5 Update `docs/references.md` with Bruneton 2017 precomputation pipeline details.
- [ ] 6.6 Update `docs/specs/rendering/atmosphere.md` with new LUT architecture.

## Testing
- [ ] 7.1 Visual test: sky color at noon — should match reference (blue zenith, white horizon).
- [ ] 7.2 Visual test: sky color at sunset — should match reference (orange/red horizon, purple zenith).
- [ ] 7.3 Visual test: aerial perspective — distant mountains should have correct blue haze.
- [ ] 7.4 Visual test: ground color — should smoothly blend with sky at horizon.
- [ ] 7.5 Performance test: measure sky PS time before (32-step) and after (LUT lookup).
- [ ] 7.6 Performance test: measure AP volume fill time before (8-step march) and after (LUT lookup).
- [ ] 7.7 Regression test: toggle between old march and new LUT to verify visual parity.
