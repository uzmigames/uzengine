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
- [x] 2.1 **M2 implementation 2026-10-14**: `cloud_raymarch_cs.hlsl:237-251` implements eight capped Oz/Kulla orders with a=b=c=0.5; `LightMarch` preserves optical depth before octave attenuation. The primary march remains capped at 256. Runtime captures compile and render non-black; see `docs/analysis/volumetric-clouds-rewrite/03-m2-execution-2026-10-14.md`.
- [x] 2.2 **M2 shipped 2026-10-14**: replaced the ambient hack with octave multi-scatter + a sky-irradiance term in cloud_raymarch_cs.hlsl. Root-caused + FIXED an initial darkening (sky-source normalization: packed E/pi was over-divided by 4pi + height-scaled without preserving base illumination; CPU pixel-replay confirmed). Corrected interiors are BRIGHTER than M1 (sun 2.07 vs 1.91, away 1.91 vs 1.77, above 3.18 vs 2.79) WITH octave-driven form/shading (see comparison.jpg - M2 clouds have 3D structure vs M1 flat haze).
- [x] 2.3 **M2 shipped 2026-10-14**: sky-irradiance gradient skyIrradiance*(1/4PI)*heightFraction*skyLightScale added (part of the corrected sky-source term in 2.2); contributes the base interior illumination that gives the clouds depth/variation.
- [x] 2.4 **M2 implementation 2026-10-14**: `cloud_raymarch_cs.hlsl:550` implements optional powder; `cloud_pass.cpp:419-421,437-439` packs/asserts offsets 312/316/320 inside the unchanged 512-byte CB. Defaults 0/2; 48-field DXC/C++ layout and negative controls pass.
- [x] 2.5 **M2 optional compile path 2026-10-14**: `cloud_raymarch_cs.hlsl:212-229` implements normalized HG/Draine with 0.988/0.556/22/0.482, default off; DXC `-D ACCURATE_PHASE_FUNCTION=1` exits 0 (`.screenshots/cloud-m2/draine.dxil`). Optional-path runtime appearance is not claimed.
- [x] 2.6 **M2 implementation 2026-10-14**: `volumetric_clouds_component.h:130-134` adds powderScale=0, powderExponent=2, multiScatterOctaves=8; JSON serialization and three frame-input assignments carry them to CloudPass. ReleaseFast exits 0; gtests 7674 pass unfiltered; explicitly filtered run 7661 pass / same 3 VfxEditorSlate failures / 17 skips, exactly matching retained M1 (delta 0). See `.screenshots/cloud-m2/gtests-filtered.log`; build target ignores trailing CLI filter, so GTEST_FILTER environment applies it.
- [x] 2.7 **M2 visual verify 2026-10-14 (shipped as a net improvement; one sub-item deferred)**: 2x+ bounded headful GBV exit 0 (134-143s), NO TDR / no compile-fail, non-black, M1 no-banding preserved (above-view HF +7.6% = more detail, not stepping bands). Interiors brighter + structured vs M1 flat haze across sun/away/above (comparison.jpg; metrics.json; brightness table in 03-m2-execution). gtests 7661 pass / 3 pre-existing VfxEditorSlate / 16 Cloud tests. DEFERRED to M2b follow-up: the sun-facing SILVER LININGS (forward-scatter edge glow) are not yet measurable (sun-window edge pixels ~zero) - needs the phase-function forward-lobe reaching cloud edges + a sun-pose silhouette metric (documented in 03-m2-execution). The shipped M2 is strictly better than M1; silver linings are an enhancement, not a regression.

