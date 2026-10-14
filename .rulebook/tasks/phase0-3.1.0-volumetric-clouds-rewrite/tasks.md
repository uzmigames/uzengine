## Phase 0: Infrastructure (SceneRenderer Integration)
- [x] 0.1 CloudPass Init/Shutdown/Resize wired into SceneRenderer lifecycle. `scene_lifecycle.cpp`.
- [x] 0.2 CloudComposite graphics pipeline created in PipelineLibrary (SrcBlend=One, DstBlend=SrcAlpha, RGBA16F).
- [x] 0.3 `cloud_composite_ps.hlsl` — fullscreen PS with bilinear upscale + premultiplied-alpha blend. Entry point `main`.
- [x] 0.4 ECS gathering of VolumetricCloudsComponent (singleton semantics) in `frame_lighting.cpp`.
- [x] 0.5 CloudPassInput filled from component + frame data (invViewProj, camera km, sun, ambient SH L0).
- [x] 0.6 CloudComposite render graph pass registered (gated by m_cloudOutputSrv validity).
- [x] 0.7 VolumetricCloudsComponent entity added to PBR demo (coverage=0.5, altitude=5km, height=10km).
- [x] 0.8 Lua API bindings: `uz.scene.create_clouds`, `set_cloud_*`, `get_cloud_*` (full component API).
- [x] 0.9 Dedicated `m_cloudCmdList` member added to SceneRenderer.
- [x] 0.10 `.claude/rules/shader-entry-point.md` — engine shaders must use `main`, not PSMain/VSMain.
- [x] 0.11 Lens flare disabled by default (`m_lensFlareEnabled=false`), Lua toggle added.
- [x] 0.12 (verified 2026-10-08 by `scripts/rendering/test_cloud_noise_assets.py` N3 on cloud_test: 180 frames, no device removal, CloudCompute 1.30 ms, with both noise volumes loaded) **BLOCKED** (historical): Cloud compute dispatch causes GPU TDR. Noise bake with trivial shaders works; real Worley noise (29KB shader, 3x3x3 nested loops) crashes GPU. Weather/raymarch/temporal also crash. Root cause: likely shader instruction count or descriptor table validation under bindless root signature. Needs: rewrite noise as precomputed texture load OR optimize Worley to 2x2x2 search OR move to dedicated compute queue with async fence.

## Phase 0c: Cloud Render Graph Migration (moved from phase2a-2.50.0)
- [x] 0c.1 **Done 2026-10-14 (M0)**: CloudPass::RegisterPasses registers Cloud_Raymarch (Write cloudOut UAV + Read depth/shape/detail noise) and Cloud_Composite (Write hdr RT + Read cloudOut) as tracked rgBuilder passes; the out-of-graph dispatch + raw barriers were removed (scene_render_frame.cpp -25). Headful GBV: no TDR / shader-fail / new barrier errors / registry mismatch / double import; clouds now render on frame 0 AND frame 1 with the CURRENT-frame camera (previously previous-frame camera + absent on frame 0). See 01-m0-execution-2026-10-14.md.
- [x] 0c.2 Superseded/stale (2026-10-14): no WaitIdle exists in the cloud path; runtime noise baking was replaced with disk assets in 8332def6. Current-build headful `--frames 180 --gpu-validation` re-verification exited 0 without a TDR. Same-frame graph submission is tracked by 0c.1.
- [x] 0c.3 **Deferred 2026-10-14**: async-compute compatibility — the render graph is single-queue (architect plan §2), so there is no async path to mark async-capable. Revisit only if/when a compute queue is added.
- [x] 0c.4 Superseded/stale (2026-10-14): the requested 10% WaitIdle-removal gain has no applicable WaitIdle call. Noise has loaded from disk since 8332def6; current-build headful 180-frame GBV re-verification exited 0 without a TDR. M0 cost parity remains a separate verification gate, not an asserted performance gain.

M0 final verification (2026-10-14): ReleaseFast build and all CB layouts pass;
four-pose harness passes C1/C2/C3 (14/14 captures), mean sky image delta
<0.003%, largest cloud-cost regression +3.33%. The strict symmetric +/-10%
cost band misses only on sun/away improvements >10%. GBV has no new barrier
errors and 0 registry mismatches/double imports; eight pre-existing id 1422
SkyCubemap/GBuffer_Velocity initialization errors remain. Full evidence and
exact changed-file list are in `docs/analysis/volumetric-clouds-rewrite/01-m0-execution-2026-10-14.md`.

## Phase 0 Bugs Found (must fix before Phase 1)
- [x] 0.B1 Superseded/stale (2026-10-14): historical runtime Worley bake replaced by disk-loaded MIT three-geospatial assets in 8332def6; no runtime Worley shader remains. Current-build headful `cloud_test --frames 180 --gpu-validation` exited 0 without a TDR.
- [x] 0.B2 Superseded/stale (2026-10-14): the historical unrolled/looped Worley bake is no longer executed (8332def6 disk-noise replacement). Current-build headful 180-frame GBV re-verification exited 0 without a TDR.
- [x] 0.B3 Superseded/stale (2026-10-14): the March bake-era crash claim predates 8332def6 and the state registry. Current-build headful 180-frame GBV re-verification exited 0 without a TDR. The real undeclared cloud dependencies and previous-frame camera defect remain tracked by 0c.1.
- [x] 0.B4 (done: loaded from disk since 8332def6 / 00331657, and reachable from uzengine.exe since phase0_clouds-noise-assets, 2026-10-08) Need to investigate: precompute noise textures at build time (uzcook) or load from disk instead of GPU-bake at runtime.

