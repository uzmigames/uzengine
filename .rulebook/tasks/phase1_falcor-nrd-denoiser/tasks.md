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

After native stage 2 the SDK provisioning, resource translation and SDK dispatch
integration were still deferred; the SDK sessions below record their completion on
2026-11-10 as the opt-in `NrdSdkDenoiser` (`r.NRD.SDK`). Firefly-specific processing
remains an NRD-side option (`r.NRD.SDK.AntiFirefly`). No commit or archive.

## Session 1: NRD SDK Integration + NrdSdkDenoiser Class (2026-11-10)

The SDK adapter lives next to the native stages as `NrdSdkDenoiser`
(`nrd_sdk_denoiser.{h,cpp}`); the native `NrdDenoiser` stays the `r.NRD=1` default.
NRD v4.17.3 publishes no binaries and has no `CreateDenoiser`/`GetTextureDescs`:
the items below record the real API (`nrd::CreateInstance`, `InstanceDesc` pools of
`TextureDesc{format, downsampleFactor}`, `GetComputeDispatches`). The Zig/mingw build
cannot link MSVC static libraries (same constraint as DLSS/FFX), so NRD is consumed as
`NRD.dll` loaded at runtime; nothing is linked. Evidence:
`E:/UzmiEngine/_evidence/lighting/nrd-denoiser/report.md`.

- [x] 1.1 `scripts/setup.ps1` clones `NVIDIA-RTX/NRD` at `v4.17.3` (commit
  `792eff196afdd350fd9c3f862119017ccb438a0e`, `Clone-Repo`, `runtime/thirdparty/nrd/`
  gitignored) and runs the new `scripts/build_nrd.ps1`: CMake + MSVC 2022, DXIL only
  through the pinned `runtime/thirdparty/dxc`, `NRD_NORMAL_ENCODING=4`,
  `NRD_ROUGHNESS_ENCODING=1`, static CRT (DLL imports kernel32 only), `SHADERMAKE_TOOL=OFF`.
  159 DXIL blobs embedded; a from-scratch build takes 17 s. `runtime/build.zig` gates
  `UZ_NRD_SDK_ENABLED` + the include path on the headers and DLL being present and deploys
  `NRD.dll` next to the executables. The static-library form was deliberately not used.
- [x] 1.2 `nrd_sdk_denoiser.h`: `NrdSdkDenoiser` with `Init(device, width, height)`,
  `Denoise(RgBuilder&, const NrdInputs&) -> RgResourceId`, `Destroy()`, `IsCompiledIn()`.
  It reuses the stage-2 `NrdInputs` contract (now also carrying `view`, `proj`, `farPlane`
  from the active camera) instead of the proposal's separate `DenoiserInputs`; settings are
  CVars (`r.NRD.SDK.*`), not a `SetSettings` call.
- [x] 1.3 `Init` loads the DLL (`LoadLibrary` + nine `extern "C"` exports), checks the
  library version/encodings against `nrd::GetLibraryDesc()`, calls `nrd::CreateInstance`
  with `REBLUR_DIFFUSE` (id 0) + `RELAX_SPECULAR` (id 1), reads `nrd::GetInstanceDesc`.
- [x] 1.4 Resource translation: the permanent/transient `nrd::TextureDesc` pools become
  committed D3D12 textures through a full `nrd::Format -> DXGI_FORMAT` table (NRD asks for
  `R8_UINT`/`R16_UINT`, which `RHIFormat` does not expose, so `rhi_create_texture` could not
  carry the pools); the user-side inputs/outputs are RHI textures (`RG16Float`,
  `RGBA16Float`, `R32Float`). One root signature + PSO per `nrd::PipelineDesc` from the
  embedded DXIL, a 3-frame descriptor ring and a 3-frame constant ring.
- [x] 1.5 `zig build -Ddll=true -Doptimize=ReleaseFast` exits 0 (`Shader integrity OK (173
  shaders verified)`, `NRD SDK: UZ_NRD_SDK_ENABLED=1`). On the RTX 5080 the log reads
  `NRD SDK v4.17.3 initialized at 640x360: REBLUR_DIFFUSE (0) + RELAX_SPECULAR (1); permanent
  pool 15 textures, transient pool 7 textures, 27 pipelines, 51 dispatch sets max, constant
  buffer 864 bytes max, 969 descriptors/frame`, then `dispatches=42/42` on the clearing
  frame and `17/17` per steady frame.

