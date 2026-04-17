# Frostbite (DICE / EA) — Lighting Analysis

**Shipped titles**: Battlefield 1–2042, Star Wars Battlefront I/II, FIFA, Need for Speed, Dragon Age: The Veilguard (2024). **Distinctive identity**: Frostbite is the engine whose **published SIGGRAPH papers are the foundation that most other engines build on**. Lumen, BlackSpace, Decima, and UzEngine all reference Frostbite's PBR + volumetric rendering papers directly.

---

## 1 · Why Frostbite matters even if you never ship a Frostbite game

The Frostbite team's public output is disproportionately influential:

- **Moving Frostbite to PBR** (Lagarde & de Rousiers, SIGGRAPH 2014) — the canonical PBR integration guide, still referenced in 2026.
- **Physically Based and Unified Volumetric Rendering** (Hillaire, SIGGRAPH 2015) — the froxel-based volumetric fog that every engine now uses.
- **A Scalable and Production Ready Sky and Atmosphere Rendering Technique** (Hillaire, 2020) — current sky implementations (UE5, UzEngine, many others).
- **Rendering the Alien Worlds of Anthem** (2018) — atmospheric effects.

When you read a BlackSpace or Lumen doc and see "Frostbite-style" — that's because these three papers define the modern defaults.

---

## 2 · Lighting pipeline (public-reference)

```text
G-Buffer
   ▼
Shadow maps (CSM + local shadow atlas + cached shadows for static)
   ▼
Deferred lighting
   ├── Analytic lights with PBR BRDF (GGX, Schlick fresnel, SmithG)
   ├── Area lights via LTC (Frostbite pioneered production use)
   ├── Photometric lights (IES profiles)
   └── Emissive surfaces as punctual-light fallbacks
   ▼
SSR (since BF1) + reflection probes + IBL (cubemap)
   ▼
SSGI (late-gen additions)
   ▼
Froxel volumetric fog
   ▼
Sky atmosphere (Bruneton/Hillaire)
   ▼
TAA
   ▼
Post + tonemap + grading
```

Frostbite has historically been **baked-dominant** for diffuse GI (like
Decima), with incremental additions of screen-space dynamic GI over
generations. As of Dragon Age: Veilguard (2024), Frostbite added a
DDGI-style probe volume system for dynamic indirect.

---

## 3 · Distinctive techniques

### 3.1 LTC area lights (production-first)

Frostbite was the first engine to ship **LTC area lights** in a shipping
AAA game (Battlefront 2017). The Heitz/Hill LTC paper came out SIGGRAPH
2016; Frostbite integrated and productionized it within a year.

See [../modern-techniques/area-lights-ltc.md](../modern-techniques/area-lights-ltc.md)
for the full technique. Frostbite specifics:

- Sphere, disk, rectangle, **tube** light shapes.
- GGX specular response via LTC transformation tables.
- Diffuse via Lambert integration over analytic shape.
- Works for arbitrarily sized / oriented lights (billboards, tubes).

Extensive shipping use in Battlefield maps: long fluorescent tube
lights in the Operation Metro atrium, disc-shaped windows in Star Wars
interiors.

### 3.2 Froxel volumetric fog (ubiquitous now)

Hillaire's 2015 froxel approach became the industry standard:

- 3D view-space frustum voxels (~160×90×64 typical).
- Per froxel: σ_t extinction + σ_s inscattering.
- Stochastic sampling for sun + N lights.
- Temporal accumulation with camera motion jitter.
- Front-to-back slice integration along view ray.

UE5 Lumen, Decima, RE Engine, CryEngine, Unity HDRP — all run variants
of this pipeline. Our UzEngine fog pipeline is a direct descendant.

### 3.3 Physically based sky

Hillaire 2020 "A Scalable and Production Ready Sky" replaces the older
Bruneton 2008 approach with a precomputed LUT-driven renderer:

- **Transmittance LUT** (8-bit, 32×32) — sun transmittance through atmosphere.
- **Multiple scattering LUT** (16-bit, 32×32) — multiple scattering contribution.
- **Sky-view LUT** (HDR, 192×108) — sky color at all pixel positions.
- **Aerial perspective volume** (HDR, 3D, 32×32×32) — scattering over distance for fog integration.

Per-frame cost: ~0.5 ms. Handles altitude, time-of-day, sun position,
atmospheric conditions.

