# Cloud aerial perspective and final QA (2026-10-17)

## Implementation

The half-resolution raymarch now writes the same R32F opacity-weighted cloud
distance already produced by the Bayer raymarch and explicit-layer marcher.
Each sample contributes `T * (1 - Tstep) * sampleDistance`; dividing by total
opacity yields kilometers from the camera. No-hit and occluded rays initialize
distance to zero. Radiance/transmittance remain RGBA16F.

The cloud composite consumes that distance and the **existing** Bruneton
transmittance LUT. For a sky-bound ray, segment attenuation is
`T(start, top) / T(end, top)`, not its inverse. For ground-intersecting rays
the reciprocal direction gives `T(end, -ray) / T(start, -ray)`, avoiding
planet-crossing LUT samples. Ratios clamp to [0,1], and paths clip to the
atmosphere shell. An eight-step Rayleigh/Mie analytical source integration
matches the existing AP-volume approach, including solar transmittance and
planet occlusion. This is single-scattering aerial perspective, not a new
multiple-scattering LUT or a full cloud/atmosphere coupled solver.

The final expression is exactly
`cloudRadiance * atmTransmittance + atmInscatter * (1 - cloudTransmittance)`.
Cloud opacity is unchanged. The cloud CVar `r.Clouds.AerialPerspective` defaults
to true. On this path the old exponential haze density is zeroed before
marching, and the screen-position tint is bypassed, so haze is not applied twice.
The old path remains available with the switch off for comparison.

The geometry `r.AerialPerspective` switch defaults off. Changing it globally
would violate the shipped-render safety requirement. Instead, cloud
registration fills a cloud-only copy of the existing PerFrameCB atmosphere
fields from the enabled sky component. That copy is bound only to the cloud
composite. The original per-frame allocation, sky, lighting, fog order and
geometry switch are untouched. HDRI/absent atmosphere selects the legacy path.
The atmosphere renderer returns the exact RGB solar illuminance it selected
for the sky (including linked-light tint/temperature), which is copied to
the cloud-only `lightColor`. An initial geometry-AP scalar source used the
directional-light vector length and over-brightened atmospheric haze. That
normalization was corrected before the final captures; geometry AP itself
was not changed.

`Cloud_FrontDepth` is owned with the raymarch output in both resolution modes.
The graph tracks its UAV write and composite/temporal reads, plus the shared
atmosphere LUT read. CloudCB remains 1024 bytes, with the existing depth-UAV
field at offset 336. Composite roots use four DWORDs; march roots use twelve;
the global 24-DWORD root block is unchanged.

The Bayer composite uses the current quarter-resolution weighted distance
alongside resolved full-resolution cloud radiance; distance is not separately
temporally accumulated. QA below measures the resulting temporal behavior.

## Reproduction

`scripts/rendering/cloud_aerial_qa.py` wraps the existing headful capture
runner. Every engine subprocess has a 175-second deadline. Modes `before`
and `after` capture sun/away/above/horizon; `reflection` captures frame 95;
`temporal` changes only Bayer upscale at 1920x1080; `stability` and
`stability-temporal` capture sixty consecutive frames; `shadows` uses the
existing silhouette scene; `sunset` uses a five-degree sun fixture.
`metrics` writes pixel comparisons, GPU times and adjacent-frame deltas.
The native `CloudAerialPerspective` tests include the exact scalar transport
functions used by the shader, rather than a separate CPU reimplementation.

## Evidence

**Result: implementation, default safety, tests and four QA gates pass;
5.4 and 8.1 remain open for their visual acceptance targets.** This is not
78/78 completion. No commit or archive is made.

Artifacts are under `.screenshots/cloud-aerial/`. Before captures were made
on the unchanged baseline after a successful ReleaseFast build. Final
reflection/sun/away/above/horizon/temporal/stability/sunset captures use
executable `fnv1a64:f641be1a9ce13ec4`. The shadow/control captures are from
the first implementation build; the subsequent change only corrected the
cloud atmospheric source, and the geometry-only shadow view does not execute
that composite branch. They are not misidentified as captures of the final
executable.

### Build, default safety and regression

- `build.log`: `cd runtime && zig build -Doptimize=ReleaseFast`, exit 0;
  shader integrity 144/144. Clangd is not installed; native compilation,
  DXC reflection and actual shader execution supply compiler verification.
