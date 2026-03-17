/// Vulkan Resource Operations — Buffer, Texture, Shader, Pipeline, Sampler
///
/// Split from vulkan_device.cpp to keep file sizes under ~1000 lines.
/// All methods are VulkanDevice members — this file is a separate TU for
/// the same class. volk.h and vk_mem_alloc.h are included WITHOUT
/// _IMPLEMENTATION defines (those live in vulkan_device.cpp).

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <volk.h>
#include <vk_mem_alloc.h>

#include "vulkan_device.h"
#include "vulkan_format_helpers.h"

#include "log.h"
#include "profiler.h"

#include <vector>

namespace uz {

using namespace uz::vk; // Format/state conversion helpers from vulkan_format_helpers.h

// ─────────────────────────────────────────────────────────────────────────────
// Buffer Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle VulkanDevice::CreateBuffer(const RHIBufferDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkBufferCreateInfo bufferCI = {};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.size = desc.sizeBytes;
    bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Map usage flags to VkBufferUsageFlags
    VkBufferUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHIBufferUsage::VertexBuffer))     usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::IndexBuffer))      usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::ConstantBuffer))   usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer)) usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess))  usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::IndirectArgs))     usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::CopySrc))          usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::CopyDst))          usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Always add transfer bits for flexibility
    usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // Enable buffer device address for GPU virtual address queries
    usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    bufferCI.usage = usageFlags;

    // Map memory type to VMA
    VmaAllocationCreateInfo allocCI = {};
    switch (desc.memory) {
        case RHIMemoryType::Default:
            allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            break;
        case RHIMemoryType::Upload:
            allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case RHIMemoryType::Readback:
            allocCI.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
            allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocInfo = {};
    VkResult result = vmaCreateBuffer(m_allocator, &bufferCI, &allocCI, &buffer, &allocation, &allocInfo);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vmaCreateBuffer failed: size={}, result={}",
               desc.sizeBytes, static_cast<int>(result));
        return RHIBufferHandle::Invalid();
    }

    // Debug name via VK_EXT_debug_utils
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(buffer);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_bufferPool.Allocate(generation);
    VulkanBuffer* slot = m_bufferPool.Get(index, generation);
    if (!slot) {
        vmaDestroyBuffer(m_allocator, buffer, allocation);
        UZ_LOG(RHI, Error, "CreateBuffer: pool allocation failed");
        return RHIBufferHandle::Invalid();
    }

    slot->buffer = buffer;
    slot->allocation = allocation;
    slot->desc = desc;
    slot->generation = generation;
    slot->mappedPtr = allocInfo.pMappedData;

    // Cache the buffer device address for GPU address lookups
    {
        VkBufferDeviceAddressInfo addrInfo = {};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = buffer;
        slot->gpuAddress = vkGetBufferDeviceAddress(m_device, &addrInfo);
    }

    // Assign logical SRV index for structured buffers
    if (HasFlag(desc.usage, RHIBufferUsage::StructuredBuffer) && desc.stride > 0) {
        slot->srvIndex = m_nextSrvIndex++;
    }

    // Assign logical UAV index for storage/unordered access buffers
    if (HasFlag(desc.usage, RHIBufferUsage::UnorderedAccess)) {
        slot->uavIndex = m_nextSrvIndex++;
    }

    UZ_LOG(RHI, Debug, "CreateBuffer: size={}, stride={} (name='{}')",
           desc.sizeBytes, desc.stride, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyBuffer(RHIBufferHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) {
        UZ_LOG(RHI, Warning, "DestroyBuffer: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (buf->bufferView != VK_NULL_HANDLE) {
        vkDestroyBufferView(m_device, buf->bufferView, nullptr);
        buf->bufferView = VK_NULL_HANDLE;
    }
    if (buf->buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, buf->buffer, buf->allocation);
        buf->buffer = VK_NULL_HANDLE;
        buf->allocation = nullptr;
    }
    buf->mappedPtr = nullptr;
    m_bufferPool.Free(handle.index);
}

