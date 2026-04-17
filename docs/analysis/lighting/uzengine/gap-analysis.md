# UzEngine Lighting — Gap Analysis

**Scope**: Current state of UzEngine's lighting & shadow pipeline vs the AAA
reference (Lumen / BlackSpace / modern techniques). Explains *why* Sponza
looks bad, *what* is missing, *what* exists but misused, and *what order*
to fix things in.

**Audience**: Engine maintainers and the author of the new pipeline
(`aaa-default-pipeline.md`).

**Last updated**: 2026-04-17

---

## 1 · Current state inventory

### 1.1 Direct lighting

Source files:
- `runtime/engine/renderer/lighting/light_renderer.{cpp,h}` (273 lines)
- `runtime/engine/renderer/lighting/brdf_math.h`
- `runtime/engine/renderer/passes/lighting/lighting_pass.{cpp,h}`
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` (~1100 lines, monolithic)
- `runtime/shaders/runtime/lighting/light_types.hlsli`
- `runtime/shaders/runtime/lighting/contact_shadows.hlsli`
- `runtime/shaders/runtime/lighting/shadow.hlsli`
- `runtime/shaders/runtime/lighting/shadow_pcss.hlsli`
- `runtime/shaders/runtime/lighting/ibl.hlsli`

Pipeline: Classical deferred.

- G-Buffer lay-down (another pass).
- `DeferredLighting` pass: single fullscreen PS that reads G-Buffer and
  iterates through all analytic lights.
- IBL sampled from a single pre-filtered cubemap.
- DDGI sampled **optionally** if atlas is populated.
- SSGI sampled **optionally** if pass ran.
- Brixelizer (AMD SDF GI) sampled **optionally** if enabled.

Observation: **the lighting PS is a stacking of disjoint techniques**.
Each one adds to `totalRadiance` independently. There is no coordinated
MIS, no unified integration, no normalization across contributions. This
is the root cause of Sponza's "flat" look: each GI technique is weak on
its own, and their sum is less coherent than any one done well.

### 1.2 Shadows

Source files:
- `runtime/engine/renderer/shadows/shadow_renderer.{cpp,h}`
- `runtime/engine/renderer/shadows/shadow_cascade.{cpp,h}` — CSM
- `runtime/engine/renderer/shadows/shadow_atlas.{cpp,h}`
- `runtime/engine/renderer/shadows/shadow_cache.{cpp,h}`
- `runtime/engine/renderer/shadows/local_light_shadow_atlas.{cpp,h}`
- `runtime/engine/renderer/shadows/sdf_atlas_manager.{cpp,h}` + `sdf_types.h`
- `runtime/shaders/runtime/lighting/shadow_depth_vs.hlsl`
- `runtime/shaders/runtime/shadows/dfs_*.hlsl` — distance-field shadows

Pipeline:
- **Directional light**: 4-cascade CSM, each 2048² (67 MB).
- **Point/spot**: local-light shadow atlas with per-slot cubemap/2D pages.
- **Distance-field shadows** (softer, cheaper): half-implemented pipeline for sun shadows via SDF cone tracing.

Observation: CSM **works** but has the classical issues:
- Cascade-boundary seams visible on long shadows.
- Moderate peter-panning on Sponza's columns.
- No PCSS integration with local lights (only directional).
- Foliage / masked geometry support is weak (alpha-test pass missing).

### 1.3 Indirect lighting (diffuse GI)

Source files:
- `runtime/engine/renderer/gi/ddgi_system.{cpp,h}` (234 lines, stub-ish)
- `runtime/engine/renderer/gi/radiance_cache.{cpp,h}` (291 lines, stub)
- `runtime/engine/renderer/gi/global_distance_field.{cpp,h}`
- `runtime/engine/renderer/gi/mesh_sdf_baker.{cpp,h}`
- `runtime/engine/renderer/gi/irradiance_probe_grid.{cpp,h}`
- `runtime/engine/renderer/gi/ssgi_renderer.{cpp,h}`
- `runtime/engine/renderer/passes/gi/brixelizer_gi_pass.{cpp,h}` — AMD FFX Brixelizer
- `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`
- `runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl`
- `runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl`
- `runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl`
- `runtime/shaders/runtime/ssgi/ssgi_trace_cs.hlsl` + temporal + spatial denoise + upsample

Pipeline (actually running):
- **SSGI**: runs every frame. Hemispherical traces in screen-space
  depth buffer. Temporal-accumulated and spatially denoised.
- **DDGI**: partially wired — atlases exist, probes update, but the
  deferred PS samples only if the atlas is populated. **Often empty
  because the DDGI trace shader is gated behind a CVar that nobody turns
  on**. Confirmed by `_indirectAOExponent` etc. constants being
  conditional.
- **Brixelizer GI**: AMD FFX plug-in. Works when enabled, but not default.
- **Radiance cache**: stubbed; not wired to anything.
- **Global SDF**: compiled; used by the half-implemented DF shadows, not
  GI.

Observation: **We have four separate GI subsystems, none of them on by
default, none of them integrated into each other, and they all compete
for the same "indirect" output slot**. This is the single biggest reason
the renderer feels flat.

### 1.4 Indirect lighting (specular / reflections)

Source files in `runtime/engine/renderer/passes/` and SSR analysis docs
already moved to `docs/analysis/reflections/`.

Pipeline:
- **SSR**: deterministic screen-space ray march. Fallback to IBL cubemap
  when ray exits screen.
- No world-space fallback.
- No roughness-gated switchover (rough surfaces still try SSR and get
  mushy results).

Observation: SSR works but is basic. Needs stochastic GGX-VNDF sampling,
proper hit-distance output, and probe/sky fallback for off-screen misses.

### 1.5 Ambient occlusion

- `runtime/engine/renderer/ibl/` + GTAO pass
- GTAO (Jimenez 2016) — implemented, good.
- Applied only to the IBL indirect term — not to direct lights, not to
  analytic indirect.

Observation: GTAO is fine. Arguably applied too narrowly — it should
also modulate SSGI and DDGI indirect contributions for consistent
contact-shadow behavior.

### 1.6 Path tracing

- `runtime/engine/renderer/passes/pathtracing/` — exists, standalone mode.
- Reference-quality but not used as a GI input.

Observation: we have it. Great for ground truth validation but not on the
critical lighting path.

### 1.7 Volumetric fog

- `runtime/engine/renderer/fog/` — froxel fog.
- Single analytic light per froxel, short temporal history.
- No RT shadow.

Observation: works, needs extending (see `crimson-desert/03-volumetric-fog-god-rays.md` §5).

### 1.8 Atmospheric scattering

- `runtime/engine/renderer/atmosphere/` — Bruneton / Hillaire style.
- Seems solid (we have good sky LUTs).

Observation: reasonable baseline. Inscattering into volumetric fog is
likely hooked up correctly.

---

## 2 · Why Sponza looks flat — root cause

Sponza is a classical rendering-test scene with:
- **Strong sun** slanting through the atrium.
- **Bright white stone** that should bounce a lot of light.
- **Tall dark-side areas** (under the columns, behind the curtains) that
  should receive significant indirect light from the sun-lit walls.

On a correctly-lit Sponza, the shadowed areas should be **softly lit by
indirect light**, showing the color of the warm stone that bounced onto
them. This is the defining test of a GI pipeline.

On UzEngine's current Sponza, the shadowed areas are:
- **Too uniform**: ambient IBL fills them with a constant color.
- **No directional tint**: they don't receive a warm tint from the lit walls.
- **No contact shadows**: the indirect term does not darken near
  surface contacts (e.g. where a column meets the floor).

Each of these is traceable to a specific missing/misconfigured system:

| Symptom | Root cause |
|---------|------------|
| Uniform shadow color | IBL is the only indirect source; DDGI empty, SSGI weak in deep shadow areas. |
| No warm wall-bounce | DDGI probes not populated with direct lighting → probes are dark when sampled. |
| No contact darkening on indirect | GTAO applied only to IBL, not to indirect diffuse sum. |
| Hard shadow edges | PCSS light size too small for the sun size we've configured. |
| Flat material response | Single-source IBL (only one cubemap direction) — no spatial variation in indirect. |

Fixing Sponza == fixing the GI pipeline. Patching any single symptom with
a local tweak will be undone by the next scene.

---

## 3 · What to keep, what to rebuild

### 3.1 Keep

| Component | Keep reason |
|-----------|-------------|
| G-Buffer layout + material system | Sound; not the problem. |
| CSM shadow infrastructure | Upgrade path to VSM is incremental. |
| Local-light shadow atlas | Works; can extend to VSM pages. |
| Atmosphere / sky LUT | Producing good output. |
| GTAO | Keep as-is, apply more broadly. |
| Path tracer (separate mode) | Ground truth for validation. |
| IBL cubemap pipeline | Still needed for sky reflection. |
| SSR (screen-space) | As the "first try" for reflections. |
| SSGI (screen-space) | As the "close-range" diffuse indirect. |
| Volumetric fog | Extend (not rebuild). |
| Mesh SDF baker | Critical for SW RT path. |
| Global SDF clipmap | Critical for SW RT path. |

### 3.2 Rewrite

| Component | Rewrite reason |
|-----------|----------------|
| Deferred lighting PS (1100 LOC monolith) | Split into coherent passes with unified integration. |
| DDGI atlas path | Currently half-wired; finish it, make it the default distant-GI. |
| Radiance cache | Stubbed; replace with proper DDGI-style implementation. |
| Reflection pipeline | Add stochastic sampling, hit distance, world-space fallback. |
| Shadow map sampler | Extend PCSS to local lights; add contact shadows uniformly. |

### 3.3 Add

| Component | Add reason |
|-----------|------------|
| Light BVH + ReSTIR DI | Many-lights shadowing at fixed cost. |
| Proper denoiser (SVGF/NRD-style) | Current SSGI denoise is too simple. |
| HWRT tracing backend | Phase 2; gated behind CVar; reuses DDGI output as target. |
| Demodulated diffuse/specular output | Precondition for any good denoiser. |
| Hit distance output buffer | Precondition for kernel-size adaptation. |
| CSM → VSM upgrade | Major quality win. |

---

## 4 · Ranked priority list

Order of fixes to maximize Sponza-visible improvement:

### P0 — Must fix (Sponza-visible bugs)

1. **Enable DDGI by default** and verify probes populate with direct light.
2. **Wire DDGI into deferred lighting PS** as the primary indirect-diffuse source.
3. **Apply GTAO to DDGI output** (not just IBL).
4. **Calibrate PCSS** light size for the default sun.

Expected outcome: Sponza's shadowed areas get colored bounce light, no longer flat.

### P1 — Should fix (quality escalation)

5. **Demodulate diffuse output** for proper denoising.
6. **Upgrade SSGI denoiser** (SVGF-style à-trous).
7. **Stochastic SSR** with GGX VNDF; add hit distance; add world-space fallback via SDF.
8. **ReSTIR DI** for scenes with many lights (large dungeons, crowded forts).

Expected outcome: competitive with Lumen at the SW-RT tier.

### P2 — Nice to have

9. **CSM → VSM** conversion.
10. **HWRT backend** for RT-capable users.
11. **NRD integration** as drop-in denoiser.
12. **Volumetric fog** extensions (stochastic light sampling, wind, 3D density sources).
13. **NGX RR plug-in** as optional advanced denoiser.

Expected outcome: competitive with Lumen HWRT on matching HW.

### P3 — Research

14. **ReSTIR GI** for path reuse.
15. **Neural Radiance Cache** investigation.
16. **Radiance cascades** for 2D (if we ever ship a 2D tool).

---

## 5 · Specific bugs and easy wins

### 5.1 DDGI atlas is empty

Check: `runtime/engine/renderer/gi/ddgi_system.cpp` probe update loop.
Likely the update runs but no lights contribute. Trace:

1. Is the directional light reaching the `DirectLighting` function inside
   the probe update CS?
2. Is the skylight component sampling the correct sky LUT / cubemap?
3. Are probes positioned at valid world-space (not inside geometry)?

See `debug-tools` section of engineering docs for probe viz.

### 5.2 PCSS light size hardcoded

Grep `deferred_lighting_ps.hlsl` for literal numbers in the penumbra
computation. Replace with a CVar `r.Shadow.PCSS.LightSize` (default 0.01).

### 5.3 Indirect light intensity

`frame_constants.cpp:269` has an `indirectLightingIntensity` scalar. Verify
it's not 0.0 in the default project config. Sponza needs ~1.0.

### 5.4 SSGI clipping

SSGI currently multiplies indirect by a clip factor when rays exit screen.
This produces dark edges on Sponza's columns where SSGI cannot see past
the screen edge. Fallback to DDGI (once working) instead of black.

### 5.5 GTAO only on IBL

`deferred_lighting_ps.hlsl:849` applies GTAO only to the ambient block.
Apply `ao = pow(gtaoRaw, indirectAOExponent)` to the combined indirect
sum (DDGI + SSGI + IBL), not just IBL.

---

## 6 · Conclusion

We have most of the building blocks for a Lumen-tier renderer. They are
**disconnected**. The redesign is 70% wiring and only 30% new code.

The proposed design is in [aaa-default-pipeline.md](./aaa-default-pipeline.md).
Read that next.
