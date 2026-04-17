# ReSTIR — Reservoir-based Spatiotemporal Importance Resampling

The single most important algorithmic advance in real-time lighting since
shadow maps. Understanding ReSTIR is mandatory for anyone implementing an
AAA lighting pipeline in 2026.

---

## 1 · The problem ReSTIR solves

Many-light direct lighting: you have 1000 lights, and you can only afford
to shadow-test ~1 per pixel. How do you pick **which** light so that the
final image looks unbiased?

Classical answer: random pick weighted by contribution estimate. Converges,
but noisily. Bright lights off-screen never get picked; small bright lights
near a pixel don't win often enough.

ReSTIR's answer: **resample** rather than sample. Keep a reservoir per pixel
that holds the currently-selected "best" sample, and update it over time
and across neighbors using a weighted random selection process that
provably converges to the correct importance-sampled distribution.

---

## 2 · The reservoir

A per-pixel struct containing:

```hlsl
struct Reservoir
{
    uint   sampleIdx;   // which light / path is currently selected
    float  W;           // weight of the selected sample (for the final estimator)
    float  weightSum;   // running sum of all candidate weights seen
    float  M;           // effective sample count (bounded, ~20)
};
```

### 2.1 Streaming update (Weighted Reservoir Sampling — WRS)

When a new candidate `s` with weight `w` comes in:

```hlsl
void UpdateReservoir(inout Reservoir r, uint newSample, float newWeight, float random01)
{
    r.weightSum += newWeight;
    r.M += 1;
    if (random01 < newWeight / r.weightSum) {
        r.sampleIdx = newSample;
    }
}
```

This is a classic reservoir sampling algorithm: any sample has probability
proportional to its weight of being the "selected" one after any number of
updates.

### 2.2 Recomputing W

After all candidates are seen, compute the final W:

```hlsl
float target = TargetPdf(r.sampleIdx);     // e.g. unshadowed contribution
r.W = r.weightSum / (r.M * target);
```

This W is the scale factor for the final contribution estimate.

---

## 3 · ReSTIR DI — Direct Illumination

### 3.1 Initial pass

For each pixel:

1. Randomly pick, say, 32 candidate lights (uniformly).
2. For each candidate, compute its "target PDF": estimated contribution
   without shadowing (distance attenuation × BRDF × cos).
3. Stream them into the pixel's reservoir using WRS.

After this pass, each pixel has ONE selected light — likely a good one.

### 3.2 Visibility test

Trace ONE shadow ray from the pixel to the selected light. If shadowed,
zero out contribution.

### 3.3 Temporal reuse

Reproject previous frame's reservoir using motion vectors. If reproject is
valid (depth/normal match), combine with current frame's reservoir via WRS.

### 3.4 Spatial reuse

Pick ~5 neighbors in a Poisson disk. Combine their reservoirs with the
current pixel's reservoir via WRS. Reject neighbors with different
depth/normal.

### 3.5 Final shading

Use the selected sample's light + the reservoir's W + the shadow-test
result to compute the final direct-light contribution.

Net result: effectively 32 × 5 neighbors × 1 frame history = ~160 candidate
samples per pixel per frame. At the cost of ONE shadow ray.

---

## 4 · ReSTIR GI — Indirect Illumination

### 4.1 Path definition

An indirect "light path" is `(origin, direction, hitPoint, incomingRadiance)`.
When we trace a GI ray and hit geometry, the *reverse* path (hit → origin)
is a candidate light path.

### 4.2 Reservoir holds a path

```hlsl
struct ReservoirGI
{
    float3 hitPoint;
    float3 hitNormal;
    float3 L_in;           // radiance at hitPoint coming from the surface cache / material
    float  W;
    float  weightSum;
    float  M;
};
```

### 4.3 Update cycle

Same WRS-based update, but candidates come from:

- **Initial pass**: shoot a real GI ray, record the path.
- **Temporal reuse**: reproject prev-frame reservoir via motion vectors, then **re-evaluate** target pdf at the current shading point using the stored path (this is the subtle part — the target pdf changes when the origin pixel changes).
- **Spatial reuse**: pick neighbors, re-evaluate their stored paths at current shading point.

### 4.4 The re-evaluation

When reusing a neighbor's path, the shading point is different from the
neighbor's. The incoming radiance at `hitPoint` is the same, but the cosine
and BRDF factors must be re-evaluated.

```hlsl
float targetPdf = BRDF(currentPixelN, dirToHit) * dot(currentPixelN, dirToHit) * length(L_in);
```

This is the "target PDF" in MIS terms.

### 4.5 Bias correction (GRIS)

Naive spatial reuse introduces bias: neighbor pixels see different
occlusion. Lin et al. 2022 "Generalized Resampled Importance Sampling" (GRIS)
provides the correct MIS weights. Industry-standard.

---

## 5 · Bounded M

Temporal reuse would let `M` grow unbounded — each frame's update added to
the previous. This fails because the Monte Carlo variance estimate `W ~ 1/M`
implies infinite accumulation, violating bias correction.

Fix: clamp `M ≤ M_max` (typically 20). Discards one sample's worth of
history per frame. Matches TAA clamping philosophy.

---

## 6 · Performance

On RTX 4080 at 1440p:

| Pass | Cost |
|------|------|
| ReSTIR DI initial + visibility | 1.5 ms |
| ReSTIR DI temporal | 0.2 ms |
| ReSTIR DI spatial | 0.4 ms |
| **ReSTIR DI total** | **2.1 ms** |
| ReSTIR GI initial (ray) | 2.5 ms |
| ReSTIR GI temporal | 0.4 ms |
| ReSTIR GI spatial | 0.6 ms |
| **ReSTIR GI total** | **3.5 ms** |
| **Both total** | **5.6 ms** |

This is the single biggest fixed cost in a BlackSpace-style pipeline. But
it replaces many-light-shadow brute force (which would be 10×+ this cost)
and delivers 10-100× lower MSE.

---

## 7 · Implementation checklist

For UzEngine's adoption of ReSTIR DI:

- [ ] Per-pixel reservoir buffer (2× for ping-pong)
- [ ] Light BVH or pre-sorted light list with PDF hierarchy
- [ ] Candidate-generation compute pass (32 candidates)
- [ ] Streaming WRS update
- [ ] Visibility trace pass (1 shadow ray per pixel)
- [ ] Temporal reproject + combine pass
- [ ] Spatial combine pass (5 neighbors)
- [ ] Final shading pass reading reservoir → shading contribution
- [ ] Clamp M = 20
- [ ] Demodulated output buffer for denoiser compatibility

First milestone: **ReSTIR DI for analytic lights** (no emissive triangles),
replacing our current all-lights loop. Gives us many-lights shadowing
essentially for free compared to brute force.

---

## 8 · References

- Bitterli, Wyman, Pharr, Shirley, Lefohn, Jarosz, "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" (SIGGRAPH 2020). benedikt-bitterli.me/restir/
- Ouyang, Liu, Kettunen, Pharr, Pantaleoni, "ReSTIR GI: Path Resampling for Real-Time Path Tracing" (EGSR 2021). research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing
- Lin, Knaus, Seidel, "Generalized Resampled Importance Sampling: Foundations of ReSTIR" (SIGGRAPH 2022) — **the** correct MIS formulation. graphics.cs.utah.edu/research/projects/gris/
- Wyman, "A Gentle Introduction to ReSTIR Path Reuse in Real-Time" (SIGGRAPH 2023 course). par.nsf.gov/servlets/purl/10519651
- NVIDIA RTXDI (open source ReSTIR implementation): github.com/NVIDIA-RTX/RTXDI — reference for production code.
- "Understanding The Math Behind ReSTIR GI" (agraphicsguynotes.com/posts/understanding_the_math_behind_restir_gi/) — accessible intuition.
