#pragma once

/// Vulkan backend resource storage types.
///
/// Each resource type holds the native Vulkan objects, VMA allocations, and
/// descriptor indices. Resources are stored in flat arrays indexed by
/// `RHIHandle::index`. The handle's generation counter validates access.
///
/// # Memory Model (VMA Integration)
/// GPU memory is managed through VMA (Vulkan Memory Allocator). VMA handles
/// suballocation from Vulkan memory heaps, reducing the number of
/// `vkAllocateMemory` calls (drivers limit these to ~4096).
///
/// Allocation strategy per resource type:
/// - **Default (GPU-only)**: `VMA_MEMORY_USAGE_AUTO` with `DEVICE_LOCAL` preference.
///   Used for render targets, depth buffers, and GPU-only storage.
/// - **Upload (CPU->GPU)**: `VMA_MEMORY_USAGE_AUTO` with
///   `HOST_ACCESS_SEQUENTIAL_WRITE`. Persistently mapped via
///   `VMA_ALLOCATION_CREATE_MAPPED_BIT`. Used for staging and dynamic buffers.
/// - **Readback (GPU->CPU)**: `VMA_MEMORY_USAGE_AUTO` with
///   `HOST_ACCESS_RANDOM`. Persistently mapped. Used for screenshot/compute readback.
///
/// # Descriptor Binding Model
/// Vulkan uses descriptor sets rather than DX12's flat descriptor heaps.
/// The `srvIndex` / `rtvIndex` / `dsvIndex` fields are **logical indices**
/// used by the RHI abstraction to map `SetDescriptorIndex()` calls to the
/// correct `vkUpdateDescriptorSets()` bindings. The actual descriptor set
/// layout and update strategy are managed by `VulkanDevice`.
///
/// # Handle Generation Validation
/// Same model as DX12/DX11: `ResourcePool<T>` flat arrays with generation
/// counters. See `rhi_resource_pool.h`.
///
/// # Lifetime Guarantees
/// VMA allocations are freed when the resource is destroyed. Callers must
/// ensure no in-flight GPU work references the resource (fence sync).
/// Swapchain images are a special case: `allocation == nullptr` and
/// `ownsImage == false` — the swapchain owns their lifetime.
///
/// # Thread Safety
/// NOT thread-safe. All resource operations must be externally synchronized.
/// VMA itself supports concurrent allocations if created with
/// `VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` cleared, but the
/// ResourcePool and descriptor updates are not thread-safe.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include "../rhi_types.h"
#include "../rhi_resource_pool.h"

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
///
/// # Memory Layout
/// ~48 bytes (VkBuffer handle + VmaAllocation ptr + VkBufferView handle +
/// mapped ptr + uint64 address + 2 uint32 + desc). Contiguous in ResourcePool.
///
/// # Descriptor Model
/// `srvIndex` is a logical binding index used by the RHI to write the correct
/// descriptor set entry. Unlike DX12's global heap offsets, Vulkan descriptors
/// are written into descriptor sets via `vkUpdateDescriptorSets()`. The logical
/// index maps to a `VkDescriptorBufferInfo` binding within the active set.
///
/// # Buffer Device Address (BDA)
/// When `VK_KHR_buffer_device_address` is available, `gpuAddress` caches the
/// 64-bit GPU virtual address from `vkGetBufferDeviceAddress()`. This enables
/// bindless buffer access in shaders via `PhysicalStorageBuffer`. Zero if BDA
/// is unavailable or the buffer was not created with the address usage flag.
struct VulkanBuffer {
    /// Native Vulkan buffer handle. `VK_NULL_HANDLE` for uninitialized slots.
    VkBuffer             buffer = VK_NULL_HANDLE;
    /// VMA allocation managing the buffer's GPU memory. Released on destroy.
    /// For persistently mapped buffers, the mapping is stored in `mappedPtr`.
    VmaAllocation        allocation = nullptr;
    /// Typed buffer view for texel buffer access (`VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER`).
    /// `VK_NULL_HANDLE` for most buffers — only needed for format-interpreted access.
    VkBufferView         bufferView = VK_NULL_HANDLE;
    /// Persistently mapped CPU pointer for Upload/Readback heap buffers.
    /// Obtained from `VmaAllocationInfo::pMappedData` at creation. `nullptr` for
    /// device-local buffers.
    void*                mappedPtr = nullptr;
    /// Cached GPU virtual address from `vkGetBufferDeviceAddress()`.
    /// Zero if BDA is unavailable or not requested for this buffer.
    uint64_t             gpuAddress = 0;
    /// Logical SRV binding index for `SetDescriptorIndex()`. Maps to a descriptor
    /// set binding. `UINT32_MAX` if no descriptor was allocated.
    uint32_t             srvIndex = UINT32_MAX;
    /// Logical UAV binding index for compute storage buffers.
    /// `UINT32_MAX` if no UAV descriptor was allocated.
    uint32_t             uavIndex = UINT32_MAX;
    /// Generation counter — must match `RHIBufferHandle::generation`.
    uint32_t             generation = 0;
    /// Backend-agnostic creation parameters (size, usage flags, heap type).
    RHIBufferDesc        desc = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanTexture — GPU texture with image view and VMA allocation
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan texture resource.
///
/// Managed by `VulkanDevice`'s `ResourcePool<VulkanTexture>`. Tracks the
/// current image layout for barrier transitions.
///
/// # Memory Layout
/// ~56 bytes (VkImage + VmaAllocation + VkImageView + VkImageLayout + 4 uint32 +
/// desc + bool). Contiguous in ResourcePool.
///
/// # Layout Tracking
/// Vulkan requires explicit image layout transitions via pipeline barriers.
/// `currentLayout` tracks the most recent layout so the backend can insert
/// correct `VkImageMemoryBarrier` transitions. Starts as `UNDEFINED` (contents
/// discarded) and transitions to `COLOR_ATTACHMENT_OPTIMAL`, `SHADER_READ_ONLY`,
/// `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`, or `PRESENT_SRC_KHR` as needed.
///
/// # Swapchain Images
/// Swapchain textures have `ownsImage == false` and `allocation == nullptr`.
/// The swapchain owns their `VkImage` lifetime — we only destroy the
/// `VkImageView` on cleanup.
struct VulkanTexture {
    /// Native Vulkan image. For swapchain textures, obtained from
    /// `vkGetSwapchainImagesKHR()` — not owned by us.
    VkImage              image = VK_NULL_HANDLE;
    /// VMA allocation owning GPU memory. `nullptr` for swapchain images.
    VmaAllocation        allocation = nullptr;
    /// Primary image view used for shader sampling, color attachment, or
    /// depth-stencil attachment. Always created (even for swapchain images).
    VkImageView          imageView = VK_NULL_HANDLE;
    /// Last known image layout. Updated after each barrier transition.
    /// Used to determine the `oldLayout` in `VkImageMemoryBarrier`.
    VkImageLayout        currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    /// Logical SRV descriptor index for shader sampling. `UINT32_MAX` if not sampled.
    uint32_t             srvIndex = UINT32_MAX;
    /// Logical RTV descriptor index for color attachment. `UINT32_MAX` if not a render target.
    uint32_t             rtvIndex = UINT32_MAX;
    /// Logical DSV descriptor index for depth-stencil. `UINT32_MAX` if not a depth target.
    uint32_t             dsvIndex = UINT32_MAX;
    /// Generation counter — must match `RHITextureHandle::generation`.
    uint32_t             generation = 0;
    /// Backend-agnostic creation parameters (dimensions, format, usage flags).
    RHITextureDesc       desc = {};
    /// Ownership flag. `false` for swapchain images — prevents `vkDestroyImage()`
    /// and `vmaFreeMemory()` on cleanup (swapchain manages their lifetime).
    bool                 ownsImage = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanShader — Compiled SPIR-V shader module
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a compiled Vulkan shader module.
///
/// Stores both the raw SPIR-V bytecode and the native `VkShaderModule`.
/// The bytecode is retained for pipeline cache serialization and hot-reload
/// (recreating the module after shader recompilation).
///
/// # Memory Layout
/// Variable size due to `bytecode` vector. Fixed fields: ~24 bytes
/// (VkShaderModule + stage + generation). The `VkShaderModule` is a thin
/// driver-side wrapper around the SPIR-V.
///
/// # Lifetime
/// `VkShaderModule` can be destroyed after all pipelines using it are created
/// (the driver copies the bytecode internally). However, we retain both the
/// module and bytecode for hot-reload support.
struct VulkanShader {
    /// Native Vulkan shader module. Created from SPIR-V bytecode via
    /// `vkCreateShaderModule()`. Referenced by `VkPipelineShaderStageCreateInfo`.
    VkShaderModule       module = VK_NULL_HANDLE;
    /// Owned copy of SPIR-V bytecode. Retained for pipeline cache and hot-reload.
    std::vector<uint8_t> bytecode;
    /// Which pipeline stage this shader targets (Vertex, Pixel, Compute, etc.).
    RHIShaderStage       stage = RHIShaderStage::Vertex;
    /// Generation counter — must match `RHIShaderHandle::generation`.
    uint32_t             generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanSampler — Texture sampler state
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan sampler.
///
/// Vulkan samplers are immutable objects created via `vkCreateSampler()`.
/// They are written into descriptor sets for shader binding. Unlike DX12's
/// sampler heap, Vulkan samplers are descriptor-set-bound — the `samplerIndex`
/// is a logical index for the RHI abstraction.
///
/// # Memory Layout
/// ~12 bytes (VkSampler handle + 2 uint32).
struct VulkanSampler {
    /// Native Vulkan sampler. Immutable after creation — encapsulates filter
    /// mode, address mode, anisotropy, LOD clamp, and border color.
    VkSampler            sampler = VK_NULL_HANDLE;
    /// Logical binding index for `SetDescriptorIndex()`. Maps to a
    /// `VK_DESCRIPTOR_TYPE_SAMPLER` or `COMBINED_IMAGE_SAMPLER` binding.
    /// `UINT32_MAX` if not yet assigned.
    uint32_t             samplerIndex = UINT32_MAX;
    /// Generation counter — must match `RHISamplerHandle::generation`.
    uint32_t             generation = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// VulkanPipeline — Graphics pipeline state object
// ─────────────────────────────────────────────────────────────────────────────

/// Internal storage for a Vulkan graphics pipeline.
///
/// Like DX12, Vulkan uses monolithic pipeline state objects that bake all
/// shader and fixed-function state at creation time. `vkCmdBindPipeline()`
/// is a cheap handle swap at draw time.
///
/// # Pipeline Layout
/// `pipelineLayout` is **borrowed** from `VulkanDevice` (shared across all
/// pipelines). It defines the push constant range and descriptor set layouts.
/// The pipeline does NOT own this layout — it must outlive all pipelines.
///
/// # Memory Layout
/// ~24 bytes (2 Vulkan handles + topology enum + generation + desc).
/// The heavy state lives inside the driver-side pipeline object.
///
/// # Lifetime
/// Destroyed via `vkDestroyPipeline()`. Must not be destroyed while GPU work
/// referencing it is in flight.
struct VulkanPipeline {
    /// Native Vulkan pipeline (graphics or compute). For graphics pipelines,
    /// bakes VS, PS, rasterizer, blend, depth-stencil, vertex input,
    /// viewport/scissor (if not dynamic), and render pass compatibility.
    /// For compute pipelines, bakes only the compute shader stage.
    VkPipeline           pipeline = VK_NULL_HANDLE;
    /// Pipeline layout borrowed from `VulkanDevice`. Defines push constant
    /// ranges and descriptor set layouts shared by all pipelines.
    VkPipelineLayout     pipelineLayout = VK_NULL_HANDLE;
    /// Primitive topology for the input assembler. Stored separately because
    /// Vulkan bakes topology into the pipeline, and we need it for validation.
    /// Unused for compute pipelines.
    RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
    /// True if this is a compute pipeline, false for graphics.
    /// Determines which bind point `SetPipeline()` uses
    /// (`VK_PIPELINE_BIND_POINT_COMPUTE` vs `VK_PIPELINE_BIND_POINT_GRAPHICS`).
    bool                 isCompute = false;
    /// Generation counter — must match `RHIPipelineHandle::generation`.
    uint32_t             generation = 0;
    /// Backend-agnostic creation parameters for debugging and hot-reload.
    RHIGraphicsPipelineDesc desc = {};
};

} // namespace uz
