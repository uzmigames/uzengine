#pragma once

/// DX11 backend implementation of IRHICommandList.
///
/// Uses the **immediate context directly** for all command recording.
/// Unlike DX12, there are no command allocators or descriptor heap bindings
/// — DX11 manages these implicitly.
///
/// # Immediate Context Model
/// and Godot both use the immediate context on DX11 — deferred contexts
/// are unreliable for real rendering. `Begin()` stores the immediate context
/// pointer, `End()` and `Execute()` are no-ops since commands are already
/// submitted as they are recorded.
///
/// # Barriers
/// DX11 manages resource state transitions implicitly — `ResourceBarrier()`
/// is a no-op. The driver inserts hazard barriers automatically.
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into this command list at a time.

#include "../rhi.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11_1.h>

namespace uz {

class DX11Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX11CommandList
// ─────────────────────────────────────────────────────────────────────────────

/// DX11 implementation of `IRHICommandList`.
///
/// Records commands directly on the immediate device context. `Begin()`,
/// `End()`, and `Execute()` exist only to satisfy the RHI interface —
/// commands take effect immediately as they are recorded.
class DX11CommandList final : public IRHICommandList {
public:
    DX11CommandList() = default;
    ~DX11CommandList() override;

    /// Creates the command list. Returns false on failure.
    ///
    /// # Arguments
    /// * `device` — Parent DX11 device (must outlive this command list).
    /// * `queueType` — Queue type (only Graphics is fully supported on DX11).
    bool Init(DX11Device* device, RHIQueueType queueType);

    // ── IRHICommandList overrides ────────────────────────────────────────

    /// Begins recording. Caches the immediate context pointer. No-op semantically
    /// since DX11 commands execute immediately, but satisfies the RHI interface.
    void Begin() override;

    /// Ends recording. No-op on DX11 — commands were already submitted on record.
    void End() override;

    /// Executes recorded commands. No-op on DX11 — the immediate context submits
    /// commands as they are recorded.
    void Execute() override;

    /// Binds a graphics or compute pipeline (rasterizer, blend, depth-stencil, shaders).
    void SetPipeline(RHIPipelineHandle pipeline) override;

    /// Binds a vertex buffer to the given input assembler slot.
    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) override;

    /// Binds an index buffer to the input assembler.
    void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) override;

    /// Sets the viewport via `RSSetViewports` on the immediate context.
    void SetViewport(const RHIViewport& viewport) override;

    /// Sets the scissor rectangle via `RSSetScissorRects` on the immediate context.
    void SetScissor(const RHIScissorRect& scissor) override;

    /// Sets a descriptor index as a root constant (emulated via scratch constant buffer).
    void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Writes root constants into a scratch DYNAMIC constant buffer and binds it.
    void SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                          uint32_t count, uint32_t offsetIn32BitValues) override;

    /// Binds a constant buffer by GPU address. DX11 does not support GPU virtual
    /// addresses — this is mapped to a buffer handle lookup.
    void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) override;

    /// Uploads CPU data into a scratch constant buffer and binds it to VS+PS or CS.
    void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                    uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) override;

    /// Binds SRV/UAV descriptors by index (translated from root param to HLSL register).
    void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Binds a texture SRV to a pixel shader slot.
    void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) override;

    /// Binds a sampler state to a pixel shader slot.
    void SetFragmentSampler(uint32_t slot, RHISamplerHandle sampler) override;

    /// Submits an indexed draw call via `DrawIndexedInstanced`.
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;

    /// Submits a non-indexed draw call via `DrawInstanced`.
    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;

    /// Dispatches a compute workload via `Dispatch`.
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    /// Submits indirect indexed draw calls via `DrawIndexedInstancedIndirect`.
    void DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                             uint32_t drawCount, uint32_t argsStride) override;

    /// Dispatches an indirect compute workload via `DispatchIndirect`.
    void DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) override;

    /// No-op on DX11 — the driver manages resource state transitions automatically.
    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override;

    /// Clears a render target view to the specified RGBA color.
    void ClearRenderTarget(RHITextureHandle texture, const float color[4]) override;

    /// Clears a depth-stencil view to the specified depth and stencil values.
    void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) override;

    /// Copies a region of bytes between two buffers via `CopySubresourceRegion`.
    void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) override;

    /// Copies an entire texture to another texture via `CopyResource`.
    void CopyTexture(RHITextureHandle src, RHITextureHandle dst) override;

    /// Copies buffer data into a texture subresource via `UpdateSubresource`.
    void CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                             uint32_t srcRowPitch,
                             RHITextureHandle dstTexture,
                             uint32_t mipLevel, uint32_t arraySlice) override;

    /// Copies a texture subresource into a staging buffer via `CopySubresourceRegion`.
    void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                             uint32_t dstRowPitch, uint32_t arraySlice = 0) override;

    /// Begins a render pass by binding render target and depth-stencil views.
    /// Creates per-slice RTVs if targeting non-zero array slices.
    void BeginRenderPass(const RHIRenderPassDesc& desc) override;

    /// Ends the render pass. Releases any temporary per-slice RTVs created in Begin.
    void EndRenderPass() override;

private:
    /// Gets or creates a scratch DYNAMIC constant buffer for a given slot.
    /// Recreates the buffer if `requiredSize` exceeds the current allocation.
    ID3D11Buffer* GetOrCreateScratchCB(uint32_t slot, uint32_t requiredSize);

    DX11Device*              m_device = nullptr;
    /// Non-owning pointer to the device's immediate context.
    ID3D11DeviceContext*     m_ctx = nullptr;
    RHIQueueType             m_queueType = RHIQueueType::Graphics;

    /// True when the most recently bound pipeline is a compute pipeline.
    /// Used by `SetRootCBV` / `SetRootConstants` to choose between
    /// VS+PS constant buffer binding (graphics) and CS binding (compute).
    bool                     m_computePipelineBound = false;

    /// Scratch constant buffers for SetRootCBV (one per slot, DYNAMIC usage).
    static constexpr uint32_t kMaxCBSlots = 8;
    ID3D11Buffer*            m_scratchCBs[kMaxCBSlots] = {};
    uint32_t                 m_scratchCBSizes[kMaxCBSlots] = {};

    /// Temporary per-slice RTVs created in BeginRenderPass, released in EndRenderPass.
    ID3D11RenderTargetView*  m_tempRtvs[8] = {};
    uint32_t                 m_tempRtvCount = 0;
};

} // namespace uz
