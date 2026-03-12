# RHI — Metal Backend

> **Status**: Phase 4 (Core implementation complete; build integration + factory registration in progress)
> **Source**: `runtime/core/rhi/metal/metal_device.h/.cpp`, `metal_command_list.h/.cpp`, `metal_swapchain.h/.mm`, `metal_fence.h/.cpp`
> **Depends on**: RHI interface (`rhi.h`, `rhi_types.h`), metal-cpp, Shader-Slang

## Purpose

The Metal backend provides native GPU rendering for macOS and iOS platforms. It translates all `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, and `IRHIFence` calls into Metal API calls. All Metal headers are confined to files under `runtime/core/rhi/metal/` — nothing else in the engine includes `<Metal/Metal.hpp>` or Objective-C header files except `metal_swapchain.mm`.

The Metal backend demonstrates the RHI abstraction working seamlessly across three completely different GPU APIs (DX12, Vulkan, Metal), with zero changes to renderer code or application layers.

## Architecture

```
MetalDevice (IRHIDevice)
├── MTL::Device*                — System default GPU (managed by macOS)
├── MTL::CommandQueue* (Primary) — Graphics + Compute + Copy commands
├── MTL::CommandQueue* (Transfer)— DMA transfers (optional, for performance)
├── Argument Buffers (Bindless)
│   ├── MTL::Buffer* (textures) — Texture descriptor argument buffer
│   ├── MTL::Buffer* (samplers) — Sampler descriptor argument buffer
│   └── MTL::Buffer* (buffers)  — Buffer descriptor argument buffer
├── Resource Pools
│   ├── ResourcePool<MetalBuffer>
│   ├── ResourcePool<MetalTexture>
│   ├── ResourcePool<MetalShader>
│   ├── ResourcePool<MetalSampler>
│   └── ResourcePool<MetalPipeline>
├── Synchronization
│   ├── MTL::SharedEvent[]      — Timeline events (CPU/GPU sync)
│   └── dispatch_semaphore_t    — CPU-side blocking wait (fallback)
├── Swapchain (Window Mode)
│   ├── CAMetalLayer*           — Drawable surface (Objective-C)
│   └── MTL::Drawable[]         — Triple-buffered back buffers
└── Frame-in-Flight Management
    └── CommandBuffer rotation  — One per frame (no triple-buffering needed like Vulkan)
```

## Initialization Sequence

Device creation follows a strict order — each step depends on the previous:

```
1. MTL::Device Creation    MTL::CreateSystemDefaultDevice()
        │                  └── Acquire GPU (auto-selected by macOS)
        ▼
2. GPU Family Detection    supportsFamilyGPU(family)
        │                  ├── Check MTLGPUFamilyApple7 (A15+)
        │                  ├── Check MTLGPUFamilyApple6 (A14)
        │                  ├── Check MTLGPUFamilyMac2 (M-series, discrete)
        │                  └── Check MTLGPUFamilyMac1 (Intel integrated)
        ▼
3. Shader Model Negotiation NegotiateShaderModel()
        │                  └── GPU family → ShaderModel enum
        ▼
4. Argument Buffer Tier    querySupport(MTLArgumentBufferTier)
        │                  ├── Tier 2: Full bindless (preferred)
        │                  └── Tier 1: Sparse bindless (fallback)
        ▼
5. Primary CommandQueue    device->newCommandQueue()
        │                  └── For graphics, compute, blit encoding
        ▼
6. Transfer CommandQueue   device->newCommandQueue() (optional)
        │                  └── For high-bandwidth async DMA
        ▼
7. Argument Buffer Setup   CreateArgumentBuffers()
        │                  ├── Texture descriptor buffer (capacity: 1M)
        │                  ├── Sampler descriptor buffer (capacity: 256)
        │                  └── Buffer descriptor buffer (capacity: 1M)
        ▼
8. Descriptor Allocators   CreateDescriptorAllocators()
        │                  └── Free-list allocators for texture/sampler/buffer slots
        ▼
9. Resource Pools          CreateResourcePools()
        │                  ├── MetalBuffer pool
        │                  ├── MetalTexture pool
        │                  ├── MetalShader pool
        │                  ├── MetalSampler pool
        │                  └── MetalPipeline pool
        ▼
