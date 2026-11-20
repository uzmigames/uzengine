# ReSTIR DI (reservoir-based spatiotemporal importance resampling for direct light)

Opt-in direct-lighting estimator of the DXR pipeline path tracer
(`docs/specs/rendering/pipeline-path-tracer.md`). With N emitters, every primary
hit shades one resampled light sample per frame instead of N (or K uniform) NEE
samples. Rulebook task `.rulebook/tasks/phase1_falcor-restir-di`; references
Bitterli et al. 2020 (ReSTIR), Talbot et al. 2005 (RIS), Lin et al. 2022 (GRIS).

## Running

```sh
runtime/build/bin/uzengine.exe --project scripts/rendering/restir-many-lights/g32-a.uzproject \
  --path-trace --pt-bounces 2 --restir-di --cvar r.LightBVH=1 --frames 200
```

`--restir-di` (both `uzengine.exe` and the application entry point) sets the
single runtime gate `r.ReSTIR.DI` (default 0). It is honoured only by the
pipeline path tracer; the raster/deferred renderer never reads it. Sub-gates:
`r.ReSTIR.Temporal` (default 1), `r.ReSTIR.Spatial` (default 1),
`r.ReSTIR.Readback` (diagnostic, default 0). With `r.LightBVH=1` the
candidates are mesh emitters and directional lights drawn through the light
hierarchy (`light_bvh.h` / `include/light_bvh.hlsli`); with the BVH off only
directional lights are resampled. Scenes with the volume or layered
material options bypass ReSTIR and keep ordinary NEE.

## Passes and resources (`PipelinePathTracerPass`)

| Pass | Shader | Reads | Writes |
| --- | --- | --- | --- |
| `ReSTIR_PrimaryHit` | DXR extraction (`restirMode=1`) | TLAS | `RestirSurface` per pixel (64 B) |
| `ReSTIR_Initial` | `restir_initial_cs.hlsl` | surfaces, light BVH, TLAS | initial `Reservoir` (116 B) |
| `ReSTIR_Temporal` | `restir_temporal_cs.hlsl` | initial, previous reservoirs, `RestirCameraHistory` | current reservoirs |
| `ReSTIR_Spatial` | `restir_spatial_cs.hlsl` | current reservoirs, TLAS | initial buffer (final reservoir) |
| `PipelinePathTracer` | `pathtracer_closesthit.hlsl` | final reservoir | radiance (one shadow ray per primary hit) |

The reservoir buffers ping-pong per frame (`reservoirs[frameIndex & 1]`); the
spatial output is never fed back into history. All declarations are mirrored in
`include/restir_di.hlsli` and `pipeline_path_tracer_pass.h` and checked by
`scripts/rendering/test_cbuffer_layouts.py` (Tier C). The pass-private root
block `PipelinePathRoot` stays 96 bytes / 24 DWORDs; the shared graphics root
signature and `PerFrameCB` are untouched.

## Estimator

- Initial RIS: 32 candidates per pixel (`RestirRandom` hashes of pixel, frame
  and dimension). Each candidate's weight is `target / sourcePdf`, where the
  target is the luminance of the unshadowed contribution at the receiving
  surface and the source PDF is the light-BVH joint area density (selection
  probability / emitter area, stratified over the 32 candidates) or the
  discrete directional-disk density. `W = weightSum / (M * target(selected))`;
  a zero target gives `W = 0`. One inline shadow query then multiplies `W`
  and `weightSum` by the selected sample's visibility (M is retained).
- Temporal reuse: the history reservoir is validated by surface normal
  (`dot >= 0.9`) and point-to-plane distance, combined with generalized
  balance MIS over both domains with visible targets, and `M` capped at 20
  (19 history + 1). `W = weightSum / target(selected)` after reuse.
- Spatial reuse: four neighbours in a 30-pixel disk, same gates, five-domain
  MIS `m_i = M_i p_i / sum_j M_j p_j` with exact visibility at every domain
  (cached only for a sample's own source), identity shift on emitter area
  (Jacobian 1; cosine / distance^2 live in the receiving contribution).
- Final shading evaluates `RestirContribution * W` for the selected sample
  after one DXR shadow ray, preserving the NEE/BSDF balance partition so the
  emitter-hit term keeps its complementary MIS weight.

The invariant `E[W g(Y)] = integral_visible g` and its proofs are recorded in
the task's `tasks.md` (stages 1-3) and in the `RestirDIStage1/2/3` gtests.

## Temporal history across camera motion

Reservoir history is independent of the accumulation counter. The pass keeps
last frame's `PerFrameCB` view-projection rows and camera position in a
`RestirCameraHistory` record (96 B, uploaded per frame slot). History is valid
when ReSTIR ran last frame at this resolution, the frames are consecutive, no
manual reset or scene edit happened, and the transport signature without the
camera (`denoiserSignature`) is unchanged (`RestirHistoryValid` in the header,
gtest `RestirDIHistory.*`). `restirHistory` in the root block is then the
record's SRV index + 1, otherwise 0.

In `restir_temporal_cs` an unchanged camera (`cameraMoved == 0`, decided on
the CPU by comparing the matrix bytes) reads the same pixel, so static-camera
results are unchanged bit for bit. A moved camera reprojects this frame's
primary hit through last frame's rows (`P.x*row0 + P.y*row1 + P.z*row2 + row3`,
the inverse of the raygen pixel mapping) and reads that pixel's reservoir; the
existing normal / point-to-plane gate still decides whether it is reused, which
is what rejects disocclusions and moved objects (static geometry only; there
are no per-object motion vectors in this path). The reservoir passes seed
their random numbers with the frames since the history was last dropped, so
a moving camera (whose accumulation resets every frame) still draws fresh
candidates each frame. GI-ReSTIR keeps the accumulation-bound history rule.

## Verification

- `scripts/rendering/measure_restir_many_lights.py` generates the
  `scripts/rendering/restir-many-lights/` scenes (16 .. 1,024 emissive cubes,
  four camera angles, a Lua camera dolly), captures bounded runs
  (`capture OUTPUT MODE`) and analyses them (`analyze OUTPUT`): converged-mean
  delta against uniform-emitter NEE, floor MSE at equal frame counts, GPU pass
  times from the run manifest, montages.
- `scripts/rendering/measure_light_bvh_grid.py`, `measure_restir_occlusion.py`
  and `measure_restir_gi.py` cover the 100-emitter, occlusion and GI studies.
- gtests: `test_restir_di.cpp`, `test_restir_di_spatial.cpp`,
  `test_restir_di_bvh.cpp` (exact-enumeration unbiasedness, MIS partition of
  unity, visibility reuse, history rule, reprojection convention).
- Evidence directories: `E:/UzmiEngine/_evidence/lighting/restir-di-finish`
  (1,024-light study) and the earlier `restir-*` / `light-bvh-finish` trees.

## Limits

- Lives in the DXR path tracer. The deferred raster lighting pass does not
  consume reservoirs; the proposal's `deferred_lighting_ps.hlsl` integration
  and a separate `RestirDiPass` class were superseded by this pass.
- Static-geometry reprojection only; dynamic objects fall back to the
  initial + spatial estimate where the surface gate rejects the history.
- Equal frame count is not equal GPU cost: the 32 BVH candidates and the
  neighbour target evaluations cost more per frame than one NEE sample. The
  win is variance per sample and the flat cost in the number of lights.
