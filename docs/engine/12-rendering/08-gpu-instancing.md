# GPU Instancing — Draw Call Collapse via Instance Batching

<!-- doc-meta
title: GPU Instancing System
category: reference
theme: rendering
created: 2026-03-15
updated: 2026-03-15
status: active
author: docs-operator
-->

## Overview

GPU instancing collapses hundreds of identical mesh+material combinations into single `DrawIndexedInstanced` calls via the **InstanceBatcher**. After the draw list is sorted by the 64-bit sort key, consecutive commands with the same mesh and material texture indices are merged into `InstanceBatch` structures, each issued as one GPU draw call with multiple instances.

This document covers the data structures, instance buffer layout, shader integration, and performance characteristics of the instancing system.

## InstanceData Struct — Per-Instance Transforms

Each instance carries its own **world matrix** and **normal matrix** in a GPU `StructuredBuffer<InstanceData>`.

```cpp
struct InstanceData {
    Mat4 world;            // Object-to-world transform (64 bytes)
    Mat4 normalMatrix;     // transpose(inverse(world)) for lighting (64 bytes)
};

// Total: 128 bytes per instance (two Mat4 matrices)
static_assert(sizeof(InstanceData) == 128);
```

### GPU Equivalent (HLSL)

```hlsl
struct InstanceData {
    row_major float4x4 world;         // 64 bytes
    row_major float4x4 normalMatrix;  // 64 bytes
};

StructuredBuffer<InstanceData> instances : register(t0);

// Vertex shader accesses per-instance data via SV_InstanceID
uint instanceID = input.instanceID;  // Built-in instance index (0 to instanceCount-1)
InstanceData inst = instances[instanceID];
float4x4 worldMatrix = inst.world;
float4x4 normalMatrix = inst.normalMatrix;
```

### Per-Instance Buffer Management

| Aspect | Details |
|--------|---------|
| **Layout** | Power-of-two StructuredBuffer capacity (1024, 2048, 4096, ...) |
| **Heap placement** | Upload heap (CPU-writable, GPU-readable) |
| **Resize strategy** | If instance count exceeds capacity, allocate 2× capacity and reupload all instances |
| **Max instances per frame** | 16,384 (`kMaxInstances` constant in instance_batcher.h) |
| **Binding** | Root constant: `instanceBufferIndex` (SRV descriptor heap index) |
| **Access pattern** | Vertex shader reads via `ResourceDescriptorHeap[push.instanceBufferIndex][SV_InstanceID]` |

## InstanceBatch — Single Batched Draw Call

```cpp
struct InstanceBatch {
    uint32_t meshId;              // Mesh resource (shared by all instances)
    uint32_t firstInstance;       // Offset into InstanceData buffer
    uint32_t instanceCount;       // Number of instances in this batch

    // Shared material SRV indices (all instances use same textures)
    uint32_t albedoSrvIndex;
    uint32_t normalSrvIndex;
    uint32_t roughnessSrvIndex;
    uint32_t metallicSrvIndex;
    uint32_t aoSrvIndex;

    // SM5 fallback texture handles (ignored on SM6.6 bindless)
    RHITextureHandle albedoTexture;
    RHITextureHandle normalTexture;
    RHITextureHandle roughnessTexture;
    RHITextureHandle metallicTexture;
    RHITextureHandle aoTexture;
};
```

### Batch Merging Criteria

Two consecutive sorted `DrawCommand` objects merge into the same `InstanceBatch` if **ALL** of these match:

```cpp
// From instance_batcher.cpp
static bool IsSameBatch(const DrawCommand& a, const DrawCommand& b) noexcept
{
    return a.meshId              == b.meshId
        && a.albedoSrvIndex      == b.albedoSrvIndex
        && a.normalSrvIndex      == b.normalSrvIndex
        && a.roughnessSrvIndex   == b.roughnessSrvIndex
        && a.metallicSrvIndex    == b.metallicSrvIndex
        && a.aoSrvIndex          == b.aoSrvIndex;
}
```

**Why these criteria?** Material SRV indices are shared across all instances in a batch (stored in a single `PerObjectCB`), so they must be identical. Mesh ID is shared because one batch = one index buffer. Only the per-instance transforms differ.

## BuildInstanceBatches — Core Batching Function

```cpp
/// Groups a sorted DrawList into InstanceBatch structures.
///
/// Scans the sorted command list and merges consecutive commands that share
/// the same mesh and material texture indices. Fills outInstances with
/// per-instance data (transforms) for GPU upload.
void BuildInstanceBatches(const DrawList& drawList,
                          std::vector<InstanceBatch>& outBatches,
                          std::vector<InstanceData>& outInstances);
```

