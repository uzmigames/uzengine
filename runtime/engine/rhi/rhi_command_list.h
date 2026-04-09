#pragma once

/// RHI command list interface for recording GPU commands (draw, dispatch, barriers).

#include "rhi_types.h"

#include <cstdint>

namespace uz {

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
    /// This follows the standard pattern of shadow-buffer + UpdateSubresource for DX11.
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

    /// Binds a buffer SRV to the vertex shader at the specified HLSL register slot.
    ///
    /// On DX11, calls `VSSetShaderResources(slot, 1, &srv)` to bind the buffer's
    /// SRV for vertex shader access (e.g., StructuredBuffer<T> at register(tN)).
    /// On DX12 (SM6.6+), this is a no-op — bindless access via ResourceDescriptorHeap[].
    /// On Vulkan/Metal, this is a no-op — resources bound via descriptor sets/argument buffers.
    ///
    /// # Arguments
    /// * `slot` — HLSL register index (maps to `register(t<slot>)` in the vertex shader).
    /// * `buffer` — Buffer handle with an SRV (created with `RHIBufferUsage::ShaderResource`).
    virtual void SetVertexSRV(uint32_t slot, RHIBufferHandle buffer) {
        (void)slot; (void)buffer;
    }

    /// Binds a texture to the fragment/pixel shader at the specified slot.
    ///
    /// On Metal, this calls `[encoder setFragmentTexture:atIndex:]` directly.
    /// On DX12 (SM5), this copies the texture's SRV into a contiguous staging
    /// block and binds it as root param 4's descriptor table.
    /// On DX12 (SM6.6+), this is a no-op (bindless via ResourceDescriptorHeap[]).
    /// On Vulkan, this is a no-op (textures bound via descriptor sets).
    ///
    /// # Arguments
    /// * `slot` — Texture slot index (maps to `[[texture(N)]]` in MSL).
    /// * `texture` — Texture handle to bind.
    virtual void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
        (void)slot; (void)texture;
    }

    /// Forces the next SetFragmentTexture call to allocate a fresh descriptor
    /// block. Call this before each draw call that binds different textures to
    /// the same slots (e.g., per-material texture binding in the GBuffer pass).
    /// No-op on backends that don't use descriptor staging (bindless, Vulkan, DX11).
    virtual void InvalidateDescriptorBlock() {}

    /// Binds a sampler to the fragment/pixel shader at the specified slot.
    ///
    /// On Metal, this calls `[encoder setFragmentSamplerState:atIndex:]` directly.
    /// On DX12/Vulkan, this is a no-op (samplers are bound via root signature/descriptor sets).
    ///
    /// # Arguments
    /// * `slot` — Sampler slot index (maps to `[[sampler(N)]]` in MSL).
    /// * `sampler` — Sampler handle to bind.
    virtual void SetFragmentSampler(uint32_t slot, RHISamplerHandle sampler) {
        (void)slot; (void)sampler;
    }

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

    /// Issues an indirect indexed draw call.
    ///
    /// Reads draw arguments from `argsBuffer` at `argsOffset`. Each draw
    /// argument structure matches `RHIDrawIndexedIndirectArgs` (20 bytes).
    ///
    /// DX12: `ExecuteIndirect` with a DrawIndexedInstanced command signature.
    /// Vulkan: `vkCmdDrawIndexedIndirect`.
    /// Metal: `drawIndexedPrimitives:indirectBuffer:indirectBufferOffset:`.
    ///
    /// # Arguments
    /// * `argsBuffer` — Buffer containing `RHIDrawIndexedIndirectArgs` structures.
    ///   Must have been created with `RHIBufferUsage::IndirectArgs`.
    /// * `argsOffset` — Byte offset into the buffer where arguments begin.
    /// * `drawCount` — Number of draw calls to issue from the buffer.
    /// * `argsStride` — Byte stride between consecutive draw argument structures
    ///   (must be >= `sizeof(RHIDrawIndexedIndirectArgs)`).
    virtual void DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                                     uint32_t drawCount, uint32_t argsStride) = 0;

    /// Issues an indirect compute dispatch.
    ///
    /// Reads dispatch arguments from `argsBuffer` at `argsOffset`. The argument
    /// structure matches `RHIDispatchIndirectArgs` (12 bytes: X, Y, Z group counts).
    ///
    /// DX12: `ExecuteIndirect` with a Dispatch command signature.
    /// Vulkan: `vkCmdDrawIndexedIndirect` → `vkCmdDispatchIndirect`.
    /// Metal: `dispatchThreadgroups:indirectBuffer:indirectBufferOffset:`.
    ///
    /// # Arguments
    /// * `argsBuffer` — Buffer containing `RHIDispatchIndirectArgs`.
    ///   Must have been created with `RHIBufferUsage::IndirectArgs`.
    /// * `argsOffset` — Byte offset into the buffer where arguments begin.
    virtual void DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) = 0;

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

    /// Copies a single subresource from one texture to a specific mip+slice of another.
    ///
    /// Source texture must be in CopySrc state, destination in CopyDst state.
    /// The source and destination subresource dimensions must match (same width/height).
    ///
    /// # Arguments
    /// * `src` — Source texture (full mip 0 of its only mip level).
    /// * `srcMip` — Mip level to copy from in the source texture.
    /// * `srcSlice` — Array slice to copy from in the source texture.
    /// * `dst` — Destination texture (mipmapped).
    /// * `dstMip` — Target mip level in the destination texture.
    /// * `dstSlice` — Target array slice in the destination texture.
    virtual void CopyTextureRegion(RHITextureHandle src, uint32_t srcMip, uint32_t srcSlice,
                                    RHITextureHandle dst, uint32_t dstMip, uint32_t dstSlice) {
        // Default: no-op. Backends override with actual implementation.
        (void)src; (void)srcMip; (void)srcSlice;
        (void)dst; (void)dstMip; (void)dstSlice;
    }

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
    /// * `arraySlice` — Array slice / cubemap face index (default 0).
    ///   For cubemaps: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
    virtual void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                                     RHIBufferHandle dstBuffer, uint64_t dstOffset,
                                     uint32_t dstRowPitch, uint32_t arraySlice = 0) = 0;

    // ── Ray tracing commands (DXR / VK_KHR_ray_tracing_pipeline) ──────

    /// Builds a bottom-level acceleration structure from triangle geometry.
    ///
    /// The BLAS must have been created via `IRHIDevice::CreateBLAS()`. The
    /// scratch buffer must be large enough for the build (query via device).
    /// After this call, a UAV barrier on the BLAS is required before use.
    ///
    /// # Arguments
    /// * `blas` — BLAS handle to build into.
    /// * `scratchBuffer` — Temporary build workspace buffer.
    /// * `update` — If true, performs an in-place refit (requires AllowUpdate flag).
    virtual void BuildBLAS(RHIBLASHandle blas, RHIBufferHandle scratchBuffer,
                           bool update = false) {
        (void)blas; (void)scratchBuffer; (void)update;
    }

    /// Builds a top-level acceleration structure from an instance buffer.
    ///
    /// # Arguments
    /// * `tlas` — TLAS handle to build into.
    /// * `instanceBuffer` — GPU buffer of `RHIRTInstanceDesc` entries.
    /// * `instanceCount` — Number of instances in the buffer.
    /// * `scratchBuffer` — Temporary build workspace buffer.
    /// * `update` — If true, performs an in-place refit (requires AllowUpdate flag).
    virtual void BuildTLAS(RHITLASHandle tlas, RHIBufferHandle instanceBuffer,
                           uint32_t instanceCount, RHIBufferHandle scratchBuffer,
                           bool update = false) {
        (void)tlas; (void)instanceBuffer; (void)instanceCount;
        (void)scratchBuffer; (void)update;
    }

    /// Dispatches rays using the given RT pipeline and shader tables.
    ///
    /// Requires a TLAS to be built and accessible via SRV. The shader tables
    /// must be populated with valid shader identifiers from the pipeline.
    ///
    /// # Arguments
    /// * `pipeline` — RT pipeline state object handle.
    /// * `desc` — Dispatch rays descriptor (shader tables + dimensions).
    virtual void DispatchRays(RHIRTPipelineHandle pipeline,
                              const RHIDispatchRaysDesc& desc) {
        (void)pipeline; (void)desc;
    }

    /// Inserts a UAV barrier for an acceleration structure.
    ///
    /// Required after BuildBLAS/BuildTLAS and before the AS is read
    /// (e.g., by DispatchRays or inline ray query).
    virtual void UAVBarrierAS() {}

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

} // namespace uz
