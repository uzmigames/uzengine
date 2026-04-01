# Proposal: phase1_falcor-light-bvh

## Why
UzEngine's light sampling iterates all lights uniformly — O(N) per sample, unsuitable for scenes with 100K+ emissive triangles (neon signs, lava flows, fire, TV screens, stained glass windows). Falcor has `LightBVH.slang` enabling O(log N) importance-weighted light selection. The light BVH organizes emissive triangles into a hierarchy where each node stores the aggregate emission power and solid angle bounds of its subtree, enabling top-down traversal that preferentially explores branches contributing more energy to the shading point. This enables real-time direct lighting from 100K+ emissive triangles at negligible additional cost per sample. Requires: DXR RT infrastructure (for visibility shadow rays). @ref Moreau 2019 "Dynamic Many-Light Sampling".

## What Changes
- Emissive triangle extraction: scan all meshes for materials with `emissive > 0`, build `EmissiveTriangleList` with (centroid, normal, emission color, area) per triangle; CPU-side update on emissive mesh change
- BVH construction (CPU, LBVH algorithm): sort triangles by Morton code, build binary BVH bottom-up from sorted order. Each internal node stores: AABB, cone of triangle normals, total emission power, child offsets. @ref Karras 2012 "Maximizing Parallelism in the Construction of BVHs"
- Upload as `StructuredBuffer<LightBVHNode>` and `StructuredBuffer<EmissiveTriangle>` to GPU
- `include/light_bvh.hlsli`: `SampleLightBVH(float3 shadingPos, float3 shadingNormal, float2 u) → (uint triangleId, float3 samplePos, float pdf)`. Traversal: at each node, compute importance estimate (emission × solid angle × cosine weight), split random variate proportionally between children.
- `EvaluateLightBVHPDF(float3 shadingPos, float3 shadingNormal, uint triangleId) → float` for MIS
- Integration: wire into ReSTIR DI candidate generation as alternative to uniform light sampling; standalone direct lighting fallback
- Affected files: new `light_bvh.h/.cpp`, new `light_bvh.hlsli`, `scene_gather.cpp` (emissive extraction), `restir_initial_cs.hlsl` (candidate generation)

## Impact
- Affected specs: new `docs/specs/rendering/light-bvh.md`
- Affected code: new `light_bvh.h/.cpp`, `light_bvh.hlsli`, scene gather, ReSTIR initial pass
- Breaking change: NO — additive; existing uniform sampling remains as fallback
- User benefit: Scenes with 100K+ emissive triangles become tractable for direct lighting; correct importance distribution means fewer samples needed for converged results; enables photorealistic neon/fire/lava lighting at real-time frame rates