### Algorithm

1. Clear both output vectors
2. Initialize first batch from first command (at `firstInstance = 0`)
3. For each subsequent command:
   - Add its transform data to `outInstances`
   - If mesh + material textures match the previous command, increment `instanceCount`
   - Otherwise, start a new batch at the current instance offset
4. Return when all commands processed or limit (`kMaxInstances`) exceeded

### Limits

| Parameter | Value | Notes |
|-----------|-------|-------|
| `kMaxInstances` | 16,384 | Max instance count per frame; excess commands logged as warning and dropped |
| Instance buffer capacity | Power-of-two | Reallocated 2× on demand (e.g., 1024 → 2048) |
| Batch reserve estimate | `commandCount / 4` | Conservative; many commands typically merge |

**Drop behavior**: If `commandCount > kMaxInstances`, a warning is logged and the remaining commands are silently dropped. This prevents GPU stalls from oversized buffers.

## Shader Integration — Bindless Path (SM6.6)

### Root Constants Layout

The root signature includes a new field in push constants:

```hlsl
struct PushConstants {
    uint vertexBufferIndex;     // SRV index for StructuredBuffer<Vertex>
    uint instanceBufferIndex;   // SRV index for StructuredBuffer<InstanceData> (NEW)
};
ConstantBuffer<PushConstants> push : register(b0);
```

### Vertex Shader Access Pattern

```hlsl
#ifdef UZ_INSTANCED
    // Bindless + instancing: read both vertex and instance data from descriptor heap
    StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[push.vertexBufferIndex];
    StructuredBuffer<InstanceData> instances = ResourceDescriptorHeap[push.instanceBufferIndex];

    Vertex vert = vb[vertexID];
    InstanceData inst = instances[instanceID];

    float4x4 worldMatrix = inst.world;
    float4x4 normalMtx = inst.normalMatrix;

    float3 worldPos = mul(float4(vert.position, 1.0), worldMatrix).xyz;
    float3 worldNorm = normalize(mul(vert.normal, (float3x3)normalMtx));

#elif defined(UZ_BINDLESS)
    // Bindless only (non-instanced): original per-object CB path
    StructuredBuffer<Vertex> vb = ResourceDescriptorHeap[push.vertexBufferIndex];
    Vertex vert = vb[vertexID];

    float4x4 worldMatrix = perObject.worldMatrix;
    // ...

#else
    // SM5 fallback: fixed register binding (no instancing)
    StructuredBuffer<Vertex> vb : register(t0);
    Vertex vert = vb[vertexID];
    // ...
#endif
```

### Pixel Shader Material Access

Material SRV indices are stored in `PerObjectCB` (same as non-instanced path):

```hlsl
ConstantBuffer<PerObjectCB> perObject : register(b1);

// All instances in a batch share the same material textures
Texture2D albedoMap   : register(t100);  // or dynamic via SRV index
Texture2D normalMap   : register(t101);
Texture2D roughnessMap : register(t102);
Texture2D metallicMap : register(t103);
Texture2D aoMap       : register(t104);
```

## Shader Variants

The instancing system requires separate PSOs for instanced vs. non-instanced rendering:

| Variant | Define | Usage | Root Signature |
|---------|--------|-------|-----------------|
| **UZ_INSTANCED** | `-DUZ_INSTANCED` | Instanced forward/shadow passes | Extended with `instanceBufferIndex` root constant |
| **Default** | (none) | Non-instanced (fallback) | Original root signature |

### PSO Registration

```cpp
// In PipelineLibrary
RHIPipelineHandle gbufferPso = CreateGraphicsPipeline({
    .shader = shaderManager.Get("mesh_vs.hlsl", "mesh_ps.hlsl"),
    .defines = {"-DUZ_DEFERRED", "-DUZ_BINDLESS"},
    .rootSig = rootSig_deferred,
});

RHIPipelineHandle gbufferInstancedPso = CreateGraphicsPipeline({
    .shader = shaderManager.Get("mesh_vs.hlsl", "mesh_ps.hlsl"),
    .defines = {"-DUZ_DEFERRED", "-DUZ_BINDLESS", "-DUZ_INSTANCED"},  // NEW
    .rootSig = rootSig_deferred_instanced,  // Extended root signature
});
```

## Fallback Paths

### SM5 (Non-Bindless)

SM5 devices cannot use the descriptor heap array indexing pattern. Instancing is **disabled** on SM5; instances are rendered as separate non-instanced draw calls:

