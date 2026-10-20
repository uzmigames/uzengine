## Stage 1: Homogeneous DXR path transport

Stage 1 is a finite, constant-density sphere in engine centimeters, enabled only
with `--path-trace --volume` or `r.PathTracer.Volume=1` while using the DXR path
tracer. It is not the heterogeneous grid/raster volume work listed below.
The default medium is disabled. Coefficients, anisotropy, sphere radius and center
are exposed under `r.PathTracer.Volume.*`.

- [x] Exponential free-flight sampling, surface survival and scattering albedo.
  The collision density is `sigmaT * exp(-sigmaT*d)`: the sampled transport weight
  is `sigmaS/sigmaT`, and surface survival has unit weight. Applying extinction
  again to either sampled branch would double-count it. Absorption-only segments
  use deterministic `exp(-sigmaT*d)`.
- [x] HG phase evaluation and sampling, phase-sampled continuation, light NEE
  with Beer-Lambert shadow transmittance, and Russian roulette. Active media
  bypass the surface-only ReSTIR and NRD paths; zero density does not bypass them.
- [x] Correct the analytic fog fixture: extend the emissive backdrop below the
  camera horizon and aim the directional light toward the camera using the
  scene's `[pitch, yaw, roll]` Euler order (`[0, -90, 0]`).
- [x] Build: `cd runtime && zig build -Doptimize=ReleaseFast`, exit 0,
  151 embedded shaders verified. `npm run test:layouts`, exit 0:
  88/88 Tier A, 31/31 Tier B, 12/12 Tier C; the private volume root is 32 bytes.
- [x] Beer-Lambert: CPU test at `sigmaT=.005`, thickness `200 cm` matches
  `exp(-1)=.3678794412` within `1e-7`. GPU central 8x8 probe rays cross known
  sphere chords: mean measured transmission `.3681700244`, geometric analytic
  mean `.3681639625`; maximum absolute per-channel error `.000198664`, below
  `.0005` including RGBA16F quantization.
- [x] Single scattering: read `scatter_255.png` and the raw-HDR captures:
  a circular, approximately symmetric glow is visible against a uniform backdrop.
  At 2047-frame capture, mean radiance `.2800254822` matches numerical integration
  of the single-scatter equation `.2800253665`. With fixed `sigmaT=.005`,
  halving `sigmaS` from `.003` to `.0015` changes scattering-only radiance from
  `.2064094543` to `.1031455994`, ratio `2.00114649`.
- [x] Zero-density no-op: `off_255.png` versus `zero_255.png`, with
  `sigmaA=sigmaS=0`, has maximum and mean code delta 0; the linear-HDR probe
  also has maximum delta 0.
- [x] Convergence: raw-HDR probe RMSE against the geometric single-scatter
  reference falls from `.014001829` at frame 31 to `.005181351` at frame 255
  to `.002261528` at frame 2047 and `.001108947` at frame 8191. The frame-8191
  mean `.2800407410` differs from the reference by `.000015375`, or `.0055%`;
  the estimator does not add or remove energy as samples grow.
  This measures the configured one-vertex single-scatter reference, not an
  infinite-bounce or heterogeneous-medium claim.
- [x] Default reflection-reference frame 95: `verified/default_95.png` versus
  `E:/UzmiEngine/f005-after/frames/f_95.png` has maximum and mean code delta 0,
  without `--path-trace` and with the medium off.
  The exact reference recipe must not add `--hidden`: that changes the
  existing raster capture by max code delta 14 even with the medium disabled.
- [x] Five `PathVolumeStage1` tests pass, including
  `BeerLambertKnownThicknessAndZeroDensity`,
  `ExponentialPDFIntegratesToCollisionProbability`,
  `AnalogSurvivalAndAlbedoEstimatorConverge`,
  `HenyeyGreensteinNormalizesAndIsotropicIsUniform`, and
  `SingleScatteringMatchesAnalyticBacklitChord`; isolated target exits 0.
  Full `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
  runs 7863 tests: 7843 pass, 17 skip, and the same three pre-existing failures
  (`VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`,
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry`). Delta: +5 passing tests
  from the 7838-pass baseline, no new failures. Full command exits 1 because of
  those existing failures; no TDR occurred.

