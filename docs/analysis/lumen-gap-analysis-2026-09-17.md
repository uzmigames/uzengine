# UzEngine lighting versus Lumen

Date: 2026-09-17. Baseline: Unreal Engine 5.6 Lumen architecture.

## Executive assessment

UzEngine has a substantial foundation for Lumen-inspired lighting, but the current implementation is not a replication of Lumen. Its strongest implemented path combines mesh distance fields, a global distance field, a world-space spherical-harmonic radiance cache, a surface diffuse gather, and SDF reflections blended with SSR. The central gaps are surface/material representation, complete lighting at secondary hits, adaptive final gathering, and a common lighting solution across diffuse, specular, and volumetric consumers.

The hardware DDGI path is less complete in lighting fidelity than its DXR infrastructure suggests: it intersects actual triangles, but shades those hits with fixed lighting, constant reflectance, and an invented normal. Brixelizer is integrated in source but explicitly disabled at initialization and pass registration.

No defensible percentage of “Lumen completion” follows from counting these modules. Several missing pieces are central architectural systems rather than polish.

## Scope and evidence limits

This is a static review of the current working tree, including existing uncommitted changes. No runtime or shader files were changed. No build, GPU capture, image comparison, performance benchmark, or test execution was performed. “Present” below means implementation and/or frame wiring were inspected; it does not certify visual correctness.

The configured reference locations `W:/UE_5.6_Source` and `E:/Falcor` were unavailable. Unreal behavior is therefore compared against Epic's versioned documentation and technical explanation, not a claimed local UE source audit. Reference subsystem names identify architectural counterparts, not locally verified source files. Rulebook tools were unavailable in this session; task coverage was inspected directly from repository files. Existing task checkmarks and historical performance claims are not treated as current validation.

## Reference baseline

Lumen couples a cached scene-surface representation with screen tracing and software or hardware scene tracing. Surface Cache cards retain material information and support amortized lighting updates. Software detail tracing can use individual mesh fields before the global field; hardware tracing can provide accurate triangle hits and reflection hit lighting. [Epic: Lumen Technical Details, UE 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine?application_version=5.6).

Lumen's final gathering shares work through screen-space radiance caching and world-space caching rather than independently tracing a large hemisphere at every shaded pixel. This is a major part of its quality/cost strategy. [Epic: Lumen technical blog](https://www.unrealengine.com/tech-blog/unreal-engine-5-goes-all-in-on-dynamic-global-illumination-with-lumen?lang=en-US).

Its feature target includes diffuse interreflection, emissive contribution, sky shadowing, reflections, and lower-quality indirect lighting for translucency and volumetric fog. These features have their own limitations; Lumen is not an unbiased ground-truth renderer. [Epic: Lumen Global Illumination and Reflections, UE 5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine?application_version=5.6).

## Current frame flow

`frame_lighting.cpp:153` registers GTAO/distance-field shadows, SDF GI, shared diffuse gathering, Brixelizer, SSR, then deferred lighting. The effective GI branches are:

```text
Mesh SDFs + scene transforms + average material reflectance
    -> camera-centered GDF clipmaps
    -> world SH probes: sky + shadowed sun + previous-probe feedback
    -> optional half-resolution surface hemisphere gather
    -> temporal/spatial filtering and reconstruction
    -> deferred diffuse lighting

GDF + world SH probes -> SDF reflection gathering -> blend with SSR

Optional TLAS -> DDGI irradiance/depth atlases -> deferred diffuse selection
                (currently simplified hit lighting)

Brixelizer integration -> disabled before execution
```

Deferred lighting contains source selection and replacement logic, not merely unconditional addition of every GI technique. For example, DDGI precedes radiance-cache sampling, while enabled world final gather replaces the diffuse irradiance later. The DXR launch script disables world final gather, avoiding that particular replacement.

## Feature matrix

