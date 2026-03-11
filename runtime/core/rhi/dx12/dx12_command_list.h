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

/// DX12 implementation of IRHICommandList.
///
/// Owns one ID3D12GraphicsCommandList and kFramesInFlight command allocators.
/// The allocators rotate each frame so previously submitted work can complete
/// while new commands are recorded.
class DX12CommandList final : public IRHICommandList {
public:
    DX12CommandList() = default;
    ~DX12CommandList() override;

    /// Creates command allocator(s) and command list. Returns false on failure.
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

    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override;

    void ClearRenderTarget(RHITextureHandle texture, const float color[4]) override;
    void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) override;

    void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) override;
    void CopyTexture(RHITextureHandle src, RHITextureHandle dst) override;

    void BeginRenderPass(const RHIRenderPassDesc& desc) override;
    void EndRenderPass() override;

    // ── DX12-specific methods (pre-texture-pool) ─────────────────────────

    /// Transitions a resource between states (used until texture pool resolves handles).
    void TransitionBarrier(ID3D12Resource* resource,
                           D3D12_RESOURCE_STATES before,
                           D3D12_RESOURCE_STATES after);

    /// Clears an RTV descriptor handle (used until texture pool manages RTV descriptors).
    void ClearRTV(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float color[4]);

    /// Returns the underlying command list (for other dx12/ files only).
    ID3D12GraphicsCommandList* GetNative() const { return m_cmdList.Get(); }

private:
    static constexpr uint32_t kFramesInFlight = 3;

    DX12Device*                          m_device = nullptr;
    ComPtr<ID3D12GraphicsCommandList>    m_cmdList;
    ComPtr<ID3D12CommandAllocator>       m_allocators[kFramesInFlight];
    uint32_t                             m_currentFrame = 0;
    D3D12_COMMAND_LIST_TYPE              m_listType = D3D12_COMMAND_LIST_TYPE_DIRECT;
};

} // namespace uz