10. SwapChain (if window)  CreateSwapChain()
        │                  ├── Attach CAMetalLayer to window (Objective-C)
        │                  └── Set triple-buffering if desired
        ▼
11. Logging / Diagnostics  LogDeviceInfo()
        │                  ├── Adapter name (e.g., "Apple M3 Max")
        │                  ├── GPU family and memory
        │                  ├── Argument buffer tier
        │                  └── Shader model negotiated
        ▼
```

### Why This Order

- **Device creation first**: Needed for all subsequent queries and allocations
- **GPU family detection before shader model**: Family determines capability, model depends on family
- **Argument buffer tier before argument buffers**: Tier determines feature set (Tier 2 required for bindless)
- **Primary queue before command encoding**: Queue is needed to allocate command buffers
- **Argument buffers before resource creation**: Resources bind descriptors to these buffers
- **SwapChain last**: Optional (headless mode doesn't need it); doesn't affect core functionality

## GPU Family Mapping

Metal organizes GPUs into **families**, each supporting different shader features:

| GPU Family | Devices | ShaderModel | Compute | Features |
|-----------|---------|------------|---------|----------|
| **Apple 7+** | iPhone 15 Pro, A17 Pro | SM6.6 | Yes | Argument Buffers (Tier 2), Indirect dispatch |
| **Apple 6** | iPhone 14 Pro, iPhone 13 Pro, A14 Bionic | SM6.0 | Yes | Argument Buffers (Tier 2), Wave ops |
| **Apple 5** | iPhone 12, iPhone 11, A12/A13 | SM5.1 | Limited | Argument Buffers (Tier 1), no indirect |
| **Mac 2** | M3, M2, M1, Intel iGPU (Iris Xe) | SM6.6 | Yes | Full features, high VRAM |
| **Mac 1** | Intel iGPU (older), AMD discrete (rare) | SM5.1 | Limited | Sparse bindless support |

**Key insight**: Argument buffers are Metal's equivalent to DX12's descriptor heaps and Vulkan's descriptor sets. **Tier 2 argument buffers are required for bindless rendering** (SM6.6 path). Tier 1 only supports sparse binding and is treated as SM5.1 fallback.

## Metal Equivalents Table

| UzEngine RHI | DX12 | Vulkan | Metal |
|---|---|---|---|
| `IRHIDevice` | `ID3D12Device` | `VkDevice` | `MTL::Device` |
| `IRHICommandList` | `ID3D12GraphicsCommandList` | `VkCommandBuffer` | `MTL::CommandBuffer` + encoders |
| `IRHISwapChain` | `IDXGISwapChain4` | `VkSwapchainKHR` | `CAMetalLayer` + `MTL::Drawable` |
| `IRHIFence` | `ID3D12Fence` | `VkSemaphore` (timeline) | `MTL::SharedEvent` |
| Resource state | `D3D12_RESOURCE_STATES` | `VkImageLayout` | N/A (automatic) |
| Descriptor heap | `ID3D12DescriptorHeap` | `VkDescriptorPool` | Argument Buffer |
| Root signature | `ID3D12RootSignature` | `VkPipelineLayout` | `MTL::ArgumentEncoder` |
| Memory allocator | D3D12MA | VMA | Built-in (`MTLHeap` optional) |
| Command encoding | Implicit (cmd list records calls) | Explicit `vkCmdDraw`, etc. | **Explicit encoders** (RenderEncoder, ComputeEncoder, BlitEncoder) |

## Command Model — Encoders

Metal uses a **three-tier command encoding model** — fundamentally different from DX12 and Vulkan:

```cpp
// Metal command recording (simplified)
MTL::CommandBuffer* cmdBuf = commandQueue->commandBuffer();

// ─── Render Encoding ───
MTL::RenderCommandEncoder* renderEncoder = cmdBuf->renderCommandEncoder(renderPass);
renderEncoder->setRenderPipelineState(pso);
renderEncoder->setVertexBuffer(vertexBuffer, 0, 0);
renderEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, 0, vertexCount);
renderEncoder->endEncoding();

// ─── Compute Encoding ───
MTL::ComputeCommandEncoder* computeEncoder = cmdBuf->computeCommandEncoder();
computeEncoder->setComputePipelineState(pso);
computeEncoder->setBuffer(buffer, 0, 0);
computeEncoder->dispatchThreadgroups(...);
computeEncoder->endEncoding();

