# VFX ribbon morphology

Per-point curve-driven attributes for trail / ribbon geometry: width, twist,
and opacity. Lets authors produce tapered, twisted, or custom-faded trails
without hand-authored meshes or extra strand parameters.

Reference: UE5 `FNiagaraRibbonShapeCustomVertex`.

## Curves

`VfxTrailMorphology` carries three optional `VfxTangentCurve` instances
(phase 7.16.0) sampled at each ribbon point's normalised arc length
`t = distanceFromHead / totalDistance`:

| Curve          | Applied to        | Empty-curve default | Effect                            |
|----------------|-------------------|---------------------|-----------------------------------|
| `widthCurve`   | Per-point width   | ×1.0                | Multiplies the base ribbon width. |
| `twistCurve`   | Per-point basis   | 0 rad               | Rotates (right, up) around tangent.|
| `opacityCurve` | Vertex alpha      | ×1.0                | Multiplies per-vertex alpha.      |

Every curve is optional — an empty curve short-circuits sampling and the
builder falls through to the legacy per-point values. A completely
default-constructed `VfxTrailMorphology` has `IsIdentity() == true` so
pre-8.14 trails render byte-for-byte identically.

## Authoring examples

- **Tapered lightning**: `widthCurve = { (0, 1.0), (0.3, 1.8), (1.0, 0.1) }`
  bulges mid-stream then tapers to a fine tip.
- **Caduceus / helix**: `twistCurve = { (0, 0), (1, 4π) }` produces two
  full turns over the ribbon length; raise the end value for tighter
  spirals.
- **Mid-stream gap**: `opacityCurve = { (0, 1), (0.4, 1), (0.5, 0),
  (0.6, 1), (1, 1) }` punches a translucent hole between 40–60%.

## Twist math

Twist is implemented as an in-plane rotation of the (right, up) basis
around the local tangent axis:

```
theta = twistCurve.Evaluate(t)
right' = right·cos(theta) + up·sin(theta)
up'    = up·cos(theta)    - right·sin(theta)
```

This keeps both vectors unit-length and orthogonal regardless of `theta`,
so the ribbon cross-section rotates cleanly without scale drift. When
adjacent samples differ, the quad strip becomes non-planar — the expected
visual for a spiralling ribbon.

## API

```cpp
VfxTrailMorphology morphology;
morphology.widthCurve.AddLinearKey(0.0f, 1.0f);
morphology.widthCurve.AddLinearKey(0.3f, 1.8f);
morphology.widthCurve.AddLinearKey(1.0f, 0.1f);

auto mesh = builder.Build(points, count, camPos,
                          widthStart, widthEnd,
                          colorStart, colorEnd,
                          totalDist, pointLifetime,
                          strandCount, strandSpacing,
                          uvMode, tilesPerRibbon,
                          &morphology);
```

Pass `nullptr` (the default) to skip morphology entirely.

## Tests

`runtime/tests/engine/vfx/test_trail_morphology.cpp` — 10 cases:
empty-curve default pass-through, linear-curve sampling at endpoints +
midpoint, sampler clamp on out-of-range `t`, `IsIdentity` default true,
`IsIdentity` false when any curve has keys, twist rotation zero preserves
basis, twist 90° rotates right → up, twist preserves orthogonality +
unit length at arbitrary angle, twist full-circle returns original.
