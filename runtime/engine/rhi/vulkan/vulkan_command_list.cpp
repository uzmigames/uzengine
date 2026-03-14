/// Vulkan Command List Implementation
///
/// Records GPU commands into VkCommandBuffer, submits via vkQueueSubmit.
/// Uses VK_KHR_dynamic_rendering for render passes and VK_KHR_synchronization2
/// for resource barriers. Command pools are triple-buffered.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>

#include "vulkan_command_list.h"
#include "vulkan_device.h"

#include <tracy/TracyVulkan.hpp>

#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIFormat to VkFormat (command list needs its own copy since
/// vulkan_device.cpp's is file-static).
static VkFormat ToVkFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8Unorm:        return VK_FORMAT_R8_UNORM;
        case RHIFormat::RG8Unorm:       return VK_FORMAT_R8G8_UNORM;
        case RHIFormat::RGBA8Unorm:     return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:      return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::BGRA8Unorm:     return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:      return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::R16Float:       return VK_FORMAT_R16_SFLOAT;
        case RHIFormat::RG16Float:      return VK_FORMAT_R16G16_SFLOAT;
        case RHIFormat::RGBA16Float:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case RHIFormat::R32Float:       return VK_FORMAT_R32_SFLOAT;
        case RHIFormat::RG32Float:      return VK_FORMAT_R32G32_SFLOAT;
        case RHIFormat::RGB32Float:     return VK_FORMAT_R32G32B32_SFLOAT;
        case RHIFormat::RGBA32Float:    return VK_FORMAT_R32G32B32A32_SFLOAT;
        case RHIFormat::R32Uint:        return VK_FORMAT_R32_UINT;
        case RHIFormat::RG32Uint:       return VK_FORMAT_R32G32_UINT;
        case RHIFormat::RGBA32Uint:     return VK_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::D16Unorm:       return VK_FORMAT_D16_UNORM;
        case RHIFormat::D32Float:       return VK_FORMAT_D32_SFLOAT;
        case RHIFormat::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default:                        return VK_FORMAT_UNDEFINED;
    }
}

/// Returns true if format is depth/stencil.
static bool IsDepthFormat(RHIFormat format) {
    return format == RHIFormat::D16Unorm || format == RHIFormat::D32Float
        || format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint;
}

/// Maps RHIResourceState to Vulkan image layout.
static VkImageLayout ToVkImageLayout(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined:         return VK_IMAGE_LAYOUT_UNDEFINED;
        case RHIResourceState::Common:            return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::ShaderResource:    return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RHIResourceState::UnorderedAccess:   return VK_IMAGE_LAYOUT_GENERAL;
        case RHIResourceState::RenderTarget:      return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthStencilWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case RHIResourceState::DepthStencilRead:  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case RHIResourceState::CopySrc:           return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RHIResourceState::CopyDst:           return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RHIResourceState::Present:           return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:                                  return VK_IMAGE_LAYOUT_GENERAL;
    }
}

/// Maps RHIResourceState to VkAccessFlags2 (synchronization2).
static VkAccessFlags2 ToVkAccessFlags(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined:         return VK_ACCESS_2_NONE;
        case RHIResourceState::Common:            return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        case RHIResourceState::VertexBuffer:      return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        case RHIResourceState::IndexBuffer:       return VK_ACCESS_2_INDEX_READ_BIT;
        case RHIResourceState::ConstantBuffer:    return VK_ACCESS_2_UNIFORM_READ_BIT;
        case RHIResourceState::ShaderResource:    return VK_ACCESS_2_SHADER_READ_BIT;
        case RHIResourceState::UnorderedAccess:   return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        case RHIResourceState::RenderTarget:      return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case RHIResourceState::DepthStencilWrite: return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case RHIResourceState::DepthStencilRead:  return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case RHIResourceState::CopySrc:           return VK_ACCESS_2_TRANSFER_READ_BIT;
        case RHIResourceState::CopyDst:           return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case RHIResourceState::Present:           return VK_ACCESS_2_NONE;
        case RHIResourceState::IndirectArgument:  return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        default:                                  return VK_ACCESS_2_NONE;
    }
}