// ─── Blit Encoding ───
MTL::BlitCommandEncoder* blitEncoder = cmdBuf->blitCommandEncoder();
blitEncoder->copyFromBuffer(src, 0, toBuffer(dst), 0, size);
blitEncoder->endEncoding();

cmdBuf->commit();  // Submit to GPU
```

**Key differences from DX12/Vulkan:**

1. **Encoders are exclusive**: Only one encoder per command buffer at a time. Starting a new encoder implicitly ends the previous one.
2. **No explicit barriers**: Metal automatically tracks resource hazards. Transitioning from render to compute encoding is implicit.
3. **Render passes describe attachments once**: Unlike Vulkan, you don't declare `VkAttachmentDescription` separately — the `MTLRenderPassDescriptor` defines everything.
4. **Blit encoder for transfers**: Copies, blits, and mips generation happen in a separate blit encoder, not render encoder.

The RHI abstraction presents a **virtual command list interface** that hides these encoder details:

```cpp
// UzEngine API (implementation detail hidden)
cmdList->SetRenderTarget(rtv);
cmdList->DrawMesh(...);
cmdList->SetComputePipeline(computePso);
cmdList->Dispatch(...);
cmdList->CopyBuffer(...);

// Internally, the Metal backend:
// 1. Batches SetRenderTarget/DrawMesh calls in a RenderEncoder
// 2. On dispatch, ends RenderEncoder, creates ComputeEncoder
// 3. On copy, ends ComputeEncoder, creates BlitEncoder
```

## MetalCommandList Implementation

The `MetalCommandList` class implements the `IRHICommandList` interface using Metal's encoder-based command model:

```cpp
class MetalCommandList : public IRHICommandList {
    MTL::CommandBuffer* cmdBuf_;
    MTL::RenderCommandEncoder* renderEncoder_;
    MTL::ComputeCommandEncoder* computeEncoder_;
    MTL::BlitCommandEncoder* blitEncoder_;
    RHIResourceState lastResourceState_;  // Track current state for auto-transitions

public:
    /// Implicit encoder switching on command type change
    void DrawMesh(const RHIDrawDesc& desc) override;
    void Dispatch(const RHIDispatchDesc& desc) override;
    void CopyBuffer(RHIBufferHandle src, RHIBufferHandle dst, size_t size) override;
    void ResourceBarrier(const RHIResourceBarrier& barrier) override;  // No-op in Metal
};
```

### Encoder Auto-Switching

When the command type changes (e.g., from render to compute), the implementation automatically:

1. **Ends the current encoder** — metal encoders are exclusive and must be ended before a new type begins
2. **Caches encoder references** — avoids repeated allocations
3. **Tracks resource state transitions** — though Metal handles most implicitly

```cpp
void MetalCommandList::Dispatch(const RHIDispatchDesc& desc) {
    // If we were rendering, end the render encoder
    if (renderEncoder_) {
        renderEncoder_->endEncoding();
        renderEncoder_ = nullptr;
    }

    // Create compute encoder if needed
    if (!computeEncoder_) {
        computeEncoder_ = cmdBuf_->computeCommandEncoder();
    }

    // Encode dispatch
    computeEncoder_->setComputePipelineState(metalPipeline->pso);
    computeEncoder_->dispatchThreadgroups(...);
}
```

### Index Buffer Binding (Deferred)

Metal's render encoder requires vertex and index buffers to be bound at the time of drawing (unlike DX12's command list, which allows binding before draw calls). To maintain the RHI abstraction, `MetalCommandList` defers index buffer binding until the actual `DrawMesh` call:

```cpp
void MetalCommandList::SetIndexBuffer(RHIBufferHandle idx, RHIIndexFormat fmt) override {
    // Store for later use in DrawMesh
    pendingIndexBuffer_ = idx;
    pendingIndexFormat_ = fmt;
}

