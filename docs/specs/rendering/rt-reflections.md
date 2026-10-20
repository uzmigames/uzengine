# Hardware Ray-Traced Reflections — Design Spec (Lumen-parity track)

Status: **DESIGN / READY TO IMPLEMENT**. Author: autonomous lighting series, 2026-10-19.
Scope owner: whoever picks up the next phase2/3a reflections slice.

This spec exists so the implementation lane (deep-high/Astra) has a decision-complete,
code-grounded starting point. Everything below is cross-checked against the current tree
(HEAD `f6ff36b9` / branch `feat/phase1-advanced-rendering`). File and symbol names are real.

---

## 0. Why this task (the gap it closes)

A code review of the renderer (2026-10-19) found the engine is **already a Lumen-class hybrid
GI stack**: mesh SDFs + a global distance field (software sphere-tracing), hardware RayQuery
(DXR 1.1 inline) for DDGI probe tracing with a transparent SDF fallback, a world-space radiance
cache (SH probes), screen-space GI (SSGI), a final gather, and FidelityFX Brixelizer GI.

The **weakest area versus Lumen is reflections**. Today reflections come from:

- **SSR (stochastic screen-space)** — `frame_gi_passes.cpp` SSR trace/resolve/temporal/spatial-denoise,
  CVars `r.SSR.*`. Only reflects what is **on screen**; off-screen geometry is missing/ghosted.
- **Reflection probes** — `probes/reflection_probe_renderer.{cpp,h}`. Pre-baked/approximate,
  no sharp dynamic contact reflections.

There is **no hardware RT reflection pass**: the only RT render pass is `passes/raytracing/rt_uv_test_pass`
(a debug UV-gradient visualization). The DXR path tracer (`passes/pathtracing/`) proves we can
trace the scene correctly, but it is the offline reference, not the real-time reflection path.

**This task adds the missing real-time RT reflection pass**, so reflections see off-screen geometry
and get a Lumen-comparable look — reusing infrastructure that already exists.

## 1. The key insight — we already have the "surface cache" equivalent

Lumen's magic for off-screen reflections is the **Surface Cache** (persistent per-mesh cards that
cache material + lighting so a reflection ray can read detailed radiance off-screen). UzEngine took a
different, already-built path that fills the same role:

- Inline `RayQuery` reports the **hit distance only** — no interpolated UV/normal/material.
- The engine solves this (see `shaders/runtime/gi/ddgi_probe_common.hlsli::TraceProbeRay` +
  `gdf_surface.hlsli`) by sampling the **Global Distance Field surface** at the hit point:
  `GdfNormal(hitP)` for the normal and `SampleGdfSurface(hitP)` for albedo (packed in GBA).
- Indirect/multi-bounce radiance at the hit comes from the **radiance cache SH probes**
  (`gi/radiance_cache.h`, 16^3 probes, L2 SH, `GetProbeSHBufferSrv()`).

**So the reflection-hit shading model is: `GDF surface attributes` + `direct sun & shadow ray` +
`radiance-cache SH indirect` + `sky on miss`.** That is exactly the role Lumen's surface cache plays,
built from parts that already ship and are already proven by DDGI. We do NOT need to build a card
surface cache to get off-screen reflections — reuse the GDF+radiance-cache substrate.

## 2. Integration points (all verified in-tree)

