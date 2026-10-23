## Phase E: DXR area-source visibility (2026-10-23)

- [x] `r.Lighting.AreaLightShadows` defaults ON. A separate deferred PSO adds
  16 Hammersley source samples per area light, toroidally shifted per pixel
  (IGN) and per frame (R2) so the existing TAA accumulates them (a shared
  fixed pattern banded the disk/tube penumbras). OFF, frames without area
  lights and the non-DXR fallback keep the old unshadowed permutation.
- Rectangle and disk samples cover their area uniformly; tube samples cover
  the cylinder and both hemispheres in proportion to surface area.
- Diffuse and LTC-specular visibility are normalized independently using
  their lobe density and the emitter cosine / squared distance. These
  self-normalized ratios are bounded in [0,1] (no fireflies), with an O(1/N)
  finite-sample bias (diffuse < 0.01; 0.031 on a sharp glossy cell in the
  gtest). No dedicated temporal history or spatial blur; TAA accumulates.
- The TLAS demand includes this switch independently of reflections and the
  RT master switch; a missing or empty TLAS selects the unshadowed fallback.
  One SRV index reuses LightingCB padding without growing its C++ allocation
  or the deferred root signature.
- CPU tests cover clear/blocked/bounded visibility, quadrature convergence,
  separate diffuse/glossy weighting and the additional TLAS-demand switch.
  `scripts/rendering/area-light-shadows/` supplies an occluder scene and
  bounded rect/disk/tube raster/PT capture and analysis commands.
- Evidence (2026-10-23, `E:/UzmiEngine/area-shadow-evidence/v2`): shadow-ROI
  RMSE vs PT OFF->ON rect 0.0625->0.0060, disk 0.0694->0.0040, tube
  0.0304->0.0031; max code delta 0 for reflection-reference OFF/ON/default
  vs c40cab0b, rect OFF vs c40cab0b, no-DXR vs OFF; default == ON. Temporal
  std in the ROI <= 1.6e-3 HDR mean (~0.5 display code). Added
  DeferredLighting (median of 3, 640x360, one light) rect +0.086 ms, disk
  +0.024 ms, tube +0.001 ms. The PT fixtures use fov 38.58: the
  `--path-trace` camera spans 2x tan(fov/2) of the raster one (pre-existing,
  open follow-up; it also affects the area-light-shapes PT pairs).

## 1. Implementation

- [x] 1.1 Generate LTC precomputed textures — `scripts/rendering/bake_ltc_lut.py` re-implements Heitz et al. 2016's reference fitter (GGX + height-correlated Smith, 64×64, Nelder-Mead on (m11, m22, m13), MIS cubic error, the reference warm-start order; 14 s on 32 cores, deterministic). Cross-checked against the published tables shipped by three.js (`RectAreaLightTexturesLib`): magnitude/fresnel agree to 2e-4 everywhere, matrix entries to a median relative 1.3e-4 for roughness ≥ 0.2 (only the degenerate θ = 1.57 row differs). Embedded as `runtime/engine/renderer/lighting/ltc_lut_data.inc` (float32, 344 KB), created on demand as 64×64 RGBA32F (M⁻¹, packed like the reference `ltc_1`) + RG32F (magnitude, fresnel) textures by `ltc_lut.cpp`. 32-bit because the near-mirror columns hold entries up to ~1e5.
- [x] 1.2 Implement LTC evaluation HLSL include (`ltc.hlsli`) — `LtcLutUV` (x = roughness, y = sqrt(1 − NoV), texel centres at both ends), `LtcInverseMatrix`, `LtcIntegrateEdge`, `LtcClipQuadToHorizon`, `LtcEvaluate` (returns the form factor: clamped-cosine integral / π; one-sided = max(0, −sum) for the engine's vertex order). Fresnel of the whole lobe: `F0·t2.x + (F90 − F0)·t2.y` (keeps the base BRDF's F90 convention).
- [x] 1.3 Implement rect light polygon clipping — the sixteen horizon configurations (3–5 vertices). Validated by `test_ltc_area_light.cpp` against brute-force integration for 1/2/3/4-vertices-above cases (≤ 1e-4 abs; the Python reference agrees to 1e-8).
- [x] 1.4 Implement LTC diffuse — identity matrix (exact Lambert form factor; the normal-incidence rect matches the analytic view factor to 1e-6).
- [x] 1.5 Add the rect light type — `RectLightData` (80 bytes: position/range, right/halfWidth, up/halfHeight, color (= radiance)/flags, specularScale/diffuseScale/shadowIndex) in `constant_buffers.h` + `light_types.hlsli`; `RectLightComponent` (width/height/range/twoSided; emits along forward, width along right, height along up — the spot-light convention); `"light": {"type": "rect", ...}` in the scene loader + saver; `RenderProxyType::RectLight`. Not the legacy `LightGPU.lightType = 3`: production lighting reads the LightingCB arrays, `LightGPU` is the unused multi-light descriptor. The frame's rect lights travel in a per-frame bindless `StructuredBuffer<RectLightData>` (`RectLightBuffer`, 3-frame ring) and the LightingCB only carries `rectLightCount / rectLightsSrv / ltcMatrixSrv / ltcMagnitudeSrv` in its former pad, so the CB keeps its 43264 bytes.
- [x] 1.6 Integrate area lights into deferred lighting — `ShadeRectLight` + the loop after the point/spot block, compiled only into a `UZ_RECT_LIGHTS=1` PSO permutation (`SceneRenderer::EnsureRectLightLightingPipeline`, also combined with `UZ_CLOUD_GROUND_SHADOWS`) that the pass selects when the frame has a rect light. The stock pipeline's DXIL is instruction-identical to before (20 170 IR lines, 0 differ, cbuffer size annotation included). A uniform `if (rectLightCount > 0)` branch in the same shader was tried first and moved 55 pixels by 1 LSB on a scene without rect lights — hence the permutation.
- [x] 1.7 Bind LTC textures — the deferred pass is bindless: the two SRV indices live in LightingCB (no t8/t9 registers). The SM5 fallback path does not evaluate rect lights (same as the clustered lists).
- [x] 1.8 Implement area light attenuation — the point-light window `(1 − (d/range)⁴)²` on the distance to the rect's centre, WITHOUT a 1/d² term: the inverse-square falloff is inside the LTC integral, so the `1/(dist² + 1)` of the original item would have double-counted it. Range 5000 in the test scene (window ≈ 1 in the ROI).
- [x] 1.9 Create area light demo — `scripts/rendering/area-light-ltc.uzscene` + `.uzproject`: a 120×80 cm rect (radiance 6) 200 cm above a roughness-0.3 dielectric floor, plus three spheres (roughness 0.1 chrome, 0.5 brushed metal, 0.9 diffuse) and the same rect as an emissive quad for the path tracer (`cube.uzmesh` is [−1,1]×[−1,1]×[0,2], so scale (40, 60, 0.1)). GI/AO/SSR/bloom off, passthrough tonemap (linear 8-bit), light BVH on, 2 bounces (= direct lighting only: NEE at the first hit, MIS-weighted emitter hits, black environment).

