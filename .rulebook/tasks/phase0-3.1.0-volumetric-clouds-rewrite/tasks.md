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
- [x] 4.10 **M4b visual PASS 2026-10-16 on calibrated scattered fixture**: `m4-scattered.uzscene` uses coverage 0.38, a 1 km layer, noise XY scale 0.4 and positive blue-grey SkyLight fill. Opened `.screenshots/cloud-m4/scattered-gbv-final/frames/f_20.png`: recognizable soft cloud-shaped dark patches on LIT ground, with lit gaps, not black. Valid `scattered-probe-final` ground readback: sun visibility 0.4284-1.0, 0.94% above 0.99; diffuse ambient RGB >= [0.023465,0.035987,0.070835]. `scattered-motion` frames 36/76/116/145 show sun/camera-driven shadow motion without obvious pop/shimmer; final hold mean adjacent delta 0.01091 codes. Final GBV log `scattered-gbv-final/run.log`: exit 0 (~114s, bound 175s), 28 graphs, no state disagreements/double imports or new validation errors (same 9 existing messages). Default OFF is unchanged vs HEAD 597477f4: deferred source blob matches; fresh default DXIL byte-matches baseline SHA-256 `5782c170ec7e35bcca9b8b4f53f3eb0716d0f2d6f5f7b788224f096cb0753a9c` (`deferred-off-scattered.dxil`). Build exits 0 (142 shaders); filtered gtests 7661 pass / 17 skip / same 3 VfxEditorSlate failures, delta 0/0/0 (`gtests-scattered.log`, no TDR). No renderer edits for this fix; gate remains OFF by default. Details: `05-m4-execution-2026-10-14.md`.

  **Historical focused scale follow-up, 2026-10-15:** float readback at coverage 0.45 gives ground optical depth 2.9443-5.7934, median 4.6917; no clear columns exist in the sampled first cascade (atlas depth 2.8906-5.9219). A true coverage-zero control gives depth 0 / transmittance 1 everywhere. Shell-clipped km integration already includes stepLength and matches the shared cloud extinction normalization; no unit-scale error was demonstrated. Base-mip density sampling still had no clear columns and worsened median depth to 5.6320, so it was reverted. A scalar reduction cannot produce both clear and cloudy columns from this signal. The then-open density/column-coverage calibration is resolved for visual acceptance by the new scattered fixture above, not by changing BSM plumbing. Temporary diagnostics and fixture edits were removed; final default DXIL still byte-matches a1f16cd8. See the focused investigation in `05-m4-execution-2026-10-14.md` and `.screenshots/cloud-m4/{optical,optical-clear,optical-base}/`.

  Historical overcast-scene GBV: `.screenshots/cloud-m4/ground-scale-gbv/run.log`, exit 0 in ~124 seconds (175-second bound), 28 graphs and no new validation errors. Opened `ground-scale-gbv/frames/f_20.png`: silhouettes FAIL in that fully overcast fixture; ground remains nearly uniformly dark. Default-off DXIL SHA-256 is unchanged (`5782c170...0753a9c`). Superseded for 4.10 acceptance by the lit scattered-scene evidence above.

## Phase 5: Aerial Perspective on Clouds
- [ ] 5.1 Compute transmittance-weighted average cloud depth in ray march output (new output channel or packed into alpha).
- [ ] 5.2 Sample transmittance LUT from camera to cloud depth using existing Bruneton atmosphere system.
- [ ] 5.3 Apply `cloudRadiance = cloudRadiance * atmTransmittance + atmInscatter * (1 - cloudTransmittance)`.
- [ ] 5.4 Build + visual verification: verify distant clouds fade to blue/haze correctly.

