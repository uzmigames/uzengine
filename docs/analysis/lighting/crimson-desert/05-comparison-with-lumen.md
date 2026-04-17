# Crimson Desert vs Lumen — Side-by-Side

A detailed comparison of the two leading real-time lighting systems
shipped in 2025–2026. This is the matrix we cite everywhere else.

---

## 1 · Philosophy

### Lumen

> "Dynamic scalable GI and reflections for the majority of games."

Goals: work on SM6 HW without mandatory RT, ship on consoles at 60 fps,
let artists light scenes without bake times, integrate with Nanite.

### BlackSpace (Crimson Desert)

> "Unify lighting under a single physical-correctness model."

Goals: replace every GI/shadow/reflection technique with one path-
tracing dispatch, leverage neural denoising for the long pole,
mandate RT HW for max tier.

---

## 2 · Technique-by-technique

### 2.1 Primary visibility

| | Lumen | BlackSpace |
|---|---|---|
| Method | Rasterized G-Buffer | Rasterized G-Buffer |
| Nanite / virtual geometry | Yes (tight integration) | Cluster LOD (similar) |

Tie. Both pay for the raster G-Buffer once because it's ~2× faster than primary RT.

### 2.2 Direct lighting

| | Lumen | BlackSpace |
|---|---|---|
| Analytic lights | Per-light loop in deferred PS | ReSTIR DI candidate + 1 shadow ray |
| Shadow method | VSM (shadow map) | RT per pixel |
| Many-lights scaling | O(N) in shader | O(1) via reservoirs |
| Sun shadow | VSM | Ray-traced |
| Spot / point shadow | VSM local atlas | Ray-traced |
| PCSS soft shadows | Yes | Natural (RT stochastic penumbra) |

BlackSpace wins on quality (exact visibility, any light count). Lumen
wins on HW compatibility (runs anywhere).

### 2.3 Indirect diffuse

| | Lumen | BlackSpace |
|---|---|---|
| Primary gather | Screen probes @ 16×16 tiles, 32 rays | 1 GI ray per pixel |
| Short-range shading | Surface cache on mesh cards | Material evaluated inline |
| Mid-range shading | Radiance cache (world probes) | Material evaluated inline |
| Long-range shading | Voxel lighting clipmap | Material evaluated inline |
| Multi-bounce | Voxel re-injection (5+ bounces) | ReSTIR GI path reuse (3–8 bounces) |
| Denoiser | SVGF + TSR | DLSS 3.5 Ray Reconstruction |
| Frequency in distance | Low (probe interpolation) | High (per-pixel ray) |

BlackSpace's per-pixel approach avoids Lumen's probe-interpolation
blurriness at distance. Lumen's cache hierarchy wins on frame time
(the cache lookup is cheaper than a ray).

### 2.4 Reflections

| | Lumen | BlackSpace |
|---|---|---|
| Glossy source | Screen trace → radiance cache fallback | Stochastic ray + RR denoise |
| Rough source | Radiance cache (octahedral sample) | Still 1 ray, wider lobe |
| Roughness gate | ~0.6 switches to radiance cache | No gate (rays scale via VNDF) |
| Mirror surfaces | Screen trace + cache | 1 ray, sharp |
| Denoise | Parallax-corrected temporal + bilateral | RR (neural) |

Lumen reflections on rough surfaces have noticeable mismatch vs diffuse
because they share the radiance cache (same source as diffuse gather).
BlackSpace reflections are a separate dedicated ray — always correct.

### 2.5 Volumetric fog

| | Lumen | BlackSpace |
|---|---|---|
| Method | Froxel grid (Hillaire 2015) | Froxel grid + stochastic light sampling |
| Density sources | Exponential height + volume sources | Height + 3D density + SDF + particles |
| Light count | Up to ~8 common | Unlimited via reservoir sampling |
| Sun shadow | CSM tap / RTS | Ray-traced per froxel |
| Temporal | 4–8 frames | 8–16 frames |
| Wind response | Manual | Built-in Perlin advection |

BlackSpace wins on many-light scenes and wind realism. Similar otherwise.

### 2.6 Character lighting

| | Lumen | BlackSpace |
|---|---|---|
| Skin SSS | Separable (Jimenez 2015) | Separable or path-traced (hero) |
| Eye shader | Standard | Dedicated cornea + iris depth |
| Hair | Strand-based Marschner | Card-based 2-lobe Marschner |
| Cloth | Generic PBR + anisotropic material | LTC area lights + anisotropic LTC |

Lumen ships stronger strand-based hair. BlackSpace's hero cloth shines
under area lights. Tie overall; wins go to specific content types.

### 2.7 Volumetric clouds