/// Maps RHIResourceState to VkPipelineStageFlags2 (synchronization2).
static VkPipelineStageFlags2 ToVkPipelineStage(RHIResourceState state) {
    switch (state) {
        case RHIResourceState::Undefined:         return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        case RHIResourceState::Common:            return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        case RHIResourceState::VertexBuffer:      return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        case RHIResourceState::IndexBuffer:       return VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        case RHIResourceState::ConstantBuffer:    return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        case RHIResourceState::ShaderResource:    return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        case RHIResourceState::UnorderedAccess:   return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case RHIResourceState::RenderTarget:      return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case RHIResourceState::DepthStencilWrite: return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        case RHIResourceState::DepthStencilRead:  return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        case RHIResourceState::CopySrc:           return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case RHIResourceState::CopyDst:           return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case RHIResourceState::Present:           return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        case RHIResourceState::IndirectArgument:  return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        default:                                  return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
}

/// Returns the VkImageAspectFlags for a given format.
static VkImageAspectFlags GetAspectFlags(RHIFormat format) {
    if (IsDepthFormat(format)) {
        VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == RHIFormat::D24UnormS8Uint || format == RHIFormat::D32FloatS8Uint) {
            flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return flags;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

VulkanCommandList::~VulkanCommandList() {
    if (!m_device) return;

    VkDevice device = m_device->GetVkDevice();
    for (uint32_t i = 0; i < kVulkanFrameCount; ++i) {
        if (m_commandPools[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, m_commandPools[i], nullptr);
        }
    }
    UZ_LOG(RHI, Info, "VulkanCommandList destroyed");
}

bool VulkanCommandList::Init(VulkanDevice* device, RHIQueueType queueType) {
    UZ_PROFILE_FUNCTION();
    if (!device || device->GetVkDevice() == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Error, "VulkanCommandList::Init — null device");
        return false;
    }

    m_device = device;
    m_queueType = queueType;

    VkDevice vkDevice = device->GetVkDevice();
    uint32_t queueFamily = device->GetGraphicsQueueFamily();

    for (uint32_t i = 0; i < kVulkanFrameCount; ++i) {
        VkCommandPoolCreateInfo poolCI = {};
        poolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCI.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolCI.queueFamilyIndex = queueFamily;

        VkResult result = vkCreateCommandPool(vkDevice, &poolCI, nullptr, &m_commandPools[i]);
        if (result != VK_SUCCESS) {
            UZ_LOG(RHI, Error, "vkCreateCommandPool failed for frame {}, result={}", i, static_cast<int>(result));
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(vkDevice, &allocInfo, &m_commandBuffers[i]);
        if (result != VK_SUCCESS) {
            UZ_LOG(RHI, Error, "vkAllocateCommandBuffers failed for frame {}, result={}", i, static_cast<int>(result));
            return false;
        }
    }

    // Allocate per-frame descriptor sets from the device's global pool
    VkDescriptorSetLayout layouts[kVulkanFrameCount];
    for (uint32_t i = 0; i < kVulkanFrameCount; ++i) {
        layouts[i] = device->GetDescriptorSetLayout();
    }

    VkDescriptorSetAllocateInfo dsAllocInfo = {};
    dsAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAllocInfo.descriptorPool = device->GetDescriptorPool();
    dsAllocInfo.descriptorSetCount = kVulkanFrameCount;
    dsAllocInfo.pSetLayouts = layouts;

    VkResult dsResult = vkAllocateDescriptorSets(vkDevice, &dsAllocInfo, m_descriptorSets);
    if (dsResult != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkAllocateDescriptorSets failed for command list, result={}", static_cast<int>(dsResult));
        return false;
    }
    UZ_STAT_INCREMENT(RHI, VkDescriptorAllocations, kVulkanFrameCount);

    UZ_LOG(RHI, Info, "VulkanCommandList created (queue={})", static_cast<int>(queueType));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::Begin() {
    UZ_PROFILE_FUNCTION();

    VkDevice device = m_device->GetVkDevice();
    vkResetCommandPool(device, m_commandPools[m_frameIndex], 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(m_commandBuffers[m_frameIndex], &beginInfo);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkBeginCommandBuffer failed, result={}", static_cast<int>(result));
    }

    m_descriptorsDirty = false;
}

void VulkanCommandList::End() {
    UZ_PROFILE_FUNCTION();

    // Collect Tracy GPU timestamps before ending the command buffer.
    // TracyVkCollect records query pool reset commands into this command buffer.
    if (m_device->GetTracyGpuContext()) {
        TracyVkCollect(m_device->GetTracyGpuContext(), m_commandBuffers[m_frameIndex]);
    }

    VkResult result = vkEndCommandBuffer(m_commandBuffers[m_frameIndex]);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkEndCommandBuffer failed, result={}", static_cast<int>(result));
    }
}

void VulkanCommandList::Execute() {
    UZ_PROFILE_FUNCTION();

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_frameIndex];

    VkResult result = vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkQueueSubmit failed, result={}", static_cast<int>(result));
    }

    // Rotate to next frame's command pool
    m_frameIndex = (m_frameIndex + 1) % kVulkanFrameCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline and Resource Binding
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::SetPipeline(RHIPipelineHandle pipeline) {
    UZ_PROFILE_FUNCTION();
    if (!pipeline.IsValid()) {
        UZ_LOG(RHI, Warning, "SetPipeline — invalid handle");
        return;
    }

    auto* vkPipeline = m_device->GetPipelinePool().Get(pipeline.index, pipeline.generation);
    if (!vkPipeline || vkPipeline->pipeline == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetPipeline — pipeline not found in pool");
        return;
    }

    vkCmdBindPipeline(m_commandBuffers[m_frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->pipeline);
}

void VulkanCommandList::SetVertexBuffer(uint32_t slot, RHIBufferHandle buffer, uint32_t offset) {
    if (!buffer.IsValid()) return;

    auto* vkBuffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!vkBuffer || vkBuffer->buffer == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetVertexBuffer — buffer not found in pool");
        return;
    }

    VkDeviceSize vkOffset = static_cast<VkDeviceSize>(offset);
    vkCmdBindVertexBuffers(m_commandBuffers[m_frameIndex], slot, 1, &vkBuffer->buffer, &vkOffset);
}

void VulkanCommandList::SetIndexBuffer(RHIBufferHandle buffer, RHIIndexFormat format, uint32_t offset) {
    if (!buffer.IsValid()) return;

    auto* vkBuffer = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!vkBuffer || vkBuffer->buffer == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetIndexBuffer — buffer not found in pool");
        return;
    }

    VkIndexType indexType = (format == RHIIndexFormat::Uint16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(m_commandBuffers[m_frameIndex], vkBuffer->buffer, static_cast<VkDeviceSize>(offset), indexType);
}

void VulkanCommandList::SetViewport(const RHIViewport& viewport) {
    // Vulkan Y-flip: use negative height to match DX12 NDC conventions
    VkViewport vp = {};
    vp.x = viewport.x;
    vp.y = viewport.y + viewport.height;
    vp.width = viewport.width;
    vp.height = -viewport.height;
    vp.minDepth = viewport.minDepth;
    vp.maxDepth = viewport.maxDepth;
    vkCmdSetViewport(m_commandBuffers[m_frameIndex], 0, 1, &vp);
}

void VulkanCommandList::SetScissor(const RHIScissorRect& scissor) {
    VkRect2D rect = {};
    rect.offset.x = scissor.left;
    rect.offset.y = scissor.top;
    rect.extent.width = static_cast<uint32_t>(scissor.right - scissor.left);
    rect.extent.height = static_cast<uint32_t>(scissor.bottom - scissor.top);
    vkCmdSetScissor(m_commandBuffers[m_frameIndex], 0, 1, &rect);
}

void VulkanCommandList::SetDescriptorIndex(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    // Map to push constants: each rootParamIndex occupies 4 bytes at offset rootParamIndex*4
    uint32_t pushOffset = rootParamIndex * sizeof(uint32_t);
    if (pushOffset + sizeof(uint32_t) > 128) {
        UZ_LOG(RHI, Warning, "SetDescriptorIndex: rootParamIndex {} exceeds push constant range", rootParamIndex);
        return;
    }
    vkCmdPushConstants(m_commandBuffers[m_frameIndex], m_device->GetPipelineLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                       pushOffset, sizeof(uint32_t), &descriptorIndex);
}

void VulkanCommandList::SetRootConstants(uint32_t rootParamIndex, const uint32_t* data,
                                          uint32_t count, uint32_t offsetIn32BitValues) {
    uint32_t pushOffset = rootParamIndex * sizeof(uint32_t) + offsetIn32BitValues * sizeof(uint32_t);
    uint32_t pushSize = count * sizeof(uint32_t);
    if (pushOffset + pushSize > 128) {
        UZ_LOG(RHI, Warning, "SetRootConstants: exceeds 128-byte push constant range");
        return;
    }
    vkCmdPushConstants(m_commandBuffers[m_frameIndex], m_device->GetPipelineLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                       pushOffset, pushSize, data);
}

void VulkanCommandList::SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) {
    // Resolve the GPU address to a VkBuffer + offset, then bind as a uniform
    // buffer descriptor. Uses root layout to translate rootParamIndex → HLSL
    // register, then applies -fvk-b-shift 32 0 convention for the binding.
    auto lookup = m_device->FindBufferByGPUAddress(gpuAddress);
    if (lookup.buffer == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetRootCBV: no buffer found for GPU address {:#x}", gpuAddress);
        return;
    }

    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = lookup.buffer;
    bufInfo.offset = lookup.offset;
    bufInfo.range = lookup.size - lookup.offset;

    // Clamp to maxUniformBufferRange (typically 65536 on most drivers)
    constexpr uint64_t kMaxUniformRange = 65536;
    if (bufInfo.range > kMaxUniformRange) {
        bufInfo.range = kMaxUniformRange;
    }

    // Translate root param index → HLSL register via root layout.
    // Root param 3 maps to b4 (shadow), not b3 — using the index directly was a bug.
    const uint32_t hlslRegister = m_device->GetRootLayout().params[rootParamIndex].shaderRegister;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSets[m_frameIndex];
    write.dstBinding = 32 + hlslRegister;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(m_device->GetVkDevice(), 1, &write, 0, nullptr);
    UZ_STAT_INCREMENT(RHI, VkDescriptorWrites);
    m_descriptorsDirty = true;
}

void VulkanCommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                                    uint64_t offsetBytes, const void* /*cpuData*/, uint32_t /*dataSize*/) {
    // Vulkan: buffer is persistently mapped — GPU reads directly. cpuData unused.
    auto* buf = m_device->GetBufferPool().Get(buffer.index, buffer.generation);
    if (!buf || buf->buffer == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetRootCBV(handle): could not resolve buffer");
        return;
    }

    VkDescriptorBufferInfo bufInfo = {};
    bufInfo.buffer = buf->buffer;
    bufInfo.offset = offsetBytes;
    bufInfo.range = buf->desc.sizeBytes - offsetBytes;

    constexpr uint64_t kMaxUniformRange = 65536;
    if (bufInfo.range > kMaxUniformRange) {
        bufInfo.range = kMaxUniformRange;
    }

    // Translate root param index → HLSL register via root layout.
    const uint32_t hlslRegister = m_device->GetRootLayout().params[rootParamIndex].shaderRegister;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSets[m_frameIndex];
    write.dstBinding = 32 + hlslRegister;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(m_device->GetVkDevice(), 1, &write, 0, nullptr);
    UZ_STAT_INCREMENT(RHI, VkDescriptorWrites);
    m_descriptorsDirty = true;
}

void VulkanCommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    // For non-bindless paths: look up the texture by SRV index and write it
    // (plus the first available sampler) to the per-frame descriptor set.
    auto* tex = m_device->FindTextureBySrvIndex(descriptorIndex);
    if (!tex || tex->imageView == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetDescriptorTable: no texture found for SRV index {}", descriptorIndex);
        return;
    }

    VkDescriptorSet dstSet = m_descriptorSets[m_frameIndex];

    // Write texture to binding 0 (t0)
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = tex->imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2] = {};
    uint32_t writeCount = 1;

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = dstSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &imageInfo;

    // Auto-bind sampler index 0 at binding 16 (s0)
    VkDescriptorImageInfo samplerInfo = {};
    auto* sam = m_device->FindSamplerByIndex(0);
    if (sam && sam->sampler != VK_NULL_HANDLE) {
        samplerInfo.sampler = sam->sampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = dstSet;
        writes[1].dstBinding = 16;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].pImageInfo = &samplerInfo;
        writeCount = 2;
    } else {
        UZ_LOG(RHI, Warning, "SetDescriptorTable: no sampler at index 0 — texture will sample with undefined sampler");
    }

    vkUpdateDescriptorSets(m_device->GetVkDevice(), writeCount, writes, 0, nullptr);
    UZ_STAT_INCREMENT(RHI, VkDescriptorWrites);
    m_descriptorsDirty = true;
}

