# RHI Module Reference

Abstract GPU interfaces and type definitions.

> **Headers**: `core/rhi/rhi.h`, `core/rhi/rhi_types.h`, `core/rhi/rhi_backend.h`, `core/rhi/rhi_config.h`
> **Guides**: [RHI Overview](../../guides/rhi/overview.md) | [RHI Config Reference](rhi-config.md)

## Handles and Types

### RHIHandle\<Tag\>

Typed generational handle for GPU resources.

```cpp
template <typename Tag>
struct RHIHandle {
    uint32_t index;       // Slot index in backend pool
    uint32_t generation;  // Version counter (0 = invalid)

    bool IsValid() const;
    static RHIHandle Invalid();
};
```

| Alias | Tag | Description |
|-------|-----|-------------|
| `RHIBufferHandle` | `BufferTag` | GPU buffers |
| `RHITextureHandle` | `TextureTag` | GPU textures |
| `RHIPipelineHandle` | `PipelineTag` | Pipeline state objects |
| `RHIShaderHandle` | `ShaderTag` | Shader modules |
| `RHISamplerHandle` | `SamplerTag` | Texture samplers |

---

## Enums

### RHIFormat

```cpp
enum class RHIFormat : uint8_t {
    Unknown,
    // Color
    R8Unorm, RG8Unorm, RGBA8Unorm, RGBA8Srgb, BGRA8Unorm, BGRA8Srgb,
    R16Float, RG16Float, RGBA16Float,
    R32Float, RG32Float, RGB32Float, RGBA32Float,
    R32Uint, RG32Uint, RGBA32Uint,
    // Depth/stencil
    D16Unorm, D32Float, D24UnormS8Uint, D32FloatS8Uint,
};
```

### RHIBufferUsage (flags)

`None`, `VertexBuffer`, `IndexBuffer`, `ConstantBuffer`, `StructuredBuffer`, `UnorderedAccess`, `CopySrc`, `CopyDst`

### RHITextureUsage (flags)

`None`, `ShaderResource`, `RenderTarget`, `DepthStencil`, `UnorderedAccess`, `CopySrc`, `CopyDst`

### RHIQueueType

`Graphics`, `Compute`, `Copy`

### RHIIndexFormat

`Uint16`, `Uint32`

---

## IRHIDevice

Central GPU device and resource factory. Thread-safe.

### Resource Creation

| Method | Returns | Description |
|--------|---------|-------------|
| `CreateBuffer(RHIBufferDesc)` | `RHIBufferHandle` | Create GPU buffer |
| `CreateTexture(RHITextureDesc)` | `RHITextureHandle` | Create GPU texture |
| `CreateShader(RHIShaderDesc)` | `RHIShaderHandle` | Create shader module |
| `CreateGraphicsPipeline(RHIGraphicsPipelineDesc)` | `RHIPipelineHandle` | Create PSO |
| `CreateSampler(RHISamplerDesc)` | `RHISamplerHandle` | Create sampler |
| `CreateSwapChain(RHISwapChainDesc)` | `IRHISwapChain*` | Create swapchain |
| `CreateFence()` | `IRHIFence*` | Create fence (initial value 0) |
| `CreateCommandList(RHIQueueType)` | `IRHICommandList*` | Create command list |

### Resource Destruction

| Method | Description |
|--------|-------------|
| `DestroyBuffer(handle)` | Invalidates handle |
| `DestroyTexture(handle)` | Invalidates handle |
| `DestroyShader(handle)` | Invalidates handle |
| `DestroyPipeline(handle)` | Invalidates handle |
| `DestroySampler(handle)` | Invalidates handle |
| `DestroySwapChain(ptr)` | Destroys swapchain |
| `DestroyFence(ptr)` | Destroys fence |
| `DestroyCommandList(ptr)` | Destroys command list |

### Queries

| Method | Returns | Description |
|--------|---------|-------------|
| `GetAdapterName()` | `const char*` | GPU name (e.g., "NVIDIA GeForce RTX 4090") |
| `GetFeatureLevel()` | `const char*` | Feature level string |
| `GetMemoryBudget()` | `RHIMemoryBudget` | VRAM budget and usage |

### Synchronization

| Method | Description |
|--------|-------------|
| `WaitIdle()` | Flush all queues, block until complete. Full pipeline stall. |

---

## IRHICommandList

GPU command recorder. **Not thread-safe** — one thread per list.

