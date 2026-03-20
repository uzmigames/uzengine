<!-- doc-meta
title: GPU Mesh SDF Bake — Data Flow
task: phase0-3.0.2
created: 2026-03-19
status: complete
-->

# GPU Mesh SDF Bake — Data Flow

## Pipeline Overview

```
CPU: vertex/index data ──► GPUTriangle StructuredBuffer (Upload heap)
                                │
                                ▼
GPU: mesh_sdf_bake_cs.hlsl [numthreads(4,4,4)]
     ├── Root constants (b0): outputUav, triSrv, triCount, resolution, volumeMin, voxelSize
     ├── Input: ResourceDescriptorHeap[g_trianglesSrv] → StructuredBuffer<GPUTriangle>
     ├── Algorithm: brute-force closest-point-on-triangle (Ericson 5.1.5) per voxel
     └── Output: ResourceDescriptorHeap[g_outputUav] → RWTexture3D<float> (R16F)
                                │
                                ▼
MeshSDF handle (srvIndex, volumeOrigin, volumeExtent, resolution, invTransform[12])
     │
     ▼
GDF Composite (global_sdf_composite_cs.hlsl) reads MeshSDF textures via bindless
     │
     ▼
Radiance Cache Trace (radiance_cache_trace_cs.hlsl) sphere-marches through GDF
     │
     ▼
Deferred Lighting (deferred_lighting_ps.hlsl) interpolates probe SH for ambient
```

## Stage Details

### 1. CPU — AABB + Triangle Upload

| Step | Code | What |
|------|------|------|
| Compute AABB | `BakeMeshGPU()` mesh_sdf_baker.cpp | Iterate vertices, find min/max, expand 10% + 1cm padding |
| Compute voxelSize | `BakeMeshGPU()` | `maxExtent / resolution` |
| Build GPUTriangle[] | `BakeMeshGPU()` | 64 bytes/tri: v0,v1,v2 (float3+pad) + precomputed normal |
| Upload | `CreateBuffer(Upload)` + `MapBuffer` | StructuredBuffer, stride=64, SRV allocated |

### 2. GPU — Compute Dispatch

| Parameter | Value |
|-----------|-------|
| Shader | `shaders/runtime/gi/mesh_sdf_bake_cs.hlsl` |
| Entry | `main` |
| Thread group | `[numthreads(4,4,4)]` = 64 threads |
| Dispatch | `ceil(res/4)^3` groups (32^3 → 8x8x8 = 512 groups) |
| Root constants (b0) | 8 uint32s: outputUav, triSrv, triCount, resolution, volumeMinXYZ, voxelSize |

### 3. Per-Voxel Algorithm

```
voxelCenter = volumeMin + (threadId + 0.5) * voxelSize
minDistSq = FLT_MAX
for each triangle:
    closestPt = ClosestPointOnTriangle(voxelCenter, tri.v0, tri.v1, tri.v2)
    distSq = lengthSq(voxelCenter - closestPt)
    if distSq < minDistSq:
        minDistSq = distSq
        bestNormal = tri.normal
        bestTriIdx = i
// Recompute closest point on winning triangle for sign
closestPt = ClosestPointOnTriangle(voxelCenter, bestTri.v0, bestTri.v1, bestTri.v2)
sign = dot(voxelCenter - closestPt, bestNormal) >= 0 ? +1 : -1
output[threadId] = sign * sqrt(minDistSq)
```

### 4. Output

- R16F 3D texture (resolution^3 texels)
- GPU handles float→float16 conversion natively on UAV store
- Transition: Undefined → UAV (compute write) → SRV (subsequent reads)
- Staging buffer destroyed after `WaitIdle()` (async fence TODO in task 4)

## Performance

| Mesh | Triangles | Resolution | GPU Time (RTX 4090) | CPU Time |
|------|-----------|------------|---------------------|----------|
| Cube | 12 | 32^3 | <1ms | 84ms |
| Sphere | 3968 | 32^3 | ~6ms | 15,540ms |
| **Speedup** | | | | **2500x** |

## Files

| File | Role |
|------|------|
| `shaders/runtime/gi/mesh_sdf_bake_cs.hlsl` | Compute shader (self-contained) |
| `engine/renderer/gi/mesh_sdf_baker.h` | MeshSDF struct, BakeMeshGPU() declaration |
| `engine/renderer/gi/mesh_sdf_baker.cpp` | BakeMeshGPU() implementation |
| `engine/renderer/core/pipeline_library.h` | GetMeshSdfBake() declaration |
| `engine/renderer/core/pipeline/pipeline_postprocess.cpp` | Pipeline creation |
| `engine/renderer/scene/scene_assets.cpp` | Call site (GPU first, CPU fallback) |
