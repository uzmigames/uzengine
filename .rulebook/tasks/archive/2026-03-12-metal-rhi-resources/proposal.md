# Proposal: Metal RHI Resource Types

## Why

The Metal backend needs concrete wrapper types for GPU resources (buffers, textures, shaders, pipelines, samplers) that map Apple's Metal API objects to the engine's `RHIHandle<Tag>` system. These are the foundational data structures used by MetalDevice, MetalCommandList, and MetalSwapChain — they must be defined first before any other Metal code can be written. Following the same `ResourcePool<T>` pattern used by DX12 (`dx12_resources.h`) and Vulkan (`vulkan_resources.h`).

## What Changes

### New File: `runtime/engine/rhi/metal/metal_resources.h`

1. **MetalBuffer** — Wraps `MTL::Buffer*` (metal-cpp). Stores storage mode, size, mapped pointer (for shared/managed buffers), and SRV descriptor index for bindless access via argument buffers.

2. **MetalTexture** — Wraps `MTL::Texture*`. Stores format, dimensions, usage flags, SRV descriptor index, and optional RTV/DSV state for render targets.

3. **MetalShader** — Wraps `MTL::Function*` (compiled from MSL via `MTL::Library`). Stores shader stage (vertex/fragment/compute), entry point name, and the parent `MTL::Library*`.

4. **MetalPipeline** — Wraps `MTL::RenderPipelineState*` or `MTL::ComputePipelineState*`. Stores pipeline type, topology, and cached descriptor for recreation.

5. **MetalSampler** — Wraps `MTL::SamplerState*`. Stores sampler descriptor index for argument buffer binding.

### Design Decisions

- **metal-cpp over Objective-C**: Uses Apple's official C++ wrapper (`metal-cpp`) to keep all code in `.cpp` files. Only the CAMetalLayer integration (swapchain) requires `.mm` (Objective-C++).
- **No VMA equivalent**: Metal handles memory management internally via `MTLHeap` and storage modes. No third-party allocator needed.
- **Argument buffer indexing**: Resources store their descriptor index for Metal's argument buffer system, analogous to DX12's descriptor heap index.

## Impact

- Affected specs: `docs/engine/03-rhi/15-metal-backend.md`
- Affected code: `runtime/engine/rhi/metal/metal_resources.h` (new)
- Breaking change: NO
- User benefit: Foundation for all Metal backend resource management
- Dependencies: metal-cpp headers (`thirdparty/metal-cpp/`)