void VulkanCommandList::SetFragmentTexture(uint32_t slot, RHITextureHandle texture) {
    if (!texture.IsValid()) return;

    auto* tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!tex || tex->imageView == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "SetFragmentTexture: invalid texture at slot {}", slot);
        return;
    }


    VkDescriptorSet dstSet = m_descriptorSets[m_frameIndex];

    // Write texture to binding `slot` (t0-t15 map to binding 0-15 via -fvk-t-shift 0 0)
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = tex->imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2] = {};
    uint32_t writeCount = 1;

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = dstSet;
    writes[0].dstBinding = slot;           // t0=0, t1=1, t2=2, ...
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &imageInfo;

    // Auto-bind sampler at binding 16+slot (s0-s15 map to binding 16-31 via -fvk-s-shift 16 0)
    VkDescriptorImageInfo samplerInfo = {};
    auto* sam = m_device->FindSamplerByIndex(0);
    if (sam && sam->sampler != VK_NULL_HANDLE) {
        samplerInfo.sampler = sam->sampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = dstSet;
        writes[1].dstBinding = 16 + slot;  // s0=16, s1=17, ...
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].pImageInfo = &samplerInfo;
        writeCount = 2;
    }

    vkUpdateDescriptorSets(m_device->GetVkDevice(), writeCount, writes, 0, nullptr);
    m_descriptorsDirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Set Flush
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::FlushDescriptorSet() {
    if (!m_descriptorsDirty) return;

    vkCmdBindDescriptorSets(m_commandBuffers[m_frameIndex],
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_device->GetPipelineLayout(),
                            0, 1, &m_descriptorSets[m_frameIndex],
                            0, nullptr);
    m_descriptorsDirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw and Dispatch
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                     uint32_t firstIndex, int32_t vertexOffset,
                                     uint32_t firstInstance) {
    FlushDescriptorSet();
    vkCmdDrawIndexed(m_commandBuffers[m_frameIndex], indexCount, instanceCount,
                     firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount,
                              uint32_t firstVertex, uint32_t firstInstance) {
    FlushDescriptorSet();
    vkCmdDraw(m_commandBuffers[m_frameIndex], vertexCount, instanceCount,
              firstVertex, firstInstance);
}

void VulkanCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                  uint32_t groupCountZ) {
    vkCmdDispatch(m_commandBuffers[m_frameIndex], groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::DrawIndexedIndirect(RHIBufferHandle /*argsBuffer*/, uint64_t /*argsOffset*/,
                                             uint32_t /*drawCount*/, uint32_t /*argsStride*/) {
    // TODO: Implement via vkCmdDrawIndexedIndirect once buffer handle resolution is available
    UZ_LOG(RHI, Warning, "DrawIndexedIndirect — Vulkan stub, not yet implemented");
}

void VulkanCommandList::DispatchIndirect(RHIBufferHandle /*argsBuffer*/, uint64_t /*argsOffset*/) {
    // TODO: Implement via vkCmdDispatchIndirect once buffer handle resolution is available
    UZ_LOG(RHI, Warning, "DispatchIndirect — Vulkan stub, not yet implemented");
}

// ─────────────────────────────────────────────────────────────────────────────
// Barriers
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    UZ_PROFILE_FUNCTION();
    if (!barriers || count == 0) return;

    for (uint32_t i = 0; i < count; ++i) {
        const auto& barrier = barriers[i];

        if (barrier.texture.IsValid()) {
            auto* tex = m_device->GetTexturePool().Get(barrier.texture.index, barrier.texture.generation);
            if (!tex || tex->image == VK_NULL_HANDLE) continue;

            VkImageMemoryBarrier2 imgBarrier = {};
            imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            imgBarrier.srcStageMask = ToVkPipelineStage(barrier.stateBefore);
            imgBarrier.srcAccessMask = ToVkAccessFlags(barrier.stateBefore);
            imgBarrier.dstStageMask = ToVkPipelineStage(barrier.stateAfter);
            imgBarrier.dstAccessMask = ToVkAccessFlags(barrier.stateAfter);
            imgBarrier.oldLayout = ToVkImageLayout(barrier.stateBefore);
            imgBarrier.newLayout = ToVkImageLayout(barrier.stateAfter);
            imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.image = tex->image;
            imgBarrier.subresourceRange.aspectMask = GetAspectFlags(tex->desc.format);
            imgBarrier.subresourceRange.baseMipLevel = 0;
            imgBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            imgBarrier.subresourceRange.baseArrayLayer = 0;
            imgBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

            VkDependencyInfo depInfo = {};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &imgBarrier;

            vkCmdPipelineBarrier2(m_commandBuffers[m_frameIndex], &depInfo);

            // Update tracked layout
            tex->currentLayout = imgBarrier.newLayout;

        } else if (barrier.buffer.IsValid()) {
            auto* buf = m_device->GetBufferPool().Get(barrier.buffer.index, barrier.buffer.generation);
            if (!buf || buf->buffer == VK_NULL_HANDLE) continue;

            VkBufferMemoryBarrier2 bufBarrier = {};
            bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            bufBarrier.srcStageMask = ToVkPipelineStage(barrier.stateBefore);
            bufBarrier.srcAccessMask = ToVkAccessFlags(barrier.stateBefore);
            bufBarrier.dstStageMask = ToVkPipelineStage(barrier.stateAfter);
            bufBarrier.dstAccessMask = ToVkAccessFlags(barrier.stateAfter);
            bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bufBarrier.buffer = buf->buffer;
            bufBarrier.offset = 0;
            bufBarrier.size = VK_WHOLE_SIZE;

            VkDependencyInfo depInfo = {};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.bufferMemoryBarrierCount = 1;
            depInfo.pBufferMemoryBarriers = &bufBarrier;

            vkCmdPipelineBarrier2(m_commandBuffers[m_frameIndex], &depInfo);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear Operations
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::ClearRenderTarget(RHITextureHandle texture, const float color[4]) {
    if (!texture.IsValid()) return;

    auto* tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!tex || tex->image == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "ClearRenderTarget — texture not found");
        return;
    }

    // Transition to TRANSFER_DST for clear (if not already)
    if (tex->currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = tex->currentLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex->image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(m_commandBuffers[m_frameIndex], &dep);
        tex->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    VkClearColorValue clearColor = {};
    std::memcpy(clearColor.float32, color, sizeof(float) * 4);

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vkCmdClearColorImage(m_commandBuffers[m_frameIndex], tex->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);
}

void VulkanCommandList::ClearDepthStencil(RHITextureHandle texture, float depth, uint8_t stencil) {
    if (!texture.IsValid()) return;

    auto* tex = m_device->GetTexturePool().Get(texture.index, texture.generation);
    if (!tex || tex->image == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "ClearDepthStencil — texture not found");
        return;
    }

    VkImageAspectFlags aspect = GetAspectFlags(tex->desc.format);

    // Transition to TRANSFER_DST for clear
    if (tex->currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = tex->currentLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex->image;
        barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

        VkDependencyInfo dep = {};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(m_commandBuffers[m_frameIndex], &dep);
        tex->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    VkClearDepthStencilValue clearValue = {};
    clearValue.depth = depth;
    clearValue.stencil = stencil;

    VkImageSubresourceRange range = {};
    range.aspectMask = aspect;
    range.baseMipLevel = 0;
    range.levelCount = VK_REMAINING_MIP_LEVELS;
    range.baseArrayLayer = 0;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;

    vkCmdClearDepthStencilImage(m_commandBuffers[m_frameIndex], tex->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy Operations
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::CopyBuffer(RHIBufferHandle src, uint64_t srcOffset,
                                    RHIBufferHandle dst, uint64_t dstOffset, uint64_t size) {
    if (!src.IsValid() || !dst.IsValid()) return;

    auto* srcBuf = m_device->GetBufferPool().Get(src.index, src.generation);
    auto* dstBuf = m_device->GetBufferPool().Get(dst.index, dst.generation);
    if (!srcBuf || !dstBuf || srcBuf->buffer == VK_NULL_HANDLE || dstBuf->buffer == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "CopyBuffer — could not resolve buffer handles");
        return;
    }

    VkBufferCopy region = {};
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size = size;
    vkCmdCopyBuffer(m_commandBuffers[m_frameIndex], srcBuf->buffer, dstBuf->buffer, 1, &region);
}

void VulkanCommandList::CopyTexture(RHITextureHandle src, RHITextureHandle dst) {
    if (!src.IsValid() || !dst.IsValid()) return;

    auto* srcTex = m_device->GetTexturePool().Get(src.index, src.generation);
    auto* dstTex = m_device->GetTexturePool().Get(dst.index, dst.generation);
    if (!srcTex || !dstTex || srcTex->image == VK_NULL_HANDLE || dstTex->image == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "CopyTexture — could not resolve texture handles");
        return;
    }

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = GetAspectFlags(srcTex->desc.format);
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = srcTex->desc.depthOrArraySize;
    region.dstSubresource = region.srcSubresource;
    region.extent = {srcTex->desc.width, srcTex->desc.height, 1};

    vkCmdCopyImage(m_commandBuffers[m_frameIndex],
                   srcTex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstTex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region);
}

void VulkanCommandList::CopyBufferToTexture(RHIBufferHandle srcBuffer, uint64_t srcOffset,
                                              uint32_t srcRowPitch,
                                              RHITextureHandle dstTexture,
                                              uint32_t mipLevel, uint32_t arraySlice) {
    if (!srcBuffer.IsValid() || !dstTexture.IsValid()) return;

    auto* buf = m_device->GetBufferPool().Get(srcBuffer.index, srcBuffer.generation);
    auto* tex = m_device->GetTexturePool().Get(dstTexture.index, dstTexture.generation);
    if (!buf || !tex || buf->buffer == VK_NULL_HANDLE || tex->image == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "CopyBufferToTexture — could not resolve handles");
        return;
    }

    // Transition to TRANSFER_DST_OPTIMAL if not already in that layout.
    // Textures start in UNDEFINED after creation — Vulkan requires an explicit
    // layout transition before vkCmdCopyBufferToImage.
    if (tex->currentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        VkImageMemoryBarrier2 preBarrier = {};
        preBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        preBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        preBarrier.srcAccessMask = VK_ACCESS_2_NONE;
        preBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        preBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        preBarrier.oldLayout = tex->currentLayout;
        preBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preBarrier.image = tex->image;
        preBarrier.subresourceRange.aspectMask = GetAspectFlags(tex->desc.format);
        preBarrier.subresourceRange.baseMipLevel = 0;
        preBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        preBarrier.subresourceRange.baseArrayLayer = 0;
        preBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        VkDependencyInfo depInfo = {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &preBarrier;
        vkCmdPipelineBarrier2(m_commandBuffers[m_frameIndex], &depInfo);

        tex->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    uint32_t mipWidth = tex->desc.width >> mipLevel;
    uint32_t mipHeight = tex->desc.height >> mipLevel;
    if (mipWidth == 0) mipWidth = 1;
    if (mipHeight == 0) mipHeight = 1;

    VkBufferImageCopy region = {};
    region.bufferOffset = srcOffset;
    region.bufferRowLength = srcRowPitch / BytesPerTexel(tex->desc.format);
    region.bufferImageHeight = 0; // Tightly packed rows
    region.imageSubresource.aspectMask = GetAspectFlags(tex->desc.format);
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {mipWidth, mipHeight, 1};

    vkCmdCopyBufferToImage(m_commandBuffers[m_frameIndex],
                           buf->buffer, tex->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);
}

void VulkanCommandList::CopyTextureToBuffer(RHITextureHandle srcTexture, uint32_t mipLevel,
                                             RHIBufferHandle dstBuffer, uint64_t dstOffset,
                                             uint32_t dstRowPitch) {
    if (!srcTexture.IsValid() || !dstBuffer.IsValid()) return;

    auto* tex = m_device->GetTexturePool().Get(srcTexture.index, srcTexture.generation);
    auto* buf = m_device->GetBufferPool().Get(dstBuffer.index, dstBuffer.generation);
    if (!tex || tex->image == VK_NULL_HANDLE || !buf || buf->buffer == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Warning, "CopyTextureToBuffer — could not resolve handles");
        return;
    }

    const uint32_t mipWidth  = std::max(1u, tex->desc.width >> mipLevel);
    const uint32_t mipHeight = std::max(1u, tex->desc.height >> mipLevel);
    const uint32_t texelSize = BytesPerTexel(tex->desc.format);

    VkBufferImageCopy region = {};
    region.bufferOffset = dstOffset;
    // bufferRowLength is in texels, not bytes
    region.bufferRowLength = dstRowPitch / texelSize;
    region.bufferImageHeight = mipHeight;
    region.imageSubresource.aspectMask = GetAspectFlags(tex->desc.format);
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {mipWidth, mipHeight, 1};

    vkCmdCopyImageToBuffer(m_commandBuffers[m_frameIndex],
                           tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buf->buffer, 1, &region);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass
// ─────────────────────────────────────────────────────────────────────────────

void VulkanCommandList::BeginRenderPass(const RHIRenderPassDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkRenderingAttachmentInfo colorAttachments[8] = {};
    uint32_t colorCount = 0;

    for (uint32_t i = 0; i < desc.colorAttachmentCount && i < 8; ++i) {
        const auto& att = desc.colorAttachments[i];
        if (!att.texture.IsValid()) continue;

        auto* tex = m_device->GetTexturePool().Get(att.texture.index, att.texture.generation);
        if (!tex || tex->imageView == VK_NULL_HANDLE) {
            UZ_LOG(RHI, Warning, "BeginRenderPass — color attachment {} has no image view", i);
            continue;
        }

        auto& vkAtt = colorAttachments[colorCount];
        vkAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        vkAtt.imageView = tex->imageView;
        vkAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkAtt.loadOp = att.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        vkAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(vkAtt.clearValue.color.float32, att.clearColor, sizeof(float) * 4);

        ++colorCount;
    }

    VkRenderingAttachmentInfo depthAttachment = {};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    bool hasDepth = false;

    if (desc.hasDepth && desc.depthAttachment.texture.IsValid()) {
        auto* depthTex = m_device->GetTexturePool().Get(
            desc.depthAttachment.texture.index, desc.depthAttachment.texture.generation);
        if (depthTex && depthTex->imageView != VK_NULL_HANDLE) {
            hasDepth = true;
            depthAttachment.imageView = depthTex->imageView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = desc.depthAttachment.clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = desc.depthAttachment.clearDepth;
            depthAttachment.clearValue.depthStencil.stencil = desc.depthAttachment.clearStencil;
        }
    }

    // Determine render area from first color attachment or depth
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    if (colorCount > 0 && desc.colorAttachments[0].texture.IsValid()) {
        auto* tex = m_device->GetTexturePool().Get(
            desc.colorAttachments[0].texture.index, desc.colorAttachments[0].texture.generation);
        if (tex) {
            renderWidth = tex->desc.width;
            renderHeight = tex->desc.height;
        }
    } else if (hasDepth && desc.depthAttachment.texture.IsValid()) {
        auto* tex = m_device->GetTexturePool().Get(
            desc.depthAttachment.texture.index, desc.depthAttachment.texture.generation);
        if (tex) {
            renderWidth = tex->desc.width;
            renderHeight = tex->desc.height;
        }
    }

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, {renderWidth, renderHeight}};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = colorCount;
    renderingInfo.pColorAttachments = colorCount > 0 ? colorAttachments : nullptr;
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;
    // Stencil shares the depth attachment if format has stencil
    renderingInfo.pStencilAttachment = hasDepth ? &depthAttachment : nullptr;

    vkCmdBeginRendering(m_commandBuffers[m_frameIndex], &renderingInfo);
    m_inRenderPass = true;
}

void VulkanCommandList::EndRenderPass() {
    if (m_inRenderPass) {
        vkCmdEndRendering(m_commandBuffers[m_frameIndex]);
        m_inRenderPass = false;
    }
}

} // namespace uz
