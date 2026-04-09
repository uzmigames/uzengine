## Session 1: Emissive Triangle Extraction + BVH Construction

- [ ] 1.1 Add emissive triangle extraction to `scene_gather.cpp`: iterate all `MeshComponent` + `MaterialComponent` pairs; if `material.emissiveColor` luminance > 0, add all triangles to `EmissiveTriangleList { position[3], normal, emissiveColor, area }`. Cache by scene change flag.
- [ ] 1.2 Implement LBVH construction in `light_bvh.cpp`: compute 30-bit Morton code for each triangle centroid (normalize centroids to [0,1]³ AABB, interleave xyz bits). Sort triangles by Morton code (radix sort or `std::sort` for CPU build).
- [ ] 1.3 Build binary BVH bottom-up from sorted triangles: `LightBVHNode { float3 aabbMin, aabbMax; float3 coneAxis; float coneCosHalfAngle; float totalEmissionPower; uint leftChild, rightChild; uint triangleOffset, triangleCount; }`. Internal nodes aggregate children's AABBs, cones (from Beckmann 1990), and emission power sums.
- [ ] 1.4 Upload `StructuredBuffer<LightBVHNode>` and `StructuredBuffer<EmissiveTriangle>` to GPU via upload heap. `LightBVH::Rebuild()` called when emissive triangle list changes.
- [ ] 1.5 Build + verify: BVH builds for a scene with 100 emissive triangles. Root node's `totalEmissionPower` matches sum of all triangle powers (CPU unit test).

## Session 2: GPU Traversal HLSL + Integration

- [ ] 2.1 Create `include/light_bvh.hlsli` — `SampleLightBVH(float3 shadingPos, float3 shadingNormal, float2 u, StructuredBuffer<LightBVHNode> bvh, StructuredBuffer<EmissiveTriangle> triangles) → LightBVHSample { uint triangleId; float3 samplePos; float3 sampleNormal; float3 emission; float pdf; }`. Iterative (non-recursive) stack-based top-down traversal with 32-depth stack.
- [ ] 2.2 Node importance estimation during traversal: `NodeImportance(node, shadingPos, shadingNormal) = node.totalEmissionPower * ApproximateSolidAngle(node.aabbMin, node.aabbMax, shadingPos) * max(0, dot(shadingNormal, normalize(nodeCentroid - shadingPos)))`. Split `u.x` proportionally between children importance values.
- [ ] 2.3 Leaf sampling: at leaf node, uniformly sample one triangle by area. Sample random point on triangle: `p = (1-√u1)*v0 + √u1*(1-u2)*v1 + √u1*u2*v2`. Compute PDF as `1/(triangleArea * leafSamplingProbability)`.
- [ ] 2.4 `EvaluateLightBVHPDF(float3 shadingPos, float3 shadingNormal, uint triangleId, ...) → float` — trace selection probability from root to the specific triangle node.
- [ ] 2.5 Wire into `restir_initial_cs.hlsl` as the candidate generator when `HAS_LIGHT_BVH=1` define is set. Build + verify: scene with 100 emissive cubes — sampling probability distribution concentrates correctly in front-facing emitters (validate via readback histogram of selected triangle IDs).

## Session 3: Validation + Demo

- [ ] 3.1 `LightBVH::Rebuild()` triggered on emissive mesh change: hook into scene dirty tracking system, call rebuild when any emissive material parameter changes
- [ ] 3.2 Demo: place 100 emissive cubes in a grid at varying intensities. Render with `--restir-di` + light BVH. Verify: brighter cubes receive proportionally more samples (histogram check). Compare convergence rate vs uniform sampling via 100-sample screenshot comparison.
- [ ] 3.3 Build + verify: `--screenshot-stdout` after 64 samples shows correct multi-emitter lighting. No visible sampling bias (no dark patches near bright emitters). GPU timestamp (`--gpu-profile`) confirms traversal cost is sub-millisecond for 100 emitters.

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
