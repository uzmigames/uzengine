# VFX World Position Offset (WPO)

Vertex-shader deformation pin for particle materials. Authors plug an HLSL
expression into the material's WPO input; the VS evaluates it per-vertex
and adds the result to the world-space position. Enables organic /
magical effects (breathing meshes, twisted magic, swimming sprites) without
CPU-side mesh editing.

Reference: UE5 `FMaterialAttributes::WorldPositionOffset`.

## Splice contract

The material compiler emits an HLSL function matching the fixed signature:

```hlsl
float3 EvaluateWPO(float3 objectPos, float3 worldPos,
                   float time, float normalizedAge) {
    return <author expression>;
}
```

Four well-known inputs are in scope:

| Name            | Type    | Source                                 |
|-----------------|---------|----------------------------------------|
| `objectPos`     | float3  | Vertex position in object space.        |
| `worldPos`      | float3  | Vertex position in world space.         |
| `time`          | float   | Global total time (seconds).            |
| `normalizedAge` | float   | Particle age / lifetime, clamped [0,1]. |

The VS splice inserts a call immediately before the `viewProj` multiply:

```hlsl
float3 wpo = EvaluateWPO(objectPos, worldPos, time, normalizedAge);
worldPos += wpo;
```

Materials without an authored expression emit no splice at all (not a
no-op — zero text), so the optimiser sees the identity vertex path and
no WPO cost is paid on materials that don't use the feature.

## Motion-vector reprojection (phase 8.1.0 integration)

The previous-frame vertex output (`prevPositionCS`) must include the
previous-frame WPO for TAA / DLSS / FSR to reproject correctly. The
compiler also emits a prev-frame splice that reads prev-frame inputs:

```hlsl
float3 prevWpo = EvaluateWPO(objectPos, prevWorldPos, prevTime, prevNormalizedAge);
prevWorldPos += prevWpo;
```

The expression is identical; only the binding names change. This means
authors get correct reprojection for free — the compiler reuses their
expression both frames.

## Authoring examples

| Goal                  | Expression                                            |
|-----------------------|-------------------------------------------------------|
| Breathing pulse       | `worldPos * (0.1 * sin(time))`                        |
| Vertical sway         | `float3(0, 0, 0.1 * sin(time + worldPos.x))`          |
| Age-driven twist      | `float3(worldPos.x * cos(a) - worldPos.y * sin(a) - worldPos.x, worldPos.x * sin(a) + worldPos.y * cos(a) - worldPos.y, 0)` where `a = normalizedAge * 6.283` |
| Wind bend             | `float3(0.2 * sin(time + worldPos.z), 0, 0)`          |

## TAA caveats

- Extreme WPO (offset magnitudes larger than ~1 m per frame) will exceed
  the RG16F motion-vector range. Temporal upscalers fall back to "reject
  history" for those pixels, which looks noisy but is correct. Authors
  who need extreme deformation should either cap the expression or
  switch off TAA for the affected material.
- Disabling WPO mid-lifetime (e.g. clamping the expression to 0 after a
  fade-out) produces a one-frame reprojection artefact because the
  previous-frame evaluation still includes the offset. The compiler
  cannot detect this; it is an authoring caveat.

## Tests

`runtime/tests/engine/vfx/test_vfx_wpo.cpp` — 14 cases:

- Attributes default disabled + enabled when expression set.
- `VfxWPOEmitEvaluateBody`: empty → zero return; enabled → wrap + return.
- `VfxWPOSpliceVS`: disabled → empty string; enabled → `EvaluateWPO` call
  + `worldPos += wpo`.
- `VfxWPOSplicePrevVS`: disabled → empty; enabled → reads `prevWorldPos`,
  `prevTime`, `prevNormalizedAge` (and asserts it does NOT read the
  current-frame names that would break motion vectors).
- CPU evaluator (`VfxEvaluateWPOBuiltin`): `zero` / `sin_time_x` /
  `breathe` / `twist` expressions match expected analytic outputs.
- Unknown id + null id guard against crashes.
- Identical-input determinism: same inputs produce identical offsets
  (foundation of the motion-vector reprojection invariant).