### Lifecycle

| Method | Description |
|--------|-------------|
| `Begin()` | Reset and open for recording |
| `End()` | Close recording |
| `Execute()` | Submit to GPU queue |

### Binding

| Method | Description |
|--------|-------------|
| `SetPipeline(handle)` | Bind graphics/compute pipeline |
| `SetVertexBuffer(slot, handle, offset)` | Bind vertex buffer |
| `SetIndexBuffer(handle, format, offset)` | Bind index buffer |
| `SetViewport(viewport)` | Set viewport rect |
| `SetScissor(scissor)` | Set scissor rect |
| `SetDescriptorIndex(rootParam, index)` | Push bindless descriptor index |

### Drawing

| Method | Description |
|--------|-------------|
| `DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)` | Indexed draw |
| `Draw(vertexCount, instanceCount, firstVertex, firstInstance)` | Non-indexed draw |
| `Dispatch(groupX, groupY, groupZ)` | Compute dispatch |

### Barriers and Clears

| Method | Description |
|--------|-------------|
| `ResourceBarrier(barriers, count)` | Insert state transitions |
| `ClearRenderTarget(texture, color[4])` | Clear RTV to color |
| `ClearDepthStencil(texture, depth, stencil)` | Clear DSV |
| `BeginRenderPass(desc)` | Start render pass |
| `EndRenderPass()` | End render pass |

### Copy

| Method | Description |
|--------|-------------|
| `CopyBuffer(src, srcOffset, dst, dstOffset, size)` | Buffer-to-buffer copy |
| `CopyTexture(src, dst)` | Full-subresource texture copy |

---

## IRHISwapChain

Window presentation. Triple-buffered (FLIP_DISCARD on DX12).

| Method | Description |
|--------|-------------|
| `GetCurrentBackBuffer()` | Current back buffer texture handle |
| `GetCurrentBackBufferIndex()` | Index 0..bufferCount-1 |
| `Present(vsync)` | Present to display |
| `Resize(width, height)` | Resize back buffers (invalidates handles) |
| `GetWidth() / GetHeight()` | Current dimensions |
| `GetFormat()` | Back buffer format |

---

## IRHIFence

CPU/GPU synchronization. Monotonically increasing values.

| Method | Description |
|--------|-------------|
| `Signal(value)` | Signal from GPU timeline |
| `WaitCPU(value)` | Block CPU until fence reaches value |
| `IsComplete(value)` | Non-blocking check |
| `GetCompletedValue()` | Highest completed value |

---

## RHI Config (`rhi_config.h`)

### ShaderModel

```cpp
enum class ShaderModel : uint8_t {
    SM5_0,  // DX11 baseline
    SM5_1,  // DX11.3+ (dynamic indexing)
    SM6_0,  // DX12 baseline (wave intrinsics)
    SM6_1,  // Barycentric intrinsics, SV_ViewID
    SM6_2,  // Float16, denorm mode control
    SM6_3,  // DirectX Raytracing (DXR 1.0)
    SM6_4,  // Variable rate shading, low-precision packed dot
    SM6_5,  // DXR 1.1, mesh shaders, sampler feedback
    SM6_6,  // DX12 Ultimate (bindless, dynamic resources)
    SM6_7,  // Advanced texture ops, relaxed precision
};
```

### RHIConfig

```cpp
struct RHIConfig {
    RHIBackend backend;          // DX12, Vulkan, Metal, Null
    ShaderModel shaderModel;     // SM5_0..SM6_7
    bool enableValidation;
    bool enableGpuValidation;
    bool enableBreadcrumbs;
    uint32_t adapterIndex;
};
```

### Functions

| Function | Description |
|----------|-------------|
| `ResolveRHIConfig(Config)` | Resolve from CLI > .uzproject > auto-detect |
| `RHIBackendToString(backend)` | "DX12", "Vulkan", etc. |
| `ShaderModelToString(sm)` | "SM5.0", "SM6.6", "SM6.7", etc. |
| `ToDeviceDesc(config)` | Convert to RHIDeviceDesc |

---

## Backend Factory (`rhi_backend.h`)

| Function | Description |
|----------|-------------|
| `CreateRHIDevice(RHIDeviceDesc)` | Create backend device (DX12, Vulkan, etc.) |
| `DestroyRHIDevice(IRHIDevice*)` | Destroy device and all owned resources |
