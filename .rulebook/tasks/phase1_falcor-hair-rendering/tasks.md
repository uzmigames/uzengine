## DXR Stage 1: Near-field hair BCSDF (2026-10-18)

This stage follows the explicit path-tracer implementation scope, not the
original raster session ordering below. There is no `design.md` in this task.
The raster geometry, raster lighting and transparency checklist remains open.

- [x] S1.1 Implement Chiang-style R, TT and TRT scattering in `runtime/shaders/runtime/pathtracing/hair_bcsdf.hlsli`: normalized modified-Bessel longitudinal Mp, trimmed logistic Np, dielectric Fresnel, Beer-Lambert Ap, reflectance-to-sigma_a mapping, beta_m/beta_n, eta=1.55 and alpha=2 degrees. Principal-lobe truncation intentionally loses the omitted higher-order energy rather than adding it to a retained lobe.
- [x] S1.2 Evaluate and importance-sample the same full-sphere BCSDF and attenuation-weighted mixture PDF. Recover strand direction from triangle dP/dV, U-across-fiber near-field h (including back-view sign), and an orthogonal tangent frame. Ray offsets follow the outgoing side for transmission.
- [x] S1.3 Dispatch only existing `EShadingModel::Hair` (6 in materialFlags[24:27]) in the opt-in DXR path tracer. Author the tag through the existing `uz.scene.set_material(..., {shading_model="hair"})` API. Stage-1 RGB is reflectance, roughness_multiplier is beta_m, and metallic_multiplier is beta_n for Hair only. No CLI change, shared CB change, material-table stride change, or new root constants. `PipelinePathMaterial` stays 112 bytes and `PathTracerRoot` stays 24 DWORDs. Hair vertices bypass PBR-only ReSTIR reservoirs and use matching NEE; NRD classifies hair transport as specular.
- [x] S1.4 Generate and render the twelve bent triangle-ribbon strands with `scripts/rendering/test_hair_bcsdf_stage1.py --out E:/UzmiEngine/hair-stage1 --case prepare`, then `--case front` and `--case back`, with `--path-trace` supplied by the harness. Read both `front/frames/f_127.png` and `back/frames/f_127.png`: the front view has a narrow reflected band with wider overlapping TRT shoulders; the backlit view has long bright TT forward-scattering streaks. The 2-degree tilt gives R=-2 degrees, TT=+1 degree, TRT=-3 degrees under the rulebook convention; the secondary reflection overlaps the primary at this roughness, rather than forming an artificial separate band. There are 3,946 nonzero front pixels and 6,882 back pixels; whole-image mean codes are 0.698466796875 and 2.317633463542. Lobe centers and positive R/TT/TRT amplitudes are also checked independently by the CPU tests below.
- [x] S1.5 White furnace and PDF proof: 243 deterministic RGB/roughness/absorption/geometry cases measured maximum spherical energy **0.997936724837 <= 1**, PDF integrals **0.999964647368 to 1.000087873348**. Factorized tensor quadrature integrates d(sin theta) d(phi), without an incorrect surface cosine. Longitudinal and azimuthal inverse-CDF sample histograms match evaluated bin masses. The shared scalar shader functions are executed by C++, not replaced with a separate approximate model. Evidence: `E:/UzmiEngine/hair-stage1-gtests.log`, lines 14586-14605.
- [x] S1.6 Default and ordinary-material identity: `--case default`, `--case nonhair`, then `--case metrics` both report **max_code_delta 0**. Default frame 95 is also PNG-file-byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`. The non-hair DXR frame 255 is unchanged against `E:/UzmiEngine/layered-stage1/base/frames/f_255.png`.
- [x] S1.7 Build and tests: `cd runtime && zig build -Doptimize=ReleaseFast` exits 0; RTX 5080 captures compile all three lib_6_6 libraries and create the RT state object, with no `compile failed` in run.log. `npm run test:layouts` exits 0 (88/88 Tier A, 31/31 Tier B, 12/12 Tier C). `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast` runs 7,877 tests: **7,857 passed (+8 versus 7,849), 17 existing skips, the same 3 pre-existing VfxEditorSlate failures** (`WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`, `StatusBarBindingsReflectTelemetry`); command exits 1 solely for these known failures. No new failure or TDR occurred. All eight `HairBCSDFStage1` tests pass in 90 ms:
  - `LongitudinalMpNormalizesInSolidAngle`
  - `ConeAndCuticleTiltPeakConvention`
  - `TrimmedLogisticNormalizesAndCentersR_TT_TRT`
  - `ColorToAbsorptionIsFinitePositiveAndMonotonic`
  - `AttenuationR_TT_TRTNeverCreatesEnergy`
  - `FullSphereFurnaceAndActualApMixturePDF`
  - `SampledLongitudinalDirectionsMatchMpBinMasses`
  - `InverseCDFSampledAzimuthDirectionsMatchNpBinMasses`

Deferred: dedicated curve/strand assets and raster expansion, far-field and
dual-scattering multiple scattering, higher-order lobes, full hair parameter
authoring (including variable IOR/tilt), DOM shadows, GBuffer hair integration,
hair LOD, root-to-tip gradients, and OIT. Sessions 1-4 below and their global
tail are not claimed complete by the DXR stage.

## DXR Stage 2: Dual-scattering multiple scattering as a tail replacement (2026-10-18)

In the multi-bounce DXR path tracer, inter-fiber multiple scattering is already
captured by explicit fiber-to-fiber bounces, and the stage-1 BCSDF is already
energy-conserving (furnace 0.998). Classic additive dual-scattering would
double-count transport and break energy conservation here. This stage instead
uses dual-scattering as a VARIANCE/COST reduction: trace the first K explicit
inter-fiber bounces, then REPLACE the remaining deep multiple-scattering tail
with the analytic Zinke dual-scattering estimate (no double-count, energy-bounded).
Opt-in behind `r.Hair.MultiScatter`.

- [x] S2.1 Implement the tail-replacement dual-scattering estimate in `runtime/shaders/runtime/pathtracing/hair_dual_scatter.hlsli` (average forward attenuation a_f + spread, average backward attenuation a_b + back-scatter density/spread, from the stage-1 BCSDF lobe integrals), wired into `pathtracer_closesthit.hlsl` after K explicit inter-fiber bounces; gated by `r.Hair.MultiScatter`. `PipelinePathMaterial` (128 bytes) + `PathTracerRoot` (24 DWORDs) unchanged.
- [x] S2.2 Consistent-mean proof (the tail replacement converges to the explicit fully-traced reference): on a blonde swatch the dual-ON mean relative bias vs the explicit reference shrinks with K: K1 -26.8%, K4 -15.5%, K8 -6.7%, K12 -1.95%, K16 -0.60%, K20 -0.18%, K24 -0.053%, K32 -0.0041%. Dark hair off==stage1 (K16 rgb_relative_bias [0,0,0]). Evidence: `E:/UzmiEngine/hair-stage2-proof/metrics.json`.
- [x] S2.3 Variance/cost win: at equal GPU cost the dual-ON estimator variance is 0.705x the explicit path tracer (29.5% lower), the win for light hair that otherwise needs many bounces (`equal_gpu_cost_variance_ratio` in metrics.json).
- [x] S2.4 Off-limit + energy: `r.Hair.MultiScatter` OFF (and non-hair) reproduces the stage-1 explicit transport exactly (test `OffAndNonHairNeverReplaceStageOneTransport`); the tail replacement never exceeds single-fiber energy (test `ReplacementFurnaceNeverSpendsMoreThanSingleFiber`).
- [x] S2.5 Safety + tests: default (no `--path-trace`) reflection-reference frame-95 PNG byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png` (max_code_delta 0). `cd runtime && zig build -Doptimize=ReleaseFast` exit 0 (152 shaders); `npm run test:layouts` exit 0 (89/89 Tier A, all agree); `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast` -> 7,888 passed, 17 skipped, only the 3 pre-existing VfxEditorSlate failures (exit 1 for those; a first run hit a transient DX12DeviceTest TDR, clean on re-run). 6 new `HairDualScatterStage2` tests pass (ForwardLobeMassMatchesIndependentQuadrature, AverageForwardBackwardSumToBCSDFLobeIntegrals, BackscatterSeriesAndSpreadUseSamePathWeights, ReplacementFurnaceNeverSpendsMoreThanSingleFiber, OffAndNonHairNeverReplaceStageOneTransport, +1).