| Area | UzEngine evidence | Assessment against Lumen |
|---|---|---|
| Direct lighting, shadows, environment | Light renderer, shadow subsystem, deferred lighting, IBL | Useful foundation; these alone do not establish Lumen parity |
| Mesh/global distance fields | `gi/global_distance_field.*`, mesh SDF baker, composite shader | Present; four dense clipmaps, with raw and expanded distances |
| Surface Cache/cards | Composite stores distance plus averaged diffuse reflectance | Major missing equivalent; no card generation/capture/lighting pipeline found in inspected runtime |
| World radiance caching | `gi/radiance_cache.*`, trace shader | Present; fixed 16³ L2 SH probe grid, temporal feedback and update budget |
| Final gather | `ssgi_world_gather.hlsli`, `sdf_diffuse_gather.hlsli` | Present but different: half-resolution surface rays, not Lumen's adaptive screen-probe solution |
| Screen/world trace integration | SSGI trace branch and deferred reflection blend | Partial: world diffuse mode returns before screen marching; reflections blend completed results |
| Diffuse multiple bounces | Previous SH snapshot sampled at SDF hits | Feedback exists; full material/light coverage and convergence parity unproven |
| Local-light/emissive indirect transport | Custom SDF hit shading uses sun, sky, SH feedback | Missing in inspected world hit-shading path; visible screen-space reuse is not off-screen transport |
| Hardware GI | DDGI resources, TLAS, RayQuery, irradiance/depth atlases | Infrastructure present; scene-correct hit lighting incomplete |
| Reflections | SDF GGX gathering plus SSR and IBL | Real off-screen path exists, but hit representation loses material detail; no equivalent high-quality hardware hit-lighting path established |
| Temporal reconstruction | SSGI accumulation, spatial denoise, upsample | Present; stability under moving lights/camera not measured |
| Fog/translucency GI | Fog ambient approximation; translucency IBL | No shared RC/DDGI integration found in inspected shaders |
| Brixelizer | Implementation and library bridge | Inactive in current renderer |
| Validation | GI resource/lifecycle and GPU tests | Useful infrastructure; insufficient evidence of image/temporal parity |

## Detailed findings and priorities

### G1 — P0: Preserve material and lighting information at secondary hits

**Evidence:** `runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl:41` samples the last texture mip at its center. `sdf_reflections.hlsli`, functions `ShadeSdfReflectionHit` and `SdfReflectionIrradiance`, uses that averaged reflectance with sun and SH irradiance.

**Consequence:** texture patterns, spatially varying emission, and detailed hit materials cannot survive this representation. More tracing samples cannot recover information that was discarded. This constrains both color bleeding and reflections.

**Lumen counterpart:** Surface Cache/material captures and scene lighting.

**Work:** implement a persistent surface representation retaining spatial material/emissive data, coverage and invalidation, then direct/indirect lighting updates. Cards are the closest architectural match to the stated replication goal. Reuse the existing GDF as an intersection representation, rather than asking its average color to replace surface data.

**Task coverage:** no dedicated equivalent found among searched active task files. Indoor-lighting and Brixelizer tasks cover adjacent work. **Complexity: very high.**

### G2 — P0: Complete lighting and material evaluation in DDGI

**Evidence:** `runtime/engine/renderer/gi/ddgi_system.cpp:125` supplies fixed sky `(0.6,0.7,0.9)`, fixed upward white light, and albedo `0.18`. `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl:174` uses `N = -dir`, forced-opaque ray queries, and returns a single sun term without a secondary shadow query or previous indirect radiance lookup.

**Consequence:** accurate geometry intersections do not imply scene-correct GI. Rotating the authored sun, changing a wall texture, or adding a local emitter is not faithfully represented by this hit shader.

**Lumen counterpart:** hardware trace hit lighting / surface-cache lighting lookup.

**Work:** bind actual scene lighting and environment; retrieve instance/primitive material data and interpolated attributes; evaluate visibility, emission, and indirect feedback. Inline ray queries can retrieve hit identifiers and barycentrics for buffer lookup; an SBT is not inherently required, despite the shader comment suggesting otherwise.

**Task coverage:** `phase0_indoor-lighting-system` covers DDGI, but completion notes overstate what the current shading implements; its tail remains unchecked. `phase1_fix-sponza-intel-lighting-parity` covers resource validity, not complete hit shading. **Complexity: high.**

### G3 — P1: Add scene-wide local-light and emissive transport

**Evidence:** SDF radiance trace lines 350–377 and `ShadeSdfReflectionHit` consume directional sun, sky and previous SH, without local-light enumeration or surface emissive lookup. Direct-light support elsewhere is not indirect support here.

**Lumen counterpart:** Lumen Scene Lighting and emissive final gathering.

**Work:** light the persistent surface data with relevant lights and emission, with visibility and invalidation when they change. Validate an off-screen colored light/emitter illuminating a visible wall.

**Task coverage:** indoor-lighting is related; no complete dedicated off-screen transport task found. **Complexity: high; depends on G1/G2.**

### G4 — P1: Implement shared, adaptive final gathering

