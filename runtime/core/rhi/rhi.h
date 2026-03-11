#pragma once

/// UzEngine Rendering Hardware Interface — Abstract Device, Command List, SwapChain, Fence
///
/// # Design
/// All rendering code above the backend layer uses only these abstract interfaces.
/// No DX12, Vulkan, or Metal types appear in this header or any file that includes it.
///
/// # Resource Model
/// GPU resources are referenced via typed generational handles (RHIHandle<Tag>).
/// The backend owns the actual GPU objects and maps handles to them internally.
///
/// # Bindless (SM6.6)
/// Resources get a persistent descriptor index in a global CBV_SRV_UAV heap.
/// Shaders access resources via `SetDescriptorIndex()` push constants.
///
/// # Threading
/// - IRHIDevice methods are thread-safe (internally synchronized).
/// - IRHICommandList is NOT thread-safe — one recording thread per list.
/// - IRHISwapChain::Present must be called from the thread that created it.

#include "rhi_types.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IRHIFence — CPU/GPU synchronization primitive
// ─────────────────────────────────────────────────────────────────────────────

/// GPU fence for synchronizing CPU and GPU work.
///
/// Fences are monotonically increasing: each Signal(value) must use a value
/// greater than any previously signaled value.
class IRHIFence {
public:
    virtual ~IRHIFence() = default;

    /// Signals the fence to the given value from the GPU timeline.
    virtual void Signal(uint64_t value) = 0;

    /// Blocks the calling CPU thread until the fence reaches the given value.
    virtual void WaitCPU(uint64_t value) = 0;

    /// Returns true if the fence has reached or passed the given value.
    virtual bool IsComplete(uint64_t value) const = 0;

    /// Returns the highest value the fence has completed.
    virtual uint64_t GetCompletedValue() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHICommandList — GPU command recording
// ─────────────────────────────────────────────────────────────────────────────

/// Records GPU commands for later submission.
///
/// # Lifecycle
/// 1. Begin() — resets and opens the command list for recording
/// 2. Record commands (SetPipeline, Draw, Barrier, etc.)
/// 3. End() — closes recording
/// 4. Execute() — submits to the GPU queue
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into a command list at a time.
class IRHICommandList {
public:
    virtual ~IRHICommandList() = default;

    // ── Recording lifecycle ──────────────────────────────────────────────

    /// Resets and opens the command list for recording.
    virtual void Begin() = 0;

    /// Closes the command list. No more commands may be recorded after this.
    virtual void End() = 0;

    /// Submits the recorded commands to the GPU queue for execution.
    virtual void Execute() = 0;

    // ── Pipeline and resource binding ────────────────────────────────────

    /// Binds a graphics or compute pipeline for subsequent draw/dispatch calls.
    virtual void SetPipeline(RHIPipelineHandle pipeline) = 0;

    /// Binds a vertex buffer to the given slot.
    virtual void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset = 0) = 0;

    /// Binds an index buffer.
    virtual void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset = 0) = 0;

    /// Sets the viewport rectangle.
    virtual void SetViewport(const RHIViewport& viewport) = 0;

    /// Sets the scissor rectangle.
    virtual void SetScissor(const RHIScissorRect& scissor) = 0;

    /// Pushes a descriptor index for SM6.6 bindless resource access.
    ///
    /// The shader reads this index via a root constant to access
    /// the resource in the global descriptor heap.
    virtual void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) = 0;

    // ── Draw and dispatch ────────────────────────────────────────────────

    /// Issues an indexed draw call.
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;

    /// Issues a non-indexed draw call.
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

    /// Issues a compute dispatch.
    virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    // ── Barriers ─────────────────────────────────────────────────────────

    /// Inserts a resource barrier (state transition).
    ///
    /// Backends translate RHIResourceState to native barrier calls
    /// (D3D12 ResourceBarrier, Vulkan PipelineBarrier2, etc.).
    virtual void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) = 0;

    // ── Clear operations ─────────────────────────────────────────────────

    /// Clears a render target to the given RGBA color.
    virtual void ClearRenderTarget(RHITextureHandle texture, const float color[4]) = 0;

    /// Clears a depth-stencil target.
    virtual void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) = 0;

    // ── Copy operations ──────────────────────────────────────────────────

    /// Copies data between buffers.
    virtual void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                            RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) = 0;

    /// Copies data between textures (full subresource).
    virtual void CopyTexture(RHITextureHandle src, RHITextureHandle dst) = 0;

    // ── Render pass scope ────────────────────────────────────────────────

    /// Begins a render pass with the given attachments.
    virtual void BeginRenderPass(const RHIRenderPassDesc& desc) = 0;

    /// Ends the current render pass.
    virtual void EndRenderPass() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — presentation to a window surface
// ─────────────────────────────────────────────────────────────────────────────

/// Manages back buffers and presentation for a window.
///
/// Triple-buffered by default (3 back buffers). The swap chain owns the
/// back buffer textures — they are valid as long as the swap chain exists
/// and are invalidated on Resize().
class IRHISwapChain {
public:
    virtual ~IRHISwapChain() = default;

