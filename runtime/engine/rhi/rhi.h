#pragma once

/// UzEngine Rendering Hardware Interface — Abstract Device, Command List, SwapChain, Fence.
///
/// This header defines the backend-agnostic GPU abstraction layer for UzEngine.
/// Application code uses only these pure virtual interfaces — never DX12, Vulkan,
/// or Metal types directly.
///
/// # Architecture
/// ```
/// Application → IRHIDevice       → DX12Device   (or VulkanDevice, MetalDevice)
///             → IRHICommandList   → DX12CommandList
///             → IRHISwapChain     → DX12SwapChain
///             → IRHIFence         → DX12Fence
/// ```
///
/// # Resource Model
/// GPU resources are referenced via typed generational handles (`RHIHandle<Tag>`).
/// The backend owns the actual GPU objects and maps handles to them internally.
/// Handles prevent use-after-free via generation counters — stale handles are
/// safely detected and rejected.
///
/// # Bindless (SM6.6)
/// Resources get a persistent descriptor index in a global CBV_SRV_UAV heap.
/// Shaders access resources via `SetDescriptorIndex()` push constants, avoiding
/// traditional descriptor table binding.
///
/// # Threading
/// - `IRHIDevice` methods (Create/Destroy) are thread-safe (internally synchronized).
/// - `IRHICommandList` is NOT thread-safe — one recording thread per list.
/// - `IRHISwapChain::Present` must be called from the thread that created it.
///
/// # Lifetime
/// All interfaces returned by `IRHIDevice` (command lists, fences, swap chains)
/// are owned by the caller and must be destroyed via the corresponding
/// `Destroy*()` method before the device is destroyed.

#include "rhi_types.h"

namespace uz {

// Forward declare — defined in rhi_config.h. Avoids circular include
// (rhi.h → rhi_config.h → rhi_backend.h → rhi.h).
enum class ShaderModel : uint8_t;

// Forward declare — defined in rhi_backend.h. Avoids circular include
// (rhi.h → rhi_backend.h → rhi.h).
enum class RHIBackend : uint8_t;

// ─────────────────────────────────────────────────────────────────────────────
// IRHIFence — CPU/GPU synchronization primitive
// ─────────────────────────────────────────────────────────────────────────────

/// GPU fence for synchronizing CPU and GPU work.
///
/// Fences are monotonically increasing: each `Signal(value)` must use a value
/// greater than any previously signaled value. The fence starts at value 0.
///
/// # Thread Safety
/// All methods are thread-safe. Multiple CPU threads can poll `IsComplete()`
/// concurrently. Only one thread should call `WaitCPU()` at a time per fence.
///
/// # Lifetime
/// Created via `IRHIDevice::CreateFence()`, destroyed via `IRHIDevice::DestroyFence()`.
/// Must be destroyed before the device.
///
/// # Examples
/// ```cpp
/// auto* fence = device->CreateFence();
/// uint64_t fenceValue = 1;
///
/// cmdList->End();
/// cmdList->Execute();
/// fence->Signal(fenceValue);
/// fence->WaitCPU(fenceValue);
/// // GPU work is now complete
///
/// device->DestroyFence(fence);
/// ```
class IRHIFence {
public:
    virtual ~IRHIFence() = default;

    /// Signals the fence to the given value from the GPU timeline.
    ///
    /// # Arguments
    /// * `value` — Monotonically increasing fence value. Must be greater
    ///   than any previously signaled value.
    virtual void Signal(uint64_t value) = 0;

    /// Blocks the calling CPU thread until the fence reaches the given value.
    ///
    /// Returns immediately if the fence has already reached or passed `value`.
    ///
    /// # Arguments
    /// * `value` — The fence value to wait for.
    virtual void WaitCPU(uint64_t value) = 0;

    /// Returns true if the fence has reached or passed the given value.
    ///
    /// # Arguments
    /// * `value` — The fence value to check against.
    ///
    /// # Returns
    /// `true` if `GetCompletedValue() >= value`, `false` otherwise.
    virtual bool IsComplete(uint64_t value) const = 0;