## 2. Visual Verification

- [x] 2.1 Screenshot: glossy floor reflecting rectangular light — `E:/UzmiEngine/area-ltc/area-light-ltc-raster-vs-pt.jpg` (raster | path tracer | |diff|×4): the elongated reflection of the rect on the roughness-0.3 floor sits where the path tracer puts it.
- [x] 2.2 Screenshot: roughness gradient — the three spheres in the same montage: sharp rect reflection (0.1), broad highlight (0.5), diffuse glow (0.9). Three samples, not a continuous gradient plane.
- [ ] 2.3 Screenshot: area light vs point light — not captured in stage 1.
- [ ] 2.4 Screenshot: large area light — soft shadows are a later stage (stage 1 is unshadowed; `shadowIndex` reserved).
- [x] 2.5 Regression: existing point/spot lights unchanged — reflection-reference frame 95 (1920×1080, same recipe as `E:/UzmiEngine/f005-after`): **max_code_delta 0** against the HEAD (ebe785b0) pre-change capture, run-to-run deterministic (two HEAD runs: max 0). Against `f005-after/frames/f_95.png`: 1 pixel / 1 code — the same single pixel HEAD itself differs by (the reference predates the PCSS/RT commits), not from this work. Scenes without a rect light create no texture, no buffer and no PSO.

## 3. Documentation