| Need | Existing symbol / file | Notes |
|---|---|---|
| Opt-in gate | `r.RayTracing.Reflections` CVar + `RTManager::IsReflectionsEnabled()` | **Already declared** in `raytracing/rt_manager.h` (stub, alongside Shadows/AO/GI). Wire the pass to it. |
| Global RT on/off + TLAS ready | `RTManager::IsEnabled()`, `IsReady()`, `GetTLASInstanceCount()` | TLAS empty (count 0) when `r.RayTracing` off → every RayQuery misses. Guard the pass. |
| TLAS binding | `RTManager::GetTLASSRVIndex()` | Bind as `RaytracingAccelerationStructure` via `ResourceDescriptorHeap[idx]`. |
| TLAS build/refit | `raytracing/tlas_manager.{cpp,h}` | Per-frame hashed build/refit (block B.6). `WriteInstanceTransform` = engine row-vector → DXR 3x4 (note: same transform convention the path-tracer TLAS fix `56f65e6c` dealt with — translation in last column, 3x3 transposed). |
| Inline trace + hit shading pattern | `shaders/runtime/gi/ddgi_probe_common.hlsli::TraceProbeRay` + `gdf_surface.hlsli` | **Copy this pattern.** `RayQuery<RAY_FLAG_FORCE_OPAQUE>`, `TraceRayInline(tlas, …)`, `GdfNormal`/`SampleGdfSurface` for attributes, shadow ray with `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`, `SkyRadiance(dir)` on miss. |
| Indirect at hit (multi-bounce) | `gi/radiance_cache.h` `GetProbeSHBufferSrv()`, `HasProducedResult()` | Evaluate L2 SH at `hitP` for the diffuse indirect bounce the reflected surface receives. |
| Glossy ray importance sampling | path tracer VNDF GGX (`shaders/runtime/pathtracing/pathtracer_math.hlsli` / `pathtracer_shared.hlsli`) | Sample the GGX visible-normal distribution for roughness > 0 reflections (Heitz 2018). Mirror (roughness→0) = single reflect vector. |
| Roughness cutoff / SSR handoff | `r.SSR.MaxRoughness` (0.35 default), `r.SSR.Intensity` | Reuse the same roughness gating; RT reflections handle what SSR cannot (off-screen, and optionally rougher). |
| GBuffer inputs | `m_gbuffer.{depth,normal,velocity}` SRVs (see `RegisterSSGIPasses`) | Reconstruct world pos from depth; normal + roughness drive the ray. Roughness lives in the material/GBuffer channel used by SSR. |
| Denoise | SSR temporal + spatial denoise (`r.SSR.Temporal`, `r.SSR.SpatialDenoise`) OR the committed NRD specular reproject (`nrd_denoiser`, stage-2 `2edef44f`) | Prefer reusing the NRD specular path (virtual reflected-ray reprojection + history clamp) if it can be fed the RT reflection signal; else the SSR denoiser. |
| Output / composite | reflection buffer read by deferred lighting; `r.SSR.Composite` (in `frame_postprocess.cpp`) | Write into the same reflection target the composite already reads, so no new composite wiring is needed. |
| Pass registration | `frame/frame_gi_passes.cpp`, called by `SceneRenderer::RegisterLightingPasses()` | Add `RegisterRTReflectionPass()` next to the SSR passes; order after GBuffer + radiance cache update, before/into the reflection composite. |
| Render graph | `render_graph/render_graph_builder.*` | `rgBuilder.AddPass(...)` with `pb.Read`/`pb.Write` declaring depth/normal/velocity/TLAS reads + reflection UAV write (declared reads matter — see the render-graph residual fix `3.1`). |

## 3. The fallback chain (Lumen-style layered reflections)

Reflections should degrade gracefully, best source first:

1. **SSR** where the reflected point is on-screen and unoccluded (cheapest, sharpest for visible geometry).
2. **RT reflection** (this task) where SSR fails (off-screen, behind geometry) — trace against the TLAS.
3. **Reflection probe** where the RT ray misses the TLAS coverage or RT is unavailable.
4. **Radiance cache / sky** as the final constant fallback.

Implement the handoff as a confidence/validity mask from the SSR pass (it already knows where its
ray left the screen or found no hit) → RT fills those texels → probe fills RT misses. This mirrors
Lumen's screen-trace → SDF/HWRT-trace → radiance-cache fallback ladder.

## 4. Constraints & safety (hard requirements)

- **OPT-IN, default OFF.** `r.RayTracing.Reflections` defaults 0. With it off (and `--path-trace` off)
  the frame must be **byte-identical** to the F-005 baseline (`E:/UzmiEngine/f005-after/frames/f_95.png`,
  reflection-reference frame-95, `max_code_delta 0`). This is the non-negotiable regression gate.
- **Root-constant budget: ≤ 20 DWORDs** for bindless root constants (durable engine limit). A compute
  pass using inline `RayQuery` keeps root constants small: TLAS SRV index, GBuffer SRVs (depth/normal/
  roughness/velocity), radiance-cache SH SRV, sky SRV, output UAV, and a small params block. Do **not**
  grow the shared graphics root or `PerFrameCB`. If a DXR *pipeline* (DispatchRays + SBT via
  `shader_table_builder`) is used instead of inline, keep its private root ≤ the path-tracer's 24 DWORDs.
- **Inline RayQuery (DXR 1.1) preferred over a full DispatchRays pipeline** for v1: it reuses the exact
  DDGI pattern, needs no SBT, and runs as an ordinary compute pass. Reserve the DispatchRays pipeline
  (`shader_table_builder`) for a later stage if per-hit material shading is needed beyond the GDF surface.
- **Do NOT touch the base BRDF (D7, `pathtracer_shared.hlsli` coupling).** Reflection energy stays
  consistent with the now-energy-correct base.