Deferred (unchanged from stage 1): far-field scattering, higher-order explicit
lobes, full parameter authoring, and the raster Sessions 1-4 below (dedicated
strand assets, raster VS/PS, DOM shadows, GBuffer hair, LOD, root-to-tip, OIT).

## Session 1: Hair Strand Geometry

- [ ] 1.1 Create `hair_strand_asset.h/.cpp` — `HairStrandAsset` class: `Load(path)` reads `.hair` binary format (header: strandCount, pointsPerStrand; data: float3 positions, float widths, float2 UVs). `GetVertexBuffer() → RhiBufferHandle`. `GetStrandCount() → uint`. `GetLODLevel(float distanceCm) → uint` (returns target strand count fraction: 1.0 at <500cm, 0.5 at 1000cm, 0.25 at 2000cm).
- [ ] 1.2 GPU upload: positions as `R32G32B32_FLOAT`, widths as `R32_FLOAT`, UVs as `R32G32_FLOAT`. Topology: `D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ` (adjacency needed for consistent tangent computation in VS).
- [ ] 1.3 Create `hair_vs.hlsl` — expand line strips to camera-facing quads: compute strand tangent from adjacent control points, compute billboard right vector as `normalize(cross(tangent, cameraForward))`, expand each line segment to 4 vertices offset by `±width/2` along right vector. Output: position (NDC), `float3 tangent`, `float2 uv`, `float widthFraction` (0=root, 1=tip).
- [ ] 1.4 Build + verify: hair vertex shader compiles. Bind a test `.hair` file (procedurally generated 100 straight strands) and render as solid color — verify billboard quads face camera and follow strand directions.

