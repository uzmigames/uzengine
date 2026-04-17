# Lumen · Screen Probe Gather

The outermost layer of Lumen's cache hierarchy. Screen probes are the
**sampling kernel** that drives the whole system: they decide what rays to
shoot, what cache layers to read, and they produce the final per-pixel
hemispherical radiance that the shading pass consumes.

---

## 1 · Why screen probes and not per-pixel rays?

Per-pixel ray tracing (the BlackSpace approach) costs 1 ray × 2M pixels =
2M rays/frame at 1080p. Even with RT cores that is a lot, and the variance
per pixel is high (you need many samples to converge).

Screen probes amortize: one probe per 16×16 tile = ~8K probes at 1080p,
each shooting 32 rays = 256K rays/frame. **10× fewer total rays**, with
higher per-probe sample count (32 vs 1) for better convergence.

The compromise: per-pixel spatial frequency is limited to ~16 px. Lumen
hides this with a clever **adaptive placement** scheme and per-pixel
bilinear interpolation between probes.

---

## 2 · Placement

### 2.1 Base grid

One probe per 16×16 pixel tile. At 1080p: 120 × 68 = 8160 base probes.
At 1440p: 160 × 90 = 14400. At 4K: 240 × 135 = 32400.

### 2.2 Adaptive subdivision

If lighting variance within a tile is high (detected from the previous
frame), extra probes are inserted. Variance is estimated as luminance
moments of previous-frame color — if a tile has high variance in the
radiance signal, add more probes.

In practice: ~20% of tiles get 2–4 extra probes. Total probe count ~1.2×
the base grid.

### 2.3 Adaptive positioning

Each probe's "anchor pixel" within its tile is chosen to land on a pixel
that is **representative** of the tile's depth and normal. The algorithm:

1. Pick the central pixel of the tile as a candidate.
2. If that pixel's depth differs from the tile's median depth by >10%, try another pixel.
3. Iterate up to 4 times; if no convergence, the tile gets a "split probe" pair.

This avoids probes anchored on edge pixels that would produce misleading
gather results for the rest of the tile.

---

## 3 · Ray direction distribution

Each probe shoots 32 rays in a **cosine-weighted hemisphere** aligned to
the probe's surface normal (G-Buffer normal at the anchor pixel).

The 32 directions are **blue-noise-distributed**:

- A precomputed 8×8×8 blue-noise texture provides the base distribution.
- Per frame, a random rotation is applied (temporal decorrelation).
- Stratification guarantees even hemisphere coverage.

Blue noise is crucial: uniform random produces clumping, stratified misses
high-frequency detail. Blue noise is the Goldilocks distribution for 1 spp
per direction.

---

## 4 · Ray tracing

Each probe's 32 rays trace the scene using the current tracing mode (SDF
or HWRT). Rays have a **max length of ~2 m** in software mode.

### 4.1 Near hits

Ray hits within 2 m → sample surface cache at the hit point → return lit
radiance.

### 4.2 Missing / far hits

Ray exits the 2 m sphere without a near hit → **continue in coarser
cache**. Options (cascade):

1. Sample the **radiance cache** (world probes) at the ray's endpoint +
   ray direction. Hit found → sample octahedral → return.
2. If no nearby world probe, sample **voxel lighting** in the ray direction.
3. If voxel lighting is empty (no allocated voxel), sample the **sky LUT**.

The cascade is O(1) per ray: a texture lookup each.

---

## 5 · Octahedral encoding

Each probe stores its gathered radiance in an **octahedral 8×8 or 16×16
layout** (64 or 256 directions). This is a 2D projection of the sphere
onto a unit square via the octahedron mapping (Cigolle et al. 2014).

Advantages over SH:
- Higher frequency (256 directions vs 9 SH coefficients).
- Hardware bilinear filter works correctly across the mapping.
- No ringing (SH has Gibbs-like overshoot).

Disadvantage:
- Larger (8KB per probe at 16×16 fp16, vs 72 B for SH2).

Lumen uses 8×8 for diffuse (64 dirs, 2 KB) and 16×16 for specular
(256 dirs, 8 KB per-pixel lookup).

---

## 6 · Per-pixel final gather

After all probes are traced, the per-pixel shading pass consumes them:

