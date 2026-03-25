#pragma once

/// Vulkan backend implementation of `IRHIDevice`.
///
/// Owns the Vulkan device stack: instance, physical device, logical device,
/// VMA allocator, descriptor pool, and resource pools. Uses volk for dynamic
/// function loading and VMA for GPU memory management.
///
/// # Initialization Order
/// 1. volkInitialize()  2. VkInstance creation (+ validation layer)
/// 3. Debug messenger  4. Physical device selection (prefer discrete)
/// 5. Logical device creation (graphics + transfer queues)
/// 6. volkLoadDevice()  7. VMA allocator  8. Descriptor pool  9. Pipeline layout
///
/// # Thread Safety
/// Resource creation/destruction methods are NOT internally synchronized.
/// Callers must serialize access from multiple threads.
///
/// # Lifetime
/// Created via `CreateRHIDevice()` which returns `std::unique_ptr<IRHIDevice>`.
/// The destructor flushes the GPU and releases all resources.

#include "../rhi.h"
#include "../rhi_backend.h"
#include "../rhi_config.h"
#include "vulkan_resources.h"

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

// Forward-declare Tracy Vulkan context type to avoid pulling TracyVulkan.hpp
// into every translation unit that includes vulkan_device.h.
// When TRACY_ENABLE is defined, TracyVkCtx is tracy::VkCtx*.
// When TRACY_ENABLE is not defined, TracyVkCtx is void*.
#if defined(TRACY_ENABLE)
namespace tracy { class VkCtx; }
using TracyVkCtx = tracy::VkCtx*;
#else
using TracyVkCtx = void*;
#endif

// Forward declare VMA allocator to avoid header inclusion
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VulkanDevice
// ─────────────────────────────────────────────────────────────────────────────

/// Vulkan implementation of `IRHIDevice`.
///
/// Initializes the Vulkan 1.2+ device stack with dynamic rendering,
/// timeline semaphores, and VMA memory management. Push constants are
/// used for root constant / descriptor index binding (128 bytes).
class VulkanDevice final : public IRHIDevice {
    friend class VulkanSwapChain;
    friend class VulkanCommandList;
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;

    /// Initializes the Vulkan device stack.
    ///
    /// # Arguments
    /// * `desc` — Device descriptor (backend, validation flags, adapter index).
    ///
    /// # Returns
    /// `true` on success, `false` on failure (logs detailed error via `UZ_LOG`).
    bool Init(const RHIDeviceDesc& desc);

    // ── IRHIDevice: Buffer operations ────────────────────────────────────