- **Serialize engine builds** (single GPU lease). Bound every capture ≤ 180 s.

## 5. Staged plan

**Stage 1 — mirror reflections, off-screen, direct+sky.**
Trace a single reflect-vector ray per pixel (roughness→0 / below `r.SSR.MaxRoughness`), shade the hit
with GDF surface + sun + shadow ray + sky-on-miss (literally `TraceProbeRay` adapted). Fallback to SSR
on-screen, probe on RT-miss. Prove off-screen geometry now appears in reflections; default byte-identical.

**Stage 2 — glossy + indirect + denoise.**
GGX VNDF importance-sampled rays for roughness > 0 (reuse path-tracer sampling); add radiance-cache SH
indirect at the hit (multi-bounce); denoise via the NRD specular reproject or SSR denoiser; temporal
accumulation. Prove glossy reflections converge and match the path-tracer reference within tolerance.

**Stage 3 (optional, later) — ReSTIR reflections / spatial reuse.**
Reservoir reuse on reflection samples (the ReSTIR infra we built for DI generalizes). Defer unless the
glossy noise at low sample counts demands it.

## 6. Acceptance criteria (what "done" means per stage)

Stage 1:
- `r.RayTracing.Reflections 1` makes **off-screen geometry appear in a mirror-ish surface** that SSR
  leaves black/ghosted — capture a scene with a reflector facing off-screen geometry; show SSR-only vs
  RT reflection. Describe + read the PNGs.
- **Default byte-identical** (`r.RayTracing.Reflections 0`): frame-95 `max_code_delta 0`.
- Validate against the **path tracer**: the RT-reflection image is closer to the `--path-trace`
  converged reference than SSR-only is (report the error delta — the path tracer is our ground truth).
- build exit 0; `npm run test:layouts` exit 0; gtests no regression vs the current baseline (7892 pass
  + 3 pre-existing VfxEditorSlate; DX12DeviceTest can transient-TDR exit 127/255 → re-run once).
- A gtest for the reflection-ray construction (reflect vector + GGX sample pdf) and the fallback-mask logic.

Stage 2 adds: glossy convergence vs path-tracer reference; multi-bounce indirect present; denoiser
stability on camera motion (reuse the NRD clamp proof shape).

## 7. Risks / known traps

- **TLAS transform convention** — the path-tracer TLAS fix (`56f65e6c`) found engine `Mat4` translation
  is `_41/_42/_43` while DXR instance transform wants translation in the last column + the 3x3 basis
  transposed. `TLASManager::WriteInstanceTransform` already handles this; verify reflection rays land
  on geometry (an off-origin reflector is the test, same class of bug).
- **Inline RayQuery has no attributes** — must use the GDF surface for normal/albedo. Hits outside every
  SDF cascade fall back to average albedo (sentinel `1e9`); accept this (same as DDGI) or mark low-confidence.
- **Self-intersection** — use `kRayTMin = 1.0 cm` and a surface bias (`kSurfaceBias = 2.0 cm`) exactly as
  `ddgi_probe_common.hlsli` does; a grazing self-hit is the same bug class as the light-BVH `cfe1b425` fix.
- **Render-graph declared reads** — the reflection pass must declare its depth/normal/TLAS reads and
  reflection-UAV write or the graph mis-orders it (see the `3.1` residual work). 
- **Root budget** — do not exceed 20 DWORDs of bindless root constants.
- **Default-safety** — the pass must be fully skipped (not just zero-output) when off, to keep the frame
  byte-identical and cost-free.

## 8. References

- Wright, Daniel et al. **"Lumen: Real-time Global Illumination in Unreal Engine 5"**, SIGGRAPH 2022
  Advances in Real-Time Rendering course — surface cache, screen/SDF/HWRT trace ladder, radiance cache,
  reflection denoising. (The architecture we are matching.)
- Majercik, Guertin, Nowrouzezahrai, McGuire. **"Dynamic Diffuse Global Illumination with Ray-Traced
  Irradiance Fields"**, JCGT 2019 — the DDGI probe model the engine already uses (`ddgi_probe_common.hlsli`).
- Stachowiak, Uludag. **"Stochastic Screen-Space Reflections"**, SIGGRAPH 2015 — the SSR pass we extend.
- Heitz. **"Sampling the GGX Distribution of Visible Normals"**, JCGT 2018 — glossy reflection ray
  importance sampling (same VNDF the path tracer uses).
