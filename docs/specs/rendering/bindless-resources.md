# Bindless Resources Spec

> **Status**: Planned (Phase 2)
> **Priority**: P0 — Rendering Scalability Blocker
> **Rationale**: The current root signature has fixed register slots (t0–t7 for SRVs, b0–b3 for CBVs). A scene with 1000 unique materials requires 1000 draw calls with descriptor table rebinds. GPU-driven rendering, material batching, and bindless texturing all require a descriptor heap–based bindless architecture. Must be designed before Phase 2 renderer to avoid a full root signature rewrite later.

---

## Overview

**Bindless Resources** eliminate per-draw descriptor binding by:
1. Uploading all texture/buffer descriptors into a large descriptor heap
2. Passing only a **material index** (uint32) per draw call
3. Shaders index into the heap directly via `ResourceDescriptorHeap[index]`

This enables:
- **Material batching**: 1000 materials → 1 draw call (instancing + bindless)
- **GPU-driven rendering**: GPU generates draw calls without CPU rebinding
- **Texture streaming**: Add/remove textures from heap at runtime without pipeline recompilation

---

## DX12 Descriptor Heap Layout

```
CBV_SRV_UAV Heap (shader-visible, 1M entries)
┌──────────────────────────────────────────────────────────┐
│ [0-99]    Reserved / null descriptors                    │
│ [100-...]  Static textures (imported assets)             │
│ [200000+] Dynamic (render targets, streaming)            │
│ [500000+] UAVs (compute write targets)                   │
│ [900000+] CBVs (constant buffers)                        │
└──────────────────────────────────────────────────────────┘

Sampler Heap (shader-visible, 2048 entries — DX12 limit)
┌──────────────────────────────────────────────────────────┐
│ [0]   LinearWrap        [4]  PointClamp                  │
│ [1]   LinearClamp       [5]  Anisotropic16x              │
│ [2]   PointWrap         [6]  Shadow (PCF)                │
│ [3]   LinearMirror      [7-N] Custom samplers            │
└──────────────────────────────────────────────────────────┘
```

---

## C++ API

### DescriptorHeap

```cpp
// src/engine/renderer/rhi/descriptor_heap.h

class DescriptorHeap {
public:
    struct Allocation {
        uint32_t                  index;      // Heap slot index (pass to shader)
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        bool                      valid() const { return index != UINT32_MAX; }
    };

    void init(ID3D12Device* device,
              D3D12_DESCRIPTOR_HEAP_TYPE type,
              uint32_t capacity,
              bool shader_visible);

    Allocation allocate();              // Thread-safe (atomic CAS)
    void       free(uint32_t index);    // Thread-safe (deferred free list)
    void       flush_deferred(uint64_t completed_fence);

    ID3D12DescriptorHeap* heap() const { return heap_.Get(); }

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    uint32_t                     descriptor_size_ = 0;
    std::atomic<uint32_t>        next_index_{0};
    // ... deferred free list for GPU-in-flight safety
};
```

### BindlessManager

```cpp
// src/engine/renderer/rhi/bindless_manager.h

class BindlessManager {
public:
    static BindlessManager& get();

    void init(ID3D12Device* device);
    void shutdown();

    // Register a texture — returns bindless index
    uint32_t register_texture(ID3D12Resource* resource,
                               const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
    void     unregister_texture(uint32_t index);

    // Register a buffer (StructuredBuffer / ByteAddressBuffer)
    uint32_t register_buffer(ID3D12Resource* resource,
                              const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);
    void     unregister_buffer(uint32_t index);

    // Register a UAV for compute writes
    uint32_t register_uav(ID3D12Resource* resource,
                           const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc);
    void     unregister_uav(uint32_t index);

    // Register a sampler — returns sampler index
    uint32_t register_sampler(const D3D12_SAMPLER_DESC& desc);

    // Bind heaps to command list (call once per frame, before any draws)
    void bind(ID3D12GraphicsCommandList* cmd);

    // Stat query
    uint32_t used_texture_slots() const;
    uint32_t used_sampler_slots() const;

private:
    DescriptorHeap cbv_srv_uav_;
    DescriptorHeap sampler_;
};
```

---

## Root Signature

