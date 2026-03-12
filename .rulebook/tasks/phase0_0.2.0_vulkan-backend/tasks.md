## 1. Implementation

### Dependencies & Setup
- [x] 1.1 Add VMA (Vulkan Memory Allocator) as thirdparty dependency
- [x] 1.2 Add volk (Vulkan function loader) for VK_NO_PROTOTYPES dynamic dispatch
- [x] 1.3 Create `runtime/engine/rhi/vulkan/` directory structure
- [x] 1.4 Add Vulkan backend sources and include paths to build.zig + all 5 demo build.zig files

### VulkanDevice (IRHIDevice — 29 methods)
- [x] 1.5 Implement VkInstance creation (validation layers in debug with availability check, required extensions)
- [x] 1.6 Implement VkPhysicalDevice selection (prefer discrete GPU, query properties/features)
- [x] 1.7 Implement VkDevice creation (graphics queue, required extensions incl. dynamic_rendering, synchronization2, timeline_semaphore, buffer_device_address)
- [x] 1.8 Implement VMA allocator initialization
- [x] 1.9 Implement descriptor pool/set layout (bindings 0-15 SAMPLED_IMAGE, 16-31 SAMPLER, 32-47 UNIFORM_BUFFER) + pipeline layout with 128B push constants
- [x] 1.10 Implement buffer operations: CreateBuffer (VMA alloc + gpuAddress cache), DestroyBuffer, MapBuffer, GetBufferSRVIndex, GetBufferGPUAddress
- [x] 1.11 Implement texture operations: CreateTexture (VMA alloc + VkImageView), DestroyTexture, GetTextureSRVIndex, GetTextureDesc
- [x] 1.12 Implement shader operations: CreateShader (from SPIR-V bytecode + VkShaderModule), DestroyShader
- [x] 1.13 Implement pipeline operations: CreateGraphicsPipeline (VkPipeline + VkPipelineLayout), DestroyPipeline
- [x] 1.14 Implement sampler operations: CreateSampler (VkSampler), DestroySampler, GetSamplerIndex
- [x] 1.15 Implement swapchain/fence/command list creation and destruction
- [x] 1.16 Implement WaitIdle (vkDeviceWaitIdle), GetAdapterName, GetFeatureLevel, GetMemoryBudget

### VulkanCommandList (IRHICommandList — 28 methods)
- [x] 1.17 Implement VkCommandPool per frame-in-flight (triple-buffered) + VkCommandBuffer allocation
- [x] 1.18 Implement Begin (reset pool + begin buffer), End (vkEndCommandBuffer), Execute (vkQueueSubmit with timeline semaphore signal)
- [x] 1.19 Implement SetPipeline, SetVertexBuffer (vkCmdBindVertexBuffers), SetIndexBuffer (vkCmdBindIndexBuffer)
- [x] 1.20 Implement SetViewport (negative height Y-flip to match DX12 NDC), SetScissor
- [x] 1.21 Implement descriptor binding: SetDescriptorIndex → push constants, SetRootConstants → push constants, SetRootCBV → uniform buffer descriptor write via FindBufferByGPUAddress, SetDescriptorTable → texture+sampler descriptor write + FlushDescriptorSet
- [x] 1.22 Implement DrawIndexed (vkCmdDrawIndexed), Draw (vkCmdDraw), Dispatch (vkCmdDispatch)
- [x] 1.23 Implement ResourceBarrier → vkCmdPipelineBarrier2 (VK_KHR_synchronization2)
- [x] 1.24 Implement ClearRenderTarget, ClearDepthStencil
- [x] 1.25 Implement CopyBuffer, CopyTexture, CopyBufferToTexture
- [x] 1.26 Implement BeginRenderPass / EndRenderPass via VK_KHR_dynamic_rendering (vkCmdBeginRendering/vkCmdEndRendering)

### VulkanSwapChain (IRHISwapChain — 7 methods)
- [x] 1.27 Implement VkSurfaceKHR creation (VK_KHR_win32_surface)
- [x] 1.28 Implement VkSwapchainKHR: MAILBOX present mode, triple-buffered, image acquisition
- [x] 1.29 Implement Present (vkQueuePresentKHR), Resize (recreate swapchain)
- [x] 1.30 Implement GetWidth/Height/Format, GetCurrentBackBuffer/Index

### VulkanFence (IRHIFence — 4 methods)
- [x] 1.31 Implement VulkanFence using VK_KHR_timeline_semaphore
- [x] 1.32 Implement Signal (vkQueueSubmit with timeline signal), WaitCPU (vkWaitSemaphores), IsComplete, GetCompletedValue

### Integration
- [x] 1.33 Register Vulkan backend in rhi_factory.cpp + GetActiveRHIBackend() global accessor
- [x] 1.34 Add `--rhi vulkan` CLI flag (already supported by existing RHIBackend enum)
- [x] 1.35 Verify all 5 existing demos work with Vulkan backend (clear_screen, triangle 5187 FPS, textured_quad 5334 FPS, camera_test 332 FPS, mesh_test 331 FPS on RTX 4090)
- [x] 1.36 Add automatic HLSL→SPIR-V compilation in ShaderPipeline (DXC -spirv with -fvk-*-shift binding remaps, zero demo code changes)

## 2. Testing

- [x] 2.1 Port test_dx12_device.cpp → test_vulkan_device.cpp (20 tests, all passing)
- [x] 2.2 Port test_dx12_render_loop.cpp → test_vulkan_render_loop.cpp (3 tests, all passing)
- [x] 2.3 Port test_dx12_resources.cpp → test_vulkan_resources.cpp (12 tests: 5 struct defaults + 7 VulkanResourcePool, all passing)
- [x] 2.4 Test Vulkan-specific: VkInstance/VkDevice creation, validation layers, VMA allocation (covered by VulkanDeviceTest fixture)
- [x] 2.5 Test timeline semaphore fence: Signal/WaitCPU/IsComplete/GetCompletedValue (FenceSignalAndWait + FenceCreateAndDestroy)
- [ ] 2.6 Test Vulkan barriers: correct VkImageLayout transitions, pipeline stage flags — deferred Phase 1
- [ ] 2.7 Test swapchain recreation on resize (VK_ERROR_OUT_OF_DATE handling) — deferred Phase 1
- [ ] 2.8 Test format conversion: all 22 RHIFormat → VkFormat mappings — deferred Phase 1
- [ ] 2.9 Cross-backend comparison: DX12 vs Vulkan produce equivalent rendering output — deferred Phase 1
- [ ] 2.10 Verify 95%+ coverage on all new Vulkan code — deferred Phase 1

## 3. Documentation

- [ ] 3.1 Add `docs/engine/03-rhi/vulkan-backend.md` documenting Vulkan-specific behaviors (dynamic rendering, timeline semaphores, descriptor layout)
- [ ] 3.2 Update `docs/engine/03-rhi/01-overview.md` with Vulkan backend details
- [ ] 3.3 Add SPIR-V compilation guide (DXC -spirv flag, -fvk-*-shift binding remaps, automatic detection via GetActiveRHIBackend)
