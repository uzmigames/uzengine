# GPU Instancing Spec

## Overview

UzEngine uses **dynamic GPU instancing** to batch draw calls for entities that share the same mesh and material. Instead of issuing one `DrawIndexedInstanced(N, 1, ...)` per entity, the renderer groups identical mesh+material pairs and issues a single `DrawIndexedInstanced(N, instance_count, ...)`. World transforms are uploaded to a `StructuredBuffer<float4x4>` and indexed by `SV_InstanceID` in the vertex shader.

This is **dynamic batching only** -- transforms are rebuilt every frame from ECS data. There is no static mesh merging (Unity-style static batching).

---

## When to Batch

Two entities are eligible for the same instance batch when:

1. **Same mesh handle** (`Handle<MeshBuffer>` equality).
2. **Same material handle** (`Handle<Material>` equality).
3. **Same shader variant key** (same set of active defines).
4. **Same render pass** (both in GBuffer pass, or both in shadow pass, etc.).

Entities with different materials, even if they use the same shader, are NOT batched together because they have different cbuffer data (albedo, roughness, etc.).

---

## Data Structures

### InstancedDrawCall

```cpp
// src/renderer/instancing/instanced_draw.h

struct InstancedDrawCall {
    Handle<MeshBuffer>  mesh;               // GPU mesh (VB + IB)
    Handle<Material>    material;           // material (shader + params)
    ShaderVariantKey    variant_key;        // active defines hash
    uint32_t            instance_count;     // number of instances in this batch
    GpuBuffer*          transform_buffer;   // StructuredBuffer<float4x4> on GPU
    uint32_t            transform_offset;   // byte offset into transform buffer
};
```

### BatchKey

```cpp
// Used for grouping in the hash map
struct BatchKey {
    Handle<MeshBuffer>  mesh;
    Handle<Material>    material;
    ShaderVariantKey    variant_key;

    uint64_t hash() const;
    bool operator==(const BatchKey& o) const;
};
```

---

## CPU-Side Batching

### BatchBuilder

The `BatchBuilder` runs once per frame, after frustum culling, to group visible entities into instanced draw calls.

```cpp
// src/renderer/instancing/batch_builder.h

struct BatchBuilder {
    // Input: list of visible CMeshRenderer + CTransform pairs
    // Output: list of InstancedDrawCall ready for GPU submission
    Array<InstancedDrawCall> build(
        Span<const VisibleEntity> visible_entities,
        Arena* frame_arena          // per-frame allocator for transform arrays
    );

private:
    // Temporary grouping map (cleared each frame)
    HashMap<BatchKey, Array<mat4>> groups;
};
```

### Algorithm

```
batch_draw_calls(visible_entities) -> Array<InstancedDrawCall>:

1. Clear grouping map.
2. For each visible entity:
   a. Compute BatchKey from {mesh, material, variant_key}.
   b. Append entity.transform.world (mat4) to groups[key].
3. For each group in map:
   a. Allocate InstancedDrawCall from frame_arena.
   b. Upload transforms to GPU via upload ring buffer.
   c. Set instance_count = group.size().
4. Return array of InstancedDrawCall.
```

This runs on the main thread after culling, before GPU command recording. The frame arena is reset at the end of each frame, so all batch data is automatically freed.

---

## GPU Upload

### Transform StructuredBuffer

Each batch uploads an array of `float4x4` world transforms to GPU memory:

```cpp
// Per-batch upload
GpuBuffer* upload_transforms(Span<const mat4> transforms, UploadRingBuffer* ring) {
    size_t size = transforms.size() * sizeof(mat4);  // 64 bytes per matrix
    GpuBuffer* buf = ring->allocate(size);
    memcpy(buf->mapped_ptr, transforms.data(), size);
    return buf;
}
```

The upload ring buffer is the same per-frame ring used for dynamic cbuffers (see `gpu_resources/upload_ring.h`). Each frame's allocation is invalidated at frame end.

### SRV Binding

The transform buffer is bound as an SRV in the descriptor table:

```
Root Param 2: Descriptor Table
  t0: albedo_texture
  t1: normal_texture
  ...
  t7: instance_transforms (StructuredBuffer<float4x4>)
```

The slot `t7` is reserved for the instance transform buffer when `#define INSTANCED` is active.

---

## Shader Side

### Vertex Shader with Instancing

```hlsl
// In pbr.hlsl (when INSTANCED is defined)

#ifdef INSTANCED
StructuredBuffer<float4x4> g_InstanceTransforms : register(t7);
#else
cbuffer PerObject : register(b0) {
    float4x4 model;
};
#endif

struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
#ifdef INSTANCED
    uint instance_id : SV_InstanceID;
#endif
};

VSOut VSMain(VSIn v) {
    VSOut o;

#ifdef INSTANCED
    float4x4 world = g_InstanceTransforms[v.instance_id];
#else
    float4x4 world = model;
#endif

    float4 world_pos = mul(world, float4(v.pos, 1.0));
    o.pos    = mul(view_proj, world_pos);
    o.normal = mul((float3x3)world, v.normal);
    o.uv     = v.uv;
    o.world  = world_pos.xyz;
    return o;
}
```

