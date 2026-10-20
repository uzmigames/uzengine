## Stage 1: Native NRD-style Diffuse Baseline (2026-10-18)

The requested bounded stage 1 is a native ReBLUR/ReLAX-style baseline, not
the NVIDIA SDK adapter described in the original sessions below. There is no
`design.md` in this task directory. The SDK-specific checkboxes remain open.

- [x] S1.1 Separate primary-diffuse radiance from the path-traced total using
  the primary BRDF's diffuse/total ratio at the same sampled direction.
  Reuse the actual path material albedo for demodulation and remodulation;
  preserve the primary-specular/emission/miss residual without filtering it.
  No ray payload growth or changes to the 24-DWORD `PathTracerRoot`.
- [x] S1.2 Reproject diffuse irradiance using the existing GBuffer pixel-motion
  convention (`previousUV = currentUV - velocity / extent`). Validate previous
  depth, oct-normal and roughness; cap temporal history at 32 samples.
  Reuse path-tracer invalidation for camera/scene/settings changes, explicit
  reset, resize, disabled accumulation and frame gaps. Camera changes are
  conservatively full-history resets in this stage.
- [x] S1.3 Apply three normalized, depth/normal/roughness-guided B3-spline
  a-trous steps (1, 2, 4), then remodulate diffuse and add the untouched residual.
  The denoiser owns a separate 16-DWORD compute root. Signal extraction uses
  a separate 4-DWORD `NrdSignalRoot` at b2 in the private DXR root; neither the
  shared graphics signature nor `PerFrameCB` grows.
- [x] S1.4 Opt in through `r.NRD=1`, `--nrd`, or `--denoiser nrd`, only when
  the DXR `--path-trace` renderer runs. Raster rendering does not schedule or
  allocate denoiser passes. Disable raster TAA jitter while this path is active.
- [x] S1.5 Prove noise reduction, small bias, temporal improvement and no
  camera-cut ghosting on RTX 5080. Reproduce with
  `python scripts/rendering/test_nrd_stage1.py --out E:/UzmiEngine/nrd-stage1`.
  All captures have a 180-second bound. Evidence is in that output directory:
  - `raw/frames/f_4.png`: 1-spp, accumulation off, MSE 472.2031597 and
    MAE 10.3449826 against `reference/frames/f_1023.png` (converged capture).
  - `denoised/frames/f_4.png`: history reset, MSE 18.4160330, MAE 1.3975521.
    At frame 8 MSE is 13.8045139; frame 32 is 13.1402300, MAE 0.8558550.
    Final MSE reduction versus raw is 97.2172507%; temporal MSE improves
    28.6479% from the first visible reset frame to frame 32.
  - RGB PNG-code means: reference 11.4749870, raw 11.2934939, denoised
    frame 32 11.4158160. Denoised mean bias is -0.5156521%; no energy clamp.
    Metrics are explicitly display-code metrics, not a claim of linear-HDR MSE.
  - `motion/run.log`: scripted 150-unit camera translation at frame 24,
    accumulation reset after 23 samples and `pass=reproject history=false`.
    `motion/frames/f_24.png` matches `cut-fresh/frames/f_4.png` at the same
    pose/sample-zero with max code delta 0: no previous-view contribution.
  - `default/frames/f_95.png` versus `E:/UzmiEngine/f005-after/frames/f_95.png`:
    max code delta 0. `metrics.json` records and asserts all image gates.
  - Denoised `run.log` records both `cs_6_6` PSOs and reproject/a-trous
    dispatches, with accepted history after initialization. No `compile failed`.
