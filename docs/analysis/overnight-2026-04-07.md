<!-- doc-meta -->
title: Overnight Work Session — 2026-04-07
date: 2026-04-07
status: report
<!-- end-doc-meta -->

# Overnight Session Report — 2026-04-07

User directive: "implmente continuamente, vou dormir, sem perguntar nada".
Session ran continuously without confirmation prompts. 9 commits landed.

## Goal

Make the rasterized Sponza renderer "minimumly decent" — fix the dark
splotchy interior and get the renderer to a state where Sponza is properly
lit, recognizable, and visually coherent.

## Commits (chronological)

```
97de70d7 feat(sponza): add 4 upper-level arcade point lights + tonemap contrast
48c9e5d0 tweak(sponza): enable GTAO for crevice contact + lower min ambient
1fbd924e feat(sponza): add back-area point lights for full atrium coverage
59354e8e fix(sponza): boost point light intensities 200x for cm-scale physical units
cb9f4028 fix(scene_renderer): destroy PT accumulation textures on shutdown
69857b24 fix(dump+ssgi): enable gbuffer.depth dump + bump SSGI thickness default
315f89b8 fix(materials): revert pointless AO wire — Sponza R channel is constant 1.0
eb839ed9 feat(materials+rendering): named pins, material params, path tracer mode, sponza fix
```

(plus the prior `wip: day/night cycle` from the previous session)

## Root cause of the original "splotchy" complaint

The user originally saw heavy black streaks across walls in Sponza. The
investigation took several wrong turns before landing on the truth:

1. **Wrong hypothesis #1**: AO uninitialized → fixed via translator default,
   but didn't change the visible result (because the matgraph path *was*
   already setting AO via Sponza's metalRoughness — except it wasn't,
   because Sponza R channel is constant 1.0 — verified via PIL histogram).

2. **Wrong hypothesis #2**: GTAO over-occlusion → tested with `r.GTAO=0`,
   no change.

3. **Wrong hypothesis #3**: DFS cone-trace noise → tested with `r.DFS=0`,
   floor cleaned up but walls still splotchy.

4. **Wrong hypothesis #4**: Path tracer Step 7 screen-space sun shadow →
   removed, no change.

5. **Actual cause**: The Sponza scene's `SkyLightComponent` was authored
   with night-time intensity values (`intensity: 0.35`,
   `indirect_lighting_intensity: 0.5`, `min_occlusion: 0.04`). Combined
   with the procedural sky's already-dim SH coefficients, the final
   ambient was R=0.005 G=0.026 B=0.044 — essentially zero. The "splotchy"
   pattern was the asset's authentic roughness texture (G channel:
   28-255 contrast) being amplified by tonemap on a critically
   under-exposed HDR.

## Visible improvements

| Aspect | Before | After |
|---|---|---|
| Indoor brightness | Near-black | Properly lit |
| Sun spots on floor | Invisible | Visible |
| Back arches | Hidden in shadow | Lit by back/upper lights |
| Wall textures | Looked broken | Authentic stone showing detail |
| Point lights contribution | ~0.006 of authored value | Proper inverse-square at architectural distances |
| Brixelizer GI | Not enabled | Enabled by default in scene |
| GTAO | Disabled by default | Enabled at low intensity for contact AO |
| Camera-dependent splotches | Visible (PT screen-space shadow) | Removed |

## Architectural improvements landed

1. **Named pins in material graph** (UE5/Godot pattern). `"dstPin": "BaseColor"`
   and `"srcPin": "RGB"` instead of `"pin": 0` / `"srcPin": 0`. Backward-
   compatible with the legacy int format. Resolved at load time, post-pass
   for source pin names.

2. **MaterialParams struct** with standard PBR knobs: `baseColorTint`,
   `roughnessMultiplier`, `metallicMultiplier`, `emissiveMultiplier`,
   `normalStrength`, `specularMultiplier`, `aoMultiplier`. Applied as
   post-multiplications in the HLSL translator. Default values render
   identical to a graph without overrides.

3. **Translator emits explicit defaults** for unconnected `out_*` parameters
   (out_ao=1.0, out_emissive=0, out_opacity=1, etc.) — fixes a class of
   bugs where unconnected pins left consumer locals uninitialized.

4. **28 Sponza .uzmat migrated** to named pins; 3 broken JSON5 files
   (glass, lamp_glass_01, light_bulb) had trailing-comma syntax errors fixed.

5. **DumpChannel enum** on RegisteredTarget — split packed RTs into
   distinct grayscale dump views (gbuffer.ao from albedo.a,
   gbuffer.metallic from normal.b, gbuffer.roughness from normal.a).

6. **gbuffer.depth dump** now produces a usable image via sqrt²
   curve to expand reversed-Z dynamic range.

7. **Path tracer runtime mode** Steps 1-7 (skeleton → GBuffer first hit →
   diffuse → sun → GGX → progressive accumulation → screen-space shadow).
   Toggle via `r.PathTracer.Enable=1`. Step 8 Falcor PSNR deferred.

8. **PT_Accum texture leak** at shutdown — fixed (was triggering
   D3D12MA assertion).