## Phase 3: Quarter-Res Bayer Temporal Upscale
- [x] 3.1 **M3 opt-in implementation 2026-10-14**: `cloud_pass.cpp:48,212-228` gates divisor 4 behind `r.Clouds.TemporalUpscale=false`; off preserves divisor 2 and the M2 shader. Final ReleaseFast build exits 0. Default shader blobs equal M2 exactly; screenshot bit-identity limitation is recorded in 3.8.
- [x] 3.2 **M3 implementation**: `cloud_bayer.hlsli:3-23` selects the inverse 4x4 Bayer slot; `cloud_raymarch_upscale_cs.hlsl:153,499` traces that full-resolution sample and uses its scene depth. The original `cloud_raymarch_cs.hlsl` is deliberately unchanged to preserve the default. Render logs verify compiled/non-black on output at sun/away/above; march cap remains 256 at :532,544.
- [x] 3.3 **M3 implementation**: `cloud_temporal_cs.hlsl:54-82` resolves full resolution, passes current Bayer pixels through without blending and reprojects the rest using cloud depth; invalid/first history uses current spatial samples. Motion screenshots `.screenshots/cloud-m3/motion-on/motion/frames/f_{36,68,117,124,132,145}.png` show no visible smear or lingering cut imprint.
- [x] 3.4 **M3 implementation**: `cloud_temporal_cs.hlsl:110-137` computes 3x3 dense quarter-grid YCoCg/transmittance mean and stddev, clips history with gamma 1.25. Static sun screenshot CoV p95 on/off=1.079x; post-motion hold=1.10x, both below 1.2x. Both 512-byte CB layouts pass 52+16 field checks; `.screenshots/cloud-m3/layout.log`.
- [x] 3.5 **M3 implementation**: `cloud_temporal_cs.hlsl:84-105` searches all 3x3 depths, uses largest reversed-Z depth and its pixel-space velocity; clear sky uses cloud-point reprojection. Authorized tracked velocity wiring: `frame_atmosphere_passes.cpp:266-267`; graph read: `cloud_pass.cpp:770`. Motion lag MAE is smallest at offset 0 (0.33247 codes), not -1/+1 (0.41549/0.41467); `motion-metrics.json`.
- [x] 3.6 **M3 implementation**: `cloud_raymarch_upscale_cs.hlsl:589,608` writes opacity/transmittance-weighted average distance in km to quarter-resolution R32F, zero for no-hit rays; `cloud_temporal_cs.hlsl:75-80` reconstructs finite cloud points for translation-aware reprojection. Existing M1/M2 march/lighting remain bounded and unchanged in the default shader.
- [x] 3.7 **M3 implementation**: `cloud_pass.cpp:739-787` alternates full-resolution RGBA16F histories, reads quarter color/front depth and current depth/velocity, excludes uninitialized history reads and composites the written result. Final moving-camera GBV exits 0 in ~132.6s, 154 graphs, 0 claim disagreements/double imports; `.screenshots/cloud-m3/motion-gbv/motion/run.log`. Only the same eight pre-existing id 1422 errors remain.
- [x] 3.8 **M3 verified 2026-10-14 (CVar-gated, default OFF)**: builds; moving-camera GBV 132.6s exit 0, zero cloud/state errors, no TDR. CVar-ON perf: cloud GPU 6.418->1.778 ms (-72.3%), GPU frame 7.041->2.392 ms (-66.0%). Motion: no visible smearing/ghosting, lag favors current frame. gtests 7661 pass / 17 skip / 3 pre-existing VfxEditorSlate (delta 0/0/0). DEFAULT-OFF EQUIVALENCE: the default raymarch/composite shader blobs BYTE-MATCH M2; the 85.41 dB (1 code-value) diff vs M2 is the engine cross-process nondeterminism floor, NOT M3 - confirmed because M3-off vs M3-off ALSO differs by 1 code value (84.23 dB). So the default path is unchanged. Bit-identical screenshot not provable due to that pre-existing nondeterminism (documented 04-m3-execution).