- `layouts.log`: `npm run test:layouts`, exit 0, all layouts agree:
  Tier A 87/87, Tier B 27/27 and Tier C 4/4; negative controls still fail
  as intended. Inventory: 165 declarations, 104 mapped, 61 explicitly
  excluded, zero unlisted. CloudCB mirrors check 46 fields; new composite
  PerFrameCB and CloudCompositeCB mirrors check 16 and 2 fields.
- `reflection/frames/f_95.png` versus
  `E:/UzmiEngine/f005-after/frames/f_95.png`: **PNG bytes identical** and
  decoded RGB maximum/mean code delta **0/0** (`metrics.json`).
  The manifest has no Cloud_* pass, and geometry `r.AerialPerspective`
  remains 0. No no-cloud rendering default was changed.
- `gtests.log` / `gtests.xml`: the environment-filtered
  `GTEST_FILTER=-RuntimeViewportTest.* zig build test -Doptimize=ReleaseFast`
  runs 7,818 tests in 35.780 seconds: **7,798 pass, 17 skip, the same
  3 VfxEditorSlate failures**, exit 1. Delta versus the supplied 7,791-pass
  baseline: **+7 passes, zero additional failures/skips**. Existing failures
  are WorkbenchHasTwoColumns, TitleBarReflectsEmitterName and
  StatusBarBindingsReflectTelemetry. No TDR occurred.
- All seven new `CloudAerialPerspective` tests pass: TransparentCloudDoesNotAddAtmosphere,
  ZeroDistancePreservesCloudRadiance, OpaqueDistantCloudApproachesInscatter,
  InscatterIsPremultipliedByCloudOpacity, SegmentUsesStartOverEndNotInverse,
  SegmentCompositionMatchesWholePath and ZeroLengthSegmentIsIdentityEvenAtLutFloor.
  These CPU tests require no GPU and do not hide device failures behind skips.
- Ordinary render logs contain no `compile failed`, DEVICE_REMOVED or
  DEVICE_HUNG; the images were opened and are non-black. Cloud graph summaries
  report zero state-claim disagreements and zero duplicate imports.
- `gbv/{run.log,manifest.json,frames/f_95.png}`: the final executable's
  180-frame headful `cloud_test --gpu-validation` run exits 0 in about
  96 seconds, within its 175-second bound. Opened frame 95 is non-black.
  The log confirms aerial perspective=true, legacyDensity=0, 183 graphs,
  zero state disagreements and zero duplicate imports. There is no shader
  failure, device removal/hang or new cloud validation error. The same
  eight existing id-1422 initialization errors remain (six SkyCubemap
  subresources and two GBuffer_Velocity resources); global zero-error GBV
  is not claimed.
- A focused `test_runtime.exe --gtest_filter=CloudAerialPerspective.*`
  run exits 0 with 7/7 passing. The final PowerShell process audit returns
  `NO_ENGINE_TEST_ZIG_PROCESSES`; no engine, test runner, Zig or build
  executable remains. Scoped `git diff --check` passes.

### QA verdicts

| Item | Verdict and evidence |
|---|---|
| 5.4 | **OPEN visual acceptance.** LUT transport renders and distant clouds acquire wavelength-dependent atmospheric haze, with unchanged cloud geometry; zero-length transport is identity. However the fixed horizon falloff target <=0.10 is not met: old fake-haze path 0.153638, physical path **0.238819**. Far-band contrast/chromaticity distance are 0.859394/0.159731 versus 0.384394/0.041860 before. `after/horizon/frames/f_95.png`, `control/horizon/frames/f_95.png`, `quality.json`. The warm horizon appearance is not presented as a successful blue-haze quality gate. |
| 8.1 | **OPEN silver-lining acceptance.** No regular stepping bands were apparent in the opened default view; sun HF/mid power is 0.00260015 (before 0.00333700). The fixed silver-lining statistic is **0.424891 sun / 0.472854 away**, difference **-0.047963**, not >=+0.15. Ground silhouettes pass separately: `shadows/frames/f_95.png` versus `shadows-off/frames/f_95.png`, minimum/median linear brightness ratio 0.049342/0.829836; 27.05% of ground is below half brightness and 32.32% remains above 95%. |
| 8.2 | **PASS visual.** Opened `after/above/frames/f_95.png`: continuous cloud tops with blue gaps, shaded structure and the distant horizon; no missing dispatch or black cloud pass. |
| 8.3 | **PASS visual.** Opened `sunset/frames/f_95.png` at five-degree solar elevation: warm forward glow and shaded cloud structure. Equal-elevation patches above the disk have display-linear Y 0.645043 toward the sun versus 0.519137 at the flanks, ratio **1.242529**. This whole-sky measurement includes atmospheric scattering; it is not an isolated cloud-phase readback or a silver-lining pass. |
| 8.4 | **PASS.** At 1920x1080, 104 timing samples each: GPU frame **9.28370 -> 3.47611 ms**; total cloud passes **8.650853 -> 2.760888 ms** (0.319146x, below 0.4x). Raymarch **8.49659 -> 2.55962 ms**, temporal 0.046328 ms on, composite 0.154263/0.154940 ms. GPU frame falls 62.56%. CPU means 10.5298/4.84462 ms include synchronous PNG capture and are not the GPU performance claim. |
| 8.5 | **PASS for the requested static-camera sequence.** Opened both 60-frame contact sheets for frames 90-149. On cloud region x=400,y=80,w=1100,h=420, off/on mean adjacent RGB code delta **0.049500/0.037850**; worst adjacent mean **0.051080/0.048644**; maximum individual-channel delta **14/9**. No persistent trails or frame-wide flicker are apparent. This is not a new moving-camera/camera-cut test. |

