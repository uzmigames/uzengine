#pragma once

/// Metal backend implementation of IRHICommandList.
///
/// Uses Metal's encoder-based model: typed encoders (MTLRenderCommandEncoder,
/// MTLComputeCommandEncoder, MTLBlitCommandEncoder) are created and ended as
/// needed during recording. Only one encoder may be active at a time.
///
/// # Encoder Management
/// Metal requires explicit encoder transitions — before starting a render pass
/// or blit copy, any active encoder must be ended first. The `EndCurrentEncoder()`
/// helper manages this automatically. Callers do not need to track encoder state.
///
/// # Barriers
/// `ResourceBarrier()` is a no-op on Metal. The Metal runtime handles resource
/// hazard tracking automatically within a single command encoder. Cross-encoder
/// dependencies are handled implicitly by encoder ordering within a command buffer.
///
/// # Triple-Buffered Command Buffers
/// Command buffers rotate per frame (3 frames in flight), matching the Vulkan
/// and DX12 triple-buffering strategy. Each `Execute()` advances the frame index.
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into this command list at a time.

#include "../rhi.h"

#include <cstdint>

// Forward declarations for Metal types (avoids Metal framework dependency)
namespace MTL {
    class CommandBuffer;
    class CommandQueue;
    class RenderCommandEncoder;
    class ComputeCommandEncoder;
    class BlitCommandEncoder;
} // namespace MTL

namespace uz {

class MetalDevice;

/// Number of in-flight frames for command buffer rotation.
static constexpr uint32_t kMetalFrameCount = 3;

/// Tracks which type of Metal encoder is currently active.
///
/// Metal requires ending the current encoder before creating a new one of a
/// different type. This enum enables O(1) dispatch in `EndCurrentEncoder()`.
enum class MetalEncoderType : uint8_t {
    None,     ///< No encoder is active.
    Render,   ///< MTLRenderCommandEncoder is active.
    Compute,  ///< MTLComputeCommandEncoder is active.
    Blit,     ///< MTLBlitCommandEncoder is active.
};

// ─────────────────────────────────────────────────────────────────────────────
// MetalCommandList
// ─────────────────────────────────────────────────────────────────────────────

/// Metal implementation of `IRHICommandList`.
///
/// Records GPU commands by creating and switching between Metal encoders
/// (render, compute, blit) as needed. Submits via `[MTLCommandBuffer commit]`
/// on `Execute()`. Command buffers are triple-buffered and rotated on each
/// `Execute()` call.
///
/// # Metal-Specific Behavior
/// - `ResourceBarrier()` is a no-op (Metal handles hazard tracking).
/// - `SetPipeline()` binds both the render pipeline state and any associated
///   depth-stencil state (Metal does not bake depth-stencil into the PSO).
/// - `SetIndexBuffer()` caches state for use at draw time (Metal binds the
///   index buffer inline with the draw call, not as a separate binding).
class MetalCommandList final : public IRHICommandList {
public:
    MetalCommandList() = default;
    ~MetalCommandList() override;

    /// Creates the command list and associates it with a device queue.
    ///
    /// # Arguments
    /// * `device` — Parent Metal device (must outlive this command list).
    /// * `queueType` — Queue type (Graphics, Compute, or Copy).
    ///
    /// # Returns
    /// `true` on success, `false` if the device is null or unavailable.
    bool Init(MetalDevice* device, RHIQueueType queueType);

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
    void SetFragmentSampler(uint32_t slot, RHISamplerHandle sampler) override;

    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
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
                             uint32_t mipLevel, uint32_t arraySlice) override;
    void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                             uint32_t dstRowPitch) override;

    void BeginRenderPass(const RHIRenderPassDesc& desc) override;
    void EndRenderPass() override;

private:
    /// Ends the currently active encoder (render, compute, or blit).
    /// Sets `m_activeEncoder` to `None` and nulls the encoder pointer.
    /// No-op if no encoder is active.
    void EndCurrentEncoder();

    /// Ensures a blit encoder is active. Ends the current encoder if it is
    /// not a blit encoder, then creates a new one from the command buffer.
    void EnsureBlitEncoder();

    /// Ensures a compute encoder is active. Ends the current encoder if it is
    /// not a compute encoder, then creates a new one from the command buffer.
    void EnsureComputeEncoder();

    MetalDevice*                   m_device = nullptr;
    class MetalGpuProfiler*        m_gpuProfiler = nullptr;
    MTL::CommandBuffer*            m_commandBuffer = nullptr;
    MTL::RenderCommandEncoder*     m_renderEncoder = nullptr;
    MTL::ComputeCommandEncoder*    m_computeEncoder = nullptr;
    MTL::BlitCommandEncoder*       m_blitEncoder = nullptr;
    MetalEncoderType               m_activeEncoder = MetalEncoderType::None;
    uint32_t                       m_frameIndex = 0;
    RHIQueueType                   m_queueType = RHIQueueType::Graphics;
    bool                           m_inRenderPass = false;

    // ── Cached state for deferred binding ────────────────────────────────

    /// Metal binds the index buffer inline with the draw call, so we cache
    /// the handle, format, and offset until DrawIndexed() is called.
    RHIBufferHandle m_currentIndexBuffer;
    uint32_t        m_indexBufferOffset = 0;
    uint32_t        m_indexType = 0; ///< 0 = MTLIndexTypeUInt16, 1 = MTLIndexTypeUInt32

    /// Currently bound pipeline (for topology lookup at draw time).
    RHIPipelineHandle m_currentPipeline;
};

} // namespace uz