- Bitterli et al. **"Spatiotemporal reservoir resampling for real-time ray tracing" (ReSTIR)**, SIGGRAPH
  2020, and NVIDIA **RTXDI** — the reservoir reuse already implemented for DI, generalizable to Stage 3.
- NVIDIA **NRD (ReBLUR / ReLAX)** — specular reflection denoising; our `nrd_denoiser` stage-2 is the
  in-engine analog to reuse.
- AMD **FidelityFX SSSR (Stochastic Screen Space Reflections)** and **Brixelizer GI** — the SDF-based
  reflection/GI analog already linked in-engine (`passes/gi/brixelizer_gi_pass`).
- McGuire et al. **"Real-Time Global Illumination using Precomputed Light Field Probes"**, 2017 — the
  radiance-cache SH probe layout (`radiance_cache.h` cites it).

## 9. Turnkey subagent task prompt (ready to fire when the lane resets)

Paste the following as a `deep-high` task when the Astra quota is back. It is self-contained.

> Work in UzEngine at E:/UzmiEngine/uzengine. The engine/GPU (RTX 5080, DXR) is AVAILABLE (serialize
> builds — you hold the lease). Branch `feat/phase1-advanced-rendering`. Implement **STAGE 1 of hardware
> ray-traced reflections** per `docs/specs/rendering/rt-reflections.md` (READ IT FIRST — it is
> decision-complete and code-grounded). Summary: add a real-time RT reflection compute pass that fills
> off-screen reflections SSR cannot, reusing the inline-RayQuery + GDF-surface + radiance-cache pattern
> from `shaders/runtime/gi/ddgi_probe_common.hlsli::TraceProbeRay`.
>
> Gate behind the EXISTING `r.RayTracing.Reflections` CVar / `RTManager::IsReflectionsEnabled()` (stub in
> `raytracing/rt_manager.h`), AND `RTManager::IsReady()` + a non-empty TLAS (`GetTLASInstanceCount() > 0`,
> `GetTLASSRVIndex()`). Register `RegisterRTReflectionPass()` in `frame/frame_gi_passes.cpp` near the SSR
> passes (declare depth/normal/velocity/TLAS reads + reflection UAV write in the render graph). Shade the
> reflection hit = GDF surface (`GdfNormal`/`SampleGdfSurface`) + sun + shadow ray + sky-on-miss; fallback
> chain SSR(on-screen) → RT(off-screen) → reflection probe. Stage 1 = mirror/low-roughness only (reuse
> `r.SSR.MaxRoughness` cutoff); defer glossy+indirect+denoise to Stage 2.
>
> CRITICAL safety: OPT-IN, default OFF → frame byte-identical to `E:/UzmiEngine/f005-after/frames/f_95.png`
> (reflection-reference frame-95, max_code_delta 0). Bindless root constants ≤ 20 DWORDs. Do NOT touch the
> base BRDF (D7). Respect the TLAS transform convention (path-tracer fix 56f65e6c), `kRayTMin`/surface bias
> for self-intersection.
>
> PROVE IT: (a) off-screen geometry appears in a reflector where SSR leaves it black — capture SSR-only vs
> RT, read PNGs; (b) default byte-identical (max 0); (c) RT reflection is closer to the `--path-trace`
> converged reference than SSR-only (report the error delta — PT is ground truth); (d) build exit 0;
> `npm run test:layouts` exit 0; gtests no regression vs 7892 + 3 pre-existing VfxEditorSlate (DX12DeviceTest
> transient-TDR exit 127/255 → re-run once); add a gtest for the reflection-ray construction + fallback mask.
>
> BOUNDS: bound every run ≤180s; kill+switch if a run hangs >3 min; stop all engine/test/zig processes when
> done; do NOT commit. Keep diffs scoped to: the new RT reflection shader(s) under `shaders/runtime/`, the
> pass wiring in `frame/frame_gi_passes.cpp` (+ `rt_manager.cpp` if the CVar needs registering), the gtest,
> and the task's tasks.md. IGNORE the uncommitted layered-stage-3 WIP + the VFX-editor WIP + untracked junk
> (`*.tmp.*`, `pathtracer_furnace_test.h`, `.agents/`, `.claude/`, capture dirs). Give a COMPLETE changed-files
> list + the off-screen proof, the byte-match, the vs-path-tracer delta, and test results.

## 10. Where this lands on the roadmap

Recommended as a new/explicit item under **phase2** (reflections) or folded into
`phase2_ssr-stochastic-refactor` (which already owns the SSR side). It is the single highest-impact
step toward Lumen-parity reflections. Diffuse dynamic GI is already close; this closes the specular gap.
