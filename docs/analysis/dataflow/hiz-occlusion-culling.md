<!-- doc-meta
title: Data Flow — HiZ Occlusion Culling Integration
category: analysis/dataflow
created: 2026-03-24
-->

# Data Flow: HiZ Occlusion Culling Integration

## Pipeline

```
BuildInstanceBatches() → ComputeBoundingSpheres() → Upload GPU
    → DepthPrepass → HiZ Pyramid → GPU Cull Pass
    → GBuffer (indirect draw, visible instances only)
```

## Stages

| Stage | File | Function | Status |
|-------|------|----------|--------|
| 1. Instance batches | frame_setup.cpp:730 | BuildInstanceBatches() | EXISTS |
| 2. Bounding spheres | frame_setup.cpp (new) | ComputeBoundingSpheres() | NEEDS IMPL |
| 3. Upload spheres | frame_setup.cpp (new) | MapBuffer/memcpy | NEEDS IMPL |
| 4. Depth prepass | frame_geometry.cpp | AddDepthPrepass() | EXISTS |
| 5. HiZ pyramid | frame_geometry.cpp | AddHiZPyramidPass() | EXISTS |
| 6. GPU cull pass | frame_geometry.cpp:78 | AddGpuCullPass() | EXISTS (not wired) |
| 7. GBuffer indirect | frame_geometry.cpp | ExecuteIndirect() | NEEDS IMPL |

## New GPU Resources (triple-buffered)

| Resource | Type | Size | SRV/UAV |
|----------|------|------|---------|
| BoundingSphereBuffer | StructuredBuffer<float4> | 16B × instances | SRV (t0 input) |
| VisibleIndexBuffer | AppendStructuredBuffer<uint> | 4B × instances | UAV (u0 output) |
| IndirectArgsBuffer | RWByteAddressBuffer | 20B | UAV (u1 output) |

## Bounding Sphere Computation

```
For each instance i in instanceDataVec:
  mesh = m_meshes[batch.meshId]
  center_os = (mesh.boundsMin + mesh.boundsMax) * 0.5
  radius_os = length(mesh.boundsMax - center_os)
  center_ws = instance.world * float4(center_os, 1.0)
  scale = max(length(col0), length(col1), length(col2)) of instance.world
  radius_ws = radius_os * scale
  spheres[i] = float4(center_ws.xyz, radius_ws)
```

## GpuCullConstants Fill

```cpp
GpuCullConstants constants;
constants.viewProjection = camera.viewProj;
constants.viewMatrix = camera.view;
constants.viewToClip = camera.projection;
constants.instanceCount = instanceCount;
constants.hizEnabled = hizMipCount > 0 ? 1 : 0;
constants.hizWidth = hizMipLevels[0].width;
constants.hizHeight = hizMipLevels[0].height;
constants.hizMipCount = hizMipCount;
for (i = 0; i < hizMipCount; i++)
    constants.hizMipSrvIndices[i] = hizMipLevels[i].srvIndex;
```

## Changes Required (in order)

1. `scene_renderer.h` — add GPU cull buffer members + CVar
2. `scene_lifecycle.cpp` — create/destroy cull buffers
3. `frame_setup.cpp` — compute bounding spheres + upload + fill cull constants
4. `frame_geometry.cpp` — register AddGpuCullPass + indirect draw path