    /// Returns the highest value the fence has completed on the GPU.
    ///
    /// # Returns
    /// The most recently completed fence value, or 0 if no signal has occurred.
    virtual uint64_t GetCompletedValue() const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHICommandList — GPU command recording
// ─────────────────────────────────────────────────────────────────────────────

/// Records GPU commands for later submission to a hardware queue.
///
/// # Lifecycle
/// 1. `Begin()` — resets and opens the command list for recording
/// 2. Record commands (`SetPipeline`, `Draw`, `ResourceBarrier`, etc.)
/// 3. `End()` — closes recording (no more commands after this)
/// 4. `Execute()` — submits to the GPU queue
///
/// After `Execute()`, the command list can be reused by calling `Begin()` again.
/// The caller must ensure the GPU has finished the previous submission (via fence)
/// before calling `Begin()`.
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into a command list at a time.
/// Multiple command lists can be recorded in parallel on different threads.
///
/// # Lifetime
/// Created via `IRHIDevice::CreateCommandList()`, destroyed via
/// `IRHIDevice::DestroyCommandList()`. Must be destroyed before the device.
///
/// # Examples
/// ```cpp
/// auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
/// cmdList->Begin();
/// cmdList->ClearRenderTarget(backBuffer, clearColor);
/// cmdList->End();
/// cmdList->Execute();
/// device->DestroyCommandList(cmdList);
/// ```
class IRHICommandList {
public:
    virtual ~IRHICommandList() = default;

    // ── Recording lifecycle ──────────────────────────────────────────────

    /// Resets and opens the command list for recording.
    ///
    /// Must not be called while the GPU is still executing a previous
    /// recording from this command list. Use a fence to synchronize.
    virtual void Begin() = 0;

    /// Closes the command list. No more commands may be recorded after this.
    ///
    /// Must be called before `Execute()`.
    virtual void End() = 0;

    /// Submits the recorded commands to the GPU queue for execution.
    ///
    /// The command list must have been closed via `End()` first.
    /// Advances the internal frame index for allocator rotation.
    virtual void Execute() = 0;

    // ── Pipeline and resource binding ────────────────────────────────────

    /// Binds a graphics or compute pipeline for subsequent draw/dispatch calls.
    ///
    /// # Arguments
    /// * `pipeline` — Handle to a previously created pipeline state object.
    virtual void SetPipeline(RHIPipelineHandle pipeline) = 0;

    /// Binds a vertex buffer to the given input assembler slot.
    ///
    /// # Arguments
    /// * `slot` — Input assembler slot index (0-based).
    /// * `buffer` — Handle to a buffer created with `RHIBufferUsage::VertexBuffer`.
    /// * `offset` — Byte offset from the start of the buffer (default 0).
    virtual void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset = 0) = 0;

    /// Binds an index buffer for indexed draw calls.
    ///
    /// # Arguments
    /// * `buffer` — Handle to a buffer created with `RHIBufferUsage::IndexBuffer`.
    /// * `format` — Element format (`Uint16` or `Uint32`).
    /// * `offset` — Byte offset from the start of the buffer (default 0).
    virtual void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset = 0) = 0;

    /// Sets the viewport rectangle for the rasterizer.
    ///
    /// # Arguments
    /// * `viewport` — Viewport dimensions and depth range.
    virtual void SetViewport(const RHIViewport& viewport) = 0;

    /// Sets the scissor rectangle for pixel clipping.
    ///
    /// # Arguments
    /// * `scissor` — Scissor rectangle in integer pixel coordinates.
    virtual void SetScissor(const RHIScissorRect& scissor) = 0;

