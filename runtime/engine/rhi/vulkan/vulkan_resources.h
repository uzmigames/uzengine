#pragma once

/// Vulkan backend resource storage types.
///
/// Each resource type holds the native Vulkan objects, VMA allocations, and
/// descriptor indices. Resources are stored in flat arrays indexed by
/// `RHIHandle::index`. The handle's generation counter validates access.
///
/// # Memory Model
/// GPU memory is managed through VMA (Vulkan Memory Allocator). Upload/Readback
/// buffers are persistently mapped via VMA's persistent mapping support.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include "../rhi_types.h"
#include "../resource_pool.h"

#include <cstdint>
#include <vector>

// Forward declare VMA types to avoid pulling in the full VMA header.
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;
struct VmaAllocationInfo;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VulkanBuffer — GPU buffer with optional views and VMA allocation
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan buffer resource.
///
/// Managed by `VulkanDevice`'s `ResourcePool<VulkanBuffer>`. Not directly
/// visible to application code — accessed only via `RHIBufferHandle`.
struct VulkanBuffer {
    VkBuffer             buffer = VK_NULL_HANDLE;     ///< Native Vulkan buffer
    VmaAllocation        allocation = nullptr;         ///< VMA allocation handle
    VkBufferView         bufferView = VK_NULL_HANDLE;  ///< Buffer view for texel buffers (optional)
    void*                mappedPtr = nullptr;           ///< Persistent CPU mapping (Upload/Readback only)
    uint64_t             gpuAddress = 0;                ///< Cached buffer device address (0 if BDA unavailable)
    uint32_t             srvIndex = UINT32_MAX;        ///< Logical SRV index (for descriptor binding)
    uint32_t             generation = 0;               ///< Matches RHIHandle generation
    RHIBufferDesc        desc = {};                    ///< Creation parameters
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanTexture — GPU texture with image view and VMA allocation
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan texture resource.
///
/// Managed by `VulkanDevice`'s `ResourcePool<VulkanTexture>`. Tracks the
/// current image layout for barrier transitions.
struct VulkanTexture {
    VkImage              image = VK_NULL_HANDLE;       ///< Native Vulkan image
    VmaAllocation        allocation = nullptr;          ///< VMA allocation (nullptr for swapchain images)
    VkImageView          imageView = VK_NULL_HANDLE;   ///< Primary image view (SRV / color / depth)
    VkImageLayout        currentLayout = VK_IMAGE_LAYOUT_UNDEFINED; ///< Tracked layout for barriers
    uint32_t             srvIndex = UINT32_MAX;        ///< Logical SRV descriptor index
    uint32_t             rtvIndex = UINT32_MAX;        ///< Logical RTV descriptor index
    uint32_t             dsvIndex = UINT32_MAX;        ///< Logical DSV descriptor index
    uint32_t             generation = 0;               ///< Matches RHIHandle generation
    RHITextureDesc       desc = {};                    ///< Creation parameters
    bool                 ownsImage = true;             ///< False for swapchain images (don't destroy)
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanShader — Compiled SPIR-V shader module
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled Vulkan shader module.
///
/// Stores both the raw SPIR-V bytecode (needed for pipeline creation)
/// and the native shader module.
struct VulkanShader {
    VkShaderModule       module = VK_NULL_HANDLE;      ///< Native shader module
    std::vector<uint8_t> bytecode;                     ///< Owned copy of SPIR-V bytecode
    RHIShaderStage       stage = RHIShaderStage::Vertex;
    uint32_t             generation = 0;               ///< Matches RHIHandle generation
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanSampler — Texture sampler state
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan sampler.
struct VulkanSampler {
    VkSampler            sampler = VK_NULL_HANDLE;     ///< Native sampler
    uint32_t             samplerIndex = UINT32_MAX;    ///< Logical index for binding
    uint32_t             generation = 0;               ///< Matches RHIHandle generation
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanPipeline — Graphics pipeline state object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan graphics pipeline.
///
/// Unlike DX11, Vulkan has monolithic pipeline state objects. The pipeline
/// layout is shared across all pipelines (single push constant range).
struct VulkanPipeline {
    VkPipeline           pipeline = VK_NULL_HANDLE;    ///< Native graphics pipeline
    VkPipelineLayout     pipelineLayout = VK_NULL_HANDLE; ///< Pipeline layout (borrowed from device)
    RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
    uint32_t             generation = 0;               ///< Matches RHIHandle generation
    RHIGraphicsPipelineDesc desc = {};                 ///< Creation parameters
};

} // namespace uz