void* VulkanDevice::MapBuffer(RHIBufferHandle handle) {
    VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return nullptr;

    // VMA persistent mapping — already mapped at creation for Upload/Readback
    if (buf->mappedPtr) return buf->mappedPtr;

    // Not a mappable buffer
    if (buf->desc.memory == RHIMemoryType::Default) return nullptr;

    // Fallback: map explicitly (should not normally happen for Upload/Readback)
    void* data = nullptr;
    VkResult result = vmaMapMemory(m_allocator, buf->allocation, &data);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "MapBuffer: vmaMapMemory failed, result={}", static_cast<int>(result));
        return nullptr;
    }
    return data;
}

void VulkanDevice::UnmapBuffer(RHIBufferHandle /*handle*/) {
    // All mappable Vulkan buffers use VMA_ALLOCATION_CREATE_MAPPED_BIT (persistent mapping).
    // Explicit unmap is not needed — VMA manages the mapping lifetime.
}

uint32_t VulkanDevice::GetBufferSRVIndex(RHIBufferHandle handle) const {
    const VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->srvIndex;
}

uint32_t VulkanDevice::GetBufferUAVIndex(RHIBufferHandle handle) const {
    const VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->uavIndex;
}

uint64_t VulkanDevice::GetBufferGPUAddress(RHIBufferHandle handle) const {
    const VulkanBuffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf || buf->buffer == VK_NULL_HANDLE) return 0;
    return buf->gpuAddress;
}

VulkanDevice::BufferAddressLookup VulkanDevice::FindBufferByGPUAddress(uint64_t gpuAddress) const {
    const auto& slots = m_bufferPool.Slots();
    for (const auto& buf : slots) {
        if (buf.buffer == VK_NULL_HANDLE || buf.gpuAddress == 0 || buf.generation == 0) continue;
        const uint64_t bufEnd = buf.gpuAddress + buf.desc.sizeBytes;
        if (gpuAddress >= buf.gpuAddress && gpuAddress < bufEnd) {
            return { buf.buffer, gpuAddress - buf.gpuAddress, buf.desc.sizeBytes };
        }
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Operations
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle VulkanDevice::CreateTexture(const RHITextureDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkImageCreateInfo imageCI = {};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = ToVkFormat(desc.format);
    imageCI.extent = {desc.width, desc.height, 1};
    imageCI.mipLevels = desc.mipLevels;
    imageCI.arrayLayers = desc.depthOrArraySize;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Map usage flags
    VkImageUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource))  usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))    usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))    usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess)) usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopySrc))         usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopyDst))         usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Always add transfer bits for flexibility
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    imageCI.usage = usageFlags;

    // Cube map flag
    if (desc.depthOrArraySize == 6) {
        imageCI.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo allocCI = {};
    allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkResult result = vmaCreateImage(m_allocator, &imageCI, &allocCI, &image, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vmaCreateImage failed: {}x{}, result={}",
               desc.width, desc.height, static_cast<int>(result));
        return RHITextureHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    // Create image view
    VkImageViewCreateInfo viewCI = {};
    viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image = image;
    viewCI.format = ToVkFormat(desc.format);
    viewCI.subresourceRange.baseMipLevel = 0;
    viewCI.subresourceRange.levelCount = desc.mipLevels;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount = desc.depthOrArraySize;

    if (IsDepthFormat(desc.format)) {
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencil(desc.format)) {
            viewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if (desc.depthOrArraySize == 6) {
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (desc.depthOrArraySize > 1) {
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    }

    VkImageView imageView = VK_NULL_HANDLE;
    result = vkCreateImageView(m_device, &viewCI, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateImageView failed, result={}", static_cast<int>(result));
        vmaDestroyImage(m_allocator, image, allocation);
        return RHITextureHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_texturePool.Allocate(generation);
    VulkanTexture* slot = m_texturePool.Get(index, generation);
    if (!slot) {
        vkDestroyImageView(m_device, imageView, nullptr);
        vmaDestroyImage(m_allocator, image, allocation);
        UZ_LOG(RHI, Error, "CreateTexture: pool allocation failed");
        return RHITextureHandle::Invalid();
    }

    slot->image = image;
    slot->allocation = allocation;
    slot->imageView = imageView;
    slot->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    slot->desc = desc;
    slot->generation = generation;
    slot->ownsImage = true;

    // Assign logical descriptor indices
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource)) {
        slot->srvIndex = m_nextSrvIndex++;
    }
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget)) {
        slot->rtvIndex = m_nextSrvIndex++;
    }
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil)) {
        slot->dsvIndex = m_nextSrvIndex++;
    }

    UZ_LOG(RHI, Debug, "CreateTexture: {}x{}, mips={} (name='{}')",
           desc.width, desc.height, desc.mipLevels,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyTexture(RHITextureHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) {
        UZ_LOG(RHI, Warning, "DestroyTexture: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (tex->imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, tex->imageView, nullptr);
        tex->imageView = VK_NULL_HANDLE;
    }
    if (tex->ownsImage && tex->image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, tex->image, tex->allocation);
        tex->image = VK_NULL_HANDLE;
        tex->allocation = nullptr;
    }
    m_texturePool.Free(handle.index);
}