## Phase 4: Beer Shadow Maps (BSM)
- [x] 4.1 **Gated implementation, 2026-10-15**: `runtime/shaders/runtime/atmosphere/cloud_shadow_cs.hlsl:25-103` marches the sun-side orthographic shell into RGBA16F BSM, capped at 256 samples. `uzembed` automatically registers CloudShadowCS and CloudShadowResolveCS (142 shaders verified). Live dispatch timings: `.screenshots/cloud-m4/ground-gbv/manifest.json`, Cloud_Shadow_Cascade0/1.
- [x] 4.2 `runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp:758-867` supports 1-3 cascades, practical split lambda, stabilized light-space footprints and VP/inverse-VP data. Fixed missing homogeneous division at :807-816. Ground GBV runs 2 x 512 cascades.
- [x] 4.3 `runtime/shaders/runtime/atmosphere/cloud_shadow_resolve_cs.hlsl:37-104` implements initialized-history gating, reprojection and 3x3 variance clipping (gamma 1.25); `cloud_pass.cpp:889-924` manages per-cascade ping-pong history. Both resolve dispatches measured in the ground GBV manifest; visual stability acceptance remains 4.10.
- [x] 4.4 `runtime/shaders/runtime/atmosphere/cloud_shadow.hlsli:53-89` samples a bindless 160-byte cascade structured buffer and returns exp(-opticalDepth), with map-edge cascade blending. Temporary diagnostic `.screenshots/cloud-m4/diagnostic_20.png` confirmed valid projection, varying BSM values and reached deferred sampling; diagnostic shader code was removed.
- [x] 4.5 `runtime/shaders/runtime/atmosphere/cloud_shadow.hlsli:30-50` implements 16-tap Vogel filtering and sun-elevation-dependent radius. Compiles and executes in the opt-in deferred variant; recognizable shadow-edge quality is not claimed (4.10).
- [x] 4.6 `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:49-52,213-216,606-610` gates sun-shadow multiplication behind UZ_CLOUD_GROUND_SHADOWS. `runtime/engine/renderer/frame/frame_lighting_pass.cpp:202-210` binds the alternate PSO and buffer/count; `passes/lighting/lighting_pass.cpp:64-65,159-176` declares resolved texture reads and pushes 11 DWORDs only when enabled (9 when off, fixed global limit 24). Default-off DXIL SHA-256 matches a1f16cd8 exactly: `5782c170ec7e35bcca9b8b4f53f3eb0716d0f2d6f5f7b788224f096cb0753a9c`. Twelve default screenshots also byte-match baseline pixel values; `.screenshots/cloud-m4/comparison.json`.
- [x] 4.7 `runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl:548-555` and the upscale counterpart replace LightMarch with BSM optical depth only under UZ_CLOUD_BSM; `cloud_pass.cpp:729-732,1004-1028` selects those pipelines and declares shadow reads. The original default shader branch and 256-iteration cap remain. Improved self-shadow appearance is not independently established.
- [x] 4.8 `runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp:665-924` owns cascade textures, matrices, 256-byte shadow CB, four upload-buffer slots and temporal history. Stable literal pass names at :877-902 fix a dangling-name/corrupted-manifest defect found during verification. `frame/frame_lighting.cpp:258` registers shadow producers before deferred lighting; cloud colour remains after lighting. Ground GBV: 28 graphs, zero state disagreements/double imports.
- [x] 4.9 `runtime/engine/ecs/environment/volumetric_clouds_component.h:157-159` and `.cpp:62-64,110-112` serialize defaults 2/512/50 km; `runtime/engine/renderer/frame/frame_atmosphere_passes.cpp:39-81` consumes them. ReleaseFast build exits 0 (142 shaders). Final filtered gtests: 7661 pass / 17 skip / same 3 VfxEditorSlate failures, delta 0/0/0; `.screenshots/cloud-m4/gtests-final.log` and XML.
- [ ] 4.10 **Visual DEFERRED 2026-10-15 (default-safe; opt-in path needs follow-up)**: the gated BSM integration RUNS correctly - clear-sky control = optical depth 0 / transmittance 1 (ground lit), the BSM dispatches + the cascade structured buffer binds + deferred sampling is reached (no unit/scale error: shell-clipped km + step-length weighting). DEFAULT (r.Clouds.GroundShadows OFF) is PROVEN safe: compiled deferred DXIL for the gate-off path byte-matches a1f16cd8 + 12/12 GBV frames zero pixel delta. gtests 0/0/0, GBV exit 0 (126s). BUT with the gate ON on the available m4-ground scene the ground is near-black, not recognizable cloud silhouettes - because the scene is fully OVERCAST (cascade depth 2.94-5.79, no clear columns) so the sun is fully blocked, and the ground lacks a sky-ambient floor. FOLLOW-UP (M4b): a scattered/broken-cloud scene to show silhouettes + an ambient-shadow floor so a fully-shadowed ground is dim (not black). This is an opt-in-path visual-tuning + content item, NOT a default-path regression.

  **Focused scale follow-up, 2026-10-15:** float readback at coverage 0.45 gives ground optical depth 2.9443-5.7934, median 4.6917; no clear columns exist in the sampled first cascade (atlas depth 2.8906-5.9219). A true coverage-zero control gives depth 0 / transmittance 1 everywhere. Shell-clipped km integration already includes stepLength and matches the shared cloud extinction normalization; no unit-scale error was demonstrated. Base-mip density sampling still had no clear columns and worsened median depth to 5.6320, so it was reverted. A scalar reduction cannot produce both clear and cloudy columns from this signal. Remaining blocker: shared density/column-coverage calibration, not missing BSM plumbing. Temporary diagnostics and fixture edits were removed; final default DXIL still byte-matches a1f16cd8. See the focused investigation in `05-m4-execution-2026-10-14.md` and `.screenshots/cloud-m4/{optical,optical-clear,optical-base}/`.

  Final follow-up GBV: `.screenshots/cloud-m4/ground-scale-gbv/run.log`, exit 0 in ~124 seconds (175-second bound), 28 graphs and no new validation errors. Opened `ground-scale-gbv/frames/f_20.png`: silhouettes still FAIL; ground remains nearly uniformly dark. Final default-off DXIL SHA-256 is unchanged (`5782c170...0753a9c`). 4.10 deliberately remains [ ].

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