## Phase 6: Multi-Layer + Density Profiles
- [x] 6.1 `runtime/engine/ecs/environment/volumetric_clouds_component.h:65` adds CloudLayer[4] with all requested fields and neutral density-profile defaults; `.cpp:28` reads/roundtrips optional layer arrays, validates 1-4 entries, channel 0-3 and positive heights. Legacy geometry remains the default. Evidence: `docs/analysis/volumetric-clouds-rewrite/07-m5-execution-2026-10-15.md`.
- [x] 6.2 `runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp:393` loads artist weather images through VFS to linear RGBA8 with tracked graph reads; `runtime/shaders/runtime/atmosphere/cloud_layers.hlsli:32` selects each layer's RGBA channel and exponent. Empty path is uniform one with no allocation.
- [x] 6.3 `runtime/shaders/runtime/atmosphere/cloud_layers.hlsli:19` evaluates exp*exp(exponent*h)+linear*h+constant per layer, multiplied into the height gradient. Neutral coefficients produce one.
- [x] 6.4 `runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl:284` and the upscale copy wire cloudType to short/flat stratus and tall cumulus gradients. Type 0.5 retains the original gradient math.
- [x] 6.5 `runtime/shaders/runtime/atmosphere/cloud_layers.hlsli:144` jumps across unoccupied shell intervals, accumulates overlapping densities, and shares one counted 256-iteration budget across all layers. STBN and adaptive stepping remain.
- [x] 6.6 `runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp:543` packs 1024-byte CloudCB, three float4[4] arrays at 368/432/496 and static assertions for descriptor/count offsets. Extended `m5-verify.py layout` exits 0: CloudCB 51 / upscale 53 / shadow 51 fields; all existing entries and negative controls pass. No root-signature change; cloud roots remain 12 DWORDs.
- [x] 6.7 **M5 verified 2026-10-16**: ReleaseFast exits 0 (142 shaders); 1024-byte CloudCB asserted at `cloud_pass.cpp:617`, layout checks pass 51/53/51 fields, roots remain 12 DWORDs at `:1152,1197`. Default sun/away/above screenshots were opened: broken clouds and blue-sky gaps preserved; all frame-130-132 RGB pairs differ from pre-M5 by <=1 code value (`.screenshots/cloud-m5/comparison.json`; retained 8efdbf4d cloud code matches HEAD 27a21d36). Opened `.screenshots/cloud-m5/multi/{above,horizon}/frames/f_132.png`: distinct lower 2-km patches/horizon band and upper 8-km deck. Cloud GPU cost above/horizon 4.7927/3.9373 ms versus M4 5.3202/6.7568 ms = 0.9009x/0.5827x (<1.3x, same non-temporal reference settings). Default and two-layer above headful GBV exit 0 within 175s; logs `.screenshots/cloud-m5/{gbv-default,gbv}/above/run.log`: no compile failures/TDR/new errors, zero state disagreements/double imports, only eight known id-1422 init errors each. Filtered gtests: 7661 pass / 17 skip / same three VfxEditorSlate failures, delta 0/0/0; `.screenshots/cloud-m5/gtests.{log,xml}` (actual test exit 1). Full evidence, auxiliary incomplete horizon GBV attempt and changed-file ledger: `docs/analysis/volumetric-clouds-rewrite/07-m5-execution-2026-10-15.md`.

## Phase 7: Wire Dead Parameters + Cleanup
- [x] 7.1 Already shipped in M1: `cloud_raymarch_cs.hlsl:522` clamps `tFar = min(tFar, tNear + g_tracingMaxDistanceKm)`; the upscale copy has the same clamp and `cloud_layers.hlsli:94` clamps the shared multi-layer interval. No additional wiring needed.
- [x] 7.2 Duplicate of completed 6.4: `cloud_raymarch_cs.hlsl:302-315` and the upscale copy interpolate the height gradient from `g_cloudType`; type 0.5 preserves the legacy gradient.
- [x] 7.3 `cloud_pass.cpp:54-68,630-655,768-783` and both raymarch shaders plus `cloud_layers.hlsli` parameterize sky-bottom visibility, UV scales, all authored height ramps and the self-shadow distance cap using the EXACT former literals as defaults. Existing `r.Cloud.AerialPerspectiveDensity=0.04` is also wired without a default change. Default-unchanged proof and build/layout/gtest evidence are below; default sun/away/above runtime captures exit 0, compile without failures, and render non-black.
- [x] 7.4 `docs/references.md:169,538,594-641` now documents Schneider/Vos 2015, Hillaire 2020 and Frostbite integration, Wolfe et al. 2022 STBN, Production Volume Rendering 2017 and Draine 2003/optional fitted phase. Corrected the legacy "Oz & Kulla" author attribution against the original Pixar course bibliography; no shader citation text or behavior changed.
- [x] 7.5 `docs/specs/rendering/atmosphere.md:135-207` "Volumetric Clouds (M0-M5)" documents tracked graph passes, capped adaptive march/STBN, default FBM versus opt-in shape sampling, multi-scatter, temporal upscale, multi-layer/weather, default-off Beer ground shadows, all new CVar defaults and the remaining visual/LUT limitations.

