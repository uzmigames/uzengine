# Clustered Forward+ Lighting

## Priority: P2 — Scalability (8 lights → 100+ lights)

## Summary

Replace brute-force light loops (8 forward, 64 deferred) with GPU-driven clustered light culling. Divide the view frustum into 3D clusters (16×16 screen tiles × depth slices), assign lights to clusters on GPU, then each pixel only evaluates lights in its cluster. Scales from 8 to 1000+ lights at constant cost per pixel.

## Current UzEngine vs UE5

| Aspect | UzEngine | UE5 | Gap |
|--------|----------|-----|-----|
| **Forward max lights** | 8 (CPU sorted) | 100+ (clustered) | ❌ |
| **Deferred max lights** | 64 (brute force loop) | 100+ (tiled/clustered) | ❌ |
| **Culling** | CPU distance sort | GPU compute tile culling | ❌ |
| **Depth slicing** | ❌ None | ✅ Logarithmic depth clusters | ❌ |
| **Light list** | Flat array in CB | Per-cluster light list UAV | ❌ |

## UE5 Reference (ClusteredDeferredShadingPass.usf)
- **Grid**: 16×16 pixel tiles × 32 depth slices (logarithmic distribution)
- **Light assignment**: Compute shader tests each light AABB against each cluster AABB
- **Per-cluster list**: Global light index list + per-cluster offset/count (compact)
- **Evaluation**: Pixel shader reads cluster from (tileX, tileY, depthSlice) → iterate only local lights

## Impact

- **Compute shaders**: 1 light assignment compute shader
- **Buffers**: Cluster grid (structuredBuffer), light index list, per-cluster offset/count
- **Performance**: 0.1ms for culling, 0-cost per skipped light in shading
- **Scale**: Support 100+ dynamic lights at interactive rates

## Acceptance Criteria

1. Scene with 100 point lights renders at 60fps
2. Each pixel evaluates only lights that affect its cluster (not all lights)
3. Debug visualization: cluster grid with light count heat map
4. Performance scales linearly with visible lights, not total lights
5. Demo: 100 colored point lights in a scene, smooth 60fps
