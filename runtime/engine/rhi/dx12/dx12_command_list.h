#pragma once

/// DX12 backend implementation of IRHICommandList.
///
/// Wraps ID3D12GraphicsCommandList with per-frame command allocators
/// for triple-buffered recording. Binds the global CBV_SRV_UAV and
/// Sampler descriptor heaps on Begin() for SM6.6 bindless access.

#include "../rhi.h"

#include <wrl/client.h>
#include <d3d12.h>
#include "dx12_guids.h"

namespace uz {

using Microsoft::WRL::ComPtr;

class DX12Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX12CommandList
// ─────────────────────────────────────────────────────────────────────────────

/// DX12 implementation of `IRHICommandList`.
///
/// Owns one `ID3D12GraphicsCommandList` and `kFramesInFlight` (3) command
/// allocators. The allocators rotate each frame so previously submitted GPU
/// work can complete while new commands are recorded into a fresh allocator.
///
/// On `Begin()`, the global CBV_SRV_UAV and Sampler descriptor heaps are
/// bound for SM6.6 bindless shader access.
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into this command list at a time.
class DX12CommandList final : public IRHICommandList {
public:
    DX12CommandList() = default;
    ~DX12CommandList() override;

    /// Creates command allocator(s) and the command list. Returns false on failure.
    ///
    /// # Arguments
    /// * `device` — Parent DX12 device (must outlive this command list).
    /// * `queueType` — Queue type determines the command list type (Direct, Compute, Copy).
    bool Init(DX12Device* device, RHIQueueType queueType);

    // ── IRHICommandList overrides ────────────────────────────────────────

    void Begin() override;
    void End() override;
    void Execute() override;

    void SetPipeline(RHIPipelineHandle pipeline) override;
    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) override;
    void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) override;
    void SetViewport(const RHIViewport& viewport) override;
    void SetScissor(const RHIScissorRect& scissor) override;
    void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) override;
    void SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                          uint32_t count, uint32_t offsetIn32BitValues) override;
    void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) override;
    void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                    uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) override;
    void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) override;
    void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) override;

    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0) override;
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                             uint32_t drawCount, uint32_t argsStride) override;
    void DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) override;

    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override;

    void ClearRenderTarget(RHITextureHandle texture, const float color[4]) override;
    void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) override;

    void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void CopyTexture(RHITextureHandle src, RHITextureHandle dst) override;
    void CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                             uint32_t srcRowPitch,
                             RHITextureHandle dstTexture,
                             uint32_t mipLevel = 0, uint32_t arraySlice = 0) override;
    void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                             uint32_t dstRowPitch, uint32_t arraySlice = 0) override;

    void BeginRenderPass(const RHIRenderPassDesc& desc) override;
    void EndRenderPass() override;

    // ── DX12-specific methods (pre-texture-pool) ─────────────────────────

    /// Transitions a resource between D3D12 states (low-level, for swap chain back buffers).
    ///
    /// Prefer `ResourceBarrier()` with `RHIBarrierDesc` for handle-based transitions.
    /// This overload takes raw D3D12 types for cases where no `RHIHandle` exists
    /// (e.g., swap chain back buffers before texture pool integration).
    ///
    /// # Arguments
    /// * `resource` — Native D3D12 resource pointer.
    /// * `before` — Current resource state.
    /// * `after` — Desired resource state.
    void TransitionBarrier(ID3D12Resource* resource,
                           D3D12_RESOURCE_STATES before,
                           D3D12_RESOURCE_STATES after);

    /// Clears a render target view by raw descriptor handle.
    ///
    /// Prefer `ClearRenderTarget()` with `RHITextureHandle` for handle-based clears.
    /// This overload is used by the swap chain which manages its own RTV descriptors.
    ///
    /// # Arguments
    /// * `rtv` — CPU descriptor handle for the render target view.
    /// * `color` — Four-element float array `[R, G, B, A]`.
    void ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]);

    /// Returns the underlying `ID3D12GraphicsCommandList` (for sibling DX12 classes only).
    ID3D12GraphicsCommandList* GetNative() const { return m_cmdList.Get(); }

private:
    /// Number of command allocators for triple-buffered frame pacing.
    static constexpr uint32_t kFramesInFlight = 3;

    /// Maximum fragment textures bindable via SetFragmentTexture (SM5 path).
    static constexpr uint32_t kMaxFragmentTextures = 16;

    DX12Device*                          m_device = nullptr;
    ComPtr<ID3D12GraphicsCommandList>    m_cmdList;
    ComPtr<ID3D12CommandAllocator>       m_allocators[kFramesInFlight];
    uint32_t                             m_currentFrame = 0;
    D3D12_COMMAND_LIST_TYPE              m_listType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    /// True when the currently bound pipeline is a compute PSO.
    /// Used to select `SetComputeRoot*` vs `SetGraphicsRoot*` variants.
    bool                                 m_isComputeBound = false;

    /// Base index in the CBV_SRV_UAV heap for the contiguous staging block
    /// used by SetFragmentTexture (SM5 descriptor table path).
    /// Allocated once in Init(), reused every frame via descriptor copies.
    uint32_t                             m_fragmentTexStagingBase = UINT32_MAX;

    /// Maximum number of temporary per-slice/mip RTVs per render pass.
    static constexpr uint32_t kMaxTempRtvs = 8;

    /// Temporary RTV descriptor indices allocated during BeginRenderPass
    /// for array slice or mip level targeting. Freed in EndRenderPass.
    uint32_t m_tempRtvIndices[kMaxTempRtvs] = {};
    uint32_t m_tempRtvCount = 0;
};

} // namespace uz