```cpp
// Bindless root signature (replaces per-draw descriptor tables)
// 4 root parameters: draw constants, scene CBV, bindless SRV heap, sampler heap

CD3DX12_ROOT_PARAMETER1 params[4];

// [0] DrawConstants — per-draw uint32 indices (material, transform, etc.)
params[0].InitAsConstants(
    sizeof(DrawConstants) / 4,  // 8 uint32s = material_idx, transform_idx, etc.
    0,                          // b0
    0                           // space0
);

// [1] SceneCBV — per-frame camera/lights constant buffer
params[1].InitAsConstantBufferView(1);  // b1

// [2] Bindless SRV heap
// D3D12_ROOT_DESCRIPTOR_FLAG_NONE — entire heap accessible from shader
CD3DX12_DESCRIPTOR_RANGE1 srv_range;
srv_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 0,
               D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
params[2].InitAsDescriptorTable(1, &srv_range);

// [3] Sampler heap
CD3DX12_DESCRIPTOR_RANGE1 sampler_range;
sampler_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2048, 0, 0);
params[3].InitAsDescriptorTable(1, &sampler_range);
```

---

## DrawConstants (Per-Draw Root Constants)

```cpp
struct DrawConstants {
    uint32_t material_idx;    // Index into MaterialBuffer
    uint32_t transform_idx;   // Index into TransformBuffer
    uint32_t instance_offset; // For GPU instancing base offset
    uint32_t flags;           // Per-draw flags (skinned, alpha, etc.)
};
```

---

## Material Buffer

All material properties live in a `StructuredBuffer<Material>` — no per-draw CBV rebinding:

```hlsl
struct Material {
    uint  albedo_idx;       // BindlessManager index for albedo texture
    uint  normal_idx;       // Normal map index
    uint  roughness_idx;    // Roughness/metallic map index
    uint  emissive_idx;     // Emissive map index
    float4 albedo_factor;
    float  roughness_factor;
    float  metallic_factor;
    float  emissive_strength;
    uint   sampler_idx;     // Which sampler to use (linear/anisotropic)
    float3 _pad;
};

StructuredBuffer<Material> g_Materials : register(t0, space1);
```

---

## HLSL Shader Usage

```hlsl
// In pixel shader — access any texture via bindless index
float4 PSMain(PSInput input) : SV_Target {
    DrawConstants dc = g_DrawConstants;  // Root constants
    Material mat = g_Materials[dc.material_idx];

    // Bindless texture access
    Texture2D albedoTex = ResourceDescriptorHeap[mat.albedo_idx];
    Texture2D normalTex = ResourceDescriptorHeap[mat.normal_idx];
    SamplerState samp   = SamplerDescriptorHeap[mat.sampler_idx];

    float4 albedo  = albedoTex.Sample(samp, input.uv) * mat.albedo_factor;
    float3 normal  = DecodeNormal(normalTex.Sample(samp, input.uv).xy);
    // ... PBR lighting
}
```

---

## Migration From Bound Resources

Existing Phase 1 code uses fixed slots (t0 = albedo, t1 = normal, etc.). Migration:

| Before | After |
|--------|-------|
| `Texture2D g_Albedo : register(t0)` | `ResourceDescriptorHeap[mat.albedo_idx]` |
| `cbuffer Material : register(b2)` | `StructuredBuffer<Material> g_Materials` |
| `OMSetDescriptorTable(...)` per draw | Single `SetDescriptorHeaps()` per frame |
| Root signature compiled per PSO | One global bindless root signature |

---

## Vulkan Equivalent

```cpp
// Vulkan: use VK_EXT_descriptor_indexing
// DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | UPDATE_AFTER_BIND_BIT
// layout(set=0, binding=0) uniform texture2D g_Textures[];
// g_Textures[push_constants.albedo_idx]
```

The RHI abstracts both paths behind `rhi_bind_bindless_heap()`.

---

## Thread Safety

| Operation | Thread-Safe? |
|-----------|-------------|
| `allocate()` | Yes — atomic CAS on `next_index_` |
| `free()` | Yes — deferred list, flushed per frame |
| `bind()` | No — call from render thread only |
| Shader reads | Yes — GPU reads only |

---

## Implementation Plan

| Week | Deliverable |
|------|-------------|
| 1 | `DescriptorHeap` with thread-safe alloc/free, DX12 heap creation |
| 2 | `BindlessManager` singleton, register/unregister texture/buffer |
| 3 | Bindless root signature, `DrawConstants`, `MaterialBuffer` |
| 4 | HLSL `ResourceDescriptorHeap` usage in PBR shaders |
| 5 | Migration of Phase 1 bound shaders → bindless |
| 6 | Profiling: draw call count, heap fragmentation stats |

---

## Dependencies

- DX12 backend (RHI)
- Phase 2 PBR renderer
- Asset pipeline (texture import → `BindlessManager::register_texture()`)

## Related Specs

- [Renderer](renderer.md)
- [RHI](rhi.md)
- [GPU Instancing](gpu-instancing.md)
- [Shader System](shader-system.md)