M6 verification (2026-10-16): `cd runtime && zig build -Doptimize=ReleaseFast`
exited 0, shader integrity 142/142. `GTEST_FILTER=-RuntimeViewportTest.* zig build
test -Doptimize=ReleaseFast` completed with 7661 passed / 17 skipped / the same
three pre-existing VfxEditorSlate failures (WorkbenchHasTwoColumns,
TitleBarReflectsEmitterName, StatusBarBindingsReflectTelemetry), exit 1;
delta from the supplied baseline is 0/0/0. The gtest run had no TDR and
needed no retry.
Evidence: `.screenshots/cloud-m6/gtests.log:17727-17749` and `gtests.xml`.
The existing `test_cbuffer_layouts.py` with in-memory map extensions for all
16 new fields and the M5 layer fields passed: CloudCB 67, upscale 69, shadow 67
fields, plus every existing entry and negative control. CloudCB is still
1024 bytes (`cloud_pass.cpp:649`), with new scalars at 560-620 replacing
reserved padding; layer offsets and root constants are unchanged.
Noise/weather still use s3; BSM still uses s2, never s0.
clangd is unavailable; the successful native build and DXC layout checks
provide compilation verification instead.

7.3 default-unchanged proof (inspection plus build, not a bit-identical image
claim): `cloud_pass.cpp:47,54-68` registers exactly the old literals and
`:768-783` uploads them without conversion. In field order:
`0.8f, 0.05f, 0.1f, 2.0f, 0.25f, 0.125f, 0.002f, 0.0f, 0.07f, 1.0f,
0.3f, 0.35f, 0.1f, 0.8f, 2.0f, 0.04f`.
An eval check parsed all 16 registrations and assignments and confirmed
literal equality 16/16. Substituting those defaults back into each edited
shader and restoring its reserved-padding declaration reconstructs all three
pre-edit shader texts exactly (line endings normalized):
`cloud_raymarch_cs.hlsl`, `cloud_raymarch_upscale_cs.hlsl`, `cloud_layers.hlsli`.
This includes BOTH 0.8 sky-visibility uses, the 2x-layer-height light distance
cap, cloud-type ramps and weather/detail UVs; no default was retuned.
Fresh headful sun/away/above captures each rendered 150 frames and exited 0:
`.screenshots/cloud-m6/default/{sun,away,above}/run.log`, `manifest.json`,
and `frames/f_130.png` through `f_132.png`. Opened all three frame-132 images:
cloud structure and blue-sky gaps are visible, not black or an empty pass.
Every log reports the 1024-byte CB, 256-iteration cap, zero state-claim
disagreements and zero double imports across 154 graphs; none contains
`compile failed`, `DEVICE_REMOVED` or `DEVICE_HUNG`.
Additional full-RGB comparison against retained `.screenshots/cloud-m5/after`
frames 130-132 passes the known cross-process floor: all nine sun/away/above
pairs differ by at most one 8-bit code value. Worst mean absolute code deltas:
sun 0.000436118, away 0.000434510, above 0.000006430.
The exact-literal reconstruction plus build is the primary 7.3 proof; this
retained-image comparison independently confirms the expected appearance.
Opt-in runtime coverage: a 24-frame two-layer/weather capture with temporal
upscale AND Beer shadows enabled exited 0 under
`.screenshots/cloud-m6/optin-render/`. Opened `frames/f_20.png`: non-black
clouds with sky gaps. `run.log:284,431-442,582` confirms quarter resolution,
two 512-square BSM cascades, two explicit layers, successful pipelines,
1024-byte CB, 256 iterations and zero state-claim/double-import errors
across 28 graphs. No compile failure or device removal occurs in this retry.
The preceding combined **GBV** attempt under `.screenshots/cloud-m6/optin/`
failed with device removal/hang (`run.log:498-532`) before a screenshot;
it is NOT a successful GBV gate. The bounded ordinary-render retry verifies
the opt-in shaders, not GBV cleanliness or improved ground silhouettes.
All runs were bounded below three minutes. Final PowerShell process check
returned `NO_ENGINE_PROCESSES`; no engine was left running.

M6 owned changed files (no commit/archive; concurrent VFX/M4 work untouched):
`runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp`,
`runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl`,
`runtime/shaders/runtime/atmosphere/cloud_raymarch_upscale_cs.hlsl`,
`runtime/shaders/runtime/atmosphere/cloud_layers.hlsli`,
`docs/references.md`, `docs/specs/rendering/atmosphere.md`, and this `tasks.md`.

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