void MetalCommandList::DrawMesh(...) override {
    // Bind deferred index buffer now
    if (pendingIndexBuffer_.IsValid()) {
        MTL::Buffer* idxBuf = resourcePool_->GetBuffer(pendingIndexBuffer_);
        renderEncoder_->setIndexBuffer(idxBuf, 0, metalFormat(pendingIndexFormat_));
    }
    // Draw
    renderEncoder_->drawIndexedPrimitives(...);
}
```

### ResourceBarrier (No-op)

The `ResourceBarrier()` method is a no-op in Metal because the GPU automatically handles resource hazards. The method exists for API compatibility across backends:

```cpp
void MetalCommandList::ResourceBarrier(const RHIResourceBarrier& barrier) override {
    // Metal handles all transitions implicitly.
    // This function is required by the RHI interface but does nothing.
    // Barrier info is logged at debug level for validation.
    UZ_LOG(RHI, Debug, "ResourceBarrier (no-op in Metal): {} → {}",
           toString(barrier.before), toString(barrier.after));
}
```

## MetalSwapChain Implementation

The `MetalSwapChain` class implements the `IRHISwapChain` interface by wrapping `CAMetalLayer` and managing drawable acquisition:

```cpp
class MetalSwapChain : public IRHISwapChain {
    CAMetalLayer* metalLayer_;
    MTL::Device* device_;
    std::vector<MTL::Drawable*> drawables_;  // Triple-buffered
    bool vsyncEnabled_;

public:
    /// Acquire next drawable (blocks if unavailable)
    IRHITexture* AcquireBackBuffer() override;
    /// Present drawable to screen
    void Present(bool waitVSync) override;
    /// Query supported present modes
    std::vector<RHIPresentMode> GetSupportedPresentModes() override;
};
```

### Drawable Acquisition via nextDrawable()

Metal provides a single API for acquiring a drawable (back buffer):

```cpp
IRHITexture* MetalSwapChain::AcquireBackBuffer() {
    // nextDrawable() blocks until one is available (typically 1-2 frames)
    MTL::Drawable* drawable = metalLayer_->nextDrawable();
    if (!drawable) {
        UZ_LOG(RHI, Error, "Failed to acquire drawable from CAMetalLayer");
        return nullptr;
    }

    // Drawable wraps a texture
    MTL::Texture* backbufferTex = drawable->texture();

    // Return RHI texture handle pointing to this texture
    return GetTextureHandle(backbufferTex);
}
```

**Key detail:** `nextDrawable()` blocks if the GPU is still rendering previous frames. This is Metal's implicit frame pacing mechanism — no need for explicit present modes like FIFO/MAILBOX in Vulkan.

### Framebuffer-Only Optimization

Metal provides a `framebufferOnly` flag on drawable textures to optimize for render-target-only usage:

```cpp
// In CAMetalLayer configuration (Objective-C)
metalLayer.framebufferOnly = YES;  // Optimize for render-only (typical for back buffers)
```

**Performance impact:** When `framebufferOnly=YES`, Metal can use fast TBDR (Tile-Based Deferred Rendering) without storing the texture to main memory. When `NO`, the texture can be read as a shader input (slower). For back buffers, `YES` is always correct.

The RHI backend automatically sets this during swapchain creation:

```cpp
void MetalSwapChain::Init() {
    metalLayer_ = CreateMetalLayer(window_);
    metalLayer_.framebufferOnly = YES;
    metalLayer_.maximumDrawableCount = 3;  // Triple buffering
    UZ_LOG(RHI, Info, "MetalSwapChain initialized: framebufferOnly=YES, triple-buffered");
}
```

### V-Sync and Present Modes

Metal doesn't expose explicit present modes like Vulkan's `VkPresentModeKHR`. Instead, V-Sync is controlled through `CAMetalLayer` properties:

```cpp
void MetalSwapChain::SetVSync(bool enabled) {
    if (enabled) {
        // Synchronize with display refresh (60 FPS by default)
        metalLayer_.displaySyncEnabled = YES;
    } else {
        // No display sync — present as fast as possible
        metalLayer_.displaySyncEnabled = NO;
    }

    // Optional: Set target frame rate
    metalLayer_.preferredFramesPerSecond = 60;  // Ignored if displaySyncEnabled=NO

    vsyncEnabled_ = enabled;
    UZ_LOG(RHI, Info, "MetalSwapChain V-Sync: {}", enabled ? "enabled" : "disabled");
}
```

**Behavior:**
- **V-Sync ON** (displaySyncEnabled=YES): `Present()` blocks until the next display refresh (~16.7 ms @ 60 Hz). Frame rate is capped at monitor refresh rate.
- **V-Sync OFF** (displaySyncEnabled=NO): `Present()` returns immediately; GPU rendering continues in the background. No frame rate cap (GPU limited).

### Present Implementation

The `Present()` method schedules the drawable for display and optionally waits for V-Sync:

```cpp
void MetalSwapChain::Present(bool waitVSync) override {
    MTL::Drawable* drawable = currentDrawable_;
    if (!drawable) {
        UZ_LOG(RHI, Warning, "No drawable to present");
        return;
    }

    // Schedule presentation (drawable content becomes visible on next refresh)
    drawable->present();

    if (waitVSync) {
        // Metal V-Sync is controlled at the layer level
        // Setting displaySyncEnabled = YES is enough
        // Present calls internally synchronize with refresh
    }

    // Release reference to this drawable
    currentDrawable_ = nullptr;
}
```

**Note:** Unlike DX12/Vulkan, Metal doesn't require explicit tearing vs. tear-free mode selection. V-Sync is implicit in the layer configuration.

## Argument Buffers (Bindless Descriptor Tables)

Metal's **argument buffers** are the equivalent of DX12's descriptor heaps and Vulkan's descriptor sets. They enable bindless rendering — accessing thousands of resources via indices in push constants.

```cpp
// Pseudo-code: Argument buffer setup (simplified)

