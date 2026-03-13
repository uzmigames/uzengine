#pragma once

/// Vulkan backend implementation of IRHICommandList.
///
/// Uses VkCommandPool per frame (triple-buffered rotation) and records into
/// VkCommandBuffer. Executes via vkQueueSubmit on the graphics queue.
///
/// # Dynamic Rendering
/// Uses VK_KHR_dynamic_rendering (vkCmdBeginRendering / vkCmdEndRendering)
/// instead of VkRenderPass objects — simpler and matches DX12's render pass model.
///
/// # Barriers
/// Uses VK_KHR_synchronization2 (vkCmdPipelineBarrier2) for resource state
/// transitions. Maps RHIResourceState to VkImageLayout + VkAccessFlags2 +
/// VkPipelineStageFlags2.
///
/// # Thread Safety
/// NOT thread-safe. Only one thread may record into this command list at a time.

#include "../rhi.h"

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>

namespace uz {

class VulkanDevice;

/// Number of in-flight frames for command pool rotation.
static constexpr uint32_t kVulkanFrameCount = 3;

// ─────────────────────────────────────────────────────────────────────────────
// VulkanCommandList
// ─────────────────────────────────────────────────────────────────────────────

/// Vulkan implementation of `IRHICommandList`.
///
/// Records GPU commands into a VkCommandBuffer, then submits via the
/// graphics queue on `Execute()`. Command pools are triple-buffered
/// and rotated on each `Execute()` call.
class VulkanCommandList final : public IRHICommandList {
public:
    VulkanCommandList() = default;
    ~VulkanCommandList() override;

    /// Creates command pools and buffers. Returns false on failure.
    ///
    /// # Arguments
    /// * `device` — Parent Vulkan device (must outlive this command list).
    /// * `queueType` — Queue type (Graphics, Compute, or Copy).
    bool Init(VulkanDevice* device, RHIQueueType queueType);

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
    /// Writes the texture and sampler to the current frame's descriptor set and
    /// marks it dirty so the next draw call binds it.
    void FlushDescriptorSet();

    VulkanDevice*        m_device = nullptr;
    VkCommandPool        m_commandPools[kVulkanFrameCount] = {};
    VkCommandBuffer      m_commandBuffers[kVulkanFrameCount] = {};
    VkDescriptorSet      m_descriptorSets[kVulkanFrameCount] = {};
    uint32_t             m_frameIndex = 0;
    RHIQueueType         m_queueType = RHIQueueType::Graphics;
    bool                 m_inRenderPass = false;
    bool                 m_descriptorsDirty = false;
};

} // namespace uz
