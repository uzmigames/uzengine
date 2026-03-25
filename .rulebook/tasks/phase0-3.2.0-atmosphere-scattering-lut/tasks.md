## Phase 1: Multi-Scatter in AP Volume (P0 — trivial fix)
- [x] 1.1 Read `aerial_perspective_volume_cs.hlsl`, identify the per-voxel ray march loop. Identified 8-step march loop at line 180.
- [x] 1.2 Add `MultiScatterLUT.SampleLevel(linearClamp, multiScatterUV, 0)` per step. Added `AP_SampleMultiScatteringLut()` in aerial_perspective.hlsli + sampling in march loop via `ResourceDescriptorHeap[multiScatterLutIdx]`.
- [x] 1.3 Accumulate multi-scattered contribution: `singleScatter = sunT * phaseScatter` + `multiScatter = multiScatteredLum * totalScatter`, combined into Frostbite analytical integral.
- [x] 1.4 Bind multi-scatter LUT SRV in `atmosphere_renderer.cpp`. Added `multiScatterLutIdx` to APVolumeCB (replacing _pad0), set from `ctx.multiScatterLutSrv`. Added `multiScatterLutDep` to APVolumePassDesc with RG read dependency.
- [x] 1.5 Build passes (zig build — zero errors). Visual verification pending demo run.

## Phase 2: Bruneton 3D Scattering LUT Precomputation (P1 — core)
- [x] 2.1 Create `runtime/shaders/runtime/atmosphere/single_scattering_lut_cs.hlsl`. 50-step Frostbite integration, 3 UAV outputs (deltaRayleigh, deltaMie, scattering packed Rayleigh.rgb+Mie.r), planet shadow via ray-sphere, exact LUT transmittance.
- [x] 2.2 Implement 4D↔3D texture coordinate mapping functions in `atmosphere_lut.hlsli`: `GetRMuMuSNuFromScatteringTextureUvwz()`, `GetScatteringTextureUvwzFromRMuMuSNu()`, `GetRMuMuSNuFromScatteringTextureFragCoord()`, plus transmittance/irradiance mappings, ray-sphere distances, sampling helpers (`SampleScatteringTexture`, `SampleScatteringTexture4`, `GetTransmittance`, `GetTransmittanceToSun`). All follow Bruneton 2017 reference exactly (BSD-3 licensed functions.glsl).
- [x] 2.3 Create `runtime/shaders/runtime/atmosphere/scattering_density_cs.hlsl`: compute density of N-th order scattered light. Reads single scattering + previous multiple scattering + irradiance textures via bindless. Integrates over 16 Fibonacci sphere directions with Rayleigh+Mie phase weighting, plus ground-reflected irradiance. CBV at b2 with ozone params, texture indices, mieAnisotropy, groundAlbedo, scatteringOrder, muSMin.
- [x] 2.4 Create `runtime/shaders/runtime/atmosphere/indirect_scattering_cs.hlsl` (replaces multiple_scattering_lut_cs concept): 50-step ray-march integrating scattering density along view rays using Frostbite analytical integration. Writes delta scattering (overwrite) and accumulates additively into total scattering texture. Uses GetTransmittance from atmosphere_lut.hlsli for exact camera-to-sample transmittance.
- [x] 2.5 Create `runtime/shaders/runtime/atmosphere/irradiance_lut_cs.hlsl`: 64x16 compute shader. Order 1: direct irradiance from transmittance LUT (T*max(mu_s,0)). Order N>1: 32-direction Fibonacci hemisphere integration of incoming scattered light weighted by cos(theta). Outputs delta irradiance + additive accumulated irradiance. Handles separate Rayleigh/Mie phase for order 2 vs combined delta for order>2.
- [x] 2.6 Implemented precomputation orchestration in `atmosphere_renderer.cpp`: SingleScatterLUT → DirectIrradiance → loop(ScatteringDensity → IndirectIrradiance → IndirectScattering) × 4 orders. Render graph barriers automatic. Dirty-flag driven via `scatteringLut3DDirty`.
- [x] 2.7 Allocated GPU resources in `scene_renderer_init.cpp`: scatteringLut3D + deltaRayleigh3D + deltaMie3D + scatteringDensity3D (256x128x32 RGBA16F 3D) + irradianceLut + deltaIrradiance (64x16 RGBA16F 2D). Context wired in `frame_atmosphere_passes.cpp`.
- [x] 2.8 Build passes (zig build — zero errors). Pipeline getters added to pipeline_library (4 new Ensure+Get pairs). Shutdown cleanup added.

