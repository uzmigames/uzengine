# VFX Scene SDF data interface

Sample the scene's global Signed Distance Field for cheap particle
collision and glow-through-geometry effects. Per-mesh SDF bakes (same as
DDGI uses) are aggregated into a world-AABB atlas; particles query
`SampleSceneSDF(worldPos)` and get signed distance + gradient back.

Reference: UE5 `UNiagaraDataInterfaceDistanceField`.

## Why SDF

| Method                     | Cost per particle | Granularity | Caveats                |
|----------------------------|-------------------|-------------|------------------------|
| Depth-buffer raycast (4.23)| expensive         | exact       | Screen-only, view-dependent |
| **Scene SDF (this DI)**    | one 3D sample     | ~voxel-sized | Requires bake, coarse edges |
| Analytic shapes            | cheap             | exact       | Limited to primitives  |

Use SDF for coarse collision (particles bouncing off walls, dust settling
on meshes) and for glow attenuation ("brighter near surfaces").

## Atlas descriptor

```cpp
struct VfxSceneSDFAtlasDesc {
    uint32_t atlasSrvIndex;   // bindless Texture3D<float>
    Vec3     worldMin, worldMax;
    uint32_t resX, resY, resZ;
};
```

- `IsBound()` validates SRV bound + non-degenerate AABB + non-zero res.
- `VfxSceneSDFVoxelSize(desc)` returns world-metres per voxel; authors
  tune resolution to trade memory for edge fidelity.

## Sampling contract

```
float  d = SampleSceneSDF(worldPos)    // negative inside, positive outside
float3 g = GradientSceneSDF(worldPos, eps)  // central diff; ~1 magnitude
```

- Out-of-atlas samples return `1e9` — a magic "very far away" so callers
  don't misclassify empty regions as on-surface (which would happen if
  we returned 0).
- Gradient magnitude is ~1 for a well-formed SDF; tests verify this on
  sphere + box primitives.
- Surface normal = `normalize(gradient)`.

## Collision response

```cpp
bool VfxSceneSDFResolveCollision(
    Vec3& position, Vec3& velocity,
    float distance, const Vec3& gradient,
    float radius, float restitution);
```

- Returns `false` and leaves state alone when `distance >= radius`.
- Push-out: `position += normalize(gradient) · (radius - distance)`.
- Reflection: `velocity -= (1 + restitution) · (velocity · n) · n` when
  `v · n < 0` (moving into the surface). Already-escaping velocity is
  left alone so multiple resolution passes don't re-reflect.
- `restitution = 0` absorbs the normal component (slide), `1` fully
  reflects (bounce). Zero-magnitude gradient falls back to world-Z so a
  pathological inside-perfectly-flat case never crashes.

## HLSL snippet

`VfxSceneSDFEmitHLSLSnippet()` returns the canonical pair:

```hlsl
float SampleSceneSDF(float3 worldPos) {
    if (sdfAtlasSrvIdx == 0xFFFFFFFFu) return 1e9;
    const float3 uvw = (worldPos - sdfWorldMin) / max(sdfWorldMax - sdfWorldMin, 1e-6);
    if (any(uvw < 0.0) || any(uvw > 1.0)) return 1e9;
    Texture3D<float> atlas = ResourceDescriptorHeap[sdfAtlasSrvIdx];
    return atlas.SampleLevel(g_trilinearClamp, uvw, 0);
}

float3 GradientSceneSDF(float3 worldPos, float eps) { /* central diff */ }
```

## Authoring

1. Bake per-mesh SDF bricks offline (DDGI already requires this).
2. Combine into an atlas that covers the gameplay AABB.
3. Upload as Texture3D<float> and register the bindless SRV.
4. Attach a `VfxSceneSDFAtlasDesc` to the emitter config; the update CS
   samples it in the collision module.

## Tests

`runtime/tests/engine/vfx/test_vfx_scene_sdf.cpp` — 20 cases:

- Atlas: default unbound, bound when valid, degenerate AABB, voxel size
  math across three axes.
- Analytic primitives: sphere surface/outside/inside; box surface/corner/
  center (negative inside).
- Gradient: sphere gradient outward-pointing with unit magnitude, box
  gradient on +Y face.
- Collision: no-hit short-circuit, push-out along gradient, reflection
  formula check for e=0/0.5/1, velocity-away-from-surface unchanged,
  zero gradient falls back.
- HLSL snippet: declares both helpers, uses bindless sampler, guards OOB
  with 1e9 sentinel.