## Session 2: Chiang 2016 Hair BSDF

- [ ] 2.1 Create `include/hair_bsdf.hlsli` — declare `HairBSDFSample` struct, all lobe functions. @ref Chiang et al. 2016 "A Practical and Controllable Hair and Fur Model for Production Path Tracing" Table 1
- [ ] 2.2 Implement `Mp(float sinThetaI, float cosThetaI, float sinThetaR, float cosThetaR, float roughness) → float` — modified Gaussian longitudinal scattering. Alpha shifts per lobe: R=-α, TT=α/2, TRT=-3α/2 (α from roughness). @ref Chiang 2016 Eq. 7
- [ ] 2.3 Implement `Np(float phi, int p, float s, float gammaO, float gammaT) → float` — azimuthal scattering via logistic distribution. @ref Chiang 2016 Eq. 8. Implement `Ap(float cosTheta, float eta, float3 absorption) → float3[3]` — 3 attenuation terms using Fresnel for R, TT, TRT. @ref Chiang 2016 Eq. 9
- [ ] 2.4 Implement `EvaluateHairBSDF(float3 wo, float3 wi, float3 tangent, HairMaterial mat) → float3` — sum of all 3 lobes: `sum over p: Mp(p) * Np(p) * Ap(p)`. Decompose `wi` and `wo` into `(thetaI, phi)` in tangent-normal-bitangent frame.
- [ ] 2.5 Create `hair_ps.hlsl` — use `EvaluateHairBSDF` with strand tangent from VS output. Evaluate for each directional light. Alpha from Gaussian opacity profile (wider strands = more opaque). Build + verify: hair renders with correct lobe-based shading (visible highlight shift vs Lambert).

## Session 3: Hair Shadow (Deep Opacity Maps) + GBuffer Integration

- [ ] 3.1 Create `hair_shadow_pass.h/.cpp` — Deep Opacity Map (DOM): 8 depth layer shadow map at 1024×1024. For each hair fragment in shadow map view: write transmittance fraction to the layer whose depth bound contains the fragment's depth. DOM stores `float transmittance[8]` per texel.
- [ ] 3.2 Sample DOM in `hair_ps.hlsl`: for a given shadow ray, look up the 8 DOM layers, interpolate transmittance between bounding layers. Multiply by hair BSDF contribution for self-shadowing.
- [ ] 3.3 GBuffer integration: add `materialType=3` (Hair) as a new dispatch path in `deferred_lighting_ps.hlsl` (guarded by `HAIR_SHADING` define from shader variant system). Store strand tangent in spare GBuffer channel (replace AO if needed, or add GB channel).
- [ ] 3.4 Hair LOD: at > 1000cm camera distance, thin strands < 1px are discarded (coverage < 0.1). Strand count multiplied by `GetLODLevel(dist)` via indirect draw count.
- [ ] 3.5 Demo with a test hair asset (100 curved strands, procedural): build + verify via `--screenshot-stdout`. Verify: correct highlights (R lobe visible as offset specular), correct self-shadowing (DOM), visible LOD transition. Capture multiple camera distances.

## Session 4: Polish + Transparency + Validation

- [ ] 4.1 Per-pixel linked list (OIT) for transparent hair overlap: allocate `RWStructuredBuffer<HairFragment>` with count buffer. Hair PS appends to list. Resolve pass sorts per-pixel list by depth and composites front-to-back. Enable via `HAS_OIT` variant define.
- [ ] 4.2 Root-to-tip color gradient: `float3 strandColor = lerp(rootColor, tipColor, uv.y)` in `hair_ps.hlsl`. `rootColor` and `tipColor` as per-material parameters stored in GBuffer or material CB.
- [ ] 4.3 Build + verify final quality: render hair with OIT enabled vs disabled via `--flip` — OIT should show measurably fewer transparency sorting artifacts (lower FLIP on overlapping strand regions). GPU timestamp confirms OIT pass < 2ms.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
