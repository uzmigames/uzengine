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

// Forward declare VMA types (same pattern as vulkan_resources.h).
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

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

    /// Begins recording. Resets the current frame's command pool and begins
    /// the command buffer with `VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT`.
    void Begin() override;

    /// Ends recording by calling `vkEndCommandBuffer`.
    void End() override;

    /// Submits the command buffer to the graphics queue via `vkQueueSubmit`
    /// and rotates the frame index for triple-buffered pool rotation.
    void Execute() override;

    /// Binds a graphics or compute pipeline via `vkCmdBindPipeline`.
    void SetPipeline(RHIPipelineHandle pipeline) override;

    /// Binds a vertex buffer to the given slot via `vkCmdBindVertexBuffers`.
    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) override;

    /// Binds an index buffer via `vkCmdBindIndexBuffer`.
    void SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) override;

    /// Sets the viewport via `vkCmdSetViewport` (flipped Y for DX-to-Vulkan convention).
    void SetViewport(const RHIViewport& viewport) override;

    /// Sets the scissor rectangle via `vkCmdSetScissor`.
    void SetScissor(const RHIScissorRect& scissor) override;

    /// Sets a descriptor index via push constants.
    void SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Writes inline root constants via a scratch UBO bound as a descriptor.
    ///
    /// Vulkan shaders compiled with `-fvk-b-shift 32 0` expect `cbuffer register(bN)`
    /// to appear as a UBO descriptor at binding `32 + N`, not as push constants.
    /// This method copies the data into a persistently-mapped scratch UBO and writes
    /// a `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` binding, matching the DX11 scratch CB pattern.
    void SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                          uint32_t count, uint32_t offsetIn32BitValues) override;

    /// Binds a constant buffer by GPU address (resolved to VkBuffer + offset via lookup).
    void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) override;

    /// Uploads CPU data into a buffer and writes a descriptor set binding for it.
    void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                    uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) override;

    /// Writes a descriptor binding for a texture or sampler at the given root parameter.
    void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) override;

    /// Writes a combined image sampler descriptor for a texture at the given slot.
    void SetFragmentTexture(uint32_t slot, RHITextureHandle texture) override;

    /// Submits an indexed draw call via `vkCmdDrawIndexed`.
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;

    /// Submits a non-indexed draw call via `vkCmdDraw`.
    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;

    /// Dispatches a compute workload via `vkCmdDispatch`.
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    /// Submits indirect indexed draw calls via `vkCmdDrawIndexedIndirect`.
    void DrawIndexedIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset,
                             uint32_t drawCount, uint32_t argsStride) override;

    /// Dispatches an indirect compute workload via `vkCmdDispatchIndirect`.
    void DispatchIndirect(RHIBufferHandle argsBuffer, uint64_t argsOffset) override;

    /// Inserts pipeline barriers via `vkCmdPipelineBarrier2` (VK_KHR_synchronization2).
    void ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) override;

    /// Clears a color image to the specified RGBA value via `vkCmdClearColorImage`.
    void ClearRenderTarget(RHITextureHandle texture, const float color[4]) override;

    /// Clears a depth-stencil image via `vkCmdClearDepthStencilImage`.
    void ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) override;

    /// Copies a region of bytes between two buffers via `vkCmdCopyBuffer`.
    void CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) override;

    /// Copies an entire texture to another via `vkCmdCopyImage`.
    void CopyTexture(RHITextureHandle src, RHITextureHandle dst) override;

    /// Copies buffer data into a texture subresource via `vkCmdCopyBufferToImage`.
    void CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                             uint32_t srcRowPitch,
                             RHITextureHandle dstTexture,
                             uint32_t mipLevel, uint32_t arraySlice) override;

    /// Copies a texture subresource into a buffer via `vkCmdCopyImageToBuffer`.
    void CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                             uint32_t dstRowPitch, uint32_t arraySlice = 0) override;

    /// Begins a render pass via `vkCmdBeginRendering` (VK_KHR_dynamic_rendering).
    /// Creates per-slice/mip image views if targeting non-zero array slices or mip levels.
    void BeginRenderPass(const RHIRenderPassDesc& desc) override;

    /// Ends the render pass via `vkCmdEndRendering`. Destroys temporary image views.
    void EndRenderPass() override;

private:
    /// Writes the texture and sampler to the current frame's descriptor set and
    /// marks it dirty so the next draw call binds it.
    void FlushDescriptorSet();

    /// Allocates a fresh descriptor set from the pool, copies the current
    /// frame's working set into it, and returns the copy. This gives each
    /// draw call its own immutable snapshot of descriptor state.
    VkDescriptorSet AllocateAndCopyDescriptorSet();

    VulkanDevice*        m_device = nullptr;
    VkCommandPool        m_commandPools[kVulkanFrameCount] = {};
    VkCommandBuffer      m_commandBuffers[kVulkanFrameCount] = {};
    VkDescriptorSet      m_descriptorSets[kVulkanFrameCount] = {};
    uint32_t             m_frameIndex = 0;
    RHIQueueType         m_queueType = RHIQueueType::Graphics;
    bool                 m_inRenderPass = false;
    bool                 m_descriptorsDirty = false;
    bool                 m_computePipelineBound = false;

    /// Tracks all descriptor sets allocated this frame for batch-free on reset.
    static constexpr uint32_t kMaxDescriptorSetsPerFrame = 4096;
    VkDescriptorSet      m_allocatedSets[kMaxDescriptorSetsPerFrame] = {};
    uint32_t             m_allocatedSetCount = 0;

    /// Bitmask of bindings that have been written to the current frame's working
    /// descriptor set. Only written bindings are copied in AllocateAndCopyDescriptorSet —
    /// copying unwritten (uninitialized) bindings is undefined behaviour in Vulkan.
    static constexpr uint32_t kTotalBindings = 64; // 16 textures + 16 samplers + 16 CBVs + 16 UAVs
    uint64_t             m_writtenBindingsMask = 0; // bit i = binding i was written

    /// Temporary per-slice/mip image views created in BeginRenderPass for
    /// non-zero arraySlice or mipLevel. Destroyed in EndRenderPass.
    static constexpr uint32_t kMaxTempImageViews = 8;
    VkImageView          m_tempImageViews[kMaxTempImageViews] = {};
    uint32_t             m_tempImageViewCount = 0;

    // ── Scratch UBOs for SetRootConstants ────────────────────────────────
    // Vulkan shaders compiled with `-fvk-b-shift 32 0` map cbuffer register(bN)
    // to UBO descriptor binding 32+N. Push constants are a different mechanism
    // that the shader never reads. These scratch UBOs provide the CPU→descriptor
    // path, mirroring DX11's scratch constant buffer pattern.

    /// Gets or creates a scratch UBO for the given slot. If the existing buffer
    /// is too small, it is destroyed and a larger one is created via VMA.
    /// Returns the VkBuffer and its persistently-mapped pointer, or
    /// {VK_NULL_HANDLE, nullptr} on failure.
    struct ScratchUBO {
        VkBuffer      buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void*         mappedPtr = nullptr;
        uint32_t      sizeBytes = 0;
    };
    ScratchUBO GetOrCreateScratchUBO(uint32_t slot, uint32_t requiredSize);

    /// Maximum number of scratch UBO slots (one per root parameter index).
    static constexpr uint32_t kMaxScratchUBOSlots = 8;
    /// Per-slot scratch UBO state. Lazily allocated on first use.
    ScratchUBO           m_scratchUBOs[kMaxScratchUBOSlots] = {};
};

} // namespace uz
