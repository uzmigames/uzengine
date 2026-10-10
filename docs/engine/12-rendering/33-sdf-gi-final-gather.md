# SDF Global Illumination: Distance Fields, Radiance Cache, Final Gather (Sponza parity, steps 3-4)

<!-- doc-meta
title: SDF Global Illumination
category: reference
engine: uzengine
theme: rendering
created: 2026-09-08
updated: 2026-09-08
status: active
author: sponza-parity-03-04
-->

## Overview

Until 2026-09-07 the two shaders at the root of this system — `mesh_sdf_bake_cs.hlsl` and
`global_sdf_composite_cs.hlsl` — were one-line stubs with empty bodies. Their C++ callers
dispatched, logged successful bakes and exposed the outputs, so a compiled pipeline and a clean
startup log never proved the scene existed in the GI representation. This page describes the system
as it now works, ported from the `test/windows-sponza-parity` investigation, and the switches that
control it.

```
mesh load ─► MeshSdfBaker (BVH, 128³ R16F per mesh) ─► GlobalDistanceField (4 clipmaps: raw R16F + expanded RGBA16F)
                                                              │
                     RadianceCache (16³ probes, L2 SH, validity, history) ◄─┘
                                │
   deferred: probe interpolation  ─or─  r.GI.FinalGather (hemisphere rays, half-res, denoised, reprojected)
   deferred: prefiltered cubemap  ─or─  r.GI.Reflections (GGX rays through the field, SSR lerped before the BRDF)
```

## Mesh distance fields

`MeshSdfBaker::BakeMeshGPU` builds a CPU BVH (`mesh_sdf_bvh.h`) over the mesh triangles, uploads it,
and dispatches `mesh_sdf_bake_cs.hlsl` at `r.GI.MeshSdf.Resolution` voxels per axis (default 32; the
Sponza project uses 128). Sign is decided by the closest triangle's *oriented* normal:
`OrientSdfFaceNormal` flips the geometric cross product to agree with the vertex normals, because
Sponza's imported winding disagrees with its normals for 3,739,444 of 3,747,286 triangles.

Orientation alone is not enough. Sponza is split into open material partitions (the exterior stone
alone has 42 boundary edges after welding), and a nearest-face sign cannot define an interior for an
open surface — six known free points inside the atrium still read negative in 10-11 meshes after
orientation. **`r.GI.MeshSdf.TwoSided=1`** produces an unsigned field for such content; the Sponza
project sets it, and any scene made of open shells should.

Both CVars take effect at bake time; a scene reload is required after changing them. Bakes are
queued only while `m_sdfGiEnabled` is true, so a scene with `r.RadianceCache=0` skips them (on Sponza
that is ~37 s of startup).

## Global distance field

`GlobalDistanceField` composites every resolved draw into four camera-centred clipmaps
(`r.GI.GDF.Resolution`, default 128; Sponza uses 256). Each clipmap keeps **two** textures:

- the *expanded* RGBA16F field (distance plus average diffuse reflectance), consumed by the radiance
  cache, and
- the *raw* R16F field, consumed by the surface gather and the reflection trace.

Expansion widens surfaces to make sphere tracing stable, but a widened field closes real openings — a
100x200 cm window is closed at mesh32/GDF128 and mesh128/GDF128, and stays open only at
mesh128/GDF256 with the raw field. Expansion is therefore applied during the trace, not stored, and
`scripts/rendering/fixtures/gdf_expanded_before_raw.h` freezes the old compositor so 200 bitwise checks can
prove the expanded output did not change when the raw one was added. Do not regenerate that fixture.

The field's origin is preserved rather than recentred on the camera. A scene revision counter is
bumped when meshes load, unload or move — not just when the camera moves — and consumers use it to
invalidate their history.

## Radiance cache

`RadianceCache` holds a 16³ grid of L2 spherical-harmonics probes over `r.GI.RadianceCache.GridExtent`
(Sponza: 2000 cm; the grid is created at the CVar value, which also fixed a producer/consumer
spacing mismatch — spacing is `2*extent/(N-1)` on both sides). `radiance_cache_trace_cs.hlsl` traces
`r.GI.RadianceCache.RaysPerProbe` rays for `r.GI.RadianceCache.ProbesPerFrame` probes per frame:

- a hit is shaded with the field's surface normal (central differences), a sun shadow ray traced
  through the field from `hit + N*2ε`, the Lambert 1/π, and the previous frame's probe irradiance as
  a feedback term, so light propagates through successive diffuse bounces;
