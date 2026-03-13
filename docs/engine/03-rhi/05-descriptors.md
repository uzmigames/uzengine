# RHI — Descriptor Management

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/rhi/dx12/dx12_descriptor_allocator.h/.cpp`, `dx12_device.h` (heap constants)
> **Depends on**: DX12 backend

## Purpose

Descriptors are the mechanism by which shaders access GPU resources (buffers, textures, samplers). In DX12, descriptors live in descriptor heaps — contiguous GPU-visible arrays that shaders index into. Descriptor management is one of the most complex parts of DX12 programming; getting it wrong causes GPU crashes with no useful error messages.

## Descriptor Heap Architecture

UzEngine maintains four descriptor heaps, each with a dedicated `DescriptorAllocator`:

```
┌──────────────────────────────────────────────────────────────┐
│  CBV_SRV_UAV Heap (1,000,000 descriptors, shader-visible)   │
│  ┌─────┬─────┬─────┬─────┬─────┬───────────────────────┐    │
│  │ SRV │ SRV │ UAV │ CBV │ SRV │  ... 999,995 free ... │    │
│  └─────┴─────┴─────┴─────┴─────┴───────────────────────┘    │
│  Shaders index via: ResourceDescriptorHeap[index]            │
├──────────────────────────────────────────────────────────────┤
│  Sampler Heap (2,048 descriptors, shader-visible)            │
│  ┌─────┬─────┬─────┬──────────────────────────────┐         │
│  │ Smp │ Smp │ Smp │  ... 2045 free ...           │         │
│  └─────┴─────┴─────┴──────────────────────────────┘         │
│  Shaders index via: SamplerDescriptorHeap[index]             │
├──────────────────────────────────────────────────────────────┤
│  RTV Heap (256 descriptors, CPU-only)                        │
│  Used for render target binding (OMSetRenderTargets)         │
├──────────────────────────────────────────────────────────────┤
│  DSV Heap (64 descriptors, CPU-only)                         │
│  Used for depth-stencil binding (OMSetRenderTargets)         │
└──────────────────────────────────────────────────────────────┘
```

### Why 1 Million CBV/SRV/UAV Descriptors?

SM6.6 bindless requires that all resources be reachable via a single global heap. The GPU hardware maximum is typically 1M+ descriptors for this heap type. With 1M descriptors, UzEngine can bind:

- All mesh vertex/index buffers as SRVs
- All textures (albedo, normal, roughness, etc.) as SRVs
- All constant buffers as CBVs
- All compute read/write resources as UAVs

...simultaneously, without descriptor table switching. This is the key advantage of bindless over traditional binding: zero descriptor management at draw time.

### Why Only 2,048 Samplers?

DX12 hardware limits sampler heaps to 2,048 descriptors. This is plenty — most engines use fewer than 100 unique sampler states (combinations of filter, address, LOD). Samplers are typically created once at startup and reused.

## DescriptorAllocator

Each heap has a `DescriptorAllocator` that manages free indices using a stack-based free list:

```
Init(heap, capacity=1M, incrementSize=32, reservedCount=0)
     │
     ▼
Allocate() → returns index (O(1): pop from free list or bump next)
     │
     ▼
Free(index) → pushes index back to free list (O(1))
```

**Handle computation:**
```cpp
CPU handle = cpuBase + index * incrementSize
GPU handle = gpuBase + index * incrementSize  // shader-visible heaps only
```

### Reserved Indices

Some indices can be reserved at initialization (e.g., swap chain RTVs occupy fixed RTV slots). The allocator starts allocating from `reservedCount`, ensuring reserved indices are never given out.

## Bindless Resource Access Pattern

When creating a resource (buffer or texture), the device:

1. Allocates the GPU resource via D3D12MA
2. Allocates a descriptor index from the CBV_SRV_UAV allocator
3. Creates the descriptor (SRV, UAV, or CBV) at that index in the heap
4. Returns a `RHIHandle` — the application later queries the index via `GetBufferSRVIndex()` or `GetTextureSRVIndex()`

At draw time:
```cpp
// Push the descriptor index to the shader via root constants
cmdList->SetDescriptorIndex(0, device->GetTextureSRVIndex(albedo));
cmdList->SetDescriptorIndex(1, device->GetTextureSRVIndex(normal));
cmdList->DrawIndexed(indexCount);
```

In the shader (HLSL SM6.6):
```hlsl
// No explicit register binding — index comes from root constant
Texture2D albedo = ResourceDescriptorHeap[descriptorIndex0];
Texture2D normal = ResourceDescriptorHeap[descriptorIndex1];
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Descriptor model | Per-frame ring buffer + bindless (UE5.4+) | Per-draw descriptor set updates | Single persistent global heap |
| Heap management | `FD3D12DescriptorCache` with multi-heap fallback | Vulkan descriptor pool per frame | One `DescriptorAllocator` per heap type |
| Bindless support | Optional, via `r.Vulkan.Bindless` / DX12 | Not yet (Godot 4.4 plans it) | **Default** — SM6.6 bindless is the primary path |
| Descriptor lifetime | Ring buffer (frame-scoped, copied per draw) | Per-frame pool reset | **Persistent** — allocated once, freed on resource destroy |

UzEngine's persistent-allocation model is simpler than ring buffers: each resource gets a descriptor when created and keeps it until destroyed. No per-frame descriptor copies, no pool resets, no ring buffer management.

## Rules

1. **NEVER** hardcode descriptor indices — always query via `Get*SRVIndex()` / `GetSamplerIndex()`
2. **ALWAYS** free descriptor indices when destroying resources — leaked indices eventually exhaust the heap
3. **NEVER** access a descriptor after its resource is destroyed — the GPU will read garbage data
4. **ALWAYS** bind both the CBV_SRV_UAV and Sampler heaps on `Begin()` — SM6.6 shaders expect them