### Shader Variant

The `INSTANCED` define is added automatically by the variant system (see [shader-variants.md](shader-variants.md)) when the draw call is an instanced batch. This means:

- **Instanced path**: Reads world transform from `StructuredBuffer` via `SV_InstanceID`.
- **Non-instanced path**: Reads world transform from per-object `cbuffer b0`.

Both paths produce identical visual output. The shader compiler generates separate PSOs for each variant.

---

## Draw Call Submission

### Instanced Draw

```cpp
void submit_instanced_draw(ID3D12GraphicsCommandList* cmd,
                           const InstancedDrawCall& draw) {
    // Bind mesh VB + IB
    cmd->IASetVertexBuffers(0, 1, &draw.mesh->vbv);
    cmd->IASetIndexBuffer(&draw.mesh->ibv);

    // Bind material PSO (INSTANCED variant)
    cmd->SetPipelineState(draw.material->pso_instanced);

    // Bind transform SRV at t7
    cmd->SetGraphicsRootDescriptorTable(2, draw.transform_srv_gpu_handle);

    // Single instanced draw call for all entities in this batch
    cmd->DrawIndexedInstanced(
        draw.mesh->index_count,
        draw.instance_count,    // N instances
        0, 0, 0
    );
}
```

### Fallback (Non-Instanced)

When `instance_count == 1`, the batch builder can use either path. For simplicity, single-instance batches use the standard per-object cbuffer path (no performance difference):

```cpp
if (draw.instance_count == 1) {
    submit_single_draw(cmd, draw);   // uses b0 cbuffer, no SV_InstanceID
} else {
    submit_instanced_draw(cmd, draw);
}
```

---

## Integration with Render Passes

### GBuffer Pass

The GBuffer pass calls `batch_builder.build(visible_opaque)` and submits all instanced draw calls. Each draw call writes to the same GBuffer targets (albedo, normal, material).

### Shadow Pass

Shadow passes also benefit from instancing. The same `BatchBuilder` is run on the shadow draw list:

```cpp
Array<InstancedDrawCall> shadow_batches = batch_builder.build(shadow_casters);
for (auto& draw : shadow_batches) {
    // Use depth-only PSO variant (INSTANCED + depth_only)
    submit_instanced_shadow_draw(cmd, draw, cascade_vp);
}
```

### Transparency Pass

Transparent objects are sorted back-to-front for correct blending. Instancing is NOT applied to transparent objects because sorting requires per-object depth ordering. Each transparent entity is drawn individually.

---

## Performance Characteristics

### Batch Statistics

| Metric | Without instancing | With instancing | Improvement |
|--------|-------------------|-----------------|-------------|
| 1000 identical cubes | 1000 draw calls | 1 draw call | 1000x fewer draw calls |
| 100 trees (5 variants) | 100 draw calls | 5 draw calls | 20x fewer draw calls |
| Mixed scene (50 unique) | 50 draw calls | 50 draw calls | No change (all unique) |

### Memory Cost

- **Transform buffer per batch**: `instance_count * 64 bytes` (one mat4 per instance).
- **1000 instances**: 64 KB (fits easily in upload ring buffer).
- **Upload ring budget**: 16 MB per frame (shared with cbuffers). Instancing uses a small fraction.

### CPU Cost

- `BatchBuilder::build()` is O(N) where N = visible entity count.
- HashMap grouping: O(1) amortized per entity.
- Total batching overhead: < 0.1ms for 10,000 entities on modern CPU.

### GPU Cost

- Instanced draws reduce command processor overhead (fewer state changes).
- No additional GPU memory bandwidth vs. non-instanced (same vertex data, same material).
- `SV_InstanceID` indexing into `StructuredBuffer` has negligible cost.

---

## Limitations

1. **No static batching**: All batching is dynamic (per-frame). Static mesh merging is not implemented.
2. **No per-instance material data**: All instances in a batch share the same material parameters. Per-instance color or other properties would require a separate `StructuredBuffer` (future enhancement).
3. **Transparent objects excluded**: Instancing is only applied to opaque and shadow passes.
4. **Skinned meshes excluded**: Skinned meshes have per-instance bone palettes which prevent simple instancing. They use a separate `StructuredBuffer<float4x4>` for bones (Phase 3).

---

## IPC Handlers

```json
renderer.batchStats {}
                --> {
                      "total_entities": 1200,
                      "instanced_batches": 45,
                      "instanced_entities": 980,
                      "single_draws": 220,
                      "draw_call_reduction": "81.6%"
                    }
```

---

## Configuration (project.uzproj)

```toml
[renderer.instancing]
enabled = true                     # global instancing toggle
min_instances_to_batch = 2         # minimum instance count to use instanced path
max_instances_per_batch = 4096     # safety limit per single draw call
transform_buffer_budget_mb = 4     # max MB for transform uploads per frame
```

---

## Related Specs

- [shader-variants.md](shader-variants.md) -- `INSTANCED` define and variant key system
- [renderer.md](renderer.md) -- render pipeline and pass ordering
- [shader-library.md](shader-library.md) -- shared HLSL includes used by instanced shaders
