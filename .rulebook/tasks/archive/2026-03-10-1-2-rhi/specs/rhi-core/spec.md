# RHI Core Interface Specification

## Overview
The RHI (Rendering Hardware Interface) provides a unified GPU abstraction layer via C++ abstract classes (virtual interfaces). All rendering code above the backend uses only these interfaces. Backend implementations (DX12, Vulkan, Metal) are swappable via the factory function.

## File Structure
```
runtime/core/rhi/
├── rhi_types.h              # Enums, descriptors, RHIHandle<Tag> generational handles
├── rhi.h                    # Abstract interfaces: IRHIDevice, IRHICommandList, IRHISwapChain, IRHIFence
├── rhi_backend.h            # Backend enum, RHIDeviceDesc, CreateRHIDevice/DestroyRHIDevice factory
└── dx12/                    # DX12 SM6.6 backend implementation
    ├── dx12_device.h/.cpp       # DX12Device : IRHIDevice
    ├── dx12_command_list.h/.cpp # DX12CommandList : IRHICommandList
    ├── dx12_swapchain.h/.cpp    # DX12SwapChain : IRHISwapChain
    ├── dx12_fence.h/.cpp        # DX12Fence : IRHIFence
    └── dx12_backend.cpp         # CreateRHIDevice factory for DX12
```

## Handle System

GPU resources use typed generational handles — no raw pointers cross the RHI boundary.

```cpp
template <typename Tag>
struct RHIHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
    bool IsValid() const { return generation != 0; }
    static RHIHandle Invalid() { return {0, 0}; }
};

using RHIBufferHandle   = RHIHandle<BufferTag>;
using RHITextureHandle  = RHIHandle<TextureTag>;
using RHIPipelineHandle = RHIHandle<PipelineTag>;
using RHIShaderHandle   = RHIHandle<ShaderTag>;
using RHISamplerHandle  = RHIHandle<SamplerTag>;
```

Generation counters prevent use-after-free — same pattern as `uz_handle_map.h`.

## Core Interfaces

### IRHIDevice — GPU device and resource factory
```cpp
class IRHIDevice {
    // Resource creation/destruction (all handle-based)
    virtual RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) = 0;
    virtual void DestroyBuffer(RHIBufferHandle handle) = 0;
    virtual RHITextureHandle CreateTexture(const RHITextureDesc& desc) = 0;
    virtual void DestroyTexture(RHITextureHandle handle) = 0;
    virtual RHIShaderHandle CreateShader(const RHIShaderDesc& desc) = 0;
    virtual void DestroyShader(RHIShaderHandle handle) = 0;
    virtual RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;
    virtual void DestroyPipeline(RHIPipelineHandle handle) = 0;
    virtual RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) = 0;
    virtual void DestroySampler(RHISamplerHandle handle) = 0;

    // Object creation (pointer-based, device owns lifetime)
    virtual IRHISwapChain* CreateSwapChain(const RHISwapChainDesc& desc) = 0;
    virtual void DestroySwapChain(IRHISwapChain* swapChain) = 0;
    virtual IRHIFence* CreateFence() = 0;
    virtual void DestroyFence(IRHIFence* fence) = 0;
    virtual IRHICommandList* CreateCommandList(RHIQueueType queue) = 0;
    virtual void DestroyCommandList(IRHICommandList* cmdList) = 0;

    // Sync and queries
    virtual void WaitIdle() = 0;
    virtual const char* GetAdapterName() const = 0;
    virtual const char* GetFeatureLevel() const = 0;
    virtual RHIMemoryBudget GetMemoryBudget() const = 0;
};
```

### IRHICommandList — GPU command recording
```cpp
class IRHICommandList {
    // Lifecycle: Begin() → record → End() → Execute()
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Execute() = 0;

    // Binding
    virtual void SetPipeline(RHIPipelineHandle pipeline) = 0;
    virtual void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset = 0) = 0;
    virtual void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset = 0) = 0;
    virtual void SetViewport(const RHIViewport& viewport) = 0;
    virtual void SetScissor(const RHIScissorRect& scissor) = 0;
    virtual void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) = 0;

    // Draw/Dispatch
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, ...) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, ...) = 0;
    virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    // Barriers, Clear, Copy, Render Pass
    virtual void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) = 0;
    virtual void ClearRenderTarget(RHITextureHandle texture, const float color[4]) = 0;
    virtual void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) = 0;
    virtual void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset, RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) = 0;
    virtual void CopyTexture(RHITextureHandle src, RHITextureHandle dst) = 0;
    virtual void BeginRenderPass(const RHIRenderPassDesc& desc) = 0;
    virtual void EndRenderPass() = 0;
};
```