    /// Pushes a descriptor index for SM6.6 bindless resource access.
    ///
    /// The shader reads this index via a root constant to access
    /// the resource in the global descriptor heap.
    ///
    /// # Arguments
    /// * `rootParamIndex` — Root parameter slot to write to.
    /// * `descriptorIndex` — Index into the global CBV_SRV_UAV heap.
    virtual void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) = 0;

    /// Pushes multiple 32-bit root constants at a root parameter slot.
    ///
    /// # Arguments
    /// * `rootParamIndex` — Root parameter slot (must be a root constants parameter).
    /// * `data` — Pointer to an array of uint32 values to push.
    /// * `count` — Number of uint32 values to push.
    /// * `offsetIn32BitValues` — Offset within the root constant range (default 0).
    virtual void SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                                  uint32_t count, uint32_t offsetIn32BitValues = 0) = 0;

    /// Binds a constant buffer via GPU virtual address at a root parameter slot.
    ///
    /// DX12/Vulkan path — uses GPU virtual address directly.
    /// On DX11 this is a no-op; use the buffer handle overload instead.
    ///
    /// # Arguments
    /// * `rootParamIndex` — Root parameter slot (must be a root CBV parameter).
    /// * `gpuAddress` — GPU virtual address of the constant buffer.
    virtual void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) = 0;

    /// Binds a constant buffer by handle + byte offset at a root parameter slot.
    ///
    /// Cross-backend constant buffer binding. DX12/Vulkan use the buffer's GPU
    /// address + offset. DX11 uses `UpdateSubresource` from `cpuData` into a
    /// scratch DEFAULT-usage buffer, then binds via `VSSetConstantBuffers`.
    /// This follows the UE5 pattern of shadow-buffer + UpdateSubresource for DX11.
    ///
    /// # Arguments
    /// * `rootParamIndex` — Root parameter slot (maps to HLSL register `b<N>`).
    /// * `buffer` — Buffer handle (DX12/Vulkan use for GPU address; DX11 ignores).
    /// * `offsetBytes` — Byte offset into the buffer (must be 256-byte aligned).
    /// * `cpuData` — CPU pointer to the constant data (DX11 uses this; DX12/Vulkan ignore).
    /// * `dataSize` — Size of the constant data in bytes (must be <= 256).
    virtual void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                            uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) = 0;

    /// Binds a descriptor table at a root parameter slot (SM5 fallback).
    ///
    /// Sets the GPU descriptor handle for a root parameter that is a descriptor table.
    /// The `descriptorIndex` is an offset into the global CBV_SRV_UAV heap.
    /// Used for SM5 SRV/UAV binding where DIRECTLY_INDEXED is not available.
    ///
    /// # Arguments
    /// * `rootParamIndex` — Root parameter slot (must be a descriptor table parameter).
    /// * `descriptorIndex` — Index into the global CBV_SRV_UAV descriptor heap.
    virtual void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) = 0;

    // ── Draw and dispatch ────────────────────────────────────────────────

    /// Issues an indexed draw call.
    ///
    /// # Arguments
    /// * `indexCount` — Number of indices to draw.
    /// * `instanceCount` — Number of instances (default 1).
    /// * `firstIndex` — Offset into the index buffer.
    /// * `vertexOffset` — Value added to each index before fetching a vertex.
    /// * `firstInstance` — Offset into per-instance data.
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;

    /// Issues a non-indexed draw call.
    ///
    /// # Arguments
    /// * `vertexCount` — Number of vertices to draw.
    /// * `instanceCount` — Number of instances (default 1).
    /// * `firstVertex` — Index of the first vertex.
    /// * `firstInstance` — Offset into per-instance data.
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

    /// Issues a compute dispatch.
    ///
    /// # Arguments
    /// * `groupCountX` — Number of thread groups in X dimension.
    /// * `groupCountY` — Number of thread groups in Y dimension.
    /// * `groupCountZ` — Number of thread groups in Z dimension.
    virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    // ── Barriers ─────────────────────────────────────────────────────────

    /// Inserts resource barriers (state transitions).
    ///
    /// Backends translate `RHIResourceState` to native barrier calls
    /// (D3D12 `ResourceBarrier`, Vulkan `PipelineBarrier2`, etc.).
    /// Multiple barriers can be batched for efficiency.
    ///
    /// # Arguments
    /// * `barriers` — Array of barrier descriptors.
    /// * `count` — Number of barriers in the array.
    virtual void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) = 0;

    // ── Clear operations ─────────────────────────────────────────────────

    /// Clears a render target to the given RGBA color.
    ///
    /// # Arguments
    /// * `texture` — Handle to a texture with `RHITextureUsage::RenderTarget`.
    /// * `color` — Four-element float array `[R, G, B, A]` in range `[0.0, 1.0]`.
    virtual void ClearRenderTarget(RHITextureHandle texture, const float color[4]) = 0;

    /// Clears a depth-stencil target.
    ///
    /// # Arguments
    /// * `texture` — Handle to a texture with `RHITextureUsage::DepthStencil`.
    /// * `depth` — Depth clear value (typically `1.0f`).
    /// * `stencil` — Stencil clear value (typically `0`).
    virtual void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) = 0;

    // ── Copy operations ──────────────────────────────────────────────────

    /// Copies data between buffers (GPU-side copy).
    ///
    /// # Arguments
    /// * `src` — Source buffer handle.
    /// * `srcOffset` — Byte offset in the source buffer.
    /// * `dst` — Destination buffer handle.
    /// * `dstOffset` — Byte offset in the destination buffer.
    /// * `size` — Number of bytes to copy.
    virtual void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                            RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) = 0;

    /// Copies an entire texture (all subresources) from source to destination.
    ///
    /// Both textures must have identical dimensions, format, and mip levels.
    ///
    /// # Arguments
    /// * `src` — Source texture handle.
    /// * `dst` — Destination texture handle.
    virtual void CopyTexture(RHITextureHandle src, RHITextureHandle dst) = 0;

    /// Copies pixel data from a staging buffer to a texture subresource.
    ///
    /// Used for uploading CPU image data to GPU textures. The source buffer must
    /// contain tightly packed pixel rows (no row padding beyond `srcRowPitch`).
    /// The backend handles row-pitch alignment requirements internally.
    ///
    /// # Arguments
    /// * `srcBuffer` — Upload-heap buffer containing pixel data.
    /// * `srcOffset` — Byte offset into the source buffer where pixel data starts.
    /// * `srcRowPitch` — Byte stride between rows in the source data.
    /// * `dstTexture` — Destination texture (must have `CopyDst` usage).
    /// * `mipLevel` — Target mip level (0 = full resolution).
    /// * `arraySlice` — Target array slice (0 for non-array textures).
    virtual void CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                                     uint32_t srcRowPitch,
                                     RHITextureHandle dstTexture,
                                     uint32_t mipLevel = 0, uint32_t arraySlice = 0) = 0;

    /// Copies a texture subresource into a buffer (GPU readback).
    ///
    /// Used for reading back rendered frames to CPU memory (e.g., screenshots).
    /// The destination buffer must have `CopyDst` usage and `Readback` memory.
    /// The source texture must be in `CopySrc` state (caller handles barriers).
    ///
    /// # Arguments
    /// * `srcTexture` — Source texture handle (must be in CopySrc state).
    /// * `mipLevel` — Source mip level to copy from.
    /// * `dstBuffer` — Readback buffer to receive pixel data.
    /// * `dstOffset` — Byte offset in the destination buffer.
    /// * `dstRowPitch` — Row pitch in the destination buffer (must meet backend
    ///   alignment requirements, e.g., 256 bytes for DX12).
    virtual void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                                     RHIBufferHandle dstBuffer, uint64_t dstOffset,
                                     uint32_t dstRowPitch) = 0;

    // ── Render pass scope ────────────────────────────────────────────────

    /// Begins a render pass with the given color and depth attachments.
    ///
    /// Sets render targets and optionally clears them. Must be paired with `EndRenderPass()`.
    ///
    /// # Arguments
    /// * `desc` — Render pass descriptor with color/depth attachments and clear values.
    virtual void BeginRenderPass(const RHIRenderPassDesc& desc) = 0;

    /// Ends the current render pass.
    virtual void EndRenderPass() = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — presentation to a window surface