**Evidence:** world mode in `ssgi_trace_cs.hlsl` calls `GatherSurfaceLighting` and returns before its screen-space ray loop. `sdf_diffuse_gather.hlsli` traces 1–32 rays per surface sample; `ssgi_renderer.cpp:61` establishes half-resolution dimensions.

**Lumen counterpart:** Screen Probe Gather and screen/world radiance caching.

**Work:** implement screen-probe placement/refinement, directional storage, reuse, disocclusion handling and interpolation; carry unresolved screen rays into scene tracing. The current world final gather and denoiser remain useful as a baseline for validation.

**Task coverage:** visual-parity roadmap mentions SSGI but does not establish this implementation. **Complexity: very high.**

### G5 — P1: Improve tracing detail and reflection fidelity

**Evidence:** `TraceSdfRay` samples global raw/expanded clipmaps. The inspected gather does not switch to individual mesh fields near the receiver. Deferred lighting lines 869–875 evaluates SDF reflections before blending SSR confidence.

**Lumen counterpart:** detail tracing, reflection tracing, and optional hardware hit lighting.

**Work:** add near-field detail tracing or accurate triangle fallback; shade secondary surfaces using G1/G2 data. Integrate screen-hit confidence before expensive fallback work where practical. Validate thin walls, glossy texture detail, off-screen objects and roughness transitions.

**Task coverage:** no dedicated complete reflection-parity task found in the searched GI task set. **Complexity: high.**

### G6 — P1: Replace brute-force quality scaling with bounded work

**Evidence:** radiance cache has 4096 probes, normally updates 1024 per frame, and refreshes all on history reset (`radiance_cache.cpp:454`). Grid movement marks history dirty. The maximum-quality launcher requests all 4096 probes, 64 rays each, 32 diffuse samples, 16 reflection samples, 256 reflection steps and 256³ GDFs.

Four GDF volumes each contain RGBA16F plus R16F: nominal texel storage is `4 × 256³ × 10 = 640 MiB`, excluding allocation overhead and all other resources. At 64³ it is 10 MiB. This is calculated storage, not measured resident memory. The nearest 50 m-wide clipmap has approximately 78 cm voxels at 64³ and 19.5 cm at 256³. A 100 m-wide 16-probe grid has approximately 6.67 m spacing; this is world-cache spacing, not final image resolution.

DDGI also repeats its ray loop per atlas texel for both irradiance and depth. With 4096 probes, 32 rays and 6² + 16² interior texels, the code describes about 38.3 million ray-query invocations per full update. This is an algorithmic count, not a GPU timing.

**Lumen counterpart:** amortized scene updates, adaptive caching, bounded tracing budgets.

**Work:** trace probe rays once and reuse them for atlas integration; preserve overlapping probes when scrolling; prioritize invalid/visible data; evaluate sparse GDF storage and measured quality tiers.

**Task coverage:** existing probe spreading and Brixelizer performance items partially cover this; they do not validate the current maximum preset. **Complexity: high.**

### G7 — P1: Reconcile implemented, disabled and validated status

**Evidence:** `frame_gi_passes.cpp:243` returns unconditionally from Brixelizer registration. Initialization is commented out in `scene_renderer_init.cpp:662`. Its task file nevertheless records successful past execution, with validation items 8.1–8.5 unchecked.

**Lumen counterpart:** an operational, validated integrated GI pipeline.

**Work:** classify Brixelizer as inactive until fixed and freshly verified. Decide its role explicitly: an alternative GI backend does not fill the custom renderer's missing Surface Cache by merely being present in the repository. Update status documents after runtime verification.

**Task coverage:** `phase0_brixelizer-gi-integration` directly applies. **Complexity: unknown until crash diagnosis.**

### G8 — P2: Share indirect lighting with fog and translucency

**Evidence:** `volumetric_fog_inject_cs.hlsl:114` uses an ambient approximation based on albedo. `translucent_ps.hlsl:221` samples environment irradiance; no world RC/DDGI consumption was found in these paths.

**Lumen counterpart:** translucency/fog indirect lighting.

**Work:** expose the world indirect solution to volumetric and translucent shading with suitable resolution and filtering.

**Task coverage:** no dedicated integration task found in the searched GI task files. **Complexity: medium to high, after opaque GI is stable.**

## Recommended implementation order