    /// Returns a handle to the current back buffer texture.
    virtual RHITextureHandle GetCurrentBackBuffer() = 0;

    /// Returns the index of the current back buffer (0..bufferCount-1).
    virtual uint32_t GetCurrentBackBufferIndex() const = 0;

    /// Presents the current back buffer to the display.
    virtual void Present(bool vsync) = 0;

    /// Resizes all back buffers. Invalidates existing back buffer handles.
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    /// Returns the current back buffer width.
    virtual uint32_t GetWidth() const = 0;

    /// Returns the current back buffer height.
    virtual uint32_t GetHeight() const = 0;

    /// Returns the back buffer format.
    virtual RHIFormat GetFormat() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHIDevice — GPU device and resource factory
// ─────────────────────────────────────────────────────────────────────────────

/// The central GPU device interface. Creates and destroys all GPU resources.
///
/// # Ownership
/// The device owns all resources created through it. Destroying the device
/// implicitly destroys all outstanding resources (but leaking handles is
/// a bug — destroy explicitly for deterministic cleanup).
///
/// # Thread Safety
/// All Create/Destroy methods are internally synchronized and safe to call
/// from any thread.
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // ── Buffer operations ────────────────────────────────────────────────

    /// Creates a GPU buffer.
    virtual RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) = 0;

    /// Destroys a GPU buffer. The handle becomes invalid after this call.
    virtual void DestroyBuffer(RHIBufferHandle handle) = 0;

    /// Returns the persistently mapped pointer for Upload/Readback buffers.
    /// Returns nullptr for Default-heap buffers or invalid handles.
    virtual void* MapBuffer(RHIBufferHandle handle) = 0;

    /// Returns the bindless SRV descriptor index for a structured buffer.
    /// Returns UINT32_MAX if the buffer has no SRV (e.g., not a StructuredBuffer).
    virtual uint32_t GetBufferSRVIndex(RHIBufferHandle handle) const = 0;

    // ── Texture operations ───────────────────────────────────────────────

    /// Creates a GPU texture.
    virtual RHITextureHandle CreateTexture(const RHITextureDesc& desc) = 0;

    /// Destroys a GPU texture. The handle becomes invalid after this call.
    virtual void DestroyTexture(RHITextureHandle handle) = 0;

    // ── Shader operations ────────────────────────────────────────────────

    /// Creates a shader module from compiled bytecode.
    virtual RHIShaderHandle CreateShader(const RHIShaderDesc& desc) = 0;

    /// Destroys a shader module.
    virtual void DestroyShader(RHIShaderHandle handle) = 0;

    // ── Pipeline operations ──────────────────────────────────────────────

    /// Creates a graphics pipeline state object.
    ///
    /// PSO creation may be expensive. The backend caches PSOs internally
    /// via content-hash lookup to avoid redundant compilation.
    virtual RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;

    /// Destroys a pipeline state object.
    virtual void DestroyPipeline(RHIPipelineHandle handle) = 0;

    // ── Sampler operations ───────────────────────────────────────────────

    /// Creates a texture sampler.
    virtual RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) = 0;

    /// Destroys a texture sampler.
    virtual void DestroySampler(RHISamplerHandle handle) = 0;

    // ── Swap chain operations ────────────────────────────────────────────

    /// Creates a swap chain for presenting to a window.
    virtual IRHISwapChain* CreateSwapChain(const RHISwapChainDesc& desc) = 0;

    /// Destroys a swap chain.
    virtual void DestroySwapChain(IRHISwapChain* swapChain) = 0;

    // ── Fence operations ─────────────────────────────────────────────────

    /// Creates a GPU fence with initial value 0.
    virtual IRHIFence* CreateFence() = 0;

    /// Destroys a GPU fence.
    virtual void DestroyFence(IRHIFence* fence) = 0;

    // ── Command list operations ──────────────────────────────────────────

    /// Creates a command list for the given queue type.
    virtual IRHICommandList* CreateCommandList(RHIQueueType queue = RHIQueueType::Graphics) = 0;

    /// Destroys a command list.
    virtual void DestroyCommandList(IRHICommandList* cmdList) = 0;

    // ── Synchronization ──────────────────────────────────────────────────

    /// Flushes all GPU queues and blocks until all submitted work completes.
    ///
    /// Use sparingly — this is a full pipeline stall. Primarily for shutdown
    /// and resource destruction that cannot be deferred.
    virtual void WaitIdle() = 0;

    // ── Device queries ───────────────────────────────────────────────────

    /// Returns the GPU adapter name (e.g., "NVIDIA GeForce RTX 4090").
    virtual const char* GetAdapterName() const = 0;

    /// Returns the feature level string (e.g., "D3D_FEATURE_LEVEL_12_2").
    virtual const char* GetFeatureLevel() const = 0;

    /// Reports the current VRAM budget and usage.
    virtual RHIMemoryBudget GetMemoryBudget() const = 0;
};

} // namespace uz