## Phase 1: Adaptive Stepping + Blue Noise Jitter

Execution roadmap: [decision-complete plan](../../../docs/analysis/volumetric-clouds-rewrite/00-execution-plan-2026-10-14.md)
and [M0 evidence / remaining M1-M6](../../../docs/analysis/volumetric-clouds-rewrite/01-m0-execution-2026-10-14.md).
M1 (shape-volume sampling, capped adaptive march and jitter), M2 (multi-scattering,
temporal resolve and aerial perspective), M3 (Bayer upscale), M4 (Beer shadow maps),
M5 (multi-layer density) and M6 (cleanup/docs/tests) are multi-session follow-on
work; their unchecked items below remain open.

- [x] 1.1 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): runtime/content/atmosphere/stbn128.bin (128x128x64 R8, 1MB, seed 20261014, SHA bafcb1bb..; low/high power 0.062/0.025 < 0.1 gate) via scripts/rendering/generate_cloud_stbn.py.
- [x] 1.2 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): cloud_raymarch_cs.hlsl:80 adds minStepSize/maxStepSize/perspectiveStepScale/stbnSrvIndex in a 512-byte CB (cloud_pass.cpp:423 static_assert); root constants stay 12 DWORDs.
- [x] 1.3 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): cloud_raymarch_cs.hlsl:448-470 adaptive perspective-scaled stepping, COUNTED 256-iteration cap + min/max clamp (cannot reintroduce the TDR).
- [x] 1.4 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): cloud_raymarch_cs.hlsl:457 STBN jitter at pixel%128/frame%64 x 2*stepT x jitterScale.
- [x] 1.5 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): cloud_raymarch_cs.hlsl:471-474 zero-density steps grow toward max (mip-scaled) without bypassing the cap.
- [x] 1.6 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): cloud_raymarch_cs.hlsl:467 perspective-footprint LOD; cloud_pass.cpp:279-350 builds real filtered mip chains (8 shape, 6 detail) - the 128^3 shape volume is now SAMPLED (was uploaded-but-unused).
- [x] 1.7 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): volumetric_clouds_component.h:141-144 defaults (0.01/0.5/1.006/jitter 1) + JSON serialization.
- [x] 1.8 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): cloud_pass.cpp:357,500-518,560-568 STBN load + CB pack + tracked graph read.
- [x] 1.9 **M1 done 2026-10-14** (docs/analysis/volumetric-clouds-rewrite/02-m1-execution): builds; 2x 180-frame headful GBV exit 0, NO TDR, no compile-fail, zero registry mismatch (8 benign id-1422 remain); frame-0 clouds non-black. NO-BANDING gate MET - the banding map (|dY|/Y) shows blue only at real cloud edges, not stepping bands (.screenshots/cloud-m1/harness/after/clouds_montage.jpg). NOTE: clouds are flatter/low-contrast pending M2 multi-scattering (expected for minimum-ship M1). gtests 7674 pass / 3 pre-existing VfxEditorSlate / 16 Cloud+STBN tests pass.

## Phase 2: Multi-Scattering Octaves
- [ ] 2.1 Implement `EvaluateMultiScattering()` function in `cloud_raymarch_cs.hlsl`: 8-octave loop with attenuation factors a=0.5, b=0.5, c=0.5 per octave (Oz/Kulla 2017).
- [ ] 2.2 Replace ambient hack (`ambientColor * ambientMultiplier * saturate(0.3 + h)`) with octave multi-scatter + sky irradiance term.
- [ ] 2.3 Add sky irradiance gradient: `skyIrradiance * (1/4PI) * heightFraction * skyLightScale`.
- [ ] 2.4 Add optional powder effect: `radiance *= 1 - powderScale * exp(-extinction * powderExponent)`. New CB fields: `powderScale` (default 0.0 = off), `powderExponent` (default 2.0).
- [ ] 2.5 Optional: implement Draine phase function as `#define ACCURATE_PHASE_FUNCTION` compile path. gHG=0.988, gD=0.556, alpha=22.0, weight=0.482.
- [ ] 2.6 Update component with `powderScale`, `powderExponent`, `multiScatterOctaves` (default 8) parameters.
- [ ] 2.7 Build + visual verification: compare before/after, verify silver linings on cloud edges, brighter interiors.