// ─────────────────────────────────────────────────────────────────────────────

/// Manages back buffers and presentation for a window surface.
///
/// Triple-buffered by default (3 back buffers). The swap chain owns the
/// back buffer textures — they are valid as long as the swap chain exists
/// and are invalidated on `Resize()`.
///
/// # Thread Safety
/// `Present()` must be called from the thread that created the swap chain.
/// `GetWidth()`, `GetHeight()`, `GetFormat()` are safe to call from any thread.
///
/// # Lifetime
/// Created via `IRHIDevice::CreateSwapChain()`, destroyed via
/// `IRHIDevice::DestroySwapChain()`. Must be destroyed before the device.
///
/// # Examples
/// ```cpp
/// RHISwapChainDesc scDesc{};
/// scDesc.windowHandle = hwnd;
/// scDesc.width = 1280;
/// scDesc.height = 720;
/// auto* swapChain = device->CreateSwapChain(scDesc);
///
/// // Render loop
/// auto backBuffer = swapChain->GetCurrentBackBuffer();
/// // ... record commands targeting backBuffer ...
/// swapChain->Present(true);
///
/// device->DestroySwapChain(swapChain);
/// ```
class IRHISwapChain {
public:
    virtual ~IRHISwapChain() = default;

    /// Returns a handle to the current back buffer texture.
    ///
    /// # Returns
    /// A valid `RHITextureHandle` for the current back buffer. The handle
    /// becomes invalid after `Resize()` is called.
    virtual RHITextureHandle GetCurrentBackBuffer() = 0;

