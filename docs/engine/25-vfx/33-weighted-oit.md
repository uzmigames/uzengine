# VFX Weighted Blended OIT

Order-independent transparency as a per-emitter alternative to depth
sorting. Eliminates sort artefacts on interpenetrating transparents and
removes the sort cost at high particle counts, at the expense of one
accumulation RT + one revealage RT + one fullscreen resolve pass.

Reference: McGuire & Bavoil 2013, *Weighted Blended Order-Independent
Transparency*.

## When to use

| Scenario                            | Use OIT? | Why                                       |
|-------------------------------------|----------|-------------------------------------------|
| Dense interpenetrating smoke        | ✅       | Sort can't untangle overlap correctly.    |
| 50 000+ additive sparks             | ✅       | Sort cost dominates frame time.            |
| Few well-separated sprite decals    | ❌       | Sort is cheap and exact; OIT costs more.   |
| Single large fade-out decal         | ❌       | Resolve quality is inferior to straight α. |

Authors opt in per emitter via `blendMode = WeightedOIT` (enum value 3,
added in phase 8.16.0).

## Pipeline

```
┌───────────────┐
│ Accumulation  │ ⇐ every OIT-enabled emitter draws into:
│  RT0 RGBA16F  │   RT0 += (color · α · w,  α · w)   (additive)
│  RT1 R8       │   RT1 *= (1 − α)                   (multiplicative)
│  Depth read-only
└───────┬───────┘
        │
        ▼
┌───────────────┐
│ Resolve PS    │   sample RT0/RT1 at screen UV,
│ (fullscreen)  │   output = (RT0.rgb / max(RT0.a, 1e-4), RT1)
└───────┬───────┘   blend: src · (1-srcAlpha) + dst · srcAlpha
        │
        ▼
  HDR scene colour (composited)
```

## Weight function

```
w(z, α) = α · max(blendFactor, min(3·10⁻², α⁵ · zWeight(z)))
zWeight(z) = clamp(0.03 / (1e-5 + z⁴), 0.01, 3·10³)
```

- Near samples (small `z`) receive the largest weight so they dominate
  the average at resolve time.
- The `α⁵` term emphasises near-opaque samples.
- `blendFactor` (authoring knob, default 0.01) keeps low-α samples from
  vanishing entirely.
- `α = 0` forces `w = 0` — fully transparent fragments do not pollute
  the accumulation buffer.

Implemented in `VfxOITWeight(linearZ, alpha, blendFactor)`. The HLSL
pixel shader's accumulation snippet mirrors this formula exactly.

## Resolve composite

```cpp
average = accumRGB / max(accumAlphaSum, 1e-4);
final   = lerp(background, average, 1 - revealage);
```

The resolve shader outputs `(average, revealage)` and the blend state
composites over the framebuffer with
`src · (1 − srcAlpha) + dst · srcAlpha`, which evaluates to the `lerp`
above. Tests cover the CPU mirror in
`runtime/tests/engine/vfx/test_vfx_oit.cpp`.

## Blend mode enum

Extends `blendModeHint` (VFXBillboardCB field) with `WeightedOIT = 3`:

| Value | Name          | Blend state                       |
|-------|---------------|-----------------------------------|
| 0     | `AlphaBlend`  | `SrcAlpha / InvSrcAlpha`          |
| 1     | `Additive`    | `One / One`                       |
| 2     | `PremulAlpha` | `One / InvSrcAlpha`               |
| 3     | `WeightedOIT` | `One / One` on RT0, `Zero / InvSrcAlpha` on RT1 |

Existing values 0–2 keep their numeric positions so saved assets
continue to render identically. Only new emitters opt in by authoring
blend mode 3.

## Commutativity

A cornerstone test verifies that different draw orders of the same
fragment set produce identical resolve output — this is the whole point
of the technique. See `ReorderingFragmentsYieldsSameResult` in the
test file.

## Tests

`runtime/tests/engine/vfx/test_vfx_oit.cpp` — 16 cases:

- Enum: string names, numeric stability.
- `VfxOITWeight`: nearer beats farther, higher α beats lower, α
  monotonicity at fixed depth, `α = 0 ⇒ w = 0`, out-of-range clamp.
- `VfxOITResolve`: full reveal ≡ background, zero reveal ≡ average,
  zero accum-sum guard, half reveal produces 50/50 mix.
- `VfxOITResolveAlpha`: `1 − reveal` mapping with negative/positive
  clamps.
- End-to-end: three-sample accumulate + resolve produces finite output
  with the nearest colour dominant; re-ordering produces identical
  output (commutativity).