// Define argument buffer layout
MTL::ArgumentDescriptor* textureDesc = MTL::ArgumentDescriptor::alloc()->init();
textureDesc->setDataType(MTL::DataTypeTexture);
textureDesc->setIndex(0);  // Descriptor slot

MTL::ArgumentBufferTier tier = device->argumentBuffersTier();
if (tier == MTL::ArgumentBufferTierTier2) {
    // Tier 2: Full bindless (preferred)
    // Can bind arbitrary textures at runtime
    argumentEncoder = computeFunction->newArgumentEncoder(0);  // Encoder for layout
    argumentBuffer = device->newBuffer(maxTextures * sizeof(uint32_t), MTL::ResourceStorageModeShared);
} else {
    // Tier 1: Sparse binding (fallback)
    // Limited support, mostly for older Apple Silicon
}
```

### Argument Buffer Tiers

| Tier | Capabilities | Supported Devices | Use in UzEngine |
|------|-------------|-----------------|-----------------|
| **Tier 2** (preferred) | Arbitrary texture/sampler/buffer binding at runtime | Apple 5+, Mac 2+ | Bindless SM6.6 path |
| **Tier 1** | Sparse binding, limited indexing | Older Apple 4, Intel iGPU | SM5.1 fallback (limited) |
| **None** (unsupported) | No argument buffers | N/A | Headless only |

The backend requires **Tier 2** for full bindless support. Devices with only Tier 1 fall back to SM5.0 (non-bindless) rendering.

## Swapchain

The swapchain integrates with the OS windowing system via **Objective-C** (Metal's native language). The RHI abstraction hides this complexity:

```mm
// metal_swapchain.mm (Objective-C++, only file mixing C++ and Objective-C)
@interface MetalView : MTKView
@end

@implementation MetalView
- (void)initWithWindow:(NSWindow*)window {
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.device = device;
    // Triple-buffered drawable acquisition
}
@end
```

**Drawable acquisition:**

```cpp
// C++ wrapper (metal_device.cpp)
MTL::Drawable* drawable = metalLayer->nextDrawable();  // Blocks until available
MTL::Texture* backbuffer = drawable->texture();
// Render to backbuffer
drawable->present();  // Enqueue for display
```

**V-Sync control:**

```cpp
// Metal doesn't expose explicit present mode like DX12/Vulkan
// V-Sync is controlled via:
// - CADisplayLink (native macOS timer)
// - metalLayer.displaySyncEnabled = true/false
// - CAMetalLayer.preferredFramesPerSecond

metalLayer->setPreferredFramesPerSecond(60);  // V-Sync at 60 Hz
```

## Fence / Synchronization

Metal uses **shared events** for CPU/GPU synchronization — similar to DX12 fences and Vulkan timeline semaphores:

```cpp
// Pseudo-code: Timeline synchronization
MTL::SharedEvent* event = device->newSharedEvent();
uint64_t fenceValue = 1;

// GPU signals event after render completes
MTL::CommandBuffer* cmdBuf = queue->commandBuffer();
// ... encode render commands ...
cmdBuf->encodeSignalEvent(event, fenceValue);
cmdBuf->commit();