```cpp
// SM5 fallback: ignore instance batching, use original per-object CB loop
if (!capabilities.supportsBindless) {
    // Original loop: one DrawIndexed per command
    for (const auto& cmd : drawList.GetCommands()) {
        IssueDrawCall(cmd);  // No instancing
    }
} else {
    // SM6.6 instancing path (as above)
    BuildInstanceBatches(drawList, batches, instances);
    // ... issue DrawIndexedInstanced for each batch
}
```

### No UZ_INSTANCED Define

If the shader is not compiled with `-DUZ_INSTANCED`, it falls back to per-object CB binding (non-instanced). This preserves backward compatibility:

```hlsl
// Shader compiled without -DUZ_INSTANCED
float4x4 worldMatrix = perObject.worldMatrix;  // Per-object CB, not per-instance SB
```

## Performance Characteristics

### Benchmark: 1001 Identical Cubes (Stonebricks Material)

| Metric | Without Instancing | With Instancing | Improvement |
|--------|-------------------|-----------------|-------------|
| **Draw calls** | 1,001 | 1–2 | **500–1000×** |
| **Root constant updates** | 1,001 | 1–2 | **500–1000×** |
| **Frame time** | 3.22ms | 3.22ms | ~0% (GPU-bound) |
| **FPS** | 310 | 310 | ~0% (limited by GPU, not draw calls) |
| **Overhead** | ~0.1–0.5ms per 1000 draw calls | Negligible | Scalable to 10k+ draws |

**Key insight**: On modern GPUs, draw call overhead is minimal (submicrosecond per call on DX12). The real win is **reduced state validation** (PSO/descriptor table checks) and **better GPU cache locality** when batching.

### Batching Reduction Example

For a scene with 5000 draw commands:

```
Before batching:
  5000 PSO changes (rare) + 5000 descriptor rebinds + 5000 VB/IB rebinds

After batching (assuming 1001 identical cubes, then various other meshes):
  ~10 batches total (each representing 100–500 instances merged)
  10 PSO changes + ~50 descriptor rebinds + ~10 VB/IB rebinds
  → 99.6% reduction in state transitions
```

## Thread Safety

`BuildInstanceBatches()` is **NOT thread-safe**. It must be called from the render thread after the draw list has been populated and sorted.

```cpp
// Valid: render thread only
drawList.Sort();
BuildInstanceBatches(drawList, batches, instances);

// Invalid: race condition
std::thread t([&] {
    BuildInstanceBatches(drawList, batches, instances);  // NOT OK — data races
});
t.join();
```

## Integration Checklist

When implementing instancing for a new render pass:

- [ ] Define new PSO variant with `-DUZ_INSTANCED`
- [ ] Extend root signature to include `instanceBufferIndex` root constant
- [ ] Allocate `InstanceBuffer` (StructuredBuffer on Upload heap)
- [ ] Call `BuildInstanceBatches()` after `drawList.Sort()`
- [ ] Upload `outInstances` to GPU before rendering
- [ ] Bind `instanceBufferIndex` root constant for each batch
- [ ] Issue `DrawIndexedInstanced()` instead of `DrawIndexed()`
- [ ] Verify shader compiles with `-DUZ_INSTANCED -DUZ_BINDLESS`
- [ ] Test on both DX12 (SM6.6) and SM5 fallback paths
- [ ] Profile: measure draw call and state transition reduction

## Known Limitations

1. **Material sharing**: All instances in a batch must use the same material (textures). Multi-material instances require separate batches.
2. **SM5 unsupported**: SM5 devices fall back to non-instanced rendering (no performance gain).
3. **Instance limit**: 16,384 instances per frame; excess commands are dropped with a warning.
4. **Texture atlas required**: 256+ unique materials exceed the 8-bit material ID limit; use material atlasing or bindless indexing.
5. **No GPU-driven batching**: Batching is CPU-driven via `BuildInstanceBatches()`. GPU-driven indirect draws are a future enhancement.

## References

- [07-sort-key-architecture.md](07-sort-key-architecture.md) — Sort key layout and batching strategy
- [06-pbr-material-system.md](06-pbr-material-system.md) — PBRMaterial struct and GPU layout
- Source: `runtime/engine/renderer/core/instance_batcher.h` / `.cpp`
- Source: `runtime/shaders/runtime/geometry/mesh_vs.hlsl` — Vertex shader instancing integration
- Source: `runtime/shaders/runtime/lighting/shadow_depth_vs.hlsl` — Shadow depth pass instancing
- Demo: `runtime/demos/instancing_test/` — 1000-cube stress test

## Changelog

| Date | Change |
|------|--------|
| 2026-03-15 | Initial creation; InstanceBatcher implementation, SM6.6 bindless integration, 1001-cube benchmark |