- only a ray that provably escapes the field receives sky radiance — a ray that exhausts its step
  budget is treated as occluded, never as sky;
- the output SH is cosine-convolved for the consumer; a uniform radiance of 1 evaluates to 1;
- record 6.w carries **validity**: a probe inside geometry writes 0 and every consumer skips it and
  renormalises the surviving weights;
- all 4096 probes receive a first-write initialisation; the history is discarded only then, when
  the scene revision changes (`InvalidateHistory`), and, since block B-02 (0a9cc732), for the probes
  inside a GDF change box (`RetraceKeepingHistory`; `r.GI.RadianceCache.RegionBlendAlpha`,
  default 1 = replace). A grid **recentre** (the snapped camera crossed a
  probe cell, spacing ≈ 267 cm on Sponza) retraces every probe in the same frame but keeps each
  probe's history: the grid moves by whole probes, so probe `c` of the new grid is probe
  `c + shift` of the old one, and the shader reads both the hysteresis history and the multi-bounce
  feedback through that shift (`historyShift*` row of the trace constants, `HistoryGridOrigin()`).
  Only the slab that entered the grid starts from a single-frame estimate;
- deferred lighting reads the buffer completed **last** frame (the per-frame constants are built
  before the GI passes), so `frame_constants.cpp` publishes `GetCompletedGridOrigin()` — the origin
  that buffer was traced with — never the current origin, which on a recentre frame is already one
  cell away. See the addendum below for what happened before these two rules.

The VFX→DDGI emissive injection (phase 16) is preserved. The trace constants are SponzaTest's 176
bytes, our 16-byte VFX tail, the 16-byte grid-recentre row and, since block B-02 (0a9cc732), 16
change-box rows: 464 bytes. The DDGI writers declare the first 13 rows and ignore the recentre row,
in `ddgi_probe_common.hlsli` since block B-06 (edfd7942).

### Consumer semantics — zero is a valid answer

In `deferred_lighting_ps.hlsl` the probe lookup weights the eight corners by trilinear factor **and**
by `saturate(dot(N, toProbe))²`, so a probe behind the surface's tangent plane cannot light or darken
it. Invalid probes are skipped and the result is divided by the surviving weight. When the cache is
enabled the result is adopted **even when it is zero**: an unlit enclosure really does receive no
bounce light. The old code treated an all-zero result as "not converged yet" and fell back to sky
irradiance, which is what made interiors glow. `skyLightMinAmbient` is applied only when no GI source
provided the irradiance.

## Final gather (`r.GI.FinalGather`)

Direct interpolation between probes ~267 cm apart does not resolve lighting on Sponza's columns and
arches. The final gather replaces the probe-interpolated diffuse irradiance with
`r.GI.FinalGather.Samples` cosine-weighted hemisphere rays per pixel (default 8, the maximum-quality
preset uses 32), traced through the raw field and lit by shadowed sun plus probe irradiance at the
hit. It runs at half resolution in the SSGI pass slot (`ssgi_trace_cs.hlsl` in *world* mode), with a
plane/normal-aware spatial filter, history reprojected with the previous view-projection, and
invalidation on scene revision. The legacy screen-space trace remains as the other mode of the same
pass. The gathered result **replaces** `cosineIrradiance`; it never adds a second ambient term.

Two activation rules matter: the renderer prepares its resources from the initialised pipeline
library (not before `ctx.pipelines` exists), and the shared pass slot opens for
`r.SSGI || r.GI.FinalGather` — gating it on `r.SSGI` alone left the gather silently inactive
(`samples=0`, sentinel SRV), which `scripts/rendering/test_gather_activation.ps1` catches (192/192).

## Reflections (`r.GI.Reflections`)

`sdf_reflections.hlsli` traces `r.GI.Reflections.Samples` GGX rays (default 4; preset 16) up to
`r.GI.Reflections.Steps` sphere-tracing steps (default 96; preset 256) and
`r.GI.Reflections.Distance`. A hit receives the shadowed sun and that surface's diffuse; an escaping
ray receives the sky; an exhausted budget is black, not sky. The ray starts from the **geometric**
normal (`ddx_fine/ddy_fine` of world position), because a normal-mapped direction can push the origin
behind the face it sits on, and the first intersection is refined before shading. SSR is blended in
**before** the BRDF — `radiance = lerp(sdf, ssr.rgb, ssr.a)` — replacing the old additive composite
after TAA.

## Switches, in one place