    /// Creates a GPU buffer on the Vulkan device via VMA. See `IRHIDevice::CreateBuffer`.
    RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) override;

    /// Destroys a Vulkan buffer, releasing its VMA allocation.
    void DestroyBuffer(RHIBufferHandle handle) override;

    /// Returns a persistently mapped CPU pointer for Vulkan Upload/Readback buffers.
    void* MapBuffer(RHIBufferHandle handle) override;

    /// No-op on Vulkan — buffers are persistently mapped via VMA. See `IRHIDevice::UnmapBuffer`.
    void UnmapBuffer(RHIBufferHandle handle) override;

    /// Returns the logical SRV index assigned at buffer creation time.
    uint32_t GetBufferSRVIndex(RHIBufferHandle handle) const override;

    /// Returns the logical UAV index assigned at buffer creation time.
    uint32_t GetBufferUAVIndex(RHIBufferHandle handle) const override;

    /// Returns the Vulkan buffer device address (via `vkGetBufferDeviceAddress`).
    uint64_t GetBufferGPUAddress(RHIBufferHandle handle) const override;

    // ── IRHIDevice: Texture operations ───────────────────────────────────

    /// Creates a GPU texture on the Vulkan device via VMA. See `IRHIDevice::CreateTexture`.
    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override;

    /// Destroys a Vulkan texture, releasing its VMA allocation and image views.
    void DestroyTexture(RHITextureHandle handle) override;

    /// Returns the logical SRV index assigned at texture creation time.
    uint32_t GetTextureSRVIndex(RHITextureHandle handle) const override;

    /// Returns the logical UAV index assigned at texture creation time.
    uint32_t GetTextureUAVIndex(RHITextureHandle handle) const override;

    /// Returns the creation descriptor for a Vulkan texture. See `IRHIDevice::GetTextureDesc`.
    RHITextureDesc GetTextureDesc(RHITextureHandle handle) const override;

    // ── IRHIDevice: Shader operations ────────────────────────────────────

    /// Creates a shader module from SPIR-V bytecode. See `IRHIDevice::CreateShader`.
    RHIShaderHandle CreateShader(const RHIShaderDesc& desc) override;

    /// Destroys a Vulkan shader module. See `IRHIDevice::DestroyShader`.
    void DestroyShader(RHIShaderHandle handle) override;

    // ── IRHIDevice: Pipeline operations ──────────────────────────────────

    /// Creates a Vulkan graphics pipeline via `vkCreateGraphicsPipelines`.
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;

    /// Creates a Vulkan compute pipeline via `vkCreateComputePipelines`.
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc& desc) override;

    /// Destroys a Vulkan pipeline object. See `IRHIDevice::DestroyPipeline`.
    void DestroyPipeline(RHIPipelineHandle handle) override;

    /// Returns empty — Vulkan uses `VkPipelineCache` for caching natively.
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle handle) override;

    // ── IRHIDevice: Sampler operations ───────────────────────────────────

    /// Creates a Vulkan sampler via `vkCreateSampler`. See `IRHIDevice::CreateSampler`.
    RHISamplerHandle CreateSampler(const RHISamplerDesc& desc) override;

    /// Destroys a Vulkan sampler. See `IRHIDevice::DestroySampler`.
    void DestroySampler(RHISamplerHandle handle) override;

    /// Returns the logical sampler index assigned at creation time.
    uint32_t GetSamplerIndex(RHISamplerHandle handle) const override;

    // ── IRHIDevice: Swap chain operations ────────────────────────────────

    /// Creates a Vulkan swap chain (VkSurfaceKHR + VkSwapchainKHR).
    IRHISwapChain* CreateSwapChain(const RHISwapChainDesc& desc) override;

    /// Destroys a Vulkan swap chain. See `IRHIDevice::DestroySwapChain`.
    void DestroySwapChain(IRHISwapChain* swapChain) override;

    // ── IRHIDevice: Fence operations ─────────────────────────────────────

    /// Creates a Vulkan fence using timeline semaphores (`VK_SEMAPHORE_TYPE_TIMELINE`).
    IRHIFence* CreateFence() override;

    /// Destroys a Vulkan timeline semaphore fence. See `IRHIDevice::DestroyFence`.
    void DestroyFence(IRHIFence* fence) override;

    // ── IRHIDevice: Command list operations ──────────────────────────────

    /// Creates a Vulkan command list (command pool + command buffer pair).
    IRHICommandList* CreateCommandList(RHIQueueType queue) override;

    /// Destroys a Vulkan command list and its command pool. See `IRHIDevice::DestroyCommandList`.
    void DestroyCommandList(IRHICommandList* cmdList) override;

    // ── IRHIDevice: Synchronization ──────────────────────────────────────

    /// Calls `vkDeviceWaitIdle` to block until all GPU queues are idle.
    void WaitIdle() override;

    // ── IRHIDevice: Copy queue (async uploads) ───────────────────────────

    /// Signals the copy queue fence, returning the signaled value.
    /// Stub — logs a warning and returns 0 until Vulkan transfer queue is fully wired.
    uint64_t SignalCopyQueue() override;

    /// Makes the graphics queue wait for the copy queue fence to reach `value`.
    /// Stub — logs a warning until Vulkan transfer queue is fully wired.
    void GraphicsWaitForCopy(uint64_t value) override;

    // ── IRHIDevice: Compute queue (async compute) ────────────────────────

    /// Signals the compute queue fence, returning the signaled value.
    /// Stub — logs a warning and returns 0 until Vulkan compute queue is fully wired.
    uint64_t SignalComputeQueue() override;

    /// Makes the graphics queue wait for the compute queue fence to reach `value`.
    /// Stub — logs a warning until Vulkan compute queue is fully wired.
    void GraphicsWaitForCompute(uint64_t value) override;

    /// Makes the compute queue wait for the graphics queue fence to reach `value`.
    /// Stub — logs a warning until Vulkan compute queue is fully wired.
    void ComputeWaitForGraphics(uint64_t value) override;

    // ── IRHIDevice: Device queries ───────────────────────────────────────

    /// Returns the Vulkan physical device name (e.g., "NVIDIA GeForce RTX 4090").
    const char* GetAdapterName() const override;

    /// Returns the Vulkan API version string (e.g., "Vulkan 1.3.280").
    const char* GetFeatureLevel() const override;

    /// Queries GPU memory via `VK_EXT_memory_budget` and returns budget/usage.
    RHIMemoryBudget GetMemoryBudget() const override;

    /// Returns the Vulkan shader model (default SM6_6). See `IRHIDevice::GetShaderModel`.
    ShaderModel GetShaderModel() const override { return m_shaderModel; }

    /// Returns `RHIBackend::Vulkan`. See `IRHIDevice::GetBackend`.
    RHIBackend GetBackend() const override { return RHIBackend::Vulkan; }

    // ── Tracy GPU Profiling ─────────────────────────────────────────────

    /// Returns the Tracy Vulkan GPU context for timestamp collection.
    ///
    /// When `TRACY_ENABLE` is not defined, `TracyVkCtx` is `void*` and this
    /// returns `nullptr`. Used by `VulkanCommandList::End()` and
    /// `VulkanSwapChain::Present()` to collect GPU timestamps each frame.
    TracyVkCtx GetTracyGpuContext() const { return m_tracyVkCtx; }

    // ── Accessors for sibling Vulkan classes ─────────────────────────────

    /// Returns the Vulkan logical device.
    VkDevice GetVkDevice() const { return m_device; }

    /// Returns the Vulkan instance.
    VkInstance GetVkInstance() const { return m_instance; }

    /// Returns the Vulkan physical device.
    VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }

    /// Returns the graphics queue.
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }

    /// Returns the graphics queue family index.
    uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }

    /// Returns the transfer queue (may alias graphics queue if no dedicated one found).
    VkQueue GetTransferQueue() const { return m_transferQueue; }

    /// Returns the transfer queue family index.
    uint32_t GetTransferQueueFamily() const { return m_transferQueueFamily; }

    /// Returns the compute queue (VK_NULL_HANDLE if no dedicated compute queue found).
    VkQueue GetComputeQueue() const { return m_computeQueue; }

    /// Returns the compute queue family index (UINT32_MAX if not found).
    uint32_t GetComputeQueueFamily() const { return m_computeQueueFamily; }

    /// Returns the VMA allocator.
    VmaAllocator GetAllocator() const { return m_allocator; }

    /// Returns the shared pipeline layout (push constants + descriptor set 0).
    VkPipelineLayout GetPipelineLayout() const { return m_pipelineLayout; }

    /// Returns the descriptor set layout for set 0 (textures + samplers).
    VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_descriptorSetLayout; }

    /// Returns the global descriptor pool.
    VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }

    /// Finds a texture by its logical SRV index.
    ///
    /// Iterates the texture pool looking for a slot whose `srvIndex` matches.
    /// Returns nullptr if no texture has that SRV index.
    VulkanTexture* FindTextureBySrvIndex(uint32_t srvIndex);

    /// Finds a texture by pool index for UAV binding.
    ///
    /// GetTextureUAVIndex returns pool index as sentinel; this resolves it
    /// to the VulkanTexture for writing STORAGE_IMAGE descriptors.
    /// Returns nullptr if the pool index is invalid or has no image view.
    VulkanTexture* FindTextureByPoolIndex(uint32_t poolIndex);

    /// Finds a sampler by its logical sampler index.
    ///
    /// Iterates the sampler pool looking for a slot whose `samplerIndex` matches.
    /// Returns nullptr if no sampler has that index.
    VulkanSampler* FindSamplerByIndex(uint32_t samplerIndex);

    /// Returns the buffer resource pool (for command list lookups).
    ResourcePool<VulkanBuffer>& GetBufferPool() { return m_bufferPool; }

    /// Returns the texture resource pool (for command list lookups).
    ResourcePool<VulkanTexture>& GetTexturePool() { return m_texturePool; }

    /// Returns the pipeline resource pool (for command list PSO binding).
    ResourcePool<VulkanPipeline>& GetPipelinePool() { return m_pipelinePool; }

    /// Returns the shader resource pool.
    ResourcePool<VulkanShader>& GetShaderPool() { return m_shaderPool; }

    /// Returns the sampler resource pool.
    ResourcePool<VulkanSampler>& GetSamplerPool() { return m_samplerPool; }

    /// Result of a buffer device address lookup.
    struct BufferAddressLookup {
        VkBuffer buffer = VK_NULL_HANDLE;  ///< The VkBuffer containing the address
        uint64_t offset = 0;               ///< Byte offset from buffer start
        uint64_t size = 0;                 ///< Total buffer size in bytes
    };

    /// Finds the VulkanBuffer whose GPU address range contains `gpuAddress`.
    ///
    /// Iterates the buffer pool checking cached device addresses. Used by
    /// `SetRootCBV` to resolve a GPU virtual address into a VkBuffer + offset
    /// pair for descriptor set writes.
    ///
    /// # Returns
    /// A `BufferAddressLookup` with the matching buffer and offset, or a
    /// default-constructed result (buffer == VK_NULL_HANDLE) if no match.
    BufferAddressLookup FindBufferByGPUAddress(uint64_t gpuAddress) const;

    /// Returns the root signature layout for translating root param indices
    /// to HLSL register numbers (matching DX12 root signature).
    const RHIRootSignatureDesc& GetRootLayout() const { return m_rootLayout; }