## Phase 3: Sky Rendering via LUT Lookup (replaces per-pixel march)
- [x] 3.1 Implemented LUT lookup inline in `sky_atmosphere_ps.hlsl` using `SampleScatteringTexture4()` from `atmosphere_lut.hlsli`. Mie extrapolation: `mie.rgb = rayleigh.rgb * (scatSample.a / max(rayleigh.r, 1e-6))`. No separate `atmosphere_common.hlsli` — logic is in the PS directly for clarity.
- [ ] 3.2 Implement `GetSkyRadianceToPoint(camera, point, sunDir)` for aerial perspective: dual lookup at camera and point positions, subtract attenuated point scattering. (Deferred to Phase 5 — AP volume upgrade.)
- [x] 3.3 Replaced 32-step ray march with LUT lookup in `sky_atmosphere_ps.hlsl`. Ray march kept as fallback inside `else` block (SM5 path or LUT not yet precomputed). Sun disk rendering preserved (analytical, unchanged). `SkyIndices` struct extended with `scatteringLut3DIdx` (5th root constant).
- [x] 3.4 Phase functions applied at lookup time: `L = rayleigh * phaseR * rayleighScatteringScale + mie * phaseM * mieScatteringScale`. Uses existing `RayleighPhase()` and `MiePhase()` (Henyey-Greenstein).
- [x] 3.5 Ground intersection handled: `RayIntersectsGround()` from `atmosphere_lut.hlsli` + `GetTransmittance()` for camera-to-ground transmittance. Existing ground contribution code (orbital altitude > 10km Lambertian reflection) works with both LUT and march paths via shared `Throughput` variable. Irradiance LUT integration deferred to Phase 4.
- [x] 3.6 Bound scattering 3D LUT SRV in sky_atmosphere_pass.h/cpp/atmosphere_renderer.cpp. 5 root constants (was 4). ScatteringLUT3D imported into render graph with proper state tracking (UAV→SRV barrier).
- [x] 3.7 Build passes (zero errors after fixing muSMin arg in single_scattering_lut_cs). Visual verification: sky renders via LUT lookup at 595 FPS, correct blue gradient + white horizon. Fallback march preserved in else block.

## Phase 4: Irradiance LUT Integration
- [x] 4.1 Irradiance LUT integrated into sky PS: `GetIrradianceTextureUvFromRMuS()` lookup in ground contribution section. Dual path: LUT when available, fallback to direct Lambertian. SkyIndices extended with `irradianceLutIdx` (6th root constant).
- [ ] 4.2 Expose irradiance LUT SRV for height fog system. (Deferred — SH projection from cubemap already provides fog inscattering color.)
- [ ] 4.3 Optional: CPU readback of irradiance. (Deferred — not needed for visual parity.)
- [x] 4.4 Build passes (zero errors). Visual verification: 601 FPS, correct sky + ground lighting, no GPU errors.

## Phase 5: AP Volume Upgrade (use scattering LUT)
- [ ] 5.1 Replace the 8-step per-voxel march in `aerial_perspective_volume_cs.hlsl` with `GetSkyRadianceToPoint()` LUT lookup (dual 4D lookup per voxel). (P2 — optimization, AP volume already has multi-scatter from Phase 1.)
- [ ] 5.2 This eliminates the per-voxel march entirely — each voxel becomes two 3D texture samples.
- [ ] 5.3 Multi-scattering is now implicit in the scattering LUT (all orders baked), so the Phase 1 fix becomes redundant (but harmless).
- [ ] 5.4 Build + visual verification.

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
