# RHI — Render Hardware Interface

The RHI is the abstraction layer between UzEngine's renderer and GPU APIs.
All rendering code uses RHI interfaces — no DX12, Vulkan, or Metal types leak
above the backend layer.

> **Related API**: [IRHIDevice](../../reference/rhi/index.md),
> [rhi_types.h](../../reference/rhi/index.md#handles-and-types)

## Design Principles

1. **Backend-agnostic** — `rhi.h` and `rhi_types.h` contain zero platform headers
2. **Typed generational handles** — GPU resources use `RHIHandle<Tag>` with index + generation to prevent use-after-free
3. **SM6.6 bindless first** — One global descriptor heap, shaders access resources via push-constant index
4. **Factory pattern** — `CreateRHIDevice()` / `DestroyRHIDevice()` are the only backend entry points

## Core Interfaces

### IRHIDevice

The central GPU device. Creates and destroys all resources.

```cpp
auto* device = CreateRHIDevice(deviceDesc);

// Create resources
auto buffer = device->CreateBuffer(bufferDesc);
auto texture = device->CreateTexture(textureDesc);
auto pipeline = device->CreateGraphicsPipeline(pipelineDesc);

// Cleanup
device->DestroyBuffer(buffer);
device->DestroyTexture(texture);
device->DestroyPipeline(pipeline);
DestroyRHIDevice(device);
```

Thread-safe: all Create/Destroy methods are internally synchronized.

### IRHICommandList

Records GPU commands. **Not thread-safe** — one thread per command list.

```cpp
auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);

cmdList->Begin();
cmdList->SetPipeline(pipeline);
cmdList->SetVertexBuffer(0, vertexBuffer);
cmdList->SetIndexBuffer(indexBuffer, RHIIndexFormat::Uint16);
cmdList->SetViewport(viewport);
cmdList->DrawIndexed(indexCount);
cmdList->End();
cmdList->Execute();
```

### IRHISwapChain

Triple-buffered presentation to a window surface.

```cpp
auto* swapChain = device->CreateSwapChain(swapChainDesc);
// ...
swapChain->Present(vsync);
swapChain->Resize(newWidth, newHeight);
```

### IRHIFence

CPU/GPU synchronization with monotonically increasing values.

```cpp
auto* fence = device->CreateFence();
queue->Signal(fence, ++fenceValue);
fence->WaitCPU(fenceValue);  // blocks until GPU reaches this value
```

## Resource Handles

GPU resources are referenced by typed handles, not raw pointers:

| Handle Type | Tag | Description |
|-------------|-----|-------------|
| `RHIBufferHandle` | `BufferTag` | Vertex, index, constant, structured buffers |
| `RHITextureHandle` | `TextureTag` | Render targets, depth stencil, SRVs |
| `RHIPipelineHandle` | `PipelineTag` | Graphics/compute PSOs |
| `RHIShaderHandle` | `ShaderTag` | Compiled shader modules |
| `RHISamplerHandle` | `SamplerTag` | Texture samplers |

Handles use generational validation:

```cpp
RHIBufferHandle handle = device->CreateBuffer(desc);
// handle.index = 42, handle.generation = 1

device->DestroyBuffer(handle);
// Slot 42 generation bumped to 2

// Stale handle detected:
// handle.generation (1) != pool[42].generation (2) → invalid
```

## Backend Selection

Resolved at startup via `ResolveRHIConfig()`:

```
CLI (--rhi=dx12)  >  .uzproject (render.rhi)  >  platform auto-detect
```

| Backend | Status | Platform |
|---------|--------|----------|
| DX12 SM6.7 | Implemented | Windows 10+ |
| DX12 SM6.6 | Implemented | Windows 10+ |
| DX12 SM5.x | Fallback | Windows 10+ |
| Vulkan | Planned | Windows, Linux |
| Metal | Planned | macOS |

## Shader Model Negotiation

The DX12 backend probes hardware capabilities at device creation:

1. Check `D3D12_FEATURE_SHADER_MODEL` for highest supported SM
2. Check `ResourceBindingTier` for Tier 3 (required for SM6.6 bindless)
3. Select highest supported: SM6.7 → SM6.6 → SM6.0 → SM5.1 → SM5.0
4. Adapt root signature: `DIRECTLY_INDEXED` flags only for SM6.6+

The Agility SDK bundled with UzEngine enables SM6.7 on hardware that supports it,
regardless of the OS-installed D3D12 runtime version.

## DX12 Backend Details

### Descriptor Heaps

| Heap | Capacity | Visibility | Purpose |
|------|----------|------------|---------|
| CBV/SRV/UAV | 1,000,000 | Shader-visible | SM6.6 bindless resource access |
| Sampler | 2,048 | Shader-visible | Texture samplers |
| RTV | 256 | CPU-only | Render target views |
| DSV | 64 | CPU-only | Depth-stencil views |

### Memory Management

GPU memory is managed by [D3D12 Memory Allocator (D3D12MA)](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) v3.1.0.
Upload and readback buffers have persistent CPU-mapped pointers for efficient
streaming.

### Debug Features

| Feature | Flag | Description |
|---------|------|-------------|
| D3D12 debug layer | `--gpu-validation` off | API call validation |
| GPU-based validation | `--gpu-validation` on | GPU-side checks (slow, thorough) |
| DRED breadcrumbs | `--gpu-breadcrumbs` | Tracks last commands before device removal |
| Page fault tracking | Always on (debug) | Reports faulting resource on TDR |

## Related Pages

- [Getting Started: First Window](../../getting-started/first-window.md) — Practical walkthrough
- [Architecture](../architecture.md) — System-level design
- [API: IRHIDevice](../../reference/rhi/index.md) — Full interface reference