private:
    bool CreateInstance(bool enableValidation);
    bool SetupDebugMessenger();
    bool SelectPhysicalDevice(uint32_t adapterIndex);
    bool CreateLogicalDevice();
    bool CreateAllocator();
    bool CreateDescriptorPool();
    bool CreateDescriptorSetLayout();
    bool CreatePipelineLayout();

    // ── State ────────────────────────────────────────────────────────────

    VkInstance                     m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT       m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice               m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                       m_device = VK_NULL_HANDLE;
    VkQueue                        m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue                        m_transferQueue = VK_NULL_HANDLE;
    VkQueue                        m_computeQueue = VK_NULL_HANDLE;
    uint32_t                       m_graphicsQueueFamily = UINT32_MAX;
    uint32_t                       m_transferQueueFamily = UINT32_MAX;
    uint32_t                       m_computeQueueFamily = UINT32_MAX;
    VmaAllocator                   m_allocator = nullptr;
    VkDescriptorPool               m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout          m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout               m_pipelineLayout = VK_NULL_HANDLE;

    // ── Resource Pools ──────────────────────────────────────────────────
    ResourcePool<VulkanBuffer>     m_bufferPool;
    ResourcePool<VulkanTexture>    m_texturePool;
    ResourcePool<VulkanShader>     m_shaderPool;
    ResourcePool<VulkanSampler>    m_samplerPool;
    ResourcePool<VulkanPipeline>   m_pipelinePool;

    /// Monotonically increasing SRV index counter for logical descriptor binding.
    uint32_t                       m_nextSrvIndex = 0;

    /// Monotonically increasing sampler index counter.
    uint32_t                       m_nextSamplerIndex = 0;

    /// Root signature layout mirroring DX12 root parameter → HLSL register mapping.
    /// Built during Init() so Vulkan command list can translate rootParamIndex to
    /// the correct HLSL register for descriptor binding (with -fvk-b-shift 32 0).
    RHIRootSignatureDesc           m_rootLayout;

    // ── Swapchain presentation synchronization ──────────────────────────
    // Set by VulkanSwapChain::GetCurrentBackBuffer() when acquiring a swapchain
    // image, consumed by VulkanCommandList::Execute() to wire semaphore wait/signal
    // into the queue submission. This keeps the abstract IRHICommandList interface
    // unchanged while enabling correct Vulkan presentation sync.

    /// Semaphore signaled by vkAcquireNextImageKHR — the next queue submit must
    /// wait on this at COLOR_ATTACHMENT_OUTPUT before writing to the back buffer.
    VkSemaphore                    m_pendingImageAvailableSem = VK_NULL_HANDLE;

    /// Semaphore to signal when rendering completes — Present() waits on this.
    VkSemaphore                    m_pendingRenderFinishedSem = VK_NULL_HANDLE;

    /// Static samplers matching DX12 root signature static samplers.
    /// s0 (binding 16): linear wrap — general texturing.
    /// s1 (binding 17): comparison LESS, border black — shadow PCF.
    VkSampler                      m_staticSamplerLinear = VK_NULL_HANDLE;
    VkSampler                      m_staticSamplerShadow = VK_NULL_HANDLE;

    // ── Async queue synchronization (timeline semaphores) ──────────────
    VkSemaphore                    m_copyTimelineSemaphore = VK_NULL_HANDLE;
    VkSemaphore                    m_computeTimelineSemaphore = VK_NULL_HANDLE;
    VkSemaphore                    m_graphicsTimelineSemaphore = VK_NULL_HANDLE;
    uint64_t                       m_copyFenceValue = 0;
    uint64_t                       m_computeFenceValue = 0;
    uint64_t                       m_graphicsFenceValue = 0;

    ShaderModel                    m_shaderModel = ShaderModel::SM6_6;
    std::string                    m_adapterName;
    std::string                    m_apiVersionStr;
    bool                           m_validationEnabled = false;
    bool                           m_bindlessSupported = false; ///< True if descriptor indexing + runtime arrays available.
    bool                           m_memoryBudgetSupported = false; ///< True if VK_EXT_memory_budget is available.

    /// Tracy GPU context for Vulkan timestamp collection.
    /// nullptr when `TRACY_ENABLE` is not defined.
    TracyVkCtx                     m_tracyVkCtx = nullptr;
};

} // namespace uz
