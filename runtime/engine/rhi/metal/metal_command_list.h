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
class MetalGpuProfiler;

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

    /// Begins recording commands. Creates a new MTLCommandBuffer from the queue.
    void Begin() override;

    /// Ends recording. Ends any active encoder and finalizes the command buffer.
    void End() override;

    /// Commits the command buffer to the GPU queue and rotates the frame index.
    void Execute() override;

    /// Binds a graphics or compute pipeline state and its associated depth-stencil state.
    void SetPipeline(RHIPipelineHandle pipeline) override;

    /// Binds a vertex buffer to the given slot on the active render encoder.
    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) override;

    /// Caches index buffer state for the next `DrawIndexed()` call.
    /// Metal binds the index buffer inline with the draw call, not as a separate binding.
    void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) override;

    /// Sets the viewport rectangle on the active render encoder.
    void SetViewport(const RHIViewport& viewport) override;

    /// Sets the scissor rectangle on the active render encoder.
    void SetScissor(const RHIScissorRect& scissor) override;

    /// Sets a descriptor index via push constants (argument buffer offset).
    void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Writes root constants (push constants) to the active encoder.
    void SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                          uint32_t count, uint32_t offsetIn32BitValues) override;

    /// Binds a constant buffer by GPU address (resolved to a Metal buffer + offset).
    void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) override;

    /// Uploads CPU data into a Metal buffer and binds it as a constant buffer.
    void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                    uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) override;

    /// Binds a descriptor table (argument buffer range) to the active encoder.
    void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Binds a texture to a fragment shader slot on the active render encoder.
    void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) override;

    /// Binds a sampler to a fragment shader slot on the active render encoder.
    void SetFragmentSampler(uint32_t slot, RHISamplerHandle sampler) override;

    /// Submits an indexed draw call using the cached index buffer state.
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;

    /// Submits a non-indexed draw call on the active render encoder.
    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;

    /// Dispatches a compute workload on the active compute encoder.
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    /// Submits indirect indexed draw calls from a GPU-generated argument buffer.
    void DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                             uint32_t drawCount, uint32_t argsStride) override;

    /// Dispatches an indirect compute workload from a GPU-generated argument buffer.
    void DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) override;

    /// No-op on Metal — the runtime handles resource hazard tracking automatically.
    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override;

    /// Clears a render target texture to the specified color via a blit encoder.
    void ClearRenderTarget(RHITextureHandle texture, const float color[4]) override;

    /// Clears a depth-stencil texture to the specified depth and stencil values.
    void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) override;

    /// Copies a region of bytes between two GPU buffers via a blit encoder.
    void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) override;

    /// Copies an entire texture to another texture of the same dimensions and format.
    void CopyTexture(RHITextureHandle src, RHITextureHandle dst) override;

    /// Copies buffer data into a texture mip level / array slice via a blit encoder.
    void CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                             uint32_t srcRowPitch,
                             RHITextureHandle dstTexture,
                             uint32_t mipLevel, uint32_t arraySlice) override;

    /// Copies a texture mip level / array slice into a buffer via a blit encoder.
    void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                             uint32_t dstRowPitch, uint32_t arraySlice = 0) override;

    /// Begins a render pass by creating a render encoder from the pass descriptor.
    void BeginRenderPass(const RHIRenderPassDesc& desc) override;

    /// Ends the current render pass and its render encoder.
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
    MetalGpuProfiler*              m_gpuProfiler = nullptr;
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