`comparison.jpg` shows all four before/after views. The physical path
intentionally changes cloud-scene RGB (mean code deltas sun/away/above/horizon
14.65954/13.25394/5.73296/11.76797); these are not cloud-free regressions.
The above-view image preserves the nearby deck's form; no claim of byte-identical
near-cloud shading is made after removing the old tint.

### Remaining decision

The feature is implemented default-on as requested, with byte-identical
cloud-free safety proved. **Cloud-scene visual adoption still needs owner
sign-off** because the measured horizon/silver-lining targets fail. The
transport formula and shared LUT/source contract are implemented; this work
does not establish that the pre-existing cloud lighting/coverage calibration
can meet those appearance targets unchanged. Changing that calibration or
substituting an artistic haze curve needs a separate visual decision.
No guessed material retune, extra attenuation or threshold weakening was
used to manufacture a pass. Keep 5.4 and 8.1 unchecked; the remaining ten
requested checklist items can be checked.

## Changed files and callers

Owned changes (all must accompany this implementation):

1. `runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp`
2. `runtime/engine/renderer/passes/atmosphere/cloud_pass.h`
3. `runtime/engine/renderer/frame/frame_atmosphere_passes.cpp`
4. `runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp`
5. `runtime/engine/renderer/atmosphere/atmosphere_renderer.h`
6. `runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl`
7. `runtime/shaders/runtime/atmosphere/cloud_composite_ps.hlsl`
8. `runtime/shaders/runtime/atmosphere/cloud_aerial_math.hlsli`
9. `runtime/tests/engine/renderer/test_cloud_aerial_perspective.cpp`
10. `scripts/rendering/test_cbuffer_layouts.py`
11. `scripts/rendering/cloud_aerial_qa.py`
12. `scripts/rendering/cloud-reference-sunset.uzproject`
13. `scripts/rendering/cloud-reference-sunset.uzscene`
14. `docs/specs/rendering/atmosphere.md`
15. `docs/analysis/volumetric-clouds-rewrite/09-aerial-perspective-2026-10-17.md`
16. `.rulebook/tasks/phase0-3.1.0-volumetric-clouds-rewrite/tasks.md`

The raymarch caller is CloudPass::RegisterPasses, including the existing
BSM-compiled raymarch variant. PipelineLibrary::GetCloudComposite supplies
the existing graphics pipeline, now with populated four-DWORD indices and
the cloud-only PerFrameCB plus the unchanged CloudCB prefix bound at b2.
The unchanged `cloud_raymarch_upscale_cs.hlsl` already writes R32F weighted
distance; the unchanged `cloud_layers.hlsli` handles explicit layers.
The unchanged `cloud_temporal_cs.hlsl` consumes distance and resolves cloud
radiance. CloudCB's upscale/shadow mirrors, CloudTemporalCB, SkyAtmosphereCB
and the global root signature have no layout change. The new HLSL math
include is also consumed by the new native test file.

Concurrent VFX-editor WIP and the pre-existing M4 document/script edits were
not modified. Captures, logs, DXIL and contact sheets are local ignored
artifacts, not additional source changes.