```hlsl
// Per-pixel pseudocode
float2 pixel    = input.svPos.xy;
float3 N        = gbuffer.normal;
float3 pos      = gbuffer.pos;
float2 tile     = pixel / 16.0;

// Find the 2x2 neighborhood of probes around this pixel
Probe p00, p10, p01, p11 = SampleProbeGrid(tile);

// For each candidate probe, compute a weight based on depth + normal similarity
float w00 = exp(-abs(depth00 - myDepth)/threshold) * saturate(dot(N, n00));
// ... same for w10, w01, w11

// Weighted sum of probe irradiance evaluated at our direction
float3 irradiance = (w00*Sample(p00, N) + ... ) / (w00 + w10 + w01 + w11);

// Shade: BRDF × irradiance
output.color = albedo * irradiance;
```

Key trick: probes are **evaluated at each pixel's normal direction**, so
the integration is per-pixel even though the probes are per-tile. The
result has per-pixel frequency in the normal variation (bump mapping
works), limited to per-tile frequency in the illumination itself.

### 6.1 Bilateral weighting

The weights `w_{ij}` use a **depth + normal bilateral filter**:

- Depth difference rejects probes from a different surface.
- Normal difference rejects probes on a different orientation.
- A falloff exponent (typically `exp(-Δ² / σ²)`) gives smooth transitions.

This is analogous to a bilateral blur but in probe-selection space.

---

## 7 · Temporal integration

Each pixel has a 1-frame history of its final-gather result. A temporal
filter reprojects the history using motion vectors and blends with the
current frame:

```
result = lerp(history_reprojected, currentGather, alpha)
```

Alpha is adaptive:
- **Low alpha (0.05)** on static scenes → deep temporal accumulation, very clean.
- **High alpha (0.5)** on disocclusion → rapid refresh, allows ghosting-free motion.

Disocclusion is detected by depth/normal mismatch with history. Disoccluded
regions start fresh (1 spp).

---

## 8 · Spatial denoise

After temporal, one more pass applies a spatial blur guided by depth and
normal (SVGF-style). Kernel radius adapts to history depth: deeper history
= narrower kernel.

---

## 9 · Putting it together — the full integration

For any given pixel, the final gathered irradiance comes from:

```
irradiance = TemporalBlend(
    SpatialFilter(
        BilinearProbeGather(
            PerProbeIntegration(
                RaysHitSurfaceCache + RaysHitRadianceCache + RaysHitSkyLUT
            )
        )
    ),
    history
)
```

That's a lot of steps, but each is cheap. Total cost at 1440p on RTX 3080
is approximately:

| Step | ms |
|------|-----|
| Probe placement | 0.2 |
| Probe ray tracing (HWRT) | 1.5 |
| Radiance cache update | 0.8 |
| Octahedral composition | 0.4 |
| Final gather (per-pixel) | 0.6 |
| Spatial filter | 0.3 |
| Temporal blend | 0.2 |
| **Total** | **4.0** |

(5.6 improvements further reduce this.)

---

## 10 · UzEngine relevance

The screen-probe-gather architecture is the cleanest part of Lumen to port.
It does not require the surface cache (we can inline-evaluate materials at
SDF hits in a simpler build). Sketch:

1. Place probes at 16×16 tile grid, no adaptive subdivision (easy).
2. Shoot 16 rays per probe using blue-noise directions (half Lumen's density).
3. Trace with our existing SDF (we have `global_distance_field.h`).
4. Inline-evaluate material at hit (no surface cache).
5. Store octahedral 8×8 per probe.
6. Bilateral bilinear probe gather in the deferred lighting pass.
7. Temporal accumulation with motion-vector reprojection.

This is ~3 weeks of engineering and gives us 70% of Lumen's GI quality.
See the full plan in [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md) §4.

---

## 11 · References

- Krzysztof Narkowicz GDC 2022 — §"Screen Probe Gather".
- Epic docs — §"Screen Probes".
- Cigolle et al., "A Survey of Efficient Representations for Independent Unit Vectors" — octahedral encoding reference.
- SimLumen — `SimLumenFinalGather` source.
- Heitz, "A Low-Distortion Map Between Triangle and Square" — blue noise references.
- Schied 2017 (SVGF) — spatial filter.
