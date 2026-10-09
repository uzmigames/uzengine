# VFX advection velocity-field data interface

Samples a 3D velocity texture and blends it into each particle's velocity
in the update CS. Different from the Vector Field DI (phase 4.15):

| DI              | Behaviour                                                    |
|-----------------|--------------------------------------------------------------|
| Vector Field    | Reads a force; **adds** to velocity (physics-like push).     |
| Advection Field | Reads a velocity; **lerps** the particle onto it (follow).   |

Reference: UE5 `UNiagaraDataInterfaceVelocityGrid`.

## Config

```cpp
struct VfxAdvectionFieldConfig {
    uint32_t fieldSrvIndex;  // bindless Texture3D<float3> SRV
    Vec3     worldMin;       // AABB of the painted region
    Vec3     worldMax;
    float    tightness;      // 0 = off, 1 = full follow
};
```

`IsEnabled()` returns true only when the SRV is bound AND the AABB is
non-degenerate on every axis.

## World → field mapping

`VfxAdvectionWorldToField(cfg, worldPos)` maps world position into the
field's `[0,1]³` UV space:

```
uvw = (worldPos - worldMin) / (worldMax - worldMin)
```

`VfxAdvectionIsInside(uvw)` tests `[0, 1]` on each axis (inclusive
edges). Out-of-field samples are **not** clamped — the caller uses the
inside test to gate: particles outside the painted region keep their
existing velocity.

## Tightness blend

`VfxAdvectionBlendVelocity(current, advected, insideField, tightness)`
lerps current → advected by the clamped tightness. Out-of-field always
returns the current velocity (particles that leave the field keep their
inertia).

| Tightness | Effect                                     |
|-----------|--------------------------------------------|
| 0.0       | Field has no effect.                        |
| 0.25      | Gentle follow; particles drift along path. |
| 0.5       | Half-mix; classic advection feel.          |
| 1.0       | Hard follow; velocity fully replaced.      |

Negative or >1 values clamp to [0, 1] at the blend site.

## HLSL integration

`VfxAdvectionEmitHLSLSnippet()` returns a canonical snippet for the
composed update-CS:

```hlsl
float3 SampleAdvection(float3 worldPos) {
    if (advectionFieldSrvIdx == 0xFFFFFFFFu) return float3(0, 0, 0);
    const float3 extent = advectionWorldMax - advectionWorldMin;
    const float3 uvw = (worldPos - advectionWorldMin) / max(extent, 1e-6);
    if (any(uvw < 0.0) || any(uvw > 1.0)) return float3(0, 0, 0);
    Texture3D<float3> field = ResourceDescriptorHeap[advectionFieldSrvIdx];
    return field.SampleLevel(g_trilinearClamp, uvw, 0);
}

// Update-CS dispatch site:
float3 advected = SampleAdvection(particle.position);
float  t        = clamp(advectionTightness, 0.0, 1.0);
particle.velocity = lerp(particle.velocity, advected, t);
```

The four CB fields (`advectionFieldSrvIdx`, `advectionWorldMin`,
`advectionWorldMax`, `advectionTightness`) are declared by the host CB
header and populated from `VfxAdvectionFieldConfig`.

## Authoring workflow

1. Bake the velocity field via the engine's volume-texture authoring tool
   (hand-paint or import from a flow sim like Houdini).
2. Upload as a Texture3D<float3> and register the bindless SRV.
3. Set `worldMin` / `worldMax` to the AABB the field covers in world
   metres.
4. Tune `tightness` to taste (0.3 for loose smoke, 0.8 for rigid flow
   tunnels).

## Tests

`runtime/tests/engine/vfx/test_vfx_advection_field.cpp` — 16 cases:

- Config: default disabled, enabled when SRV + AABB valid, degenerate
  AABB rejected, unbound SRV disables.
- World→field: center = 0.5, corners = 0/1, OOB returns > 1, inside test
  (edges inclusive, outside of [0,1] rejected).
- Blend: out-of-field preserves current; tightness 0 ignores field;
  tightness 1 full follow; tightness 0.5 midpoint; negative / over-range
  tightness clamped.
- HLSL snippet: declares `SampleAdvection`, uses
  `ResourceDescriptorHeap` + `SampleLevel`, guards out-of-bounds →
  `float3(0, 0, 0)`.