UzEngine's atmosphere pipeline (`runtime/engine/renderer/atmosphere/`) is
based directly on this. We should verify all 4 LUTs are present and
correctly wired.

### 3.4 Photometric lights (IES)

Frostbite has long supported **IES profiles** — photometrically-measured
real-world light distributions. Characterized by angular intensity
distribution tables from real fixtures (lamp manufacturers).

Implementation: single 1D LUT per IES profile, indexed by angle between
light direction and sample direction. Adds ~0.05 ms at 1080p for many
lights.

UzEngine's lights are currently anisotropic-pass-through (spotlight
cone only). Adding IES is a 1-week task with big quality win for
architectural scenes (spots with real-world lamp profiles look much
more convincing).

### 3.5 Cached shadow maps for static scenes

Frostbite caches shadow maps for static objects across frames, only
re-rendering when a dynamic object moves within shadow range. Reduces
shadow pass cost by ~40% in typical scenes.

---

## 4 · Comparison matrix

| Axis | Lumen | BlackSpace | **Frostbite (Veilguard-era)** |
|---|---|---|---|
| Direct lighting | Analytic + VSM | Analytic + RT shadow | Analytic + CSM + LTC area lights |
| Diffuse indirect | Lumen | Per-pixel RT | DDGI-style probes (new) + baked fallback |
| Specular indirect | Lumen | Per-pixel RT | SSR + reflection probes |
| Volumetric | Froxel (Hillaire 2015) | Froxel extensions | **Hillaire 2015 (the original)** |
| Sky | Runtime atmosphere | Runtime atmosphere | **Hillaire 2020 (the origin)** |
| PBR | Karis 2013 | Karis 2013 | **Lagarde 2014 (the reference)** |
| Philosophy | Dynamic | Full physical | **Baked + additive dynamic, methodical** |

---

## 5 · UzEngine takeaways

1. **Add LTC area lights**. Tube lights, rectangles, discs — all the
   light shapes Sponza's corridors could benefit from are cheap to add
   via LTC. The tables (amp, matrix coefficients) are published and
   free to use.

2. **Add IES profiles**. Real-world light fixtures have IES data freely
   available from manufacturers. Big quality win for architectural
   scenes (Sponza has no IES, which is why all spot lights look
   fake-uniform).

3. **Verify sky atmosphere is Hillaire 2020 quality**. Check that we
   have: transmittance LUT, multiple scattering LUT, sky-view LUT,
   aerial perspective volume. Any missing = degraded sky.

4. **Cached shadow maps**. Implementation-wise simpler than VSM but
   captures ~80% of the performance benefit. Worth considering as an
   interim step before VSM.

5. **Read the Frostbite papers, not their wrapper engines**. Every time
   UzEngine borrows from UE5 or BlackSpace, the underlying papers are
   often Frostbite's. Going to source material gives us the correct
   parameters and formulas without UE5-specific wrapping.

### 5.1 Concrete action: LTC integration path

Phase 1 of LTC integration:
1. Download Heitz LTC tables (free, published, no IP issues).
2. Add CRectLight / CDiscLight / CTubeLight ECS components.
3. New shader permutation for area light evaluation.
4. Integrate into deferred lighting pass.

~2 weeks. Adds 3–5 new light types usable throughout the engine. Big
visual upgrade for scenes with any architectural lighting.

---

## 6 · References

- Sébastien Lagarde & Charles de Rousiers, "Moving Frostbite to Physically Based Rendering 3.0" (SIGGRAPH 2014). seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
- Sébastien Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite" (SIGGRAPH 2015).
- Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (2020). sebh.github.io/publications/egsr2020.pdf
- Heitz, Dupuy, Hill, Neubelt, "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines" (SIGGRAPH 2016). eheitzresearch.wordpress.com/415-2/
- Hill & Heitz, "Real-Time Line- and Disk-Light Shading with Linearly Transformed Cosines" (SIGGRAPH 2017 course). blog.selfshadow.com/publications/s2017-shading-course/heitz/s2017_pbs_ltc_lines_disks.pdf
- Mikhail Drobot, "Physically Based Area Lights" (GPU Pro 5 chapter).
- advances.realtimerendering.com/s2016/s2016_ltc_rnd.pdf — "LTC: A Journey from Research to Production" (integration retrospective).