## Phase 3: Quarter-Res Bayer Temporal Upscale
- [ ] 3.1 Change ray march resolution from half-res to quarter-res in `CloudPass::OnResize()`.
- [ ] 3.2 Implement 4x4 Bayer matrix selection in `cloud_raymarch_cs.hlsl`: only trace pixel where `bayerMatrix[pixel.x % 4][pixel.y % 4] == frameCount % 16`.
- [ ] 3.3 Rewrite `cloud_temporal_cs.hlsl` to full-resolution resolve: current-frame pixels pass through, others reproject + variance clip.
- [ ] 3.4 Implement variance clipping: 3x3 neighborhood mean/stddev in YCoCg space, clip history to `mean ± gamma * stddev`.
- [ ] 3.5 Add closest-depth velocity dilation: 3x3 search for closest fragment, use its velocity for reprojection.
- [ ] 3.6 Add cloud front depth output from ray march (transmittance-weighted average depth) for better reprojection.
- [ ] 3.7 Update ping-pong to work with quarter-res input + full-res output history.
- [ ] 3.8 Build + visual verification: verify no ghosting, clean temporal convergence, measure frame time improvement.

## Phase 4: Beer Shadow Maps (BSM)
- [ ] 4.1 Create `cloud_shadow_cs.hlsl`: orthographic ray march from sun direction, output RGBA16F BSM (frontDepth, meanExtinction, maxOpticalDepth, tailOpticalDepth).
- [ ] 4.2 Implement cascade frustum splitting (1-3 cascades, configurable) with practical split lambda.
- [ ] 4.3 Create `cloud_shadow_resolve_cs.hlsl`: per-cascade temporal AA with variance clipping.
- [ ] 4.4 Implement BSM sampling function: `SampleCloudShadow(worldPos, sunDir)` → `exp(-opticalDepth)`.
- [ ] 4.5 Add Vogel disk 16-tap PCF for cloud shadow sampling with sun-angle-dependent filter radius.
- [ ] 4.6 Integrate BSM sampling into `deferred_lighting_ps.hlsl`: `sunShadow *= SampleCloudShadow(worldPos, sunDir)`.
- [ ] 4.7 Integrate BSM sampling into `cloud_raymarch_cs.hlsl` for improved self-shadow (replace current light march).
- [ ] 4.8 Update `CloudPass` C++ to manage cascade textures, VP matrices, temporal history per cascade.
- [ ] 4.9 Add component parameters: `shadowCascadeCount` (default 2), `shadowResolution` (default 512), `shadowDistance` (default 50 km).
- [ ] 4.10 Build + visual verification: verify ground shadows from clouds, no shadow pop/shimmer.

## Phase 5: Aerial Perspective on Clouds
- [ ] 5.1 Compute transmittance-weighted average cloud depth in ray march output (new output channel or packed into alpha).
- [ ] 5.2 Sample transmittance LUT from camera to cloud depth using existing Bruneton atmosphere system.
- [ ] 5.3 Apply `cloudRadiance = cloudRadiance * atmTransmittance + atmInscatter * (1 - cloudTransmittance)`.
- [ ] 5.4 Build + visual verification: verify distant clouds fade to blue/haze correctly.

## Phase 6: Multi-Layer + Density Profiles
- [ ] 6.1 Extend `VolumetricCloudsComponent` with `CloudLayer` sub-struct array (up to 4 layers). Per layer: channel, altitude, height, densityScale, shapeAmount, detailAmount, weatherExponent, shapeAlteringBias, densityProfile(exp,exp,linear,constant).
- [ ] 6.2 Update weather map to use RGBA channels (one per layer). Support artist-authored weather texture path.
- [ ] 6.3 Implement per-layer density profile evaluation: `expTerm * exp(exponent * h) + linearTerm * h + constantTerm`.
- [ ] 6.4 Wire `cloudType` parameter to height-density gradient (stratus=flat, cumulus=towering profiles).
- [ ] 6.5 Implement layer interval skipping: skip ray segments between layers where no clouds exist.
- [ ] 6.6 Update CB packing to support per-layer float4 arrays.
- [ ] 6.7 Build + visual verification: verify multiple distinct cloud layers at different altitudes.

## Phase 7: Wire Dead Parameters + Cleanup
- [ ] 7.1 Wire `tracingMaxDistanceKm` to shader: clamp ray march `tExit = min(tExit, tracingMaxDistKm)`.
- [ ] 7.2 Wire `cloudType` to height gradient (if not done in 6.4).
- [ ] 7.3 Remove hardcoded constants: parameterize `kSkyLightCloudBottomVisibility`, UV scale factors, height gradient ramp ranges, shadow march distance.
- [ ] 7.4 Update `docs/references.md` with new cloud-related paper references.
- [ ] 7.5 Update `docs/specs/rendering/atmosphere.md` cloud section.

## Testing
- [ ] 8.1 Visual test: screenshot at default camera — no banding, silver linings, ground shadows visible.
- [ ] 8.2 Visual test: screenshot from above clouds — correct top-down appearance.
- [ ] 8.3 Visual test: screenshot at sunset angle — correct phase function glow toward sun.
- [ ] 8.4 Performance test: measure frame time before/after Bayer temporal upscale at 1080p.
- [ ] 8.5 Temporal stability test: capture 60-frame sequence, verify no ghosting or flickering.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
