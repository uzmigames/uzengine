# Proposal: Vulkan RHI Backend (Phase 0.2)

## Why

Vulkan is the only cross-platform low-level GPU API (Windows, Linux, Android, macOS via MoltenVK). Without Vulkan, the engine is locked to Windows-only via DirectX. Implementing Vulkan alongside DX12 ensures the RHI abstraction is truly backend-agnostic — if both explicit APIs work through the same interface, the abstraction is proven correct. The dev machine (Windows 10) has the Vulkan SDK installed, so all 3 backends (DX12 + DX11 + Vulkan) can be tested on the same machine.

## What Changes

### New Backend

1. **VulkanDevice** — Implements all 29 `IRHIDevice` methods. VkInstance + VkPhysicalDevice selection + VkDevice creation. VMA (Vulkan Memory Allocator) for memory management. Descriptor pool/set management for bindless (VK_EXT_descriptor_indexing) and slot-based fallback.

2. **VulkanCommandList** — Implements all 28 `IRHICommandList` methods. VkCommandPool per frame-in-flight + VkCommandBuffer recording. Maps RHI barriers to vkCmdPipelineBarrier with VkImageMemoryBarrier / VkBufferMemoryBarrier. Render pass via VkRenderPass + VkFramebuffer (or dynamic rendering VK_KHR_dynamic_rendering where available).

3. **VulkanSwapChain** — Implements all 7 `IRHISwapChain` methods. VkSurfaceKHR + VkSwapchainKHR with mailbox (no-vsync) or FIFO (vsync) present mode. Triple-buffered. Handles VK_ERROR_OUT_OF_DATE_KHR for resize.

4. **VulkanFence** — Implements all 4 `IRHIFence` methods. Uses VkSemaphore (timeline semaphore via VK_KHR_timeline_semaphore) for native GPU timeline fence support — direct mapping of Signal/WaitCPU/IsComplete/GetCompletedValue.

5. **Vulkan Resource Helpers** — VkFormat conversion, VMA buffer/image allocation, VkImageView/VkSampler creation, descriptor set updates.

6. **SPIRV Shader Path** — CreateShader accepts SPIR-V bytecode (compiled externally by DXC with `-spirv` flag or glslc). Shader compilation remains external to the RHI.

### Dependencies

7. **VMA (Vulkan Memory Allocator)** — Add as thirdparty dependency. Handles sub-allocation, defragmentation, memory type selection.

8. **Vulkan SDK Headers** — Use Vulkan SDK installed on the machine. VK_NO_PROTOTYPES + dynamic dispatch via volk or manual function pointers.

### Build System

9. **Backend Selection** — `UZ_BACKEND_VULKAN` compile-time flag. `--rhi vulkan` CLI argument.

## Impact

- Affected specs: `docs/engine/03-rhi/` (multi-backend, Vulkan-specific docs)
- Affected code: `runtime/core/rhi/vulkan/` (new), `runtime/core/rhi/rhi_backend.h` (factory), `runtime/build.zig` (Vulkan files + VMA)
- Breaking change: NO — additive backend
- User benefit: Cross-platform rendering path; validates RHI abstraction; future Linux/Android/macOS support
- Enables: Linux port, Android port, macOS via MoltenVK, Steam Deck support