### IRHISwapChain — presentation
```cpp
class IRHISwapChain {
    virtual RHITextureHandle GetCurrentBackBuffer() = 0;
    virtual uint32_t GetCurrentBackBufferIndex() const = 0;
    virtual void Present(bool vsync) = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual RHIFormat GetFormat() const = 0;
};
```

### IRHIFence — CPU/GPU synchronization
```cpp
class IRHIFence {
    virtual void Signal(uint64_t value) = 0;
    virtual void WaitCPU(uint64_t value) = 0;
    virtual bool IsComplete(uint64_t value) const = 0;
    virtual uint64_t GetCompletedValue() const = 0;
};
```

### Factory
```cpp
enum class RHIBackend : uint8_t { DX12, Vulkan, Metal, Null };

struct RHIDeviceDesc {
    RHIBackend backend = RHIBackend::DX12;
    bool enableValidation = false;
    bool enableGpuValidation = false;
    uint32_t adapterIndex = 0;
};

IRHIDevice* CreateRHIDevice(const RHIDeviceDesc& desc);
void DestroyRHIDevice(IRHIDevice* device);
```

## DX12 SM6.6 Backend Requirements

### Device Initialization
- IDXGIFactory6 + IDXGIAdapter4 enumeration (prefer high-performance GPU)
- ID3D12Device10 with feature level auto-detection (SM6.6 → fallback SM6.0)
- D3D12MemoryAllocator (D3D12MA) for GPU heap management
- Three memory pools: Default (GPU-only), Upload (CPU→GPU), Readback (GPU→CPU)
- Global CBV_SRV_UAV descriptor heap (SM6.6 bindless)
- Global sampler descriptor heap

### Command Recording
- Triple-buffered command allocators (one per frame-in-flight)
- ID3D12GraphicsCommandList7 wrapping
- Root signature with bindless push constants (SM6.6 ResourceDescriptorHeap[])

### Pipeline State
- PSO cache with content-hash lookup (xxHash64)
- Single global root signature for SM6.6 bindless model
- Async PSO compilation on worker thread (future)

### Swapchain
- IDXGISwapChain4 via CreateSwapChainForHwnd
- Triple-buffered back buffers
- DXGI_PRESENT_ALLOW_TEARING for variable refresh rate
- Resize: release back buffer refs → ResizeBuffers → re-acquire

### Debug & Profiling
- ID3D12Debug6 activation in dev builds
- GPU-based validation (optional)
- DRED for device removal diagnostics
- GPU timestamp queries (triple-buffered ID3D12QueryHeap)
- GPU breadcrumbs via WriteBufferImmediate

## Design Decisions

### Why C++ Virtual Classes (not C API)?
- Type-safe method dispatch via vtable (compile-time errors for wrong signatures)
- Natural inheritance for backend implementations (DX12Device : IRHIDevice)
- Still ABI-stable within a single binary (no cross-DLL boundary)
- C wrapper can be added later for N-API binding if needed

### Why Handle-Based Resources?
- No raw GPU resource pointers cross the abstraction boundary
- Generation counters detect use-after-free in debug builds
- Backend manages actual COM/API objects internally
- Same pattern as HandleMap from Milestone 1.1

### Why Compile-Time Backend Selection?
- Single backend per build (no runtime overhead)
- No DX12/Vulkan headers pulled into non-backend code
- UZ_BACKEND_DX12/UZ_BACKEND_VULKAN defines control compilation

### Resource Lifetime
- Device owns all resources — destroying device implicitly destroys everything
- Explicit Destroy*() preferred for deterministic cleanup
- WaitIdle() called before device destruction
- Double-free checks via generation counter in debug builds

## Testing Requirements

#### Scenario: Handle validity
- RHIHandle::Invalid() has generation=0, IsValid()=false
- Created handles have generation>0, IsValid()=true
- Handles with same index but different generation are not equal

#### Scenario: Enum bitwise operations
- BufferUsage flags combine with |, test with HasFlag()
- TextureUsage flags combine with |, test with HasFlag()

#### Scenario: Device creation (requires GPU)
- CreateRHIDevice with DX12 backend succeeds on SM6.6 hardware
- GetAdapterName returns non-empty string
- GetFeatureLevel returns valid level string
- GetMemoryBudget reports non-zero budget

#### Scenario: Clear screen render loop
- Create device + swapchain + command list + fence
- Record: Barrier(Present→RT) → ClearRenderTarget(animated color) → Barrier(RT→Present)
- Execute + Present for 10 frames
- No D3D12 debug layer validation errors
- Frame time ≤16.6ms (60fps target)

#### Scenario: Swapchain resize
- Create swapchain at 800x600
- Resize to 1280x720
- GetWidth()/GetHeight() reflect new size
- Present succeeds after resize

#### Scenario: Shutdown cleanup
- Destroy all resources explicitly
- DestroyRHIDevice succeeds
- No GPU memory leaks (D3D12MA reports zero live allocations)
