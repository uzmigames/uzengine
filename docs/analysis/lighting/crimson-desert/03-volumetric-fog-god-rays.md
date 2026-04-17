# Crimson Desert · Volumetric Fog & God Rays

Pearl Abyss's volumetric solution is one of the quietly impressive features
of Crimson Desert. The fog is **heterogeneous** (density varies spatially),
reacts to **wind in real time**, and every analytic light in the scene casts
**volumetric shadows** through it. This goes beyond the Frostbite 2018
froxel-based approach used by Lumen / UE5.

---

## 1 · What they render

Observed behavior in the gameplay trailers and CES 2026 demo:

- **Ground fog** at dawn/dusk pooled into low-lying areas, reacting to wind.
- **God rays** (crepuscular rays) through ruined architecture.
- **Local light scattering**: torches light up fog in a visible cone.
- **Particulate inscattering** from smoke/dust, mixing with ambient fog.
- **Density falloff with height** (exponential).
- **No visible froxel aliasing** when moving the camera — the temporal
  accumulation is doing significant work.

---

## 2 · Pipeline

Based on Hillaire 2018 ("Physically Based Sky, Atmosphere and Cloud
Rendering in Frostbite") + extensions needed for heterogeneous density +
light-BVH sampling:

```text
 ┌── Density field ──┐     ┌── Wind field ──┐   ┌── Light list ──┐
 │ 3D texture or     │     │ Perlin noise   │   │ BVH of all     │
 │ exp height + SDF  │     │ advected by    │   │ analytic +     │
 │ clouds + particle │     │ camera vel     │   │ emissive       │
 │ sources           │     │                │   │                │
 └────────┬──────────┘     └────────┬───────┘   └────────┬───────┘
          │                         │                    │
          └──────────────┬──────────┴──────────┬─────────┘
                         │                     │
                         ▼                     ▼
              ┌─────────────────────┐    ┌────────────────┐
              │  Scattering froxel  │    │  Shadow froxel │
              │  160×90×64          │    │  160×90×64     │
              │  (σ_s, σ_t)         │    │  (visibility   │
              │                     │    │   per light)   │
              └──────────┬──────────┘    └────────┬───────┘
                         │                        │
                         └───────────┬────────────┘
                                     │
                                     ▼
                      ┌─────────────────────────────┐
                      │  In-scattering froxel       │
                      │  for each voxel:            │
                      │    Li = Σ lights:           │
                      │        shadow * phase * σ_s │
                      │      + sky LUT sample * σ_s │
                      └───────────────┬─────────────┘
                                      │
                                      ▼
                      ┌─────────────────────────────┐
                      │  Temporal accumulation      │
                      │  8-frame history, jittered  │
                      │  in W (depth) axis          │
                      └───────────────┬─────────────┘
                                      │
                                      ▼
                      ┌─────────────────────────────┐
                      │  Integration along ray      │
                      │  (front-to-back slice merge)│
                      └───────────────┬─────────────┘
                                      │
                                      ▼
                      composited volumetric output
```

### 2.1 Froxel grid

Standard 3D view-space frustum voxels. Typical resolution 160×90×64 at 1080p
internal. Near plane is tight, far plane extends to ~2 km (Crimson Desert
has long draw distances). **Exponential depth distribution** along the W axis
(near slices are much thinner) so that near-camera fog is sampled at high
fidelity.

### 2.2 Density sources

Heterogeneous density is the sum of:

- **Exponential height fog** (baseline).
- **3D density texture** (baked for scene-fixed fog like dungeon mist).
- **Particle emitters** (smoke/dust injected into the froxel in a separate pass).
- **SDF-driven volumes** (e.g., fog inside a room bounded by an SDF).

### 2.3 Wind advection

A single Perlin-noise 3D texture is advected by the camera-relative wind
vector per frame. This gives the "fog blowing" look. Cheap and convincing.

### 2.4 Shadow froxels

This is where BlackSpace differs from Frostbite/Lumen. Instead of shadowing
fog only against the sun's CSM, they sample **every analytic light** per
froxel. For N lights and V voxels this is N×V visibility queries — way too
many.

**Solution**: stochastic sampling. Per froxel, pick ONE light (importance-
sampled by contribution estimate) and test visibility. Accumulate temporally
across 8–16 frames. This is exactly ReSTIR DI applied to froxels instead of
pixels.

### 2.5 Inscattering evaluation

Per froxel:
```
L_in(voxel) = σ_s(voxel) × ΣL[vis(voxel, light) × phase(v, L) × light_color / r²]
            + σ_s(voxel) × sky_LUT_sample(voxel)
            + σ_e(voxel)   [emissive fog]
```
where `phase(v, L)` is the Henyey-Greenstein phase function with
`g ≈ 0.7` for dust, `g ≈ 0.0–0.3` for water vapor.

### 2.6 Temporal accumulation

Froxels are jittered along the W (depth) axis every frame by a small random
amount. Over 8 frames this samples the full depth axis without artifacts.
The result is averaged against history, weighted by a confidence that drops
when the camera moves rapidly.

### 2.7 Integration

The final volumetric radiance is the slice-by-slice front-to-back
integration of scattering × transmittance. Standard; every engine does this
the same way.

---

## 3 · God rays (crepuscular rays)

God rays are the visual effect of volumetric in-scattering where direct
sunlight passes through a mostly transparent medium. They emerge naturally
from the pipeline above **as long as**:

1. The sun is sampled per-froxel as a high-intensity light.
2. Shadow-froxel visibility is correctly evaluated (CSM tap or RT).
3. Phase function has a pronounced forward lobe (`g ≈ 0.6–0.8`) so that
   forward-scattering dominates.
4. Density is non-trivial in the affected volume.

Crimson Desert god rays look cleaner than most engines because:

- **High sun sample rate**: the sun is always in the reservoir, no temporal starvation.
- **Ray-traced sun visibility** per froxel (instead of CSM tap): no acne, no CSM boundary artifacts.
- **16-frame temporal accumulation** on stationary camera makes the rays pin-sharp.

---

## 4 · Comparison with Lumen / UE5 volumetric

| Feature | UE5 VolFog (Frostbite-style) | BlackSpace |
|---------|------------------------------|------------|
| Height fog | Yes | Yes |
| Heterogeneous density | Limited (requires Volumetric Cloud or particle system to inject) | First-class 3D density field |
| Many-light scattering | Limited (N ≤ 8 typical in production) | Stochastic (ReSTIR-ish), unlimited |
| Sun shadow | CSM tap or RTS | RT per froxel |
| Wind advection | Manual (material nodes) | Built-in |
| Temporal | Yes | Yes, deeper history |

UE5's volumetric fog is already very good — Crimson Desert's advantage is
the stochastic light-sampling extension and the RT sun shadow. These are
implementable incrementally on top of a Frostbite-style base.

---

## 5 · What UzEngine has today

From `runtime/engine/renderer/fog/` and `runtime/shaders/runtime/atmosphere/`:

- We have a froxel-based volumetric fog pass.
- Density is currently exponential-height only — no heterogeneous sources.
- Sun shadow is a single CSM tap per froxel.
- No per-froxel local-light contribution (only one global analytic light).
- Temporal accumulation exists but is 4-frame shallow.

**Gap list** to reach Crimson Desert level:

1. Add 3D density texture source (scene-authored fog volumes).
2. Add per-froxel wind advection of a Perlin noise cloud.
3. Add local-light stochastic sampling per froxel (reservoir).
4. Extend temporal history to 16 frames.
5. Add RT sun-visibility fallback when HWRT is enabled.

All five are incremental on our current pass. See [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md) §6.

---

## 6 · References

- Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (Frostbite, 2020).
- Bart Wronski, "Volumetric Fog" (SIGGRAPH 2014) — the original froxel approach.
- Jan Novák, Iliyan Georgiev, Johannes Hanika, Wojciech Jarosz, "Monte Carlo methods for volumetric light transport simulation" (SIGGRAPH 2018 course).
- Nikolas Stefanov, "Volumetric clouds and fog in Red Dead Redemption 2" (SIGGRAPH 2019).
- Bitterli et al., "Spatiotemporal reservoir resampling" (SIGGRAPH 2020) — reservoir approach used per-froxel for many-light scattering.
- Crimson Desert Pearl Abyss dev notice on fog/god-ray system.
