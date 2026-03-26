#pragma once

/// RHI device interface for GPU resource creation, queue submission, and synchronization.

#include "rhi_fence.h"
#include "rhi_command_list.h"
#include "rhi_swapchain.h"
#include "rhi_types.h"
#include "rhi_gpu_profiler_types.h"

#include <cstdint>
#include <vector>

namespace uz {

// Forward declare — defined in rhi_config.h. Avoids circular include
// (rhi.h → rhi_config.h → rhi_backend.h → rhi.h).
enum class ShaderModel : uint8_t;

// Forward declare — defined in rhi_backend.h. Avoids circular include
// (rhi.h → rhi_backend.h → rhi.h).
enum class RHIBackend : uint8_t;

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
/// All `Create*`/`Destroy*` methods are internally synchronized and safe to call
/// from any thread. Device queries (`GetAdapterName`, `GetMemoryBudget`) are
/// also thread-safe.
///
/// # Lifetime
/// Created via `CreateRHIDevice()` (see `rhi_backend.h`), which returns a
/// `std::unique_ptr<IRHIDevice>`. The device must outlive all resources
/// created from it.
///
/// # Examples
/// ```cpp
/// RHIDeviceDesc desc{};
/// desc.backend = RHIBackend::DX12;
/// desc.enableValidation = true;
/// auto device = CreateRHIDevice(desc);
///
/// auto buffer = device->CreateBuffer({.sizeBytes = 1024, .usage = RHIBufferUsage::VertexBuffer});
/// // ... use buffer ...
/// device->DestroyBuffer(buffer);
/// // device is destroyed automatically when unique_ptr goes out of scope
/// ```
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // ── Buffer operations ────────────────────────────────────────────────