- [x] 3.1 Document LTC algorithm with Heitz 2016 reference — `ltc.hlsli` header, `ltc_lut.h`, `bake_ltc_lut.py` docstring (derivation, packing, cross-check).
- [x] 3.2 Add area light parameters with UE5 RectLight equivalents — `RectLightComponent` (SourceWidth / SourceHeight / AttenuationRadius; `color * intensity` = emitted radiance, matching an emissive quad's `emissiveIntensity`).
- [x] 3.3 Document LTC texture format and generation process — `ltc_lut.h` + the bake script (run `python scripts/rendering/bake_ltc_lut.py` to regenerate the .inc).

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation — in the code headers and this file (docs/ is out of this stage's scope).
- [x] 4.2 Write tests covering the new behavior — `runtime/tests/engine/renderer/test_ltc_area_light.cpp` (10 tests: table finiteness/bounds, known cells vs theory (1 − ln 2 at α = 1, mirror end, isotropic row 0), a C++ port of the HLSL evaluator vs the analytic view factor, vs brute force with horizon clipping, one-sided/two-sided, hemisphere = 1, a real table lobe vs brute force; RectLightData / LightingCB offsets); `test_cbuffer_layouts.py` (LightingCB Tier A fields + RectLightData Tier C stride).
- [x] 4.3 Run tests and confirm they pass — see the evidence below.

## Stage 1 evidence (2026-10-20, RTX 5080, pinned zig 0.15.2 ReleaseFast)

- Build: `zig build -Doptimize=ReleaseFast` exit 0, Shader integrity OK (156 shaders). `npm run test:layouts` exit 0 (Tier A 90/90 incl. the LightingCB rect fields, Tier C incl. RectLightData 80 B).
- No-area-light byte match: reflection-reference frame 95 max_code_delta 0 vs the HEAD capture (`E:/UzmiEngine/area-ltc/ref-final` vs `ref-before`); 1 px / 1 code vs f005-after, identical to HEAD's own delta.
- Rect light vs analytic: floor radiance under the rect centre 0.246 (roughness 0.3) / 0.2531 (roughness 1.0) vs the analytic Lambert value 0.6 · F · 6 = 0.2532 (F = 0.070344, the view factor of a 120×80 rect at 200 cm): −2.6 % / −0.04 %.
- Rect light vs path tracer (`area-raster-final2` vs `area-pt-final2`, linear 8-bit codes): floor ROI [100,360,1080,340] RMSE 2.55 codes, mean(raster − PT) +1.29 codes, linear ratio 1.36; lamp pool ROI RMSE 9.8 codes, ratio 1.44; glossy highlight ROI RMSE 4.2 codes, ratio 1.24 (highlight peak within 4 %). The residual is the path tracer's diffuse model, not the LTC: the PT scales Lambert by its D7 energy budget (1 − E_spec(NoV, roughness)) and loses more at grazing light incidence, while the raster's direct lights use plain Lambert. PT/raster at the pool centre is 0.77 on the roughness-0.3 floor (grazing view, NoV ≈ 0.24, Fresnel-heavy E_spec) and 0.95 on a roughness-1.0 floor; in the hemisphere limit (a 2000×2000 emitter) raster 0.310, PT 0.303, analytic ≈ 0.31. Both the raster and the PT agree with the analytic solution where the models coincide.
- gtests: `GTEST_FILTER='-RuntimeViewportTest.*' zig build test -Doptimize=ReleaseFast` — 7965 tests, 7945 passed, 3 failed = the pre-existing VfxEditorSlate trio (baseline 7942 / 7920 / the same 3); the 10 new LTC/rect tests pass; no DX12DeviceTest TDR. `LightingParams.MetadataMemberCount` stays at 4 (the rect fields are not registered in the metadata, like the B.7 cluster fields).
- Montage: `E:/UzmiEngine/area-ltc/area-light-ltc-raster-vs-pt.jpg`.

Deferred to later stages: disk / line / tube (sphere+capsule) area lights, area-light shadows (`shadowIndex` reserved), rect lights in the SM5 (non-bindless) lighting path and in the path tracer's own light list (it only sees emissive meshes), light cookies (`sourceTexture`). Pre-existing observations, out of scope: the raster G-buffer only writes emissive when an emissive texture is bound (a constant-only emissive material shows black in raster, visible in the PT), and the path tracer skips emissive NEE when `PathCanContinue(bounce + 1, maxBounces)` is false, so `MaxBounces` must be ≥ 2 for direct lighting from emissive meshes.

## Phase E: disk and line/tube LTC (2026-10-23)

- [x] Disk/ellipse: 32-vertex contour, the existing LTC matrix/magnitude LUTs,
  and a convex-polygon integral with the horizon closing edge retained.
- [x] Line/tube: a constant-radiance capsule, not a flat rectangular billboard.
  Integrate its visible silhouette: exposed arcs of the endpoint spheres'
  tangent circles, joined by the cylinder's two tangent generators (up to 66 vertices).
  End-on and zero-length sphere limits are covered.
- [x] Scene types `disk`, `tube`, and `line` (canonicalized to `tube` on save).
  Reuse `RectLightComponent::Shape` and the 80-byte `RectLightData` record:
  disk width/height are full diameters; tube width is center-segment length
  along local right and height is diameter. Color times intensity is radiance.
- [x] Additive, type-gated `UZ_SHAPED_LIGHTS` permutation, also compiled with
  cloud-ground shadows. Both stock and rectangle-only permutations remain separate.
  The LightingCB stays 43264 bytes and the structured-buffer stride stays 80 bytes;
  no layout-test or LUT changes were needed. The shared deferred budget remains 16 lights.
- [x] Keep new shapes out of the legacy CPU rectangle list, merging them only
  for deferred LTC upload. This prevents the unchanged rectangle-only surface-cache
  consumer from treating disks/capsules as quads. New shapes are unshadowed,
  deferred/bindless direct lights; GI-cache integration is not part of this stage.
- [x] Four new gtests: disk on-axis analytic; ellipse horizon and table-lobe
  area integral; capsule side/oblique/horizon/table-lobe area integral; capsule
  end-on, zero-length sphere, inside and zero-radius cases.
- [x] Reproducible reference scenes, emissive disk/capsule mesh generation,
  bounded capture/analysis/performance commands in `scripts/rendering/area-light-shapes/`.
  Run `verify.py prepare --out E:/UzmiEngine/area-ltc-shapes/final` before capture.

### Final evidence

All builds used `.tools/zig-x86_64-windows-0.15.2/zig.exe`; all runs were bounded
to at most 180 seconds. No commit or archive was created.

- Pinned ReleaseFast build: exit 0, shader integrity 163 shaders. Both new shader
  permutations (plain and cloud-ground-shadow) also compile with DXC, exit 0.
- `npm run test:layouts`: exit 0, including RectLightData 80-byte stride.
- Final required gtests: 8022 tests / 8002 passed / 17 skipped / exactly the
  three documented pre-existing failures:
  `VfxEditorSlate.WorkbenchHasTwoColumns`,
  `VfxEditorSlate.TitleBarReflectsEmitterName`,
  `VfxEditorSlate.StatusBarBindingsReflectTelemetry`.
  Test command exits 1 for those failures; all four new shape tests pass.
  No DX12DeviceTest TDR occurred.
- Fresh detached `7b5d3c15` worktree baseline: final reflection-reference frame 95
  at 1920x1080 has **max_code_delta 0**. Existing rectangle-only LTC scene,
  frame 95 at 1280x720, also has **max_code_delta 0**. Captures:
  `E:/UzmiEngine/area-ltc-shapes/{baseline,rect-baseline}` versus
  `E:/UzmiEngine/area-ltc-shapes/final/{no-shape-ref,no-shape-rect}`.
  The fresh baseline's engine artifact built and rendered successfully; its
  default all-artifact build cannot compile the tracked layered-BSDF tests because
  their included untracked `pathtracer_furnace_test.h` is absent in a fresh worktree.
  That excluded header was not changed or copied.
- Correctness: 640x360, central 32x32 floor ROI, albedo 0.6, roughness 1,
  radiance 6, height 200 cm, black environment, exposure 1, TAA HDR records.
  Disk diameters 120x120 cm; capsule center segment 120 cm, diameter 30 cm.
  Smooth-surface Lambert integration includes both capsule hemispheres.
  Relative RMSE is normalized by mean analytic ROI radiance:

  | Type | Analytic mean Lo | LTC mean Lo | PT mean Lo (frame 383) | LTC/analytic RMSE | PT/analytic RMSE | LTC/PT RMSE |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: |
  | Disk | 0.29630207 | 0.29438076 | 0.29309822 | 0.6487% | 1.2509% | 0.7612% |
  | Tube | 0.11463619 | 0.11456797 | 0.11371355 | 0.0615% | 2.8428% | 2.8260% |

  Disk on-axis analytic `F=3600/43600`, `Lo=0.2972477064`; quadrature agrees.
  Quadrature order refinement changes disk by at most 1.7e-16 and tube by 3.3e-7.
  CPU capsule-center contour form factor is 0.03192527 versus independent Lambert
  area integration 0.03193241 (about -0.022%).
  PT convergence from frame 95 to 383 reduces LTC/PT ROI RMSE from 0.8766% to
  0.7612% (disk) and 4.8415% to 2.8260% (tube). PT retains the existing intrinsic
  dielectric specular/diffuse energy partition; that residual is not pure LTC error.
  `verify.py analyze --frame 383 --max-relative-rmse .03 --out E:/UzmiEngine/area-ltc-shapes/final`
  exits 0. Reports: `analysis.json`, `analysis-f383.json`.
  Montage: `E:/UzmiEngine/area-ltc-shapes/final/montage-f383.png`
  (disk and tube, raster on left / PT on right); inspected after final build.
- RTX 5080 GPU performance, 640x360, one light versus matched `r.Lights.AreaLTC=0`
  control, 100 measured timestamp samples starting at frame 32:
  disk DeferredLighting 0.298673 - 0.0731709 = **+0.225502 ms/light**;
  tube 0.627703 - 0.0734054 = **+0.554298 ms/light**.
  GPU frame deltas are +0.225691 and +0.557542 ms respectively.
  These are single bounded timing runs, not confidence intervals.
- Fourteen final capture logs contain no renderer/device/region errors.
  Logs are not warning-free: the same baseline SkyCubemapCapturePS warmup,
  font/VFX glyph and play-mode warnings remain, outside this change's scope.