| CVar | Default | Sponza project | Effect |
|---|---|---|---|
| `r.RadianceCache` | 0 | 1 | master switch; also gates SDF bakes |
| `r.GI.MeshSdf.Resolution` | 32 | 128 | per-mesh field, bake time |
| `r.GI.MeshSdf.TwoSided` | 0 | 1 | unsigned fields for open shells, bake time |
| `r.GI.GDF.Resolution` | 128 | 256 | clipmap resolution |
| `r.GI.RadianceCache.GridExtent` | 5000 | 2000 | probe grid half-extent (cm) |
| `r.GI.RadianceCache.RaysPerProbe` / `.ProbesPerFrame` | — / 1024 | — | trace budget |
| `r.GI.FinalGather` / `.Samples` | 0 / 8 | 1 | hemisphere gather replaces probe interpolation |
| `r.GI.Reflections` / `.Samples` / `.Steps` / `.Distance` | 0 / 4 / 96 / 5000 | 1 | SDF reflections replace the prefiltered cubemap |
| `r.GI.Intensity` | — | — | scales the adopted irradiance |

`r.RadianceCache=0` honours the CVar only since 2026-09-08: `SceneRenderer::Init()` used to
re-assign `m_sdfGiEnabled` without the CVar term after `ApplyCVars()` had computed it, and the
project's `cvars` block used to be applied *after* the command line (`Config` now front-inserts
project values; see `docs/guides/build-and-deployment.md`).

## Cost

Maximum preset (FG32, 16 reflection samples, 256 steps, GDF256, mesh128) at 1920x1080: ~12.5 fps on
the development GPU here, ~1 fps on a 6 GB GTX 1660 SUPER (SponzaTest). Project defaults (FG8, 4
samples, 96 steps) are the intended interactive setting; FG32 mainly removes gather noise on
surfaces in shadow, not exposure.

## Known open

- One of 32 gather rays leaks for some seed/orientation pairs: `test_sdf_surface_origin.ps1 -Label axis`
  fails at max channel error 0.009328. The assertion stays enabled.
- A grazing ray at normal cosine 0.01 exhausts 256 steps: `test_sdf_dynamic_trace.ps1` 14/15.
- No per-texel surface cache, no complete emissive transport; Lumen parity is not claimed.
- Stone in Sponza decodes as metallic in a noticeable fraction of pixels (`--view-mode metallic`);
  that comes from the graph's Metallic/Roughness texture channels with the neutral
  `metallicMultiplier: 1.0`, not from this system, and predates it.

## Instruments — what they do and do not show

- `--diag` validates a 4x4 block at each target's centre. Since 2026-09-16 (followup-06 4.3),
  `gbuffer.ao`, `gbuffer.metallic` and `gbuffer.roughness` are read on their own channel, and depth
  or oversized targets report `SKIPPED`. 3D targets are read through their centre slice (`gi.gdf_*`
  non-zero on 2026-10-08). Each target's `channel` field says what was measured: a whole RGBA target
  mixes all four channels, alpha included, into min/max/avg. Use the region dump for per-pixel
  questions.
- `--view-mode detaillighting` is directional NdotL only and `reflections` the SSR resolve only.
  Since 2026-09-16 (followup-06 4.2), `lighting` shows the direct light, `lightingonly` direct +
  indirect from the lighting pass's split targets (no albedo), `shadows` the shadow mask and `ao` the
  GTAO result (the G-buffer alpha when GTAO is off); these four are bindless-only. None of them
  isolates IBL or GI specular.
- `--pixel-debug` is dead (`pixel_debug_cs.hlsl` is a stub).
- Shaders are embedded at build time; the `ShaderVFS` mount points at a directory that does not
  exist, so every shader edit needs `zig build`.
- `SDF GI: frame=N, active=…, probesReady=…, probeSrv=…` at `--log-level debug` every 120 frames is
  the cheapest live confirmation that the cache is producing and being consumed.

## Re-verification

```powershell
foreach ($t in 'test_mesh_sdf_bvh','test_gdf_raw_composite','test_gdf_raw_lifecycle','test_sdf_dynamic_origin',
               'test_rc_trace_exhaustion','test_filtered_gather','test_gather_activation',
               'test_surface_geometric_normal','test_reflection_selection',
               'test_sdf_reflection_firefly','test_rc_recentre_scroll') {
  powershell -NoProfile -ExecutionPolicy Bypass -File "scripts/$t.ps1"
}
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_radiance_cache_sh.ps1 -Zig (Get-Command zig).Source
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_window.ps1 -CompileOnly   # shared GPU fixtures
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_refined_controls.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_sdf_diffuse_gpu.ps1
```

