## 1. Implementation

### Dependencies & Setup
- [ ] 1.1 Add VMA (Vulkan Memory Allocator) as thirdparty dependency
- [ ] 1.2 Add volk (Vulkan function loader) or manual dynamic dispatch for VK_NO_PROTOTYPES
- [ ] 1.3 Create `runtime/core/rhi/vulkan/` directory structure
- [ ] 1.4 Add `UZ_BACKEND_VULKAN` compile flag to build.zig, link Vulkan SDK, add source files

### VulkanDevice (IRHIDevice — 29 methods)
- [ ] 1.5 Implement VkInstance creation (validation layers in debug, required extensions)
- [ ] 1.6 Implement VkPhysicalDevice selection (prefer discrete GPU, query properties/features)
- [ ] 1.7 Implement VkDevice creation (graphics + compute + transfer queues, required extensions)
- [ ] 1.8 Implement VMA allocator initialization
- [ ] 1.9 Implement descriptor pool/set management (VK_EXT_descriptor_indexing for bindless)
- [ ] 1.10 Implement buffer operations: CreateBuffer (VMA alloc + VkBufferView), DestroyBuffer, MapBuffer, GetBufferSRVIndex, GetBufferGPUAddress
- [ ] 1.11 Implement texture operations: CreateTexture (VMA alloc + VkImageView), DestroyTexture, GetTextureSRVIndex, GetTextureDesc
- [ ] 1.12 Implement shader operations: CreateShader (from SPIR-V bytecode + VkShaderModule), DestroyShader
- [ ] 1.13 Implement pipeline operations: CreateGraphicsPipeline (VkPipeline + VkPipelineLayout), DestroyPipeline
- [ ] 1.14 Implement sampler operations: CreateSampler (VkSampler), DestroySampler, GetSamplerIndex
- [ ] 1.15 Implement swapchain/fence/command list creation and destruction
- [ ] 1.16 Implement WaitIdle (vkDeviceWaitIdle), GetAdapterName, GetFeatureLevel, GetMemoryBudget (VMA stats)

### VulkanCommandList (IRHICommandList — 28 methods)
- [ ] 1.17 Implement VkCommandPool per frame-in-flight + VkCommandBuffer allocation
- [ ] 1.18 Implement Begin (reset pool + begin buffer), End (vkEndCommandBuffer), Execute (vkQueueSubmit)
- [ ] 1.19 Implement SetPipeline, SetVertexBuffer (vkCmdBindVertexBuffers), SetIndexBuffer (vkCmdBindIndexBuffer)
- [ ] 1.20 Implement SetViewport (vkCmdSetViewport), SetScissor (vkCmdSetScissor)
- [ ] 1.21 Implement descriptor binding: SetDescriptorIndex → descriptor set update, SetRootConstants → push constants, SetRootCBV, SetDescriptorTable
- [ ] 1.22 Implement DrawIndexed (vkCmdDrawIndexed), Draw (vkCmdDraw), Dispatch (vkCmdDispatch)
- [ ] 1.23 Implement ResourceBarrier → vkCmdPipelineBarrier (VkImageMemoryBarrier / VkBufferMemoryBarrier)
- [ ] 1.24 Implement ClearRenderTarget, ClearDepthStencil (vkCmdClearColorImage / vkCmdClearDepthStencilImage)
- [ ] 1.25 Implement CopyBuffer, CopyTexture, CopyBufferToTexture (vkCmdCopyBuffer, vkCmdCopyImage, vkCmdCopyBufferToImage)
- [ ] 1.26 Implement BeginRenderPass (VkRenderPass + VkFramebuffer or VK_KHR_dynamic_rendering), EndRenderPass

### VulkanSwapChain (IRHISwapChain — 7 methods)
- [ ] 1.27 Implement VkSurfaceKHR creation (Win32: VK_KHR_win32_surface)
- [ ] 1.28 Implement VkSwapchainKHR: mailbox or FIFO present mode, triple-buffered, image acquisition
- [ ] 1.29 Implement Present (vkQueuePresentKHR), Resize (recreate swapchain on VK_ERROR_OUT_OF_DATE_KHR)
- [ ] 1.30 Implement GetWidth/Height/Format, GetCurrentBackBuffer/Index

### VulkanFence (IRHIFence — 4 methods)
- [ ] 1.31 Implement VulkanFence using VK_KHR_timeline_semaphore
- [ ] 1.32 Implement Signal (vkSignalSemaphore), WaitCPU (vkWaitSemaphores), IsComplete, GetCompletedValue

### Integration
- [ ] 1.33 Register Vulkan backend in rhi_backend.h factory
- [ ] 1.34 Add `--rhi vulkan` CLI flag
- [ ] 1.35 Verify all 5 existing demos work with Vulkan backend
- [ ] 1.36 Add SPIR-V shader compilation path to shader pipeline (DXC with -spirv flag)

## 2. Testing

- [ ] 2.1 Port test_dx12_device.cpp → test_vulkan_device.cpp (all 20 tests adapted for Vulkan)
- [ ] 2.2 Port test_dx12_render_loop.cpp → test_vulkan_render_loop.cpp (3 tests)
- [ ] 2.3 Port test_dx12_resources.cpp → test_vulkan_resources.cpp (12 tests)
- [ ] 2.4 Test Vulkan-specific: VkInstance/VkDevice creation, validation layers, VMA allocation
- [ ] 2.5 Test timeline semaphore fence: Signal/WaitCPU/IsComplete/GetCompletedValue
- [ ] 2.6 Test Vulkan barriers: correct VkImageLayout transitions, pipeline stage flags
- [ ] 2.7 Test swapchain recreation on resize (VK_ERROR_OUT_OF_DATE handling)
- [ ] 2.8 Test format conversion: all 22 RHIFormat → VkFormat mappings
- [ ] 2.9 Cross-backend comparison: DX12 vs Vulkan produce equivalent rendering output
- [ ] 2.10 Verify 95%+ coverage on all new Vulkan code

## 3. Documentation

- [ ] 3.1 Add `docs/engine/03-rhi/vulkan-backend.md` documenting Vulkan-specific behaviors (explicit barriers, timeline semaphores, descriptor indexing)
- [ ] 3.2 Update `docs/engine/03-rhi/01-overview.md` with Vulkan backend details
- [ ] 3.3 Add SPIR-V compilation guide (DXC -spirv flag, shader model mapping)
