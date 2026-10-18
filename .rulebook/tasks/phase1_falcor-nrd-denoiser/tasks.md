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
