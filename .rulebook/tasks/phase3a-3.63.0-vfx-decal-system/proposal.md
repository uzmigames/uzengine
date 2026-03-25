# VFX Decal System — Deferred Decals, Projection, Atlasing, Pooling

## Summary

Implement the deferred decal system for UzEngine: box-projected decals that modify the G-Buffer (albedo, normal, roughness) post-render, a decal pooling system for efficient reuse, texture atlasing to minimize state changes, and fade curves for lifetime management. Used for bullet holes, blood stains, scorch marks, footprints, and environmental damage marks.

## Why

Decals are essential for environmental storytelling and gameplay feedback:

1. **Visual feedback**: Bullet impacts, explosion damage, footprints make the world feel interactive
2. **Deferred rendering**: Modify G-Buffer directly — no mesh manipulation, works on any surface
3. **Performance**: Box projection is cheap per-pixel, pooling avoids allocation spikes
4. **Scale**: Support 500+ simultaneous decals with atlased textures and efficient sorting

## UE5 Reference (Clean-Room)

### UDecalComponent Architecture
1. **Deferred decal pass**: Renders after G-Buffer, before lighting. Each decal is a box mesh — pixel shader tests if fragment is inside the box, then projects UV onto decal texture and blends with G-Buffer.
2. **Sort order**: Decals sorted by priority then by distance. Higher priority renders later (on top).
3. **Blend modes**: UE5 supports translucent, stain (multiply), emissive, modulate. UzEngine implements Opaque, Additive, Multiply, Screen.
4. **Normal blending**: Decal normals blended with surface normals using adjustable intensity.
5. **Fade**: Distance-based and lifetime-based fading. Both supported.

## Impact

- **New files**: `runtime/engine/vfx/decals/` — decal component, pool, atlas, manager
- **New shaders**: `runtime/shaders/vfx/decal_deferred_ps.hlsl` — deferred decal pixel shader
- **New render pass**: Deferred decal pass (post G-Buffer, pre-lighting)
- **Performance target**: 100 active decals < 0.5ms

## Dependencies

- G-Buffer access (✅ exists) — albedo, normal, roughness, depth textures
- Render graph (✅ exists) — pass scheduling
- RHI texture creation (✅ exists) — atlas texture
- ECS component system (✅ exists) — DecalComponent

## Acceptance Criteria

1. DecalComponent ECS component with position, rotation, half_extents, material, lifetime, fade_curve, blend_mode, sort_order
2. Deferred decal pass renders after G-Buffer: box mesh → pixel shader tests containment → projects UV → blends with G-Buffer
3. Four blend modes: Opaque (replace), Additive (add), Multiply (multiply), Screen (1-(1-a)*(1-b))
4. Normal blending with adjustable intensity (0=no decal normal, 1=full decal normal)
5. Roughness override (optional, per-decal)
6. Lifetime-based fade using .uzcurve fade curves
7. DecalPool: object pool for reusing decal components, configurable pool size
8. DecalAtlas: packs multiple decal textures into shared atlas, reduces texture switches
9. DecalManager: centralized update (lifetime, fade) and cleanup of expired decals
10. Frustum culling: skip decals outside camera frustum
11. Demo: bullet impact decals on walls — shoot to place decals, fade over time
12. All unit tests pass, 95%+ coverage
