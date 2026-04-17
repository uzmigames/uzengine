# Decima (Guerrilla + Kojima Productions) — Lighting Analysis

**Shipped titles**: Killzone: Shadow Fall (2013), Horizon Zero Dawn (2017), Death Stranding (2019), Horizon Forbidden West (2022), Horizon Forbidden West: Burning Shores (2023), Death Stranding 2: On the Beach (2025).

**Distinctive identity**: Photoreal outdoor open-world rendering with stunning atmospheric weather, cinematic character lighting, and **baked-dominant GI that survived the transition into 2025 with surgical RT additions**. Unlike UE5 (Lumen, fully dynamic) or BlackSpace (full path-tracing), Decima is the modern master of the hybrid baked + real-time approach.

---

## 1 · Lighting pipeline overview

Decima's pipeline (accumulated from SIGGRAPH 2015–2024 talks and Digital
Foundry technical reviews):

```text
Precomputed (offline):
  1. Irradiance volumes (light probes) baked per-cell in the world
  2. Sky occlusion maps (per-level skylight attenuation)
  3. Reflection probes (captured at design-time placements)
  4. Static shadow maps for distant terrain

Runtime (per frame):
  1. Early-Z prepass + motion vectors
  2. G-Buffer (with their famous "material blending" layer)
  3. Shadow maps:
     - Directional: CSM 4 cascades + distant static shadow contribution
     - Local: 512² atlas pages, allocated on visibility
  4. Deferred lighting:
     - Analytic direct + CSM
     - Probe lookup for diffuse indirect (baked irradiance volumes)
     - Reflection probe hierarchy (nearest-N blend) for specular indirect
     - Screen-space contact shadows for detail
  5. SSR (classical) — fallback to reflection probes
  6. SSGI (as of Horizon Forbidden West 2022, mixed with probes)
  7. Volumetric fog (froxel) with custom aerial perspective
  8. Dynamic sky (atmospheric scattering)
  9. TAA / TAA+ (Guerrilla's "Checkerboard Rendering" ancestor)
  10. Post: bloom, tonemap, grading

Death Stranding 2 additions (2025, RT tier):
  11. RT reflections (replaces/augments SSR)
  12. RT shadows (optional for hero lighting)
  13. RT AO (optional)
  14. DLSS 4.5 / Ray Reconstruction integration
```

### 1.1 The famous "hybrid" approach

Death Stranding 2 is the first Decima game with PC ray tracing, but
Guerrilla chose to **not** go full Lumen-style real-time GI. Instead:

- **Diffuse GI remains baked** (irradiance volumes) — fast, stable, artistically tuned.
- **Only specific problem channels get RT**: reflections (where SSR is worst) + optional shadows + optional AO.

Digital Foundry's review praised this specifically: RT reflections fixed
the screen-space artifacts of the original Death Stranding 1 without the
frame-time cost of a full Lumen-tier system.

---

## 2 · Distinctive techniques

### 2.1 Irradiance volumes with sky occlusion

The headline technique from the 2015 Guerrilla SIGGRAPH talk "Decima
Engine: Advances in Lighting and AA": **precomputed irradiance volumes**
with **per-probe sky occlusion masks**.

- 3D grid of probes around the world (sparse, snapped to geometry).
- Each probe stores SH3 irradiance from the static lighting.
- Each probe **also** stores a per-probe **sky occlusion value** (how much of the sky is visible from this probe).
- At runtime, the sky lighting contribution (which changes per weather / time-of-day) is **applied per-probe** by multiplying the current sky lighting by the baked sky occlusion.

This lets them **dynamically change sky color / sun direction** while
the heavy GI bake stays valid. The time-of-day system on Horizon and
Death Stranding relies on this trick.

### 2.2 Dynamic weather volumetrics

Horizon and Death Stranding 2 are famous for **dramatic weather systems**:
- Timefall (Death Stranding): rain with temporal decay affecting material appearance.
- Sandstorms (Horizon): dense particulate occlusion of sun lighting.
- Fog: extremely realistic, with height-based density + particulate noise.