Expected: 3082 exact BVH comparisons; 210/210 (200 bitwise); 34/34; 4/4; 12/12; filter PASS; 192/192;
11/11; 8/8; 18/18; 42/42; 90/90; GPU controls with max channel error ≤ 0.000195.

## Related

- `31-energy-and-bias-corrections.md`, `32-graph-material-draw-metadata.md` — the two steps this builds on
- `.rulebook/archive/2026-10-10-phase0_sponza-parity-03-sdf-gi-radiance-cache/notes.md` — port decisions and the
  2026-09-08 regression hunt
- `docs/analysis/rendering/` — the SponzaTest reports, once task 06 ports them

## Addendum 2026-09-08 — estimator fixes and noise

- `GatherSdfReflections` weights rays with the Karis HDR weight `1/(1+luma)`, like the SSR
  resolve, and keeps below-horizon rays' lobe weight in the denominator: renormalising over the
  surviving rays let one sun-lit ray become the pixel with up to 4× inflation.
- The radiance-cache consumer selects probes with the **geometric** normal
  (`ddx_fine/ddy_fine` of world position); the shading normal is used only for `EvalSH_L2`.
  Selecting with the normal-mapped normal let neighbouring texels admit different probe sets.
- The SDF reflection path has no spatial or temporal filter. Rays that leave the distance field
  through an opening return sky radiance; at 4 rays per pixel this is binary per pixel and shows
  as white specks on dark walls beside windows. `r.TAA` (off by default) suppresses ~75–85 % of
  it and is now set in `sample/projects/sponza.uzproject`. No filter was built: in linear radiance, the isolated pixels that reach the TAA output are not
  reflection pixels (ledger H-41, H-68, 2026-09-23; H-68's rule is the re-test), so a project that
  enables `r.GI.Reflections` sets `r.TAA 1`. Guard: `scripts/rendering/test_sdf_reflection_firefly.ps1`.

## Addendum 2026-09-08 — grid recentre without a blink

Three defects made every probe-cell crossing (≈ 267 cm of camera travel on any axis at Sponza's
`GridExtent` 2000) visible while flying; all three are fixed and guarded:

1. **Readiness gate.** `HasProducedResult()` was false on the recentre frame, so the per-frame
   constants published `radianceCacheEnabled = 0`, which also switched off the final gather and the
   SDF reflections: one frame of unoccluded sky-SH under an interior exposure (near-white flash,
   18.9 → 236.3 → 15.4 /255 on a horizontal sweep). The gate now depends only on the first write.
2. **Origin/buffer mismatch.** The per-frame constants are built before the GI passes, so lighting
   samples the buffer completed *last* frame — it must be indexed with the origin that buffer was
   traced with (`GetCompletedGridOrigin()`), not the current one, which on a recentre frame is a
   whole cell away (floor lit by the probes 267 cm above it: 83.4 → 95.3 → 88.0 on a vertical sweep).
3. **History loss.** The retrace ran as a first write (alpha 1, feedback 0): every probe restarted
   from a single-frame estimate and the lighting drifted for ~40 frames at alpha 0.12. The grid
   moves by whole probes, so the trace shader now reads each probe's history — hysteresis and
   multi-bounce feedback — at `coordinate + shift` through the `historyShift*` row of the trace
   constants; only the entering slab starts fresh, and only the first write or a scene-revision
   change (`InvalidateHistory`) discards history (since block B-02, a GDF change box does too, for
   the probes inside it).

After the three fixes the vertical sweep reads 83.4 → 83.9 → 84.1 across the crossing with
adjacent-frame differences at the motion-only level (2.0 % of pixels > 40/255, versus 12.4 % at
the flash). Repro scenes: `scripts/sponza-longsweep-camera.*` (+X), `scripts/sponza-vertical-camera.*`
(+Z, the user's pose). Guards: `scripts/rendering/test_sdf_reflection_firefly.ps1` (gate),
`scripts/rendering/test_rc_recentre_scroll.ps1` (origin + history scroll, CB layout; re-anchored 2026-10-08 to
the 464-byte layout, 42 checks).

Related, same day (`phase0_sponza-followup-07`): the TAA jitter was applied to the combined
view-projection as if it were the projection matrix, which turned the sub-pixel offset into a
`jitter · world_z / w` shear — several pixels on near geometry. `frame/taa_jitter.h` now folds a true
NDC translation; gtest `TaaJitter.*`. The Sponza project's `r.TAA: 1` (set for the firefly work
above) is what exposed it.