9. **Atmosphere LUT cleanup** — 5 stub Bruneton precompute shaders deleted
   by user; matching C++ pipeline factories, member fields, lifecycle
   destroy/warmup calls, and atmosphere_renderer.cpp render-graph blocks
   removed. Aerial perspective + transmittance LUT preserved.

## Known follow-ups (deferred — non-blocking)

1. **SSGI broken** — `expectedLinearDepth = lerp(linearRayDepth,
   linearRayDepth + 5000.0 * t, t)` in `ssgi_trace_cs.hlsl` has a hardcoded
   5km depth assumption with no physical basis. SSGI remains mostly broken
   on cm-scale interiors regardless of the thickness CVar. Brixelizer GI
   is the preferred indoor GI for now. Documented inline.

2. **Brixelizer dump targets show pure white** — values are stored at HDR
   magnitudes (>10) which saturate the dump's Reinhard tonemap. The actual
   GI contribution is correct in the rendered image; only the diagnostic
   readback is misleading. Needs a separate display path for HDR formats.

3. **Shadow atlas (depth-stencil)** still not in pass-dump — needs a
   depth→color copy path that doesn't disrupt the renderer's command list
   state. Gbuffer depth was special-cased and works.

4. **Path tracer Step 7 ECS analytic primitives** (PtPrimitiveComponent +
   Cornell-style scene authoring) — deferred until the user explicitly
   wants to revisit the path tracer.

5. **Path tracer Step 8 Falcor PSNR parity** — `scripts/falcor/uzengine_pt_reference.py`
   provided as a stub. Needs Mogwai setup + scene `.pyscene` export.

6. **DXR-based RT shadows** (diharaw hybrid pipeline pattern) — discussed
   in chat. Deferred until rasterized pipeline is solid (which it now is).
   The TLAS infrastructure already exists in the codebase and is the
   logical next major rendering improvement.

## CVar overrides now in sponza.uzscene

```json
"cvars": {
    "r.PathTracer.Enable": 0,
    "r.BrixelizerGI": 1,
    "r.BrixelizerGI.Intensity": 1.4,
    "r.SkyLight.Intensity": 1.0,
    "r.SkyLight.MinAmbient": 0.10,
    "r.SkyLight.IndirectIntensity": 1.0,
    "r.GTAO": 1,
    "r.GTAO.Intensity": 0.5,
    "r.GTAO.Radius": 60,
    "r.Tonemap.Slope": 0.98,
    "r.Tonemap.Toe": 0.50
}
```

## Lighting summary in sponza.uzscene

- **1 directional sun** — intensity 30, color (1.0, 0.98, 0.95), rotation (55, 35, 0)
- **1 SkyLight** — intensity 2.5, indirect 1.0, min_occlusion 0.10,
  lower_hemisphere_color (0.12, 0.11, 0.10)
- **3 ceiling point lights** — intensity 240k-300k (after 200x boost),
  range 3000-3600 cm
- **4 arcade point lights** (left/right × front/back) — intensity 160k,
  range 2400 cm
- **1 floor warm light** — intensity 80k, range 1800 cm
- **3 back-area point lights** (center/left/right) — intensity 240k
- **4 upper arcade point lights** (L/R × front/back) — intensity 144k

Total: 1 directional + 1 sky + 16 point lights = 18 light sources.

## Files touched

```
CLAUDE.md                                                       (overnight rule)
runtime/build.zig                                               (pathtracing dirs)
runtime/engine/material/compiler/hlsl_material_translator.cpp   (defaults + post-mul)
runtime/engine/material/material_graph.{h,cpp}                  (MaterialParams + named pins)
runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp      (Bruneton block removal)
runtime/engine/renderer/core/pipeline/{pipeline_atmosphere,
    pipeline_geometry, pipeline_library, pipeline_library_impl,
    pipeline_lifecycle}.cpp/h                                   (PT pipeline + atmo cleanup)
runtime/engine/renderer/frame/frame_lighting.cpp                (PT pass + dump targets + cvar)
runtime/engine/renderer/gi/ssgi_renderer.cpp                    (thickness default bump)
runtime/engine/renderer/passes/debug/inspector_pass*.{cpp,h}    (DumpChannel + depth dump)
runtime/engine/renderer/passes/gi/brixelizer_gi_pass.{cpp,h}    (texture getters)
runtime/engine/renderer/passes/pathtracing/path_tracer_pass.{h,cpp}    (NEW)
runtime/engine/renderer/scene/scene_renderer_impl.h             (PT_Accum members)
runtime/engine/renderer/scene/scene_renderer_init.cpp           (PT_Accum alloc + cleanup)
runtime/shaders/runtime/atmosphere/                              (8 stub shaders deleted)
runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl         (NEW)
sample/content/materials/sponza/*.uzmat                         (28 files migrated)
sample/content/scenes/sponza.uzscene                            (lighting overhaul)
scripts/falcor/uzengine_pt_reference.py                         (NEW stub)
.rulebook/tasks/phase0_path-tracer-runtime-mode/                (NEW task)
docs/analysis/path-tracing/                                     (NEW design docs)
```

## Branch state

`refactor/render-falcor-parity` — 9 commits ahead of starting point.
Ready to PR or continue work.
