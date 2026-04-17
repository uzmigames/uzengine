# Radiance Cascades

Alexander Sannikov's novel GI algorithm (Grinding Gear Games / Path of
Exile). Unlike probe-based or path-traced GI, it is a **structure-driven**
technique: a cascade of radiance-encoding buffers at different spatial
and angular resolutions, merged bottom-up. The output per pixel is a
queryable hemispherical radiance with no stochastic noise — **deterministic
output at O(constant) cost per pixel** regardless of light count or scene
complexity.

Promising. Still research-tier for 3D scenes but production-ready for 2D
(used in 2D games and confirmed for a future Path of Exile extension).

---

## 1 · The core principle

> "To resolve radiance emitted by an object, you need higher LINEAR
> resolution next to it and higher ANGULAR resolution farther away."

Near-field: you care about which direction a light is in (linear
position), and the answer is simple (dominant light fills a small cone).

Far-field: you don't care where exactly a light is (they're all far away),
but you care which direction (angular resolution).

A standard light probe has the same angular resolution everywhere — waste
near the probe, insufficient far away.

Radiance cascades **inverts** this: each cascade level stores fewer
probes but more angular samples per probe. Merged, you get the right
resolution at every distance.

---

## 2 · Cascade structure

```text
Cascade 0:  Many probes (high linear density), few rays per probe (low angular)
            e.g. 512×512 probes, 4 directions each

Cascade 1:  Half the probes, 2× the rays
            256×256 probes, 8 directions each

Cascade 2:  128×128 probes, 16 directions each

...

Cascade N:  1×1 probes, 256×256 directions  (essentially one environment map)
```

Each cascade has the same memory (probes × rays = constant × 4 per level).

### 2.1 Per-ray length

Cascade 0 rays are short (~1 m).
Cascade 1 rays are medium (~4 m).
Cascade N rays are very long (cover the whole scene).

Each cascade handles one "range slice" of the radiance field.

---

## 3 · Merge operation

Cascades are merged bottom-up:

1. Trace cascade 0 rays.
2. At the far end of each cascade 0 ray, sample cascade 1.
3. Combine the two: cascade 0's direct radiance + cascade 1's continuation.
4. Recurse up.

The merge is a **bilinear interpolation** between cascades (spatially and
angularly). Done right, the interpolation is smooth and produces a
continuous radiance field with no banding.

---

## 4 · Properties

### 4.1 Deterministic

No stochastic rays. Output is exact given the scene state. No temporal
accumulation needed in principle (though recommended for moving geometry).

### 4.2 Constant time per pixel

Per-pixel sampling is O(1) lookups into the cascades. Independent of light
count or scene complexity.

### 4.3 Memory-heavy

Cascade memory scales with resolution. At 1080p full-screen in 3D,
cascades can require 200+ MB. Comparable to DDGI but differently shaped.

### 4.4 No "missing samples"

Unlike ReSTIR / path tracing, every direction in every cascade is populated
every frame. No disocclusion darkness, no history invalidation.

---

## 5 · 2D vs 3D

### 5.1 2D (shipping)

Works exceptionally well. Path of Exile 2 uses radiance cascades for
dynamic lighting in their 2D top-down ARPG. Frame budget for GI: 0.3 ms
on mid-range GPUs.

### 5.2 3D (research)

More difficult. The "curse of dimensionality" hits hard — the angular
resolution required at the far cascade is 2D (solid angle), not 1D (polar
angle). 3D radiance cascades paper is recent (Sannikov 2023 preprint;
extensions by others in 2024).

**Holographic Radiance Cascades** (arxiv 2505.02041, 2025) extends the 2D
version with a specialized acceleration structure; shows real-time 2D GI
at dramatically improved quality.

For UzEngine's 3D renderer, radiance cascades is not production-ready
today, but is worth watching. Tentative production-quality
implementations for 3D exist in research codebases.

---

## 6 · Why we mention it here

Even if we don't adopt radiance cascades, the underlying insight
("balance spatial resolution with angular resolution per distance
bucket") applies to probe grids in general. It's a design lens, not just
an algorithm.

Specifically, for UzEngine's distant-GI layer:

- **Probe spacing should scale with distance** (not uniform). Far probes
  can be sparse.
- **Angular resolution should scale with distance** inverse to above (far
  probes need more directions to avoid blocky light leaks at range).

This gives us **probe cascades** — a light-weight version of the
Sannikov structure applied to our existing DDGI-style probe system.

---

## 7 · References

- Alexander Sannikov, "Radiance Cascades: A Novel Approach to Calculating Global Illumination" (preprint 2023). github.com/Raikiri/RadianceCascadesPaper
- 80.lv, "Radiance Cascades: New Approach to Calculating Global Illumination" (2024 summary).
- tmpvar.com/poc/radiance-cascades/ — interactive 2D demo.
- GM Shaders guest post by Xor & Alex, "Radiance Cascades" — intuition.
- Holographic Radiance Cascades paper (2025), arxiv 2505.02041.
- Sannikov, Grinding Gear Games GDC 2024 talk (partial slides public).