1. Establish a reproducible visual baseline and truthful runtime feature status (G7). Keep reference images, configuration and timings together.
2. Define a common secondary-hit material/lighting contract; fix DDGI's synthetic inputs (G2) and implement persistent surface data (G1).
3. Complete local-light, emissive and multi-bounce transport (G3), with controlled energy tests.
4. Add adaptive screen probes and integrated fallback tracing (G4), measuring against the existing surface gather.
5. Complete detailed reflections and near-field tracing (G5).
6. Optimize storage, scheduling and reuse using measurements (G6); apply the same indirect solution to fog/translucency (G8).

These are recommendations, not newly created Rulebook tasks. Preserve existing task sequence when turning this analysis into implementation work.

## Acceptance evidence still needed

- Cornell-style colored walls: indirect color and energy, with sun disabled.
- Closed room and thin walls: sky leakage and visibility under camera motion.
- Off-screen emitter and local light: persistent illumination independent of camera visibility.
- Moving light, moving occluder and camera cut: convergence time, stale lighting and ghosting.
- Glossy floor/mirror and roughness sweep: off-screen detail and SSR/world transitions.
- Matched Sponza scene: geometry, materials, light units, exposure and camera held constant across renderers.
- Per-pass GPU timings, memory and frame-time distributions for each actual preset.

Use pass dumps and diagnostics before visual bug fixes. When Falcor becomes available, apply the project's PSNR ≥35 dB gate under a documented image/color-space protocol; supplement it with regional and temporal checks. A full-frame score alone can hide local leaks and instability. The inspected GI tests primarily establish resource and data-layout behavior, not these visual properties.

## Conclusion

Preserve the existing distance fields, tracing helpers, world cache, denoising and renderer integration. The next decisive step toward the original Lumen goal is a coherent scene-surface lighting representation and complete secondary-hit shading, followed by adaptive gathering. Raising samples or enabling the current DXR mode cannot substitute for those systems.

---

## Dated correction, 2026-09-17: the DDGI section (G2) is out of date, and what was measured since

This section corrects the report above; **the original text is preserved as written**. The report was
produced against a different checkout and, for the DDGI path, describes code this repository no longer
has. Every claim below was re-checked against `E:\UzmiEngine\uzengine` at this date, and the items
marked **measured** come from the followup-01 instrumentation runs of the same day, not from reading.

### What G2 says that is no longer true of the main path

| G2's claim | State in this checkout |
|---|---|
| fixed sky `(0.6,0.7,0.9)` | the sky comes from the frame: `constants.skyColor{R,G,B} = pf->skyLightColor{R,G,B} * pf->skyLightIntensity`, `ddgi_system.cpp:202-204`. The fixed values survive only in the `else` branch taken when there is no per-frame data, where the sun is also zeroed (`:223-230`) |
| fixed upward white light | sun direction, colour and intensity come from the frame: `ddgi_system.cpp:210-216`. **Measured:** the writer's identity line logs `lightColor=(10, 9.8, 9.5) lightDir=(0.107, 0.549, 0.829) lightIntensity=1` — the same values the SDF cache logs |
| albedo `0.18` | `0.18` is the documented fallback for a hit outside every GDF cascade (`ddgi_system.cpp:163`); a hit inside one takes the field's reflectance. **Measured:** albedo luminance at DDGI hits has median 0.234, max 0.270 |
| `N = -dir` | only when the field gradient is degenerate: `if (dot(N, N) < 0.5) N = -dir;`, `ddgi_probe_update_cs.hlsl:281` |
| returns a single sun term **without a secondary shadow query** | there is a shadow ray: an inline `RayQuery` with `ACCEPT_FIRST_HIT_AND_END_SEARCH` toward the sun, `ddgi_probe_update_cs.hlsl:289-301` |
| no sky sampling | the escaping ray samples the prefiltered sky cubemap, mip 0, Z-up→Y-up swizzle, gated by `r.GI.DDGI.DirectionalSky` (default on), `ddgi_probe_update_cs.hlsl:239-247` |

### What G2 gets right, and is now quantified rather than inferred

* **There is still no general indirect-feedback term in the DDGI hit shading.** The hit returns
  `albedo * (sunRad / kPI)` (`ddgi_probe_update_cs.hlsl:325`) against the SDF cache's
  `albedo * (sunRad / kPI + indirect)` (`radiance_cache_trace_cs.hlsl:376`). The writer's identity
  line states it: `indirectTerm=none`. **Measured** at Camera C: on the probes where the cache has
  hits (n = 1622) its hit radiance is sun 0.0 + indirect 0.375 — that is, **entirely** the term the
  DDGI does not have. (The sun term is legitimately zero at that pose: the view is fully shadowed.)
