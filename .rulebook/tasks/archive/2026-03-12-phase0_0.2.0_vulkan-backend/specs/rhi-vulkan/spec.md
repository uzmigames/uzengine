# Vulkan RHI Backend — Spec Delta

## ADDED Requirements

### Requirement: Vulkan Full Interface Parity
The Vulkan backend SHALL implement all 4 RHI interfaces with complete method coverage: 29 Device, 28 CommandList, 7 SwapChain, 4 Fence methods. Every method MUST produce functionally equivalent results to the DX12 backend.

#### Scenario: All interfaces implemented
Given the Vulkan backend is compiled
When each RHI interface method is called
Then no method throws "not implemented" and all produce correct GPU behavior

### Requirement: Vulkan Instance and Device Creation
The VulkanDevice SHALL create a VkInstance with validation layers (VK_LAYER_KHRONOS_validation) in debug builds. It MUST select a VkPhysicalDevice preferring discrete GPUs. VkDevice MUST be created with graphics, compute, and transfer queues.

#### Scenario: Device created with Vulkan SDK
Given a Windows 10 machine with Vulkan SDK installed
When CreateRHIDevice is called with Vulkan backend
Then VkInstance and VkDevice are created successfully
And GetAdapterName returns the GPU name from VkPhysicalDeviceProperties

#### Scenario: Validation layers in debug
Given debug build with validation enabled
When VkInstance is created
Then VK_LAYER_KHRONOS_validation is active
And API errors produce debug messenger callbacks

### Requirement: VMA Memory Management
The Vulkan backend MUST use Vulkan Memory Allocator (VMA) for all GPU memory allocation. Buffer and texture creation SHALL use VMA for sub-allocation, memory type selection (device-local, host-visible), and defragmentation support.

#### Scenario: Buffer allocated via VMA
Given a request to create a 1MB vertex buffer
When CreateBuffer is called
Then VMA allocates device-local memory
And MapBuffer returns nullptr for Default memory type
And MapBuffer returns a valid pointer for Upload memory type

### Requirement: Vulkan Explicit Barriers
The Vulkan CommandList SHALL map ResourceBarrier to vkCmdPipelineBarrier with correct VkImageMemoryBarrier (layout transitions) and VkBufferMemoryBarrier (access flag transitions). RHIResourceState MUST map to appropriate VkImageLayout and VkAccessFlags.

#### Scenario: Texture barrier transitions layout
Given a texture in ShaderResource state
When ResourceBarrier transitions it to RenderTarget
Then vkCmdPipelineBarrier is issued with oldLayout=SHADER_READ_ONLY_OPTIMAL and newLayout=COLOR_ATTACHMENT_OPTIMAL

#### Scenario: Present barrier
Given a swapchain image in RenderTarget state
When ResourceBarrier transitions it to Present
Then the image layout transitions to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR

### Requirement: Vulkan Timeline Semaphore Fence
The VulkanFence SHALL use VK_KHR_timeline_semaphore for native GPU timeline fence support. Signal MUST use vkSignalSemaphore. WaitCPU MUST use vkWaitSemaphores. IsComplete MUST query the semaphore value without blocking.

#### Scenario: Timeline fence synchronization
Given a Vulkan fence initialized to 0
When Signal(5) is called after GPU work submission
And WaitCPU(5) is called
Then it blocks until the GPU reaches timeline value 5
And GetCompletedValue returns 5

### Requirement: Vulkan Descriptor Indexing (Bindless)
The Vulkan backend MUST support VK_EXT_descriptor_indexing for bindless resource access where available. SetDescriptorIndex SHALL update the descriptor set with the specified resource index. When descriptor indexing is unavailable, the backend SHALL fall back to traditional descriptor set binding.

#### Scenario: Bindless texture access
Given VK_EXT_descriptor_indexing is supported
When a texture SRV is created and SetDescriptorIndex is called with its index
Then the shader can access the texture via descriptor indexing

#### Scenario: Fallback without descriptor indexing
Given an older Vulkan driver without descriptor indexing
When SetDescriptorIndex is called
Then the backend falls back to traditional descriptor set updates per draw

### Requirement: Vulkan SwapChain Recreation
The VulkanSwapChain SHALL handle VK_ERROR_OUT_OF_DATE_KHR and VK_SUBOPTIMAL_KHR by recreating the VkSwapchainKHR. Resize() MUST destroy old swapchain images and create new ones at the requested dimensions.

#### Scenario: Window resize triggers recreation
Given a swapchain at 800x600
When Resize(1920, 1080) is called
Then the old VkSwapchainKHR is destroyed
And a new one is created at 1920x1080
And GetCurrentBackBuffer returns a valid texture handle

#### Scenario: OUT_OF_DATE handled transparently
Given VK_ERROR_OUT_OF_DATE_KHR is returned from vkAcquireNextImageKHR
When the next frame begins
Then the swapchain is automatically recreated
And rendering continues without application-visible errors

### Requirement: SPIR-V Shader Support
CreateShader on the Vulkan backend MUST accept SPIR-V bytecode and create VkShaderModules. The shader pipeline MUST support compiling HLSL to SPIR-V via DXC with the `-spirv` flag.

#### Scenario: SPIR-V shader loaded
Given a valid SPIR-V bytecode blob compiled from HLSL
When CreateShader is called with stage=Vertex
Then a VkShaderModule is created successfully
And it can be used in CreateGraphicsPipeline

### Requirement: Vulkan Render Pass
The CommandList SHALL support render passes via VkRenderPass + VkFramebuffer. Where VK_KHR_dynamic_rendering is available, it SHOULD use dynamic rendering instead for simpler pass management. BeginRenderPass MUST bind color and depth attachments. EndRenderPass MUST end the current pass.

#### Scenario: Render pass with color and depth
Given a render pass desc with 1 color attachment and 1 depth attachment
When BeginRenderPass is called
Then a VkRenderPass is active with both attachments bound
And draw calls render to the specified targets

### Requirement: Backend Factory Vulkan Selection
The factory SHALL support `--rhi vulkan` CLI argument. When both DX12 and Vulkan are available, the factory MUST prefer DX12 by default (Windows primary). Vulkan MUST be selectable explicitly.

#### Scenario: CLI selects Vulkan
Given the engine is launched with --rhi vulkan
When the RHI device is created
Then the Vulkan backend is used

### Requirement: Cross-Backend Visual Equivalence
The same RHI draw calls on Vulkan and DX12 backends MUST produce visually equivalent rendering output. Vulkan's Y-axis flip (compared to DX12) MUST be handled internally by the backend so the application sees consistent NDC conventions.

#### Scenario: NDC Y-axis consistent
Given the same vertex data and shaders
When rendered on Vulkan and DX12
Then the image is not vertically flipped
And both produce matching pixel output (within floating-point tolerance)
