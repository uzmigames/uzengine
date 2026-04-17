# VFX → DDGI emissive injection

Emissive particles (fire, lightning, muzzle flashes, magic) contribute to
scene indirect lighting via DDGI probe updates. Bright VFX events now
actually light the world — an explosion lights nearby walls orange, a
magic aura glows on the caster's surroundings.

Reference: UE5 `FNiagaraLumenCommitter` + `FLumenSceneRadianceCache`
(UzEngine targets DDGI instead of Lumen).

## Pipeline

```
  ┌────────────────────────────┐
  │ VFX emissive readback (4.13)│  picks top-N particles by luminance
  │   VfxEmissiveSample[]        │
  └──────────────┬──────────────┘
                 │
                 ▼
  ┌────────────────────────────┐
  │ DDGI probe-update CS        │  each probe accumulates across samples
  │   VfxDDGIProbeContribution   │
  └──────────────┬──────────────┘
                 │
                 ▼
  ┌────────────────────────────┐
  │ SH irradiance + VFX emissive│  lights all subsequent shading
  └────────────────────────────┘
```

All helpers are pure CPU so the GI contribution can be unit-tested
without a GPU; the production DDGI CS mirrors the math.

## Sample schema

```cpp
struct VfxEmissiveSample {
    Vec3  position;         // world-space
    Vec3  emissive;          // linear RGB radiance
    float influenceRadius;   // falloff cutoff (metres)
};
```

`Luminance()` returns Rec. 709 luma (`0.2126·R + 0.7152·G + 0.0722·B`)
for ranking. The 4.13 readback sorts by this luminance and keeps the top
N; this module mirrors that selection in `VfxDDGISelectTopN`.

## Attenuation

Karis 2013 smooth-cutoff inverse-square:

```
f(d) = saturate(1 − (d / r)⁴)² / (d² + 1)
```

Key properties:

- `d = 0 → 1` (maximum contribution).
- `d ≥ r → 0` (smooth cutoff, no pop).
- Monotonically decreasing across `[0, r)` — tests verify this.
- Finite at `d → 0` (no singularity).
- `r ≤ 0` disables the sample entirely (`f → 0`).

## Per-probe accumulation

```cpp
Vec3 total = VfxDDGIAccumulateAtProbe(samples, count, probePos);
// total is in the same linear-RGB space as sample.emissive and can be
// added directly to the probe's SH irradiance result.
```

Samples beyond `influenceRadius` contribute exactly 0 so iterating all N
is cheap (no branches in the inner loop; the attenuation naturally
zeroes far samples).

## Top-N selection

`VfxDDGISelectTopN(in, inCount, out, maxN)` copies the input into `out`
and `std::partial_sort`s descending by luminance, returning
`min(inCount, maxN)`. In-place aliasing (`in == out`) is explicitly
supported because the GPU readback path wants to rank in a single
buffer.

## Integration checklist

1. The VFX readback (4.13) fills a CPU buffer of
   `VfxEmissiveSample[inCount]`.
2. Call `VfxDDGISelectTopN` to collapse to the top N (N ≈ 16–32).
3. Upload the buffer to a bindless structured buffer that the DDGI
   probe-update CS reads.
4. Inside the CS, per probe call
   `VfxDDGIAccumulateAtProbe(samples, count, probePos)` and add the
   result to the probe's irradiance accumulator before SH convolution.

## Tests

`runtime/tests/engine/vfx/test_vfx_ddgi_injection.cpp` — 16 cases:

- Luminance: Rec. 709 weighting + green dominates red.
- Attenuation: zero-distance = 1, beyond-radius = 0, zero-radius = 0,
  monotonic across [0, r), negative-distance clamped.
- Probe contribution: zero beyond radius, matches emissive at d=0,
  falls off with distance.
- Top-N: picks highest-luminance first, clamps to input count, zero
  inputs / zero maxN return 0, in-place aliasing safe.
- Accumulation: sums across samples, zero-count returns zero.
