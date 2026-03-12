# Proposal: Metal RHI Device (MetalDevice)

## Why

`MetalDevice` is the central hub of the Metal backend — it owns the `MTL::Device`, creates all GPU resources (buffers, textures, shaders, pipelines, samplers), manages argument buffers for bindless-style resource access, and provides device capability queries. This is the largest and most complex Metal component, equivalent to `DX12Device` (~1500 LOC) and `VulkanDevice` (~1500 LOC).

## What Changes

### New Files

1. **`metal_device.h`** — `MetalDevice : public IRHIDevice` declaration (~200 lines)
2. **`metal_device.cpp`** — Implementation of all 25+ `IRHIDevice` methods (~1200 lines)

### Architecture

```
MetalDevice (IRHIDevice)
├── MTL::Device*                — System default GPU device
├── MTL::CommandQueue*          — Primary command queue (graphics + compute + copy)
├── MTL::CommandQueue*          — Transfer queue (async copy operations)
├── Argument Buffer             — Bindless resource table (Metal's descriptor equivalent)
│   ├── MTL::Buffer* (textures) — Argument buffer for texture descriptors
│   └── MTL::Buffer* (samplers) — Argument buffer for sampler descriptors
├── Resource Pools
│   ├── ResourcePool<MetalBuffer>
│   ├── ResourcePool<MetalTexture>
│   ├── ResourcePool<MetalShader>
│   ├── ResourcePool<MetalSampler>
│   └── ResourcePool<MetalPipeline>
└── Capabilities
    ├── GPU Family detection     — Apple/Mac/Common family
    ├── Shader model mapping     — Metal GPU family → ShaderModel enum
    └── Feature flags            — Argument buffers tier, texture tier
```

### Metal API Mapping (IRHIDevice methods)

| IRHIDevice Method | Metal API |
|---|---|
| `CreateBuffer()` | `device->newBuffer(length, options)` |
| `DestroyBuffer()` | `buffer->release()` |
| `MapBuffer()` | `buffer->contents()` (shared/managed storage) |
| `CreateTexture()` | `device->newTexture(descriptor)` |
| `DestroyTexture()` | `texture->release()` |
| `CreateShader()` | `library->newFunction(name)` (from compiled MSL) |
| `DestroyShader()` | `function->release()`, `library->release()` |
| `CreateGraphicsPipeline()` | `device->newRenderPipelineState(descriptor)` |
| `DestroyPipeline()` | `pipelineState->release()` |
| `CreateSampler()` | `device->newSamplerState(descriptor)` |
| `DestroySampler()` | `samplerState->release()` |
| `CreateSwapChain()` | Delegates to `MetalSwapChain` constructor |
| `CreateFence()` | Delegates to `MetalFence` constructor |
| `CreateCommandList()` | Delegates to `MetalCommandList` constructor |
| `WaitIdle()` | Submit empty command buffer + waitUntilCompleted |
| `GetAdapterName()` | `device->name()` |
| `GetMemoryBudget()` | `device->recommendedMaxWorkingSetSize()` |
| `GetShaderModel()` | Mapped from `device->supportsFamily()` |
| `IsBindless()` | `argumentBuffersTier >= MTLArgumentBuffersTier2` |

### Design Decisions

- **metal-cpp (C++ wrapper)**: Uses Apple's official `metal-cpp` headers for type-safe C++ access. No Objective-C in device code.
- **Argument Buffers Tier 2**: Required for bindless-style access. Tier 2 is available on all Apple Silicon and AMD GPUs on macOS 10.15+.
- **No external memory allocator**: Metal manages GPU memory internally. `MTLHeap` used for transient resources only. No D3D12MA/VMA equivalent needed.
- **Single queue model**: Metal's `MTLCommandQueue` handles all command types. Secondary transfer queue for async copies.

## Impact

- Affected specs: `docs/engine/03-rhi/15-metal-backend.md`
- Affected code: `runtime/engine/rhi/metal/metal_device.h/.cpp` (new), `rhi_factory.cpp` (Metal case)
- Breaking change: NO
- User benefit: Full GPU resource management on macOS/iOS
- Dependencies: `metal-rhi-resources`, `metal-rhi-fence`