## Session 2: Per-Frame Denoise + Illumination Decomposition + Demo (2026-11-10)

- [x] 2.1 Decomposition: the stage-1/2 DXR signal (`nrd_signal.hlsli`: primary diffuse,
  primary specular, albedo, F0 modulation, virtual position, guide; residual = raw -
  diffuse - specular keeps emission/background) is reused unchanged. `nrd_sdk_pack_cs.hlsl`
  derives NRD's inputs from it: `IN_MV` (-velocity, pixels), `IN_NORMAL_ROUGHNESS` (NRD
  RGBA16 packing), `IN_VIEWZ`, `IN_DIFF_RADIANCE_HITDIST` (YCoCg + normalized hit distance
  from the virtual point), `IN_SPEC_RADIANCE_HITDIST`. No existing shader was modified.
- [x] 2.2 `Denoise`: `nrd::SetCommonSettings` (view/proj + previous, motion scale,
  `RESTART` on the path tracer's reset), `nrd::SetDenoiserSettings` for both denoisers,
  `nrd::GetComputeDispatches`, each dispatch executed on the native DX12 command list
  (`NRDIntegration`-style transition/UAV barriers, states restored for the render graph,
  `RebindBindlessHeaps()` afterwards).
- [x] 2.3 Shader binding: `PipelineDesc::computeShaderDXIL` goes straight to
  `CreateComputePipelineState` with a root signature built from
  `resourceRanges` (SRV table, UAV table) + root CBV + NRD's two static samplers;
  descriptors are written per dispatch from `DispatchDesc::resources`.
- [x] 2.4 `nrd_sdk_modulate_cs.hlsl`: `diffuseDenoised * albedo + specularDenoised * F0 +
  residual` into the resolved HDR texture before the existing resolve copy.
- [x] 2.5 `--denoiser nrd-sdk` (sets `r.NRD=1`, `r.NRD.SDK=1`); default off. RTX 5080,
  `python scripts/rendering/test_nrd_sdk.py --out <inside repo>` (PNG display-code metrics,
  frame 32 vs a 1024-frame reference): full-frame MSE raw 206.94 -> SDK 6.40 (-96.9%; native
  stage 2 5.63), mean bias -4.2% (stage 2 -3.7%), glossy-floor ROI 184.45 -> 0.44 (stage 2
  0.55), reset frame f4 21.1 (stage 2 18.0), f28->f32 MAE 0.20 (stage 2 0.32). Camera-step
  frame matches the new pose (MSE 21.9) not the old (275): no ghosting. FLIP was not used;
  the task's `--flip` criterion is replaced by these MSE gates. NRD's pre-passes default off
  and REBLUR anti-firefly runs only for the 8 frames after a reset (`r.NRD.SDK.*` restore
  NRD's values): with NRD's defaults the specular pre-pass loses 14% energy on sparse emitter
  reflections (SDK 7.48 / -8.0% bias). Remaining honest gaps: the raster default frame is
  nondeterministic run to run (13-674 differing pixels between runs of one exe) so its
  identity is shown within that envelope (9-63 pixels vs HEAD with a fresh shader cache),
  while the raw path-traced and native-NRD frames are byte-identical to the pristine build;
  `IN_DIFF_CONFIDENCE` / `OUT_VALIDATION` are not wired; DX12 only.
- [x] 2.6 Gates: `npm run test:layouts` exits 0 (`NrdSdkRoot: DXC 224 bytes (56 DWORDs),
  sizeof 224`); `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
  runs 8180 tests: 8160 pass (+5 `NrdSdk`), 17 skip, only the 3 pre-existing
  `VfxEditorSlate` failures. Documentation: `docs/specs/rendering/nrd-denoiser.md`.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 1.1 Update or create documentation covering the implementation — native stage-1 scope, reproduction, evidence above; SDK adapter in `docs/specs/rendering/nrd-denoiser.md` and the `runtime/CHANGELOG.md` row.
- [x] 1.2 Write tests covering the new behavior — five `NrdStage1` gtests and `scripts/rendering/test_nrd_stage1.py`; five `NrdSdk` gtests (`test_nrd_sdk.cpp`), the `NrdSdkRoot` layout entry and `scripts/rendering/test_nrd_sdk.py`.
- [x] 1.3 Run tests and confirm they pass — all new gtests, shader layouts, and all image assertions pass; full-suite pre-existing exceptions are recorded above.