| | Lumen | BlackSpace |
|---|---|---|
| Cloud representation | 3D noise + detail noise | 3D noise + detail noise |
| Ray-march samples | 32–64 | Similar |
| Sky integration | Aerial perspective volume | Same |

Tie. Both use Schneider-style approaches.

### 2.8 Denoise

| | Lumen | BlackSpace |
|---|---|---|
| Primary | SVGF-derived + TSR | DLSS 3.5 Ray Reconstruction |
| Fallback | (none) | Classic SVGF for non-RTX |
| Input requirement | Standard buffers | **Demodulated** diffuse + specular + hit distance + history |
| Output | Clean LDR at output resolution | Clean HDR at output resolution |

BlackSpace's RR is fundamentally higher quality but requires RTX. Lumen's
SVGF+TSR is universal but demonstrably softer.

### 2.9 Performance budget

At 4K output, 1440p internal, high settings (approximate — published benchmarks, may be stale):

| Pass | Lumen (RTX 3080) | BlackSpace (RTX 4080) |
|------|------------------|-----------------------|
| G-Buffer             | 2.0 ms | 2.0 ms |
| Shadows              | 1.2 ms (VSM) | 1.5 ms (RT shadows) |
| Direct lighting      | 0.8 ms | 2.1 ms (ReSTIR DI) |
| Surface cache / Indirect | 2.5 ms (surface update + voxel + probes) | 3.5 ms (ReSTIR GI) |
| Reflections          | 1.8 ms (HWRT) | 1.8 ms (stochastic) |
| Volumetric fog       | 1.0 ms | 1.4 ms |
| Denoise (incl. upscale) | 2.3 ms (SVGF + TSR) | 2.5 ms (DLSS 3.5 RR) |
| Post                  | 1.0 ms | 1.0 ms |
| **Total**             | **12.6 ms** | **15.8 ms** |

BlackSpace costs ~25% more but has higher quality. DF's CES 2026 claim
that BlackSpace costs "noticeably less than Lumen" is presumably
comparing RTX 4080 BlackSpace vs RTX 4080 Lumen (not 3080-vs-4080). At
equal hardware generation, BlackSpace is competitive.

### 2.10 Memory

| | Lumen | BlackSpace |
|---|---|---|
| Surface cache atlas | ~600 MB full | N/A |
| Voxel lighting | ~100 MB | N/A |
| Radiance cache | ~100 MB | N/A |
| ReSTIR reservoirs | N/A | ~300 MB (per pixel × 2) |
| BVH | ~100 MB (HWRT) | ~500 MB (full-scene BVH) |
| Total | ~900 MB | ~800 MB |

Similar memory footprints; BlackSpace saves the surface cache, spends
it on larger BVHs and reservoirs.

---

## 3 · Where Lumen wins

- **HW compatibility**: SM6 with SDF tracing works on 95% of modern PC GPUs.
- **Console consistency**: PS5 / XSX ship Lumen reliably at 60 fps.
- **Surface cache caching**: multi-bounce propagation is cheap.
- **Mature documentation**: extensive Epic-provided material.
- **Strand-based hair**: more detail up close.

## 4 · Where BlackSpace wins

- **Distant GI frequency**: per-pixel rays preserve high-frequency detail.
- **Many-lights scenes**: ReSTIR DI scales to hundreds of lights at O(1).
- **Denoise quality**: RR's neural denoise preserves GI high-frequency better than SVGF.
- **Disocclusion handling**: 1 spp with RR recovers faster than Lumen's probe system.
- **Volumetric lighting**: stochastic light sampling per froxel.
- **Per-surface reflection correctness**: dedicated reflection rays.

---

## 5 · Verdict for UzEngine's reference target

UzEngine cannot target BlackSpace's tier in the next 12 months. We
lack:
- A production-ready BVH / HWRT backend.
- ReSTIR DI+GI infrastructure.
- DLSS-RR integration.
- A path-traced reference to validate against at production quality.

UzEngine **can** target Lumen-SW tier in the next 6 months with the
plan in [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md).
That gets us Tier 1 in our planning matrix.

BlackSpace stays aspirational for Tier 3. We design the pipeline so
that Tier 3 is a phase 8+ upgrade, not a rewrite.

---

## 6 · References

- Everything from [00-blackspace-engine.md](./00-blackspace-engine.md), [../lumen/00-architecture.md](../lumen/00-architecture.md).
- Digital Foundry CES 2026 BlackSpace coverage.
- Tom Looman "Unreal Engine 5.6 Performance Highlights" (tomlooman.com/unreal-engine-5-6-performance-highlights/) — recent Lumen numbers.
- NVIDIA "Unreal Engine 5.4 Ray Tracing Guide" — HWRT Lumen numbers.