uint32_t VulkanDevice::GetTextureSRVIndex(RHITextureHandle handle) const {
    const VulkanTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return UINT32_MAX;
    return tex->srvIndex;
}

uint32_t VulkanDevice::GetTextureUAVIndex(RHITextureHandle handle) const {
    const auto* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex || !tex->imageView) return UINT32_MAX;
    // Vulkan doesn't use bindless descriptor indices for UAV.
    // Return pool index as sentinel so callers detect "UAV available".
    return handle.index;
}

RHITextureDesc VulkanDevice::GetTextureDesc(RHITextureHandle handle) const {
    const VulkanTexture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return {};
    return tex->desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIShaderHandle VulkanDevice::CreateShader(const RHIShaderDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!desc.bytecode || desc.bytecodeSize == 0) {
        UZ_LOG(RHI, Error, "CreateShader: null/empty bytecode (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    VkShaderModuleCreateInfo moduleCI = {};
    moduleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCI.codeSize = static_cast<size_t>(desc.bytecodeSize);
    moduleCI.pCode = reinterpret_cast<const uint32_t*>(desc.bytecode);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(m_device, &moduleCI, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateShaderModule failed, result={} (name='{}')",
               static_cast<int>(result), desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(shaderModule);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_shaderPool.Allocate(generation);
    VulkanShader* slot = m_shaderPool.Get(index, generation);
    if (!slot) {
        vkDestroyShaderModule(m_device, shaderModule, nullptr);
        UZ_LOG(RHI, Error, "CreateShader: pool allocation failed");
        return RHIShaderHandle::Invalid();
    }

    slot->module = shaderModule;
    slot->bytecode.assign(
        static_cast<const uint8_t*>(desc.bytecode),
        static_cast<const uint8_t*>(desc.bytecode) + desc.bytecodeSize);
    slot->stage = desc.stage;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateShader: stage={}, size={} (name='{}')",
           static_cast<int>(desc.stage), desc.bytecodeSize,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyShader(RHIShaderHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanShader* shader = m_shaderPool.Get(handle.index, handle.generation);
    if (!shader) {
        UZ_LOG(RHI, Warning, "DestroyShader: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (shader->module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, shader->module, nullptr);
        shader->module = VK_NULL_HANDLE;
    }
    shader->bytecode.clear();
    m_shaderPool.Free(handle.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Operations
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    VulkanShader* vsShader = m_shaderPool.Get(desc.vertexShader.index, desc.vertexShader.generation);
    if (!vsShader || vsShader->module == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: invalid vertex shader");
        return RHIPipelineHandle::Invalid();
    }

    VkPipelineShaderStageCreateInfo vsStage = {};
    vsStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vsStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vsStage.module = vsShader->module;
    vsStage.pName = "main";
    shaderStages.push_back(vsStage);

    if (desc.pixelShader.IsValid()) {
        VulkanShader* psShader = m_shaderPool.Get(desc.pixelShader.index, desc.pixelShader.generation);
        if (psShader && psShader->module != VK_NULL_HANDLE) {
            VkPipelineShaderStageCreateInfo psStage = {};
            psStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            psStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            psStage.module = psShader->module;
            psStage.pName = "main";
            shaderStages.push_back(psStage);
        }
    }

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputCI = {};
    vertexInputCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkVertexInputBindingDescription bindingDesc = {};
    std::vector<VkVertexInputAttributeDescription> attributeDescs;

    if (desc.inputElementCount > 0 && desc.inputElements) {
        bindingDesc.binding = 0;
        bindingDesc.stride = desc.vertexStride;
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attributeDescs.resize(desc.inputElementCount);
        for (uint32_t i = 0; i < desc.inputElementCount; ++i) {
            const auto& elem = desc.inputElements[i];
            attributeDescs[i].binding = elem.inputSlot;
            attributeDescs[i].location = i;
            attributeDescs[i].format = ToVkFormat(elem.format);
            attributeDescs[i].offset = elem.byteOffset;
        }

        vertexInputCI.vertexBindingDescriptionCount = 1;
        vertexInputCI.pVertexBindingDescriptions = &bindingDesc;
        vertexInputCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size());
        vertexInputCI.pVertexAttributeDescriptions = attributeDescs.data();
    }

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI = {};
    inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyCI.topology = ToVkTopology(desc.topology);
    inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

    // Viewport/scissor — dynamic state
    VkPipelineViewportStateCreateInfo viewportCI = {};
    viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportCI.viewportCount = 1;
    viewportCI.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizerCI = {};
    rasterizerCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerCI.depthClampEnable = VK_FALSE;
    rasterizerCI.rasterizerDiscardEnable = VK_FALSE;
    rasterizerCI.polygonMode = (desc.rasterizer.fillMode == RHIFillMode::Wireframe)
                                   ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizerCI.cullMode = ToVkCullMode(desc.rasterizer.cullMode);
    rasterizerCI.frontFace = desc.rasterizer.frontCounterClockwise
                                 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizerCI.depthBiasEnable = (desc.rasterizer.depthBias != 0.0f || desc.rasterizer.slopeScaledDepthBias != 0.0f)
                                       ? VK_TRUE : VK_FALSE;
    rasterizerCI.depthBiasConstantFactor = desc.rasterizer.depthBias;
    rasterizerCI.depthBiasSlopeFactor = desc.rasterizer.slopeScaledDepthBias;
    rasterizerCI.lineWidth = 1.0f;

    // Multisample — no MSAA
    VkPipelineMultisampleStateCreateInfo msCI = {};
    msCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth-stencil
    VkPipelineDepthStencilStateCreateInfo depthStencilCI = {};
    depthStencilCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilCI.depthTestEnable = desc.depthStencil.depthEnable ? VK_TRUE : VK_FALSE;
    depthStencilCI.depthWriteEnable = desc.depthStencil.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencilCI.depthCompareOp = ToVkCompareOp(desc.depthStencil.depthFunc);

    // Color blend
    VkPipelineColorBlendAttachmentState blendAttachments[8] = {};
    for (uint32_t i = 0; i < desc.renderTargetCount; ++i) {
        const auto& blend = desc.renderTargetBlend[i];
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                           | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments[i].blendEnable = blend.blendEnable ? VK_TRUE : VK_FALSE;
        blendAttachments[i].srcColorBlendFactor = ToVkBlendFactor(blend.srcColor);
        blendAttachments[i].dstColorBlendFactor = ToVkBlendFactor(blend.dstColor);
        blendAttachments[i].colorBlendOp = ToVkBlendOp(blend.colorOp);
        blendAttachments[i].srcAlphaBlendFactor = ToVkBlendFactor(blend.srcAlpha);
        blendAttachments[i].dstAlphaBlendFactor = ToVkBlendFactor(blend.dstAlpha);
        blendAttachments[i].alphaBlendOp = ToVkBlendOp(blend.alphaOp);
    }

    VkPipelineColorBlendStateCreateInfo colorBlendCI = {};
    colorBlendCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendCI.attachmentCount = desc.renderTargetCount;
    colorBlendCI.pAttachments = blendAttachments;

    // Dynamic state: viewport and scissor
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.dynamicStateCount = 2;
    dynamicStateCI.pDynamicStates = dynamicStates;

    // Dynamic rendering — specify color attachment formats in pNext chain
    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.renderTargetCount);
    for (uint32_t i = 0; i < desc.renderTargetCount; ++i) {
        colorFormats.push_back(ToVkFormat(desc.renderTargetFormats[i]));
    }

    VkPipelineRenderingCreateInfo renderingCI = {};
    renderingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCI.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    renderingCI.pColorAttachmentFormats = colorFormats.data();
    renderingCI.depthAttachmentFormat = (desc.depthStencilFormat != RHIFormat::Unknown)
                                            ? ToVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
    renderingCI.stencilAttachmentFormat = HasStencil(desc.depthStencilFormat)
                                              ? ToVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;

    // Create the pipeline
    VkGraphicsPipelineCreateInfo pipelineCI = {};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.pNext = &renderingCI;
    pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCI.pStages = shaderStages.data();
    pipelineCI.pVertexInputState = &vertexInputCI;
    pipelineCI.pInputAssemblyState = &inputAssemblyCI;
    pipelineCI.pViewportState = &viewportCI;
    pipelineCI.pRasterizationState = &rasterizerCI;
    pipelineCI.pMultisampleState = &msCI;
    pipelineCI.pDepthStencilState = &depthStencilCI;
    pipelineCI.pColorBlendState = &colorBlendCI;
    pipelineCI.pDynamicState = &dynamicStateCI;
    pipelineCI.layout = m_pipelineLayout;
    pipelineCI.renderPass = VK_NULL_HANDLE; // Dynamic rendering — no render pass

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
    if (pipelineResult != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateGraphicsPipelines failed, result={} (name='{}')",
               static_cast<int>(pipelineResult), desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    VulkanPipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        vkDestroyPipeline(m_device, pipeline, nullptr);
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: pool allocation failed");
        return RHIPipelineHandle::Invalid();
    }

    slot->pipeline = pipeline;
    slot->pipelineLayout = m_pipelineLayout;
    slot->topology = desc.topology;
    slot->generation = generation;
    slot->desc = desc;

    UZ_LOG(RHI, Debug, "CreateGraphicsPipeline: {} RT(s) (name='{}')",
           desc.renderTargetCount, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

RHIPipelineHandle VulkanDevice::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // Resolve compute shader module from pool
    VulkanShader* csShader = m_shaderPool.Get(desc.computeShader.index, desc.computeShader.generation);
    if (!csShader || csShader->module == VK_NULL_HANDLE) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: invalid compute shader handle (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    VkPipelineShaderStageCreateInfo stageCI = {};
    stageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageCI.module = csShader->module;
    stageCI.pName = "main";

    VkComputePipelineCreateInfo pipelineCI = {};
    pipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineCI.stage = stageCI;
    pipelineCI.layout = m_pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateComputePipelines failed, result={} (name='{}')",
               static_cast<int>(result), desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    // Debug name
    if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo = {};
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = VK_OBJECT_TYPE_PIPELINE;
        nameInfo.objectHandle = reinterpret_cast<uint64_t>(pipeline);
        nameInfo.pObjectName = desc.debugName;
        vkSetDebugUtilsObjectNameEXT(m_device, &nameInfo);
    }

    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    VulkanPipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        vkDestroyPipeline(m_device, pipeline, nullptr);
        UZ_LOG(RHI, Error, "CreateComputePipeline: pool allocation failed (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIPipelineHandle::Invalid();
    }

    slot->pipeline = pipeline;
    slot->pipelineLayout = m_pipelineLayout;
    slot->isCompute = true;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateComputePipeline: created (name='{}')",
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroyPipeline(RHIPipelineHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanPipeline* pipeline = m_pipelinePool.Get(handle.index, handle.generation);
    if (!pipeline) {
        UZ_LOG(RHI, Warning, "DestroyPipeline: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (pipeline->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, pipeline->pipeline, nullptr);
        pipeline->pipeline = VK_NULL_HANDLE;
    }
    m_pipelinePool.Free(handle.index);
}

std::vector<uint8_t> VulkanDevice::GetPipelineCachedBlob(RHIPipelineHandle /*handle*/) {
    // Vulkan uses VkPipelineCache for disk persistence natively
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Operations
// ─────────────────────────────────────────────────────────────────────────────

RHISamplerHandle VulkanDevice::CreateSampler(const RHISamplerDesc& desc) {
    UZ_PROFILE_FUNCTION();

    VkSamplerCreateInfo samplerCI = {};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    switch (desc.filter) {
        case RHIFilterMode::Point:
            samplerCI.magFilter = VK_FILTER_NEAREST;
            samplerCI.minFilter = VK_FILTER_NEAREST;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case RHIFilterMode::Linear:
            samplerCI.magFilter = VK_FILTER_LINEAR;
            samplerCI.minFilter = VK_FILTER_LINEAR;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        case RHIFilterMode::Anisotropic:
            samplerCI.magFilter = VK_FILTER_LINEAR;
            samplerCI.minFilter = VK_FILTER_LINEAR;
            samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCI.anisotropyEnable = VK_TRUE;
            samplerCI.maxAnisotropy = static_cast<float>(desc.maxAnisotropy);
            break;
    }

    samplerCI.addressModeU = ToVkAddressMode(desc.addressU);
    samplerCI.addressModeV = ToVkAddressMode(desc.addressV);
    samplerCI.addressModeW = ToVkAddressMode(desc.addressW);
    samplerCI.mipLodBias = desc.mipLodBias;
    samplerCI.compareEnable = (desc.compareFunc != RHICompareFunc::Never) ? VK_TRUE : VK_FALSE;
    samplerCI.compareOp = ToVkCompareOp(desc.compareFunc);
    samplerCI.minLod = desc.minLod;
    samplerCI.maxLod = desc.maxLod;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VkSampler sampler = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(m_device, &samplerCI, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateSampler failed, result={}", static_cast<int>(result));
        return RHISamplerHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_samplerPool.Allocate(generation);
    VulkanSampler* slot = m_samplerPool.Get(index, generation);
    if (!slot) {
        vkDestroySampler(m_device, sampler, nullptr);
        UZ_LOG(RHI, Error, "CreateSampler: pool allocation failed");
        return RHISamplerHandle::Invalid();
    }

    slot->sampler = sampler;
    slot->samplerIndex = m_nextSamplerIndex++;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateSampler: index={} (name='{}')",
           slot->samplerIndex, desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void VulkanDevice::DestroySampler(RHISamplerHandle handle) {
    UZ_PROFILE_FUNCTION();
    VulkanSampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) {
        UZ_LOG(RHI, Warning, "DestroySampler: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    if (sampler->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, sampler->sampler, nullptr);
        sampler->sampler = VK_NULL_HANDLE;
    }
    m_samplerPool.Free(handle.index);
}

uint32_t VulkanDevice::GetSamplerIndex(RHISamplerHandle handle) const {
    const VulkanSampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) return UINT32_MAX;
    return sampler->samplerIndex;
}

} // namespace uz