    /// Creates a GPU buffer with the specified size, usage, and memory type.
    ///
    /// # Arguments
    /// * `desc` — Buffer descriptor (size, stride, usage flags, memory type, debug name).
    ///
    /// # Returns
    /// A valid `RHIBufferHandle` on success, or `RHIBufferHandle::Invalid()` on failure.
    virtual RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) = 0;

    /// Destroys a GPU buffer and frees its memory and descriptor slots.
    ///
    /// # Arguments
    /// * `handle` — Buffer handle to destroy. Becomes invalid after this call.
    ///   Passing an invalid handle logs a warning and returns.
    virtual void DestroyBuffer(RHIBufferHandle handle) = 0;

    /// Returns a CPU-writable pointer for Upload/Readback buffers.
    ///
    /// On DX12/Vulkan, returns a persistently mapped pointer (no Unmap needed).
    /// On DX11, returns a transiently mapped pointer — `UnmapBuffer()` MUST be
    /// called after writing data, before the GPU can access the buffer.
    ///
    /// # Arguments
    /// * `handle` — Buffer handle to map.
    ///
    /// # Returns
    /// A valid pointer for `RHIMemoryType::Upload` or `RHIMemoryType::Readback` buffers.
    /// Returns `nullptr` for `Default`-heap buffers or invalid handles.
    virtual void* MapBuffer(RHIBufferHandle handle) = 0;

    /// Unmaps a previously mapped buffer, flushing writes to the GPU.
    ///
    /// On DX12/Vulkan this is a no-op (buffers are persistently mapped).
    /// On DX11 this calls `ID3D11DeviceContext::Unmap()` — **required** after
    /// writing to a mapped Upload buffer.
    ///
    /// # Arguments
    /// * `handle` — Buffer handle previously passed to `MapBuffer()`.
    virtual void UnmapBuffer(RHIBufferHandle handle) = 0;

    /// Returns the bindless SRV descriptor index for a structured buffer.
    ///
    /// # Arguments
    /// * `handle` — Buffer handle to query.
    ///
    /// # Returns
    /// The SRV index in the global CBV_SRV_UAV heap, or `UINT32_MAX` if the
    /// buffer has no SRV (e.g., not created with `RHIBufferUsage::StructuredBuffer`).
    virtual uint32_t GetBufferSRVIndex(RHIBufferHandle handle) const = 0;

    /// Returns the bindless UAV descriptor index for a buffer.
    ///
    /// # Arguments
    /// * `handle` — Buffer handle to query.
    ///
    /// # Returns
    /// The UAV index in the global CBV_SRV_UAV heap, or `UINT32_MAX` if the
    /// buffer has no UAV (e.g., not created with `RHIBufferUsage::UnorderedAccess`).
    virtual uint32_t GetBufferUAVIndex(RHIBufferHandle handle) const = 0;

    /// Returns the GPU virtual address of a buffer.
    ///
    /// Used for root CBV binding (`SetGraphicsRootConstantBufferView`).
    ///
    /// # Arguments
    /// * `handle` — Buffer handle to query.
    ///
    /// # Returns
    /// The GPU virtual address, or `0` if the handle is invalid.
    virtual uint64_t GetBufferGPUAddress(RHIBufferHandle handle) const = 0;

    // ── Texture operations ───────────────────────────────────────────────

    /// Creates a GPU texture with the specified dimensions, format, and usage.
    ///
    /// Automatically creates SRV (if `ShaderResource`), RTV (if `RenderTarget`),
    /// DSV (if `DepthStencil`), and UAV (if `UnorderedAccess`) descriptors.
    ///
    /// # Arguments
    /// * `desc` — Texture descriptor (dimensions, format, usage flags, debug name).
    ///
    /// # Returns
    /// A valid `RHITextureHandle` on success, or `RHITextureHandle::Invalid()` on failure.
    virtual RHITextureHandle CreateTexture(const RHITextureDesc& desc) = 0;

    /// Destroys a GPU texture and frees its memory and descriptor slots.
    ///
    /// # Arguments
    /// * `handle` — Texture handle to destroy. Becomes invalid after this call.
    virtual void DestroyTexture(RHITextureHandle handle) = 0;

    /// Returns the bindless SRV descriptor index for a texture.
    ///
    /// # Arguments
    /// * `handle` — Texture handle to query.
    ///
    /// # Returns
    /// The SRV index in the global CBV_SRV_UAV heap, or `UINT32_MAX` if the
    /// texture has no SRV (e.g., depth-only without `ShaderResource` usage).
    virtual uint32_t GetTextureSRVIndex(RHITextureHandle handle) const = 0;

    /// Returns the UAV descriptor heap index for a texture.
    ///
    /// # Returns
    /// The UAV index in the global CBV_SRV_UAV heap, or `UINT32_MAX` if the
    /// texture was not created with `UnorderedAccess` usage.
    virtual uint32_t GetTextureUAVIndex(RHITextureHandle handle) const = 0;

    /// Returns the creation descriptor for a texture.
    ///
    /// # Arguments
    /// * `handle` — Texture handle to query.
    ///
    /// # Returns
    /// The `RHITextureDesc` used to create the texture.
    /// Returns a default-constructed desc if the handle is invalid.
    virtual RHITextureDesc GetTextureDesc(RHITextureHandle handle) const = 0;

    // ── Shader operations ────────────────────────────────────────────────

    /// Creates a shader module from compiled bytecode (DXIL, SPIR-V, or MSL).
    ///
    /// The bytecode is copied internally — the caller's pointer need not persist.
    ///
    /// # Arguments
    /// * `desc` — Shader descriptor (bytecode pointer, size, stage, entry point).
    ///
    /// # Returns
    /// A valid `RHIShaderHandle` on success, or `RHIShaderHandle::Invalid()` on failure.
    virtual RHIShaderHandle CreateShader(const RHIShaderDesc& desc) = 0;

    /// Destroys a shader module and frees its bytecode storage.
    ///
    /// # Arguments
    /// * `handle` — Shader handle to destroy. Becomes invalid after this call.
    virtual void DestroyShader(RHIShaderHandle handle) = 0;

    // ── Pipeline operations ──────────────────────────────────────────────

    /// Creates a graphics pipeline state object (PSO).
    ///
    /// PSO creation may be expensive (shader compilation, state validation).
    /// The backend may cache PSOs internally via content-hash lookup.
    ///
    /// # Arguments
    /// * `desc` — Pipeline descriptor (shaders, rasterizer, blend, depth-stencil state).
    ///
    /// # Returns
    /// A valid `RHIPipelineHandle` on success, or `RHIPipelineHandle::Invalid()` on failure.
    virtual RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;

    /// Creates a compute pipeline state object (PSO).
    ///
    /// Compute pipelines only require a compute shader — no rasterizer, blend,
    /// or depth-stencil state. The pipeline shares the same global root signature
    /// as graphics pipelines.
    ///
    /// # Arguments
    /// * `desc` — Compute pipeline descriptor (shader handle, debug name, optional cached blob).
    ///
    /// # Returns
    /// A valid `RHIPipelineHandle` on success, or `RHIPipelineHandle::Invalid()` on failure.
    /// Failure causes include invalid shader handle, wrong shader stage, or driver error.
    virtual RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc& desc) = 0;

    /// Destroys a pipeline state object (graphics or compute).
    ///
    /// # Arguments
    /// * `handle` — Pipeline handle to destroy. Becomes invalid after this call.
    virtual void DestroyPipeline(RHIPipelineHandle handle) = 0;

    /// Extracts the cached compilation blob from a pipeline state object.
    ///
    /// On DX12, calls `ID3D12PipelineState::GetCachedBlob()` to retrieve the
    /// driver-compiled pipeline state. This blob can be stored to disk and
    /// passed back via `RHIGraphicsPipelineDesc::cachedBlobData` on subsequent
    /// runs to skip GPU compilation.
    ///
    /// On Vulkan, `VkPipelineCache` handles caching natively — returns empty.
    /// On Metal, returns empty (binary archives not yet implemented).
    ///
    /// # Arguments
    /// * `handle` — Pipeline handle to extract the blob from.
    ///
    /// # Returns
    /// The cached blob as a byte vector. Empty on failure, unsupported backend,
    /// or invalid handle.
    virtual std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle handle) = 0;

    // ── Sampler operations ───────────────────────────────────────────────

    /// Creates a texture sampler with the given filter and addressing modes.
    ///
    /// # Arguments
    /// * `desc` — Sampler descriptor (filter, address modes, LOD, anisotropy).
    ///
    /// # Returns
    /// A valid `RHISamplerHandle` on success, or `RHISamplerHandle::Invalid()` on failure.
    virtual RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) = 0;

    /// Destroys a texture sampler and frees its descriptor slot.
    ///
    /// # Arguments
    /// * `handle` — Sampler handle to destroy. Becomes invalid after this call.
    virtual void DestroySampler(RHISamplerHandle handle) = 0;

    /// Returns the index of a sampler in the global sampler descriptor heap.
    ///
    /// # Arguments
    /// * `handle` — Sampler handle to query.
    ///
    /// # Returns
    /// The sampler index in the global sampler heap, or `UINT32_MAX` if invalid.
    virtual uint32_t GetSamplerIndex(RHISamplerHandle handle) const = 0;

    // ── Swap chain operations ────────────────────────────────────────────

    /// Creates a swap chain for presenting rendered frames to a window.
    ///
    /// # Arguments
    /// * `desc` — Swap chain descriptor (window handle, dimensions, buffer count, format).
    ///
    /// # Returns
    /// A valid `IRHISwapChain*` on success, `nullptr` on failure.
    /// The caller owns the returned pointer and must call `DestroySwapChain()`.
    virtual IRHISwapChain* CreateSwapChain(const RHISwapChainDesc& desc) = 0;

    /// Destroys a swap chain and releases its back buffer resources.
    ///
    /// # Arguments
    /// * `swapChain` — Pointer returned by `CreateSwapChain()`. Must not be null.
    virtual void DestroySwapChain(IRHISwapChain* swapChain) = 0;

    // ── Fence operations ─────────────────────────────────────────────────

    /// Creates a GPU fence with initial value 0.
    ///
    /// # Returns
    /// A valid `IRHIFence*` on success, `nullptr` on failure.
    /// The caller owns the returned pointer and must call `DestroyFence()`.
    virtual IRHIFence* CreateFence() = 0;

    /// Destroys a GPU fence and releases its OS event handle.
    ///
    /// # Arguments
    /// * `fence` — Pointer returned by `CreateFence()`. Must not be null.
    virtual void DestroyFence(IRHIFence* fence) = 0;

    // ── Command list operations ──────────────────────────────────────────

    /// Creates a command list for the given queue type.
    ///
    /// # Arguments
    /// * `queue` — Queue type (`Graphics`, `Compute`, or `Copy`). Default is `Graphics`.
    ///
    /// # Returns
    /// A valid `IRHICommandList*` on success, `nullptr` on failure.
    /// The caller owns the returned pointer and must call `DestroyCommandList()`.
    virtual IRHICommandList* CreateCommandList(RHIQueueType queue = RHIQueueType::Graphics) = 0;

    /// Destroys a command list and its allocators.
    ///
    /// # Arguments
    /// * `cmdList` — Pointer returned by `CreateCommandList()`. Must not be null.
    virtual void DestroyCommandList(IRHICommandList* cmdList) = 0;

    // ── Synchronization ──────────────────────────────────────────────────

    /// Flushes all GPU queues and blocks until all submitted work completes.
    ///
    /// Use sparingly — this is a full pipeline stall. Primarily for shutdown
    /// and resource destruction that cannot be deferred.
    virtual void WaitIdle() = 0;

    // ── Device queries ───────────────────────────────────────────────────

    /// Returns the GPU adapter name (e.g., `"NVIDIA GeForce RTX 4090"`).
    ///
    /// # Returns
    /// Null-terminated UTF-8 string. Valid for the lifetime of the device.
    virtual const char* GetAdapterName() const = 0;

    /// Returns the feature level string (e.g., `"D3D_FEATURE_LEVEL_12_2"`).
    ///
    /// # Returns
    /// Null-terminated string. Valid for the lifetime of the device.
    virtual const char* GetFeatureLevel() const = 0;

    /// Reports the current VRAM budget and usage from the OS/driver.
    ///
    /// # Returns
    /// An `RHIMemoryBudget` with `budgetBytes` (total available) and `usageBytes` (currently used).
    /// Both are 0 if the query fails or the adapter is unavailable.
    virtual RHIMemoryBudget GetMemoryBudget() const = 0;

    /// Returns the negotiated shader model for the current GPU.
    ///
    /// # Returns
    /// The highest shader model the GPU supports, as determined during device creation.
    /// Use `IsBindless()` to check if SM6.6+ bindless resource access is available.
    virtual ShaderModel GetShaderModel() const = 0;

    /// Returns true if the device supports SM6.6+ bindless resource access.
    ///
    /// When true, shaders use `ResourceDescriptorHeap[index]` for resource access
    /// and pipelines need no input layout. When false, shaders use traditional
    /// register-based binding and the Input Assembler for vertex data.
    ///
    /// Returns the RHI backend type of this device (DX12, DX11, Vulkan, etc.).
    ///
    /// # Returns
    /// The `RHIBackend` enum value corresponding to this device's backend.
    virtual RHIBackend GetBackend() const = 0;

    /// Defined out-of-line in rhi_config.h (needs full ShaderModel enum).
    bool IsBindless() const;

    /// Returns true if the display supports variable refresh rate (VRR/FreeSync/G-Sync).
    ///
    /// DX12: queries `IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING)`.
    /// Other backends: returns false (no VRR query available).
    virtual bool SupportsVariableRefreshRate() const { return false; }

    // ── GPU Profiler ─────────────────────────────────────────────────────

    /// Resets the GPU profiler timestamp counter for a new frame.
    ///
    /// Call once at the beginning of each frame, before any BeginTimestamp/EndTimestamp.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list (reserved for future use).
    virtual void GpuProfilerBeginFrame(IRHICommandList* cmdList) { (void)cmdList; }

    /// Resolves all recorded timestamps into the readback buffer and advances the frame index.
    ///
    /// Must be called at the end of the frame, before the command list is submitted.
    /// Writes a ResolveQueryData command that copies GPU timestamp values into the
    /// readback buffer for later CPU access via GpuProfilerResolve().
    ///
    /// # Arguments
    /// * `cmdList` — Active command list to record the resolve command on.
    virtual void GpuProfilerEndFrame(IRHICommandList* cmdList) { (void)cmdList; }

    /// Reads back resolved timestamps from a previous frame and computes millisecond deltas.
    ///
    /// Due to triple-buffering, this reads results from frame N-2 relative to the
    /// current recording frame. Must be called after the GPU has completed the frame
    /// whose timestamps are being read (i.e., after a fence wait).
    virtual void GpuProfilerResolve() {}

    /// Records a begin timestamp for a named GPU scope.
    ///
    /// Must be paired with a subsequent GpuProfilerEndTimestamp() call with the same name.
    /// Up to `kMaxGpuTimestamps` (64) pairs can be recorded per frame.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list to record the timestamp query on.
    /// * `name` — Human-readable scope name (string literal, not owned or copied).
    virtual void GpuProfilerBeginTimestamp(IRHICommandList* cmdList, const char* name) {
        (void)cmdList; (void)name;
    }

    /// Records an end timestamp for a previously begun GPU scope.
    ///
    /// # Arguments
    /// * `cmdList` — Active command list to record the timestamp query on.
    /// * `name` — Scope name (kept for symmetry with BeginTimestamp).
    virtual void GpuProfilerEndTimestamp(IRHICommandList* cmdList, const char* name) {
        (void)cmdList; (void)name;
    }

    /// Returns the most recent resolved GPU timing results.
    ///
    /// # Returns
    /// A vector of `GpuTimingResult` entries (one per Begin/End timestamp pair).
    /// Empty if no timestamps were recorded or GpuProfilerResolve() has not been called.
    virtual const std::vector<GpuTimingResult>& GpuProfilerGetResults() const {
        static const std::vector<GpuTimingResult> kEmpty;
        return kEmpty;
    }

    /// Returns the total GPU frame time spanning all profiled scopes.
    ///
    /// # Returns
    /// Time in milliseconds from the first BeginTimestamp to the last EndTimestamp.
    /// Returns 0.0f if no timestamps were resolved.
    virtual float GpuProfilerGetFrameTimeMs() const { return 0.0f; }

    // ── Copy queue (async uploads) ──────────────────────────────────────

    /// Signals a fence on the copy queue, returning the fence value.
    /// Graphics queue can wait on this value via GraphicsWaitForCopy().
    virtual uint64_t SignalCopyQueue() { return 0; }

    /// Makes the graphics queue wait until the copy queue fence reaches `value`.
    /// Creates a GPU-side dependency without CPU stall.
    virtual void GraphicsWaitForCopy(uint64_t /*value*/) {}

    // ── Compute queue (async compute) ──────────────────────────────────

    /// Signals a fence on the compute queue, returning the fence value.
    /// Graphics queue can wait on this value via GraphicsWaitForCompute().
    virtual uint64_t SignalComputeQueue() { return 0; }

    /// Makes the graphics queue wait until the compute queue fence reaches `value`.
    /// Creates a GPU-side dependency without CPU stall.
    virtual void GraphicsWaitForCompute(uint64_t /*value*/) {}

    /// Makes the compute queue wait until the graphics queue fence reaches `value`.
    /// Creates a GPU-side dependency without CPU stall.
    virtual void ComputeWaitForGraphics(uint64_t /*value*/) {}
};

} // namespace uz