- [x] S1.6 Verify build/layouts/regressions. `cd runtime && zig build
  -Doptimize=ReleaseFast` exits 0. `npm run test:layouts` exits 0, including
  `NrdRoot` (64 bytes/16 DWORDs), `NrdSignalRoot` (16 bytes/4 DWORDs), and
  unchanged `PipelinePathRoot` (96 bytes/24 DWORDs).
  `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
  runs 7858 tests: 7838 pass (+5 versus 7833 baseline), 17 skip, and exactly
  the 3 pre-existing failures below (command exits 1; no new regression).
  `E:/UzmiEngine/nrd-stage1/gtests.xml` retains the full result.
  - `NrdStage1.ClipToPreviousUVUsesPerspectiveDivideAndFlippedY`
  - `NrdStage1.PixelVelocityReprojectsCurrentMinusPrevious`
  - `NrdStage1.AtrousDepthWeightStopsEdgesAndIsSymmetric`
  - `NrdStage1.AtrousNormalWeightStopsOrthogonalAndOppositeSurfaces`
  - `NrdStage1.PrivateRootsDoNotGrowSharedGraphicsConstants`
  - Existing failures: `VfxEditorSlate.WorkbenchHasTwoColumns`,
    `VfxEditorSlate.TitleBarReflectsEmitterName`,
    `VfxEditorSlate.StatusBarBindingsReflectTelemetry`.
  LSP diagnostics were unavailable because clangd is not installed; compilation,
  DXC reflection, real GPU dispatch and the tests provide validation instead.

Deferred: SDK provisioning/linking and resource translation, SDK compute
dispatch integration, full separate specular denoising, ReBLUR/ReLAX-specific
history clamping, disocclusion-fix and firefly-specific stages. In particular,
the unfiltered specular residual can still produce isolated bright outliers
(frame 24's static MSE is 22.2176476); this stage does not claim their removal.
The two original SDK sessions below are not marked complete by this native
baseline. No setup script was run, commit created, or task archived.

## Stage 2: Separate Specular and Temporal Stability (2026-10-18)

- [x] S2.1 Extract primary-specular radiance independently of diffuse and primary
  emission, using the same sampled-direction attribution as stage 1. Demodulate
  by primary F0 and remodulate after filtering. Keep primary emission/background
  in the untouched residual. No base-BRDF or ray-payload size change.
- [x] S2.2 Use roughness-dependent spatial radius (1 to 4) and temporal history
  (2 to 32). Unfold the reflected segment through the primary tangent plane and
  reproject its virtual point using previous/current camera projections; blend
  toward surface motion with roughness squared. Static virtual points produce
  zero camera-motion displacement, including stochastic glossy ray directions.
- [x] S2.3 Clamp accepted diffuse/specular history to the current edge-aware
  5x5 mean +/- 3 sigma box; above one pixel of motion, use +/- 0.5 sigma and
  shorten history to 2 diffuse / 2..4 specular samples. Rejected history uses only the current frame and a
  dense, guide-weighted spatial reconstruction before the a-trous passes.
  Stage-2 guides come from the actual DXR primary hit, including opaque
  backfaces that raster guides do not describe.
- [x] S2.4 Prove specular error reduction without large bias, post-motion clamp
  improvement, and reduced error on genuinely newly uncovered pixels with
  `python scripts/rendering/test_nrd_stage2.py --out E:/UzmiEngine/nrd-stage2`.
  RTX 5080 evidence lives in that output directory; all image assertions exit 0.
  Metrics are RGB PNG display-code metrics, not linear-HDR MSE:
  - Metallic=1, roughness=0.45 floor ROI `(160,250,320,75)`, frame 32 against
    the converged 1024-frame capture `reference/frames/f_1023.png`:
    raw 1-spp MSE 100.5043333 / MAE 6.5380833;
    diffuse-only stage 1 MSE 104.2655833 / MAE 6.5939167;
    stage 2 MSE 0.4682917 / MAE 0.4359583. MSE falls 99.5341% against raw and
    99.5509% against stage 1. Mean 4.49525 versus converged 4.636875 is -3.05432%;
    specular peak 14 and p99 13 both exactly match converged 14 / 13.
    `raw/`, `stage1/`, `stage2/` retain their frame-4/frame-32 PNGs; `stage2/`
    also retains frames 8, 12, 16, 20, 24, 28.
  - Scripted 100-unit camera translation at frame 24, trailing-boundary ROI
    `(400,80,40,80)` against `motion-reference/frames/f_1023.png`:
    no-clamp MSE 239.784375; clamp/anti-lag MSE 10.2340625 (95.7320% lower);
    stage 1 MSE 14.9496875. Files are `motion-no-clamp/frames/f_24.png`,
    `motion-stage2/frames/f_24.png`, `motion-stage1/frames/f_24.png`.
  - Newly revealed strip left of the moved occluder `(300,160,20,30)`:
    stage 1 MSE 16.0233333; stage 2 MSE 0.9216667 (94.2480% lower).
    Disabling the dense disocclusion fix gives MSE 0.9433333; the fix itself
    improves this by 2.2968%. This very dark strip has reference mean 0.93
    and stage-2 mean 0.53833: fresh-frame low-signal bias is not hidden.
  - `default/frames/f_95.png` versus `E:/UzmiEngine/f005-after/frames/f_95.png`:
    max code delta 0. `metrics.json` retains every ROI, error, mean and peak.
  - `stage2/run.log` records both `cs_6_6` PSOs and diffuse/specular
    reprojection plus three a-trous dispatches, with valid temporal history.
    All capture logs pass the no-`compile failed` gate. Every engine run has
    a 180-second bound. Signal diagnostics also verified emission/background
    remain in the unfiltered residual.
- [x] S2.5 Verify ReleaseFast build, shader dispatch/compilation, unchanged
  default frame 95, layouts and full regression suite. New pure tests:
  `NrdStage2.RoughnessWidensSpecularRadiusMonotonically`,
  `NrdStage2.RoughnessSlowsTemporalAccumulationMonotonically`,
  `NrdStage2.HistoryClampUsesMeanPlusMinusKSigma`,
  `NrdStage2.FullyRoughNormalGateIsFiniteAtOrthogonalEdges`,
  `NrdStage2.VirtualRayUnfoldsReflectedHitThroughTangentPlane`,
  `NrdStage2.PrivateComputeRootFitsD3D12Budget`.
  All six pass. `cd runtime && zig build -Doptimize=ReleaseFast` exits 0.
  `npm run test:layouts` exits 0, including the 96-byte `NrdStage2Root`,
  32-byte `NrdSignalRoot`, and seven offsets in the read-only `NrdCamera`
  prefix. `GTEST_FILTER='-RuntimeViewportTest.*' zig build test
  -Doptimize=ReleaseFast` runs 7893 tests: 7873 pass (+6 versus the 7867
  baseline), 17 skip, and only the same three pre-existing VfxEditorSlate
  failures named in stage 1 (exit 1; no new regression).
  `E:/UzmiEngine/nrd-stage2/gtests.xml` retains the final result.
  No TDR occurred. clangd is not installed, so LSP diagnostics were unavailable;
  C++ compilation, DXC/layout reflection, real GPU captures and gtests ran.

Stage 2 remains opt-in through `r.NRD`. `r.NRD.Specular=0` reproduces the
diffuse-only reconstruction; `r.NRD.HistoryClamp=0` and
`r.NRD.DisocclusionFix=0` are diagnostic controls. Camera motion is validated
per pixel; transport/settings changes, explicit reset, frame gaps, resize and
disabled accumulation still invalidate history. The private `NrdSignalRoot`
is 32 bytes/8 DWORDs; the compute roots are 16 + 24 DWORDs plus a read-only
CBV for the existing camera-buffer prefix. `PipelinePathRoot`, shared graphics
roots and `PerFrameCB` do not grow.
`r.NRD.DebugSignal` optionally displays raw radiance (1), the unfiltered
emission/background residual (2), raw specular (3) or raw diffuse (4);
the default 0 displays the recombined result.

Deferred after native stage 2: full NVIDIA SDK provisioning, resource translation
and SDK dispatch integration, plus firefly-specific processing. The SDK sessions
below are not completed by the native implementation. No commit or archive.

## Session 1: NRD SDK Integration + NrdDenoiser Class (deferred)

- [ ] 1.1 Add NRD SDK to `scripts/setup.ps1` — `Clone-Repo` or `Download-Header` for NRD headers (`NRD.h`, `NRDDescs.h`, `NRDSettings.h`), static library, and HLSL include files. Store under `runtime/thirdparty/nrd/`. Do NOT run the script — user runs manually.
- [ ] 1.2 Create `nrd_denoiser.h` — `NrdDenoiser` class: `bool Init(device, width, height)`, `DenoisedBuffers Denoise(cmd, DenoiserInputs inputs)`, `void SetSettings(NrdSettings)`, `void Destroy()`. `DenoisedBuffers { RhiTextureHandle diffuse; RhiTextureHandle specular; }`. `DenoiserInputs { diffuseSRV, specularSRV, emissionSRV, hitDistSRV, normalRoughnessSRV, viewZSRV, motionVectorsSRV, viewMatrix, projMatrix, frameIndex }`.
- [ ] 1.3 Implement `NrdDenoiser::Init` — call `nrd::CreateDenoiser` with `nrd::DenoiserDesc` configuring `RELAX_SPECULAR` and `REBLUR_DIFFUSE` methods at target resolution. Allocate NRD-managed textures via `nrd::GetTextureDescs` (internal history buffers).
- [ ] 1.4 NRD resource translation: map `nrd::TextureDesc` (format, usage) to `RhiTextureHandle` via `rhi_create_texture`. Store in `std::vector<RhiTextureHandle> nrdTextures`.
- [ ] 1.5 Build + verify: NRD SDK links cleanly. `NrdDenoiser::Init` runs without crash. Log internal texture count via `UZ_LOG`.

## Session 2: Per-Frame Denoise + Illumination Decomposition + Demo (SDK stages deferred)

- [ ] 2.1 Illumination decomposition in path tracer: split `PathPayload.radiance` at each hit into `diffuseContrib = kd * diffuse_lighting` and `specularContrib = ks * specular_lighting`. Write to separate `uDiffuse` and `uSpecular` UAVs. Emission stored separately in `uEmission`.
- [ ] 2.2 Implement `NrdDenoiser::Denoise` — call `nrd::SetMethodSettings` with camera matrices (view, proj, prev-view, prev-proj) and frame index. Call `nrd::GetComputeDispatches` to obtain list of compute dispatches. For each dispatch: bind NRD's shader, bind input/output textures via RHI, set push constants, call `rhi_dispatch`.
- [ ] 2.3 NRD shader binding: NRD provides pre-compiled DXIL blobs; load them via `rhi_create_compute_pipeline_from_bytecode`. Bind textures by the indices specified in `nrd::DispatchDesc::resourceRanges`.
- [ ] 2.4 Create `modulate_illumination_cs.hlsl` — `[numthreads(8,8,1)]` `main`: `float3 result = tDiffuseDenoised * tAlbedo + tSpecularDenoised + tEmission`. Write to `uHdr`. Wire into render graph after NRD passes, before tone mapping.
- [ ] 2.5 CLI: `--denoiser nrd` activates `NrdDenoiser`. Build + verify on RTX hardware: `demo.exe --path-trace --denoiser nrd --screenshot-stdout --frames 5` produces a clearly denoised image at 1 SPP. Compare first frame (noisy) vs denoised via `--flip`; expect FLIP score improvement > 50%.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 1.1 Update or create documentation covering the implementation — native stage-1 scope, reproduction, evidence and deferred SDK stages above.
- [x] 1.2 Write tests covering the new behavior — five `NrdStage1` gtests and `scripts/rendering/test_nrd_stage1.py`.
- [x] 1.3 Run tests and confirm they pass — all five new gtests, shader layouts, and all image assertions pass; full-suite pre-existing exceptions are recorded above.