Evidence (2026-10-18): `E:/UzmiEngine/volume-stage1-resume/verified/`,
including `*-manifest.json`, clean render logs, PNGs and `*-region/region_*.bin`.
All final engine runs exit 0, contain no `[error]` or `compile failed`, and are
individually bounded to 180 seconds. `highscatter.log` records the enabled volume
path, `.002` absorption, `.003` scattering, `g=0` and radius `100 cm`.
The actual probe dumps have valid GPU frame stamps (including frame 8191);
some single-frame manifests report zero files despite the valid flushed file
being present, so validation reads the files themselves.
Read standalone `--region-fields extra` channel `taa_tap_centre` for unfiltered
linear HDR, with `r.TAA=1`; requesting deferred-lighting records (`taa`) while
path tracing gives invalid lighting stamps and is not acceptable proof.
The full test log is `E:/UzmiEngine/volume-stage1-resume-gtest.log`.
LSP diagnostics could not run because clangd is not installed; the real build
and DXC/layout compilation passed.

Reproduce the single-scatter capture from the repository root:

```sh
runtime/build/bin/uzengine.exe \
  --project scripts/rendering/path-volume-stage1.uzproject \
  --hidden --verbose --path-trace --volume --cvar r.TAA=1 \
  --frames 2055 --screenshot /absolute/output/scatter.png \
  --screenshot-frames 31-2047:224 \
  --region-dump /absolute/output/scatter-region \
  --regions center:156,86,8,8 --region-fields extra \
  --region-first 31 --region-last 2047
```

For each probe ray, intersect the radius-100 sphere to obtain entry `t0` and
chord length `D`. The reference is
`L = L_backdrop*exp(-sigmaT*D) + sigmaS*10/(4*pi) *
integral_0^D exp(-sigmaT*(s + shadow_exit_distance(P(s)))) ds`.
The recorded reference uses 4096 midpoint quadrature intervals per ray and the
fixture's pinhole projection; `P(s)=camera+direction*(t0+s)` and the shadow ray
points along `-Y`. The axial closed form is
`L_backdrop*exp(-1) + .003*10/(4*pi)*(1-exp(-2))/.01`.
Use raw linear HDR, not ratios of tone-mapped PNG code values.

Deferred: heterogeneous density grids/assets/components, delta/Woodcock tracking,
emissive media and spectral transport. The original Sessions 1-3 below remain
unchecked because they describe later heterogeneous/raster integration work.
Homogeneous stage 1 is complete; no source changes were needed beyond the
existing transport WIP. This completion fixed the test fixture and supplied the
missing correctness, neutrality, convergence and regression evidence.

## Stage 2: Heterogeneous DXR path transport

Stage 2 extends the same opt-in `r.PathTracer.Volume` sphere with a procedural
linear density field. `r.PathTracer.Volume.Variation` is in [0,1], default 0.
For sphere-local `q = (x+y)/(sqrt(2)*radius)`, density is `1 + variation*q`;
the majorant is `(sigmaA+sigmaS)*(1+variation)`. Both coefficients scale by
the same field, preserving scattering albedo and the stage-1 HG phase.
Variation 0 takes the exact stage-1 free-flight and Beer-Lambert branches.

- [x] Delta tracking accepts real collisions with local extinction / majorant;
  null collisions continue without an iteration truncation.
- [x] Ratio tracking estimates camera absorption and all medium/surface NEE
  shadow transmittances using the product of null-collision probabilities.
- [x] CPU analytic, acceptance, homogeneous-limit and zero-density proofs.
  Five `PathVolumeStage2` tests pass (isolated target exit 0):
  `MajorantBoundsDensityAndAcceptance`,
  `DeltaAndRatioTrackingMatchAnalyticLinearProfile`,
  `HomogeneousLimitIsExactlyStageOne`,
  `ZeroDensityAndEmptyIntervalsAreNoOps`, and
  `RatioTrackingConvergesWithoutEnergyDrift`.
  Acceptance is `.2502212524` versus `.25` (262144 trials).
  The known linear slab has `sigmaT=.005`, `D=200`, variation `.8`,
  `q0=-.8`, `dq=.006`, hence optical depth `.84`.
  At 524288 trials, delta survival is `.4319629669` and ratio transmission
  is `.4317229072`, versus analytic `.4317105349` (tolerance `.0025`).
  CPU ratio error falls from `.0241770107` at 1024 samples to `.0013914972`
  at 32768 and `.0004481261` at 1048576.
- [x] GPU analytic linear-density transmission and homogeneous stage-1 comparison.
  Frame-8191 central 8x8 transmission is `.3686666489` versus analytic
  `.3684192978`. Off-axis left transmission is `.3365818659` versus
  `.3370659741`; right is `.5196619034` versus `.5190174042`.
  Every region's mean radiance error is below `.0006` (transmission `.003`).
  The reference integrates the exact linear optical depth,
  `sigmaT*D*(1+variation*(q0+.5*dq*D))`, using rays reconstructed with the
  recorded view-projection inverse at clip `z=.5`, exactly as DXR does.
  The previous ideal-pinhole/X-axis assumptions were not valid off-axis.
  At frame 2047, constant stage 2 and committed homogeneous stage 1 both
  have HDR mean `.2800254821777344`: maximum HDR delta 0, PNG code delta 0,
  and identical PNG bytes. Retained `baseline-region/region_2047.bin` and
  `baseline_2047.png` are byte-identical to the independently verified
  stage-1 `convergence-region/region_2047.bin` and `convergence_2047.png`.
