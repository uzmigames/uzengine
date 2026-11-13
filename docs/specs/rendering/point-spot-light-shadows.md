<!-- doc-meta
title: Point and Spot Light PCSS
category: rendering
status: current
updated: 2026-11-13
-->

# Point and spot light soft shadows

Deferred point and spot lights support PCSS with contact hardening: increasing
blocker-to-receiver separation widens the shadow edge. The implementation uses
the existing local atlas, depth pass and tile budget. The
[local shadow overview](../../analysis/lighting/shadows/local-light-overview.md)
documents allocation, projections and radial-depth comparisons.

## Controls and selection

| Control | Default | Meaning |
|---|---|---|
| `r.Shadows.SoftPCSS` | 1 | Master soft-shadow gate, including local lights. |
| `r.Shadows.SoftPCSS.Local` | 1 | Additional point/spot gate. |
| `r.Shadows.SoftPCSS.Local.SourceRadius` | 5 cm | Fallback emitter radius, range 0..100 cm. |
| `r.Shadow.PCSSMinPenumbra` / `r.Shadow.PCSSMaxPenumbra` | 0.5 / 16 texels | Shared filter radius window. |
| Light JSON `softShadowRadius` | 0 cm | Positive overrides emitter size; zero inherits; negative selects the hard path for that light. |

`PointLightComponent::softShadowRadius` is inherited by `SpotLightComponent`.
A point with zero override inherits positive `sourceRadius`, then the fallback
CVar; a spot with zero override always inherits the fallback CVar. Consequently
a fallback of zero only makes lights hard when they do not have a positive
override or a positive inherited point source radius. Either gate set to zero
calls the unchanged `SampleLocalShadow` path regardless of radius. `castShadows`
and atlas-budget eligibility remain prerequisites for any shadow sampling.

Example light objects within an entity's `components.light`:

```json
{"type":"point","castShadows":true,"softShadowRadius":12}
```

```json
{"type":"spot","castShadows":true,"softShadowRadius":-1}
```

The scene loader reads and saves the field for both light types.
`ExtractLightingFromECS` uploads the signed value unchanged. Selection occurs in
the shader's `LocalPcssRadius`, not during CPU extraction.

## Filtering and atlas safety

`LocalShadow` dispatches to `SampleLocalShadowSoft` in
`deferred_lighting_ps.hlsl`. A 16-tap blocker search reads distance/range from
the local atlas and estimates physical penumbra radius using similar triangles:
`max(receiver - blocker, 0) * max(emitterRadius, 0) / max(blocker, 1e-3)`.
Distances are radial cm, not perspective Z. The radius is projected to texels,
clamped and used for 16 comparison taps. No blockers yields fully lit.

Every tap intersects the geometric receiver plane for its radial comparison,
with the same distance-scaled normal offset and cm bias as the hard sampler.
Point kernel rays can cross cube faces: `LocalShadowTapUV` selects the
neighbouring face and reprojects before clamping to a half-texel inset. Spot
taps stay within their one tile. Kernel rotation has no frame term. Cross-face
filtering cannot repair missing rasterized depth; the previously recorded thin
point-face raster seam remains a known limitation.

This support is in bindless deferred lighting. It does not enable PCSS for
forward/translucent or volumetric local-light sampling. Function contracts are
listed in [the local shader library](shader-library.md).

## Data contract and tests

The signed radius occupies former padding: byte 52 in 64-byte `PointLightData`,
byte 68 in 80-byte `SpotLightData`. `ShadowDataCB` remains 2304 bytes:
`localSoftPCSS` at 752, `localPcssSourceRadius` at 756; local entries start at
768 and each occupy 96 bytes. No atlas layout or CB size changes are needed.

CPU coverage is shared with existing tests:

- `test_local_light_shadows.cpp`: allocation, tile bounds, light selection and
  budgets, face/cone projections, entry layout and signed radius extraction.
- `test_shadow_pcss.cpp`: contact hardening, perspective similar triangles,
  distance/radius scaling, finite zero-blocker behavior, shared clamp and CB offsets.
- `test_deferred_pipeline.cpp` and `test_rendering_parity.cpp`: radius defaults,
  point/spot ABI offsets and sizes.
- `test_scene_loader.cpp`: absent-field inheritance and signed-radius
  load/save/load round trips for both light types.

CPU tests do not execute HLSL or establish visual continuity or GPU timing.
The task ledger preserves the earlier GPU evidence; this documentation and
test tail does not repeat those captures.
