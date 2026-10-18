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