- [x] Read heterogeneous PNG and verify single-scatter convergence in linear HDR.
  Read `scatter/frames/f_8191.png`, the homogeneous comparison PNG and the
  absorption PNG. The sphere has an asymmetric scattering gradient: denser
  screen-left material occludes more and has higher scattering-only radiance
  `.2121532521` versus `.1795000978` on the right, matching references
  `.2124209562` and `.1795970880`.
  Central HDR RMSE against the 4096-interval single-scatter integral falls
  from `.0192729880` (frame 31) to `.0059125710` (255), `.0024843520`
  (2047), and `.0010688029` (8191). Final mean `.2799682617` matches
  reference `.2800343645` within `.000066103` (about `.024%`).
  This proves the configured one-vertex transport, not infinite-bounce convergence.
- [x] Build, shader logs, layouts, default byte match and full gtest delta.
  `cd runtime && zig build -Doptimize=ReleaseFast` exits 0, 152 shaders
  verified. Heterogeneous `run.log` records variation `.8`, delta free flight
  and ratio shadow tracking; all final captures exit 0 with no `[error]`
  or `compile failed`. `npm run test:layouts` exits 0: Tier A 89/89,
  Tier B 33/33, Tier C 12/12; the private volume root is now 48 bytes.
  Zero density versus flag-off has maximum HDR delta 0 and identical PNG
  bytes. Default reflection-reference frame 95, without `--path-trace`,
  `--volume` or `--hidden`, is byte-identical to
  `E:/UzmiEngine/f005-after/frames/f_95.png`, maximum code delta 0.
  `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast`
  runs 7898 tests: 7878 pass, 17 skip, and the same three pre-existing
  `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`, and
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry` failures.
  Delta against the supplied 7873-pass baseline: +5 passing tests, no new
  failures. Full command exits 1 only for those failures; no TDR or rerun.

Evidence (2026-10-18): `E:/UzmiEngine/volume-stage2/metrics.json`,
`gtest.xml`, and `absorption`, `scatter`, `constant`, `off`, `zero`,
`default` subdirectories with manifests, clean `run.log`, PNGs and HDR dumps.
Each run was bounded to at most 180 seconds; the full gtest run took 42.987 s.
Reproduce from the repository root with
`python scripts/rendering/test_path_volume_stage2.py --out E:/UzmiEngine/volume-stage2 --case CASE`
for each capture case, then `--case metrics`. The `baseline` case must be
captured with the committed stage-one binary, not the stage-two binary.
Standalone `--region-fields extra` writes a valid GPU TAA footer but leaves
the CPU header's TAA stamp unset. The proof checks the actual footer stamp,
valid bit and output dimensions for every region; it never accepts unstamped data.
Clangd is unavailable; real C++ compilation, DXC and layout checks passed.

Unbiasedness: majorant-Poisson thinning accepts with `sigmaT(x)/majorant`,
so delta survival is `exp(-integral sigmaT(x) dx)`. The Poisson product
expectation of ratio weights `1-sigmaT(x)/majorant` is the same exponential.
There is no fixed null-collision cap, and the common density factor cancels
from scattering albedo. Variation zero directly calls the original stage-one
distance and Beer-Lambert kernels, rather than approximating their limit.
The transport scaffold needed no further shader changes; completion corrected
the proof harness's camera and stamp assumptions and supplied the missing evidence.
Procedural heterogeneous stage 2 is complete.

Deferred after this procedural stage: sampled density assets and grids, ECS
volume collection, sparse/VDB storage, raster volume passes and temporal
reprojection, emissive media and spectral transport. The original Sessions 1-3
below still describe those deferred asset/raster integrations.

## Session 1: Volume Asset + CVolumeComponent

- [ ] 1.1 Create `volume_asset.h/.cpp` — `VolumeAsset` class: `Load(path, format)` reads raw binary 3D grid (header: uint32 width, height, depth; data: R8_UNORM or R16F per voxel). `GetTexture3D() → RhiTextureHandle`. `GetDimensions() → uint3`. `GetMaxDensity() → float` (precomputed maximum value for Woodcock tracking majorant).
- [ ] 1.2 Binary format: magic `UZVOL`, version 1, width/height/depth as uint32, format enum (R8=0, R16F=1), then raw data. Include a CPU utility `GenerateSphereVolume(radius, density)` for test volumes.
- [ ] 1.3 Create `volume_component.h` — `CVolumeComponent` ECS component: `VolumeAsset* asset`, `float sigmaA = 0.1f` (absorption coefficient, cm⁻¹), `float sigmaS = 0.9f` (scattering coefficient), `float g = 0.0f` (Henyey-Greenstein anisotropy, [-1,1]), `float3 halfExtents` (OBB size). Default sigma values from Novak 2018 Table 1 smoke example.
- [ ] 1.4 Add `CVolumeComponent` collection to `scene_gather.cpp`: gather up to 4 volume components per frame, upload as `StructuredBuffer<VolumeDesc>` (transform matrix, sigma values, anisotropy, Texture3D index in bindless heap).
- [ ] 1.5 Build + verify: volume asset loads a test sphere binary. `CVolumeComponent` attaches to an entity. Scene gather populates volume buffer. No crashes.

## Session 2: Volume Ray March Shader

- [ ] 2.1 Create `volume_raymarch_cs.hlsl` — `[numthreads(8,8,1)]` entry `main`. For each pixel: reconstruct world-space ray from depth buffer. Ray/OBB intersection test (transform ray to volume local space, axis-aligned slab test). Clamp ray to [tMin, tMax] from intersection.
- [ ] 2.2 Woodcock delta tracking for transmittance: set majorant `mu_t_max = (sigmaA + sigmaS) * volume.maxDensity`. Advance by free path `t += -log(rand) / mu_t_max`. At each sample: read `Texture3D` density, compute `mu_t_real = (sigmaA + sigmaS) * density`. Accept with probability `mu_t_real / mu_t_max` (rejection sampling). Accepted samples contribute in-scattering. @ref Novak 2018 Eq. 13
- [ ] 2.3 In-scattering at accepted sample: cast shadow ray toward directional light. Evaluate Henyey-Greenstein phase function `p(cosTheta, g) = (1 - g²) / (4π(1 + g² - 2g*cosTheta)^1.5)`. @ref Henyey & Greenstein 1941. Accumulate: `radiance += transmittance * sigmaS * phaseFunction * lightColor * (1 - shadowOcclusion)`
- [ ] 2.4 Beer-Lambert extinction for shadow ray: integrate density along shadow ray using fixed-step march (8 steps), compute transmittance as `exp(-sigmaT * integratedDensity)`. Use this as shadow term in step 2.3.
- [ ] 2.5 Output: `RWTexture2D<float4> uVolume` stores `float4(scatteredRadiance, transmittance)` per pixel. Build + verify: constant-density sphere volume produces correct forward-scattering cone (brighter in forward direction with g>0).

## Session 3: VolumePass Integration + Temporal Reprojection + Demo

- [ ] 3.1 Create `volume_pass.h/.cpp` — `VolumePass` render graph pass: dispatch `volume_raymarch_cs` at half resolution (for performance). Inputs: scene depth SRV, volume buffer SRV, Texture3D array SRV. Outputs: volume radiance + transmittance at half-res.
- [ ] 3.2 Upscale + composite: bilinear upsample volume output to full resolution. Composite over scene color: `finalColor = sceneColor * volumeTransmittance + volumeRadiance`. Insert in engine loop after deferred lighting, before bloom/tone map.
- [ ] 3.3 Temporal reprojection: ping-pong `uPrevVolume` buffer. Reproject previous frame via camera delta: `prevUV = projectWorldPos(worldPos, prevViewProj)`. Blend: `accumulated = lerp(current, prevSample, 0.9)` if reprojection UV is in [0,1] and depth matches. Reset on disocclusion.
- [ ] 3.4 Demo: create a smoke volume using `GenerateSphereVolume` (radius=200cm, density=0.5). Place directional light on one side. Run `demo.exe --screenshot-stdout --frames 30`. Verify: visible volumetric shadow cone, forward-scattering halo, transmittance fade at volume edges.
- [ ] 3.5 Build + verify: volume renders without device removal. GPU timestamp < 3ms for single 128³ volume at 1080p half-res. Temporal reprojection reduces frame-to-frame noise (compare frame 1 vs frame 30 via `--flip`, expect significant noise reduction).

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 1.1 Document homogeneous stage 1, its reproduction command, and deferred stages above.
- [x] 1.2 Add the five shared CPU/HLSL `PathVolumeStage1` correctness tests.
- [x] 1.3 Run stage-1 tests (5/5 pass); record the unchanged full-suite failures above.