// CPU waits for GPU to reach fenceValue
// (Note: MTL::SharedEvent doesn't have direct wait — use dispatch_semaphore)
dispatch_semaphore_t sema = dispatch_semaphore_create(0);
cmdBuf->addCompletedHandler(^(MTL::CommandBuffer* cb) {
    dispatch_semaphore_signal(sema);
});
dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
```

**Why dispatch_semaphore for CPU waits?** Metal's SharedEvent is designed for GPU-to-GPU synchronization (e.g., compute shader waiting on render shader). For CPU waits, the completion handler + `dispatch_semaphore` is the standard pattern.

## Resource Barriers (None Needed)

Unlike DX12 and Vulkan, **Metal automatically handles resource state transitions**. There are no explicit `ResourceBarrier` calls:

```cpp
// DX12 equivalent (REQUIRED):
cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
    texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_SHADER_RESOURCE));

// Metal equivalent (NOT NEEDED):
// Transition is implicit when you bind texture to different encoder stages
```

**The only exception**: **Aliasing** (reusing memory for different resources). Metal provides `makeAliasable()` for transient allocations:

```cpp
// Reuse heap memory for two textures in different passes
MTL::Texture* temp1 = heap->newTexture(desc1);
temp1->makeAliasable();  // Allow alias
// ... render with temp1 ...

MTL::Texture* temp2 = heap->newTexture(desc2);  // Reuses temp1's memory
// ... render with temp2 ...
```

## Shader Pipeline

The shader compilation pipeline for Metal follows the pattern:

```
HLSL Source
    ↓
DXC compiler (SM negotiation)
    ↓
SPIR-V bytecode
    ↓
Shader-Slang (SPIRV→MSL translation)
    ↓
MSL source code
    ↓
Metal compiler (metalc) or runtime MTLLibrary loading
    ↓
MTL::Function (GPU-executable code)
```

See [slang-metal-compilation.md](../05-shader-pipeline/07-slang-metal-compilation.md) for the complete pipeline.

**Binding translation** (HLSL register → Metal buffer index):

| HLSL Register | Metal Binding | Semantics |
|---|---|---|
| `t0-t999` | Buffer index 0-999 | Textures via argument buffer |
| `s0-s99` | Buffer index 1000-1099 | Samplers via argument buffer |
| `b0-b99` | Buffer index 1100-1199 | Constant buffers / Push constants |
| `u0-u99` | Buffer index 1200-1299 | UAV / RWBuffer |

All bindings are **unified** in argument buffers — no separate texture/sampler/buffer slots like DX12.

## Storage Modes

Metal resources must declare a **storage mode** — affecting where they live in memory and how they're accessed:

| Mode | Location | CPU Access | GPU Access | Use Case |
|------|----------|-----------|-----------|----------|
| **Private** | GPU-only VRAM | ❌ No | ✅ Yes | Textures, render targets, most buffers |
| **Shared** | System RAM | ✅ Yes | ✅ Yes (slower) | Upload buffers, staging, readback |
| **Managed** | GPU VRAM + System RAM | ✅ Yes (slow) | ✅ Yes | Macintosh only, rarely used |

**Mapping to RHIMemoryType:**

| RHIMemoryType | Metal Storage Mode |
|---|---|
| `GPU_ONLY` | Private |
| `CPU_VISIBLE` | Shared |
| `UPLOAD` | Shared (write-once, GPU read-many) |
| `READBACK` | Shared (GPU write-once, CPU read) |

**Performance tip**: Apple Silicon (M-series) has unified memory — all storage modes use the same physical RAM. Intel iGPU and discrete GPUs need careful placement (Private for GPU work, Shared for staging).

## Third-Party Dependencies

| Library | Version | Purpose | Integration |
|---------|---------|---------|-------------|
| **metal-cpp** | Latest | C++ wrapper for Metal API | Header-only, `thirdparty/metal-cpp/` |
| **Shader-Slang** | 0.27+ | SPIRV → MSL transpilation | Source or precompiled, dylib linking |
| **Cocoa framework** | macOS SDK | Window management (CAMetalLayer, NSWindow) | System framework, `#import` in `.mm` files only |

### Why metal-cpp Instead of Objective-C

The standard Metal API is Objective-C (`Metal.framework`). However:

1. **Apple provides metal-cpp**: A pure C++ wrapper that eliminates need for `.mm` files in the backend
2. **Fewer Objective-C interactions**: Only `CAMetalLayer` (swapchain) requires Objective-C — keeps it isolated
3. **Cross-compilation friendly**: Metal-cpp works with standard C++ compilers; raw Objective-C requires Xcode/Clang

The backend uses metal-cpp exclusively except for `metal_swapchain.mm` (minimal Objective-C).

### Why Shader-Slang

Shader-Slang is the industry-standard SPIR-V → MSL transpiler:

- **Maintained by NVIDIA**: Production-quality, regular updates
- **MSL code quality**: Generates efficient, readable MSL (vs SPIRV-Cross which is more mechanical)
- **Binding translation**: Built-in support for HLSL register → Metal buffer index mapping

See [slang-metal-compilation.md](../05-shader-pipeline/07-slang-metal-compilation.md) for detailed integration.

## Performance

Benchmark targets for Milestone 2.0 (M3 Max, 16-core GPU):

| Demo | Target Frame Rate | Frame Time |
|------|-----------------|-----------|
| triangle | 5000+ FPS | <0.2ms |
| textured_quad | 4500+ FPS | <0.22ms |
| camera_test | 300+ FPS | <3.3ms |
| mesh_test | 280+ FPS | <3.6ms |

**Metal advantages over DX12/Vulkan on macOS:**
- No SPIR-V compilation overhead (direct HLSL → MSL)
- Implicit synchronization (no barrier overhead)
- Apple Silicon: Unified memory, zero-copy between CPU/GPU

**Metal disadvantages:**
- Apple/macOS only (limited market penetration vs Windows)
- Smaller developer community than DX12/Vulkan

## Phase Roadmap

| Phase | Component | Status | Notes |
|-------|-----------|--------|-------|
| **Phase 3** | MetalDevice (GPU init, feature detection) | **Done** | Family detection, shader model negotiation, argument buffer setup |
| **Phase 3** | MetalCommandList (encoder-based command recording) | **Done** | Auto-switching, deferred index buffer binding, ResourceBarrier no-op |
| **Phase 3** | MetalSwapChain (back buffer acquisition + presentation) | **Done** | CAMetalLayer integration, V-Sync control, framebufferOnly optimization |
| **Phase 3** | MetalFence (CPU/GPU synchronization) | **Done** | SharedEvent timeline fences, completion handlers for CPU waits |
| **Phase 4** | Build integration (Zig build.zig + CMake Windows support) | **In Progress** | macOS platform detection, Metal framework linking, .mm compilation flags |
| **Phase 4** | Factory registration (CreateRHIDevice Metal backend) | **In Progress** | Platform auto-detection, shader model selection, config validation |
| **Phase 5+** | Validation layer + shader slang integration | **Planned** | Diagnostic tools, runtime error checking |
| **Phase 5+** | Metal tests (comprehensive test suite) | **Planned** | Device creation, command encoding, synchronization, swapchain lifecycle |

## Rules

1. **NEVER** include `<Metal/Metal.hpp>` outside `runtime/core/rhi/metal/` — all Metal types are internal to the backend
2. **NEVER** use Objective-C in backend files except `metal_swapchain.mm` — keep Objective-C isolated to swapchain only
3. **ALWAYS** use **metal-cpp** (C++ wrapper) for type-safe Metal API access — never mix Objective-C and backend C++
4. **ALWAYS** use `MTL::SharedEvent` for fences, never `dispatch_semaphore_t` for GPU-GPU synchronization — semaphores are for CPU waits only
5. **ALWAYS** require **Argument Buffers Tier 2** for bindless rendering — Tier 1 devices fall back to SM5.0 path
6. **ALWAYS** use Shader-Slang for HLSL → MSL compilation — never manual MSL authoring or SPIRV-Cross
7. **NEVER** manually manage Metal resource state transitions — they are automatic; only use `makeAliasable()` for memory aliasing
8. **ALWAYS** validate GPU family support at init** — log adapter name, family, memory, and argument buffer tier
9. **ALWAYS** defer index buffer binding until DrawMesh** — Metal requires buffers bound at encode time, not earlier
10. **ALWAYS** set framebufferOnly=YES on back buffer drawables** — enables TBDR optimization for render-target-only usage
11. **ALWAYS** control V-Sync via metalLayer.displaySyncEnabled** — never expose explicit present mode selection to the RHI user