    /// Returns the index of the current back buffer.
    ///
    /// # Returns
    /// Index in range `[0, bufferCount)`.
    virtual uint32_t GetCurrentBackBufferIndex() const = 0;

    /// Presents the current back buffer to the display.
    ///
    /// # Arguments
    /// * `vsync` — If `true`, waits for vertical blank (sync interval 1).
    ///   If `false`, presents immediately (may tear unless tearing is supported).
    virtual void Present(bool vsync) = 0;

    /// Resizes all back buffers. Invalidates all existing back buffer handles.
    ///
    /// Call after the window is resized. Passing `(0, 0)` is a no-op (minimized window).
    /// The GPU must be idle before calling this (call `WaitIdle()` first).
    ///
    /// # Arguments
    /// * `width` — New back buffer width in pixels.
    /// * `height` — New back buffer height in pixels.
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    /// Returns the current back buffer width in pixels.
    virtual uint32_t GetWidth() const = 0;

    /// Returns the current back buffer height in pixels.
    virtual uint32_t GetHeight() const = 0;

    /// Returns the back buffer pixel format.
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
/// All `Create*`/`Destroy*` methods are internally synchronized and safe to call
/// from any thread. Device queries (`GetAdapterName`, `GetMemoryBudget`) are
/// also thread-safe.
///
/// # Lifetime
/// Created via `CreateRHIDevice()` (see `rhi_backend.h`), destroyed via
/// `DestroyRHIDevice()`. The device must outlive all resources created from it.
///
/// # Examples
/// ```cpp
/// RHIDeviceDesc desc{};
/// desc.backend = RHIBackend::DX12;
/// desc.enableValidation = true;
/// auto* device = CreateRHIDevice(desc);
///
/// auto buffer = device->CreateBuffer({.sizeBytes = 1024, .usage = RHIBufferUsage::VertexBuffer});
/// // ... use buffer ...
/// device->DestroyBuffer(buffer);
///
/// DestroyRHIDevice(device);
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

    /// Destroys a pipeline state object.
    ///
    /// # Arguments
    /// * `handle` — Pipeline handle to destroy. Becomes invalid after this call.
    virtual void DestroyPipeline(RHIPipelineHandle handle) = 0;

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
};

} // namespace uz