Implementation uses a froxel volumetric system (similar to Frostbite
2020) with **extended tricks**:
- Multi-resolution froxel (fine near camera, coarse at range).
- Stochastic light scattering per froxel (similar to ReSTIR's spirit, pre-ReSTIR).
- **Aerial perspective bake** combined with runtime sky state.

### 2.3 "Hero character" lighting

Death Stranding 2's character faces are bordering on offline-quality. The
pipeline includes:

- **Sub-surface scattering** for skin (separable SSS per Jimenez 2015).
- **Eye-specific shading** (caustics inside the cornea, iris plane).
- **Detail normal maps** driven by animation for micro-expressions.
- **Hero-only probe ray tracing** — certain key characters have their
  diffuse indirect computed by tracing a few rays against the full scene
  BVH, not sampling the probe grid. Expensive but only for 1–2 characters.

### 2.4 Adaptive probe placement

Unlike DDGI's uniform grid, Decima's probes are placed **at authoring
time** with artist guidance, heavily concentrated at:
- Room thresholds (doorways)
- Floor/wall creases
- Under overhangs

This produces dense probe coverage where GI changes fast, sparse where
it's stable (open plains).

---

## 3 · Reflection probe hierarchy

One of the under-appreciated Decima features. Multiple reflection probes
per area:

- Far probe (environment-scale).
- Mid probe (building-scale).
- Near probe (room-scale).

At runtime, the shader blends the nearest 2–3 by BRDF distance and
roughness. This gives:

- Smooth transitions as character moves through spaces.
- Correct box-projection for interiors.
- No harsh cut when entering/exiting rooms.

UE5 has reflection captures with similar hierarchical logic but less
aggressive; Decima's version is denser and more artistically tuned.

---

## 4 · Death Stranding 2 RT specifics

From Digital Foundry's DS2 review and optimization guides:

- **RT Reflections**: 1 spp, denoised via custom-built temporal + spatial filter. DLSS 4.5 Ray Reconstruction optional.
- **RT Shadows**: sun only. One ray per pixel, cone-sampled for penumbra. Denoised.
- **RT AO**: one ray per pixel. Provides tight contact occlusion that the baked SSAO misses.
- **Diffuse GI remains baked** — they did NOT add RTGI. Decima's baked irradiance volumes are that good.

### 4.1 Why diffuse GI wasn't RTGI'd

From public statements: the baked GI is **artistically** tuned to reduce
banding and noise. Going to RTGI would require rebalancing **every**
lit scene in the game. Not worth the perceptual payoff when baking
produces results this clean.

---

## 5 · Published material

- **Guerrilla Games, "Decima Engine: Advances in Lighting and AA"** — SIGGRAPH 2017 course. [guerrilla-games.com/read/decima-engine-advances-in-lighting-and-aa](https://www.guerrilla-games.com/read/decima-engine-advances-in-lighting-and-aa) — *the* reference for the irradiance volume + sky occlusion approach.
- **Guerrilla Games, SIGGRAPH 2014** — "Killzone: Shadow Fall Demo Post-Processing" (more about AA but covers the pipeline architecture).
- **Guerrilla Games, SIGGRAPH 2020** — "Volumetric Sky for Horizon Zero Dawn's Photo Mode".
- **Automaton Media interview with Kojima Productions CTO** — automaton-media.com/en/interviews/we-ask-kojima-productions-why-theyve-stuck-to-decima-for-nearly-ten-years
- **Digital Foundry, "Death Stranding 2 tech review"** — digitalfoundry.net/articles/digitalfoundry-2025-death-stranding-2-tech-review
- **Various GDC talks by Kojima Productions** — share modifications back to Guerrilla.

---

## 6 · Comparison matrix

| Axis | Lumen | BlackSpace | **Decima** |
|---|---|---|---|
| Direct lighting | Analytic + VSM | Analytic + RT shadows | Analytic + CSM + RT shadow optional |
| Diffuse indirect | Probe cache (runtime) | Per-pixel RT | **Baked irradiance volumes + sky occlusion** |
| Specular indirect | Cache + screen space | Per-pixel RT | Probe hierarchy + RT reflections (DS2+) |
| Dynamic lighting response | Full | Full | Sun/sky/weather only; bulk lighting stays baked |
| HW floor | SM6 | RT-capable | SM6 (RT optional) |
| Philosophy | All dynamic | All physical | **Art-directed hybrid** |

---

## 7 · UzEngine takeaways

Decima is the engine whose **philosophy** is closest to what UzEngine
should ship first:

1. **Accept baking as an option.** Our renderer can win big with an
   offline-baked irradiance volume for static scenes (Sponza, test
   levels), plus dynamic DDGI for gameplay scenes. Decima proves you
   don't need full real-time GI to look AAA.

2. **Sky occlusion masks per probe.** Store a single scalar per probe
   (sky visibility 0..1) so that runtime sky color changes propagate
   correctly. Trivial addition with big quality win.

3. **Reflection probe hierarchy.** We have a single IBL cubemap; adding
   multiple probes with blending would fix many Sponza reflection issues
   (different probes for atrium, side halls, ceiling).

4. **RT only what SSR can't do.** Add RT reflections as the specific
   fallback for when SSR fails, not as a replacement. Cheaper, simpler
   to ship.

5. **Hybrid mindset.** Our new default pipeline (in
   [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md))
   should have an "offline bake" option for static GI in addition to
   the runtime DDGI. Decima's success validates this.

### 7.1 Concrete action: Sky occlusion per DDGI probe

Our existing DDGI probes store irradiance + visibility octahedrals. Add
one scalar per probe: **sky occlusion**. Cost: negligible memory. Gain:
runtime sky-color changes propagate into bounced indirect without
needing a full probe relight.

---

## 8 · References

- Kotarov, "Decima Engine: Advances in Lighting and AA" (SIGGRAPH 2017). guerrilla-games.com/read/decima-engine-advances-in-lighting-and-aa
- Wikipedia: Decima (game engine) — en.wikipedia.org/wiki/Decima_(game_engine)
- Digital Foundry, "Death Stranding 2 tech review" (2025).
- Optimized Gaming forum thread "Death Stranding 2 Performance Optimization Guide" — cost-per-feature breakdown.
- DSOGaming, "Death Stranding 2 is the first Decima game with Ray Tracing on PC" (2025).
- PCGamer interview, "13-year-old Decima engine allows Death Stranding team to accomplish many things that would be difficult to achieve elsewhere".
