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
- [x] 0.12 (verified 2026-10-08 by `scripts/test_cloud_noise_assets.py` N3 on cloud_test: 180 frames, no device removal, CloudCompute 1.30 ms, with both noise volumes loaded) **BLOCKED** (historical): Cloud compute dispatch causes GPU TDR. Noise bake with trivial shaders works; real Worley noise (29KB shader, 3x3x3 nested loops) crashes GPU. Weather/raymarch/temporal also crash. Root cause: likely shader instruction count or descriptor table validation under bindless root signature. Needs: rewrite noise as precomputed texture load OR optimize Worley to 2x2x2 search OR move to dedicated compute queue with async fence.

## Phase 0c: Cloud Render Graph Migration (moved from phase2a-2.50.0)
- [ ] 0c.1 Register cloud compute as a proper `rgBuilder.AddPass("Cloud_Raymarch", ...)` with declared resource dependencies (depth SRV read, cloud output UAV write).
- [ ] 0c.2 Remove WaitIdle: after render graph integration, the cloud compute runs as part of the normal command list submission without explicit GPU synchronization.
- [ ] 0c.3 Verify async compute compatibility: if cloud compute can overlap with shadow rendering or GBuffer fill, mark the pass as async-capable in the render graph.
- [ ] 0c.4 Performance verification: measure frame time before/after. The WaitIdle removal should reduce frame time by eliminating the full GPU drain. Target: at least 10% frame time improvement on scenes with clouds.

## Phase 0 Bugs Found (must fix before Phase 1)
- [ ] 0.B1 Noise shaders: `VoronoiCornerSample` had `normalize(zero)` → NaN. Fixed with rsqrt guard. But real noise still TDRs — shader too heavy for single dispatch (128^3 × 27-iteration Worley).
- [ ] 0.B2 Noise shaders: `[unroll]` on 3x3x3 Worley loops → 29KB shader binary. Changed to `[loop]` but still TDRs.
- [ ] 0.B3 Cloud compute dispatch on main cmdList corrupts render graph state (HDR goes white). Pre-graph dispatch tested, post-graph dispatch tested, dedicated cmdList tested — all crash or corrupt.
- [x] 0.B4 (done: loaded from disk since 8332def6 / 00331657, and reachable from uzengine.exe since phase0_clouds-noise-assets, 2026-10-08) Need to investigate: precompute noise textures at build time (uzcook) or load from disk instead of GPU-bake at runtime.

## Phase 1: Adaptive Stepping + Blue Noise Jitter
- [ ] 1.1 Generate or integrate STBN 3D blue noise texture (128x128x64 R8_UNORM). Compute shader or embedded asset.
- [ ] 1.2 Add new CB fields to `cloud_raymarch_cs.hlsl`: `minStepSize`, `maxStepSize`, `perspectiveStepScale`, `stbnSrvIndex`.
- [ ] 1.3 Replace uniform step loop with adaptive perspective-scaled stepping: `stepT *= perspectiveStepScale` each iteration, clamped to `[minStepSize, maxStepSize]`.
- [ ] 1.4 Add blue noise jitter: initial ray offset `t += stepT * stbn[pixel % 128, frame % 64] * 2.0`.
- [ ] 1.5 Add empty space skipping: when `density <= 0`, enlarge step toward `maxStepSize` based on mip level.
- [ ] 1.6 Add MIP level computation: `mipLevel = log2(max(1, startTexelsPerPixel + rayDistance * 1e-5))` for LOD on texture samples.
- [ ] 1.7 Update `VolumetricCloudsComponent` with new parameters (minStepSize=0.01, maxStepSize=0.5, perspectiveStepScale=1.006).
- [ ] 1.8 Update `CloudPass` C++ to create STBN texture, bind SRV, pack new CB fields.
- [ ] 1.9 Build + visual verification: capture screenshots at multiple camera positions, verify no banding.

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
