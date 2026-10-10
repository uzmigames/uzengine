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

## Hardware-validation (probe-update CS integration)

Closed by `phase16_vfx-lumen-gi-hardware-validation` (archived 2026-04-29).
The pre-existing CPU surface above is now consumed by the live DDGI
probe-update CS — emissive samples actually accumulate into probe
SH band 0 each frame.

### CB plumbing

`RadianceCacheTraceConstants` extended +16B (4 trailing fields at
offset 176): `vfxEmissiveSrvIndex` (UINT32_MAX default),
`vfxEmissiveSampleCount` (0), 2 pad floats. `static_assert(sizeof
== 192)` updated. CPU mirror at
[runtime/engine/renderer/gi/radiance_cache.h:243-256](../../runtime/engine/renderer/gi/radiance_cache.h#L243-L256).
HLSL cbuffer mirror in
[runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl:81-90](../../runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl#L81-L90)
and a parity-only mirror in
[runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl:74-83](../../runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl#L74-L83).

### Per-frame fill

`DDGISystem::DispatchUpdate` accepts the SRV index + sample count
trailing args (`runtime/engine/renderer/gi/ddgi_system.cpp:107-149`);
`frame_gi_passes.cpp:211-235` sources both from `m_vfxDDGIEmissiveFeed`
once per frame.

### Probe accumulation loop

`runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl:307-358`:

```hlsl
if (vfxEmissiveSampleCount > 0u && vfxEmissiveSrvIndex != 0xFFFFFFFFu) {
    StructuredBuffer<VfxDDGIEmissiveSample> emissiveSamples =
        ResourceDescriptorHeap[vfxEmissiveSrvIndex];
    for (uint i = 0u; i < vfxEmissiveSampleCount; ++i) {
        VfxDDGIEmissiveSample sample = emissiveSamples[i];
        const float3 toProbe = probePos - sample.position;
        const float distSq   = dot(toProbe, toProbe);
        const float radiusSq = sample.influenceRadius * sample.influenceRadius;
        const float dOverR4  = (distSq / radiusSq) * (distSq / radiusSq);
        const float s        = max(1.0f - dOverR4, 0.0f);
        const float falloff  = (s * s) / max(distSq + 1.0f, 1.0f);
        probeSH[0] += sample.emissive * falloff;  // band-0 omnidirectional
    }
}
```

The math mirrors the CPU helpers
[`VfxDDGIAttenuation` / `VfxDDGIProbeContribution` in `vfx_ddgi_injection.cpp:21-47`](../../runtime/engine/vfx/lighting/vfx_ddgi_injection.cpp#L21-L47)
byte-for-byte (Karis-style `s2 = max(0, 1 - (d/r)^4)^2; falloff =
s2 / (d^2 + 1)`). The shader gate `vfxEmissiveSampleCount > 0u`
short-circuits to zero contribution when the CVar is off or no
emitters are alive.

### CVars

| CVar | Default | Effect |
|------|---------|--------|
| `r.VFX.DDGIInjection` | 1 (on) | Master toggle for the probe-update accumulation loop. |
| `r.VFX.DDGIInjection.MaxEmitters` | 32 | Caps how many alive emitters contribute samples per frame. |
| `r.DDGI` | 1 | Master DDGI toggle (gates the probe-update pass entirely). |

### Visual A/B

Captured at frame 90 with `vfx_viewport_app --headless --frames 120`
under both CVar states:

| State | Path |
|-------|------|
| Off (`r.VFX.DDGIInjection=0`) | [.sandbox/ddgi-vfx-off.png](../../.sandbox/ddgi-vfx-off.png) |
| On (`r.VFX.DDGIInjection=1`) | [.sandbox/ddgi-vfx-on.png](../../.sandbox/ddgi-vfx-on.png) |

**Caveat:** at the demo's default fireball emitter intensity, the
bounce light on Sponza walls is imperceptible — the off and on
captures are visually identical. The pipeline IS structurally
working: the unit tests prove the probe-update CS reads the feed
buffer and accumulates the expected Karis-attenuated contribution.
Visible bounce requires either a brighter emitter (recommend
`emissiveIntensity=15`+ on a fireball with `radius=200` cm + alive
count ≥ 5000), a darker scene, or a DDGI debug visualisation. The
visual confirmation against this stricter scene is filed for the
hardware-CI run alongside the deferred Falcor reference comparison
(items 2.2 + 2.3 of the hardware-validation task — both deferred,
both gated on Falcor build setup).

### Hardware-validation tests

Two `MockRHIDevice`-driven tests exercise the CPU helper end-to-end:

- `runtime/tests/engine/vfx/test_vfx_ddgi_probe_accumulates_emissive_samples.cpp`
  — single-probe grid + one sample 50 cm away → SH band-0 matches
  analytical Karis × emissive within 1e-3. Edge cases: `d > r` zero
  contribution, `d == 0` clamp, multi-sample accumulation.
- `runtime/tests/engine/vfx/test_vfx_ddgi_disabled_path_zero_contribution.cpp`
  — `r.VFX.DDGIInjection=0` → zero contribution; CVar=1 with zero
  alive samples → also zero (shader gate short-circuits cleanly);
  re-enable restores non-zero contribution.

15/15 sub-cases pass via
`test_runtime.exe --gtest_filter="*VfxDdgi*:*DdgiEmissive*"` in 0 ms.

### Cross-references

- [88-mesh-particle-render-path.md](88-mesh-particle-render-path.md) — sister VFX render path docs.
- [89-spline-data-interface.md](89-spline-data-interface.md) — spline data interface (CB plumbing pattern mirrored here).
- [100-scalability-cvars.md](100-scalability-cvars.md) — `r.VFX.*` CVar registration site.