* **Hitting a triangle and then fetching normal and reflectance from an approximate volume is not
  evaluating that triangle's material.** Unchanged, and it is the architectural point G1/G2 make.
* The reflectance in that volume is the **last mip of the albedo texture sampled at UV (0.5, 0.5)**,
  i.e. one colour per mesh (`global_sdf_composite_cs.hlsl:40-46`, `DiffuseReflectance`). More rays
  cannot recover what is averaged away before tracing starts.
* Emissive: there is a **specific** VFX emissive injection into the DDGI probes, gated by
  `r.VFX.DDGIInjection` (`ddgi_probe_update_cs.hlsl:495-512`). It is not general emissive or
  local-light transport, so G3 stands.

### A measured bound on how much G2's gap costs, at one pose

With identical ray directions and the whole probe grid traced in the same frame (the confounds that
made earlier comparisons unusable), the two writers' per-probe mean radiance agrees to about 1 %:
median ratio **0.9893** inside the building, **1.000** below the floor, and the sky-fraction difference
is one ray in 32. The missing indirect term is real but barely visible **there**, because the sky
carries 90.8 % of the cache's mean at that pose. Where it must matter is an interior dominated by
bounce light, and that is **not measured**. See `.rulebook/tasks/phase0_sponza-followup-01-*/notes.md`
(stage 3) and ledger rows H-51/H-52.

### Other items confirmed as written

* **Brixelizer is not an active feature** — stronger than "interrupted by a return": the pass is never
  constructed, its creation is commented out at `scene_renderer_init.cpp:670-673`, so every call site
  early-outs on a null pointer.
* **640 MiB is the nominal storage of the four clipmap pairs at 256³** (RGBA16F + R16F = 10 bytes per
  voxel, 16.78 M voxels per clipmap, four of them), and **256³ is the resolution the Sponza project
  actually sets** (`sample/projects/sponza.uzproject: "r.GI.GDF.Resolution": 256`), not only a maximum
  preset. It is not a measurement of resident memory.
* **Translucency does not consume the world GI.** The translucent shader lights from IBL, ambient and
  the sky-light constants; it reads no radiance-cache probe buffer, no DDGI atlas and no probe SH
  (`translucent_ps.hlsl`). G8 stands.

### Standing

The report's conclusions and priority order are unaffected: the two large gaps remain the surface
representation (G1) and shared, adaptive gathering (G4), and fixing the current flicker is a different
deliverable from building a Lumen-like architecture. **G2 should be read as "the hit shading lacks
material evaluation and indirect feedback", not as "the DDGI uses a fixed sky, a fixed sun and an
invented normal".** This correction is a static re-check plus measurements at one camera pose; no
benchmark and no visual-parity certification was run for it either.

### Three adjustments to the correction above (2026-09-18, review)

1. **"One colour per mesh" names the wrong unit.** The GDF composite builds one `MeshSDF` record per
   **draw command** — a mesh section with its own material (`frame_gi_passes.cpp:197-209`:
   `sdfCopy.albedoSrv = draw.albedoSrvIndex`, tint and flags likewise). A mesh split into sections
   with distinct materials therefore gets one averaged reflectance **per material record**, not one
   for the whole original mesh. The loss is the same in kind — the last-mip average discards all
   spatial detail inside each record — but its granularity is the draw, and "one colour per mesh"
   overstates it where meshes are sectioned.
2. **The surface representation is the architectural priority, not a prerequisite for every
   improvement.** G2 can move without a complete Surface Cache: an inline ray query already yields the
   hit instance, primitive and barycentrics, from which the triangle's own material and interpolated
   attributes can be fetched. The sentence "G2, G3 and G5 all depend on it" is withdrawn in favour of:
   they are all *limited* by it, and G2 has an independent step.
3. **"None of these is solved or caused by the Surface Cache's absence" is too categorical.** What the
   measurements support is narrower: the mechanisms measured (reconstruction at the door, H-45-48;
   probe temporal variance at 32 rays with per-frame rotation and EMA 0.12) do not require that absence
   to occur, and building the cache does not by itself correct them — but a change in the surface
   representation and in how samples are shared can change their magnitude. Replace the sentence with
   that.

The 0.9893 writer agreement remains what it is: one pose, one protocol (identical directions, whole
grid traced, sky-dominated view). It does not close parity in a bounce-dominated interior, which is a
separate measurement still to run.
