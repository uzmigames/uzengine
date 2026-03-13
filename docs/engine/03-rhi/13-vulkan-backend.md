# RHI — Vulkan Backend

> **Status**: Done (Milestone 1.3)
> **Source**: `runtime/engine/rhi/vulkan/vulkan_device.h/.cpp`
> **Depends on**: RHI interface (`rhi.h`, `rhi_types.h`), volk, VMA, Vulkan 1.3 headers

## Purpose

The Vulkan backend is the second RHI implementation, providing cross-platform support for Linux, Steam Deck, and Windows. It translates all `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, and `IRHIFence` calls into Vulkan API calls. All Vulkan headers are confined to files under `runtime/engine/rhi/vulkan/` — nothing else in the engine includes `vulkan.h`.

The Vulkan backend demonstrates the RHI abstraction layer working seamlessly with a completely different GPU API, requiring zero changes to renderer code or application layers.

## Architecture

```
VulkanDevice (IRHIDevice)
├── VkInstance              — Vulkan instance (validation layers, extensions)
├── VkPhysicalDevice        — Selected GPU adapter
├── VkDevice                — Logical device with queues
├── VMA::Allocator          — Memory allocator (Vulkan Memory Allocator)
├── VkDescriptorPool        — Descriptor pool (reusable descriptors)
├── VkDescriptorSetLayout   — Descriptor set layout (binding structure)
├── VkPipelineLayout        — Pipeline layout (descriptor + push constant space)
├── Command Pools
│   ├── VkCommandPool       — Triple-buffered per-frame pool (rotation)
│   └── VkCommandBuffer × 3 — Command buffers (one per frame-in-flight)
├── Resource Pools
│   ├── ResourcePool<VulkanBuffer>
│   ├── ResourcePool<VulkanTexture>
│   ├── ResourcePool<VulkanShader>
│   ├── ResourcePool<VulkanSampler>
│   └── ResourcePool<VulkanPipeline>
├── Synchronization
│   ├── VkSemaphore[] (timeline) — GPU/CPU synchronization
│   └── VkFence[]             — Internal submission tracking
└── Swapchain
    ├── VkSwapchainKHR        — MAILBOX present mode, VK_KHR_win32_surface
    └── VkImage[] (backbuffers)
```

## Initialization Sequence

Device creation follows a strict order matching Vulkan's initialization requirements:

```
1. volkInitialize           Load Vulkan loader
     │
     ▼
2. Validation Setup         Enable VK_LAYER_KHRONOS_validation (if available)
     ▼
3. VkInstance Creation      Create with required extensions + validation layers
     │                      └── VK_KHR_win32_surface (Windows)
     │                      └── VK_KHR_get_physical_device_properties2
     │                      └── VK_EXT_debug_utils (if validation)
     ▼
4. volkLoadInstance         Load instance-level Vulkan functions
     ▼
5. Physical Device Selection EnumPhysicalDevices → select by GPU preference
     ▼
6. Feature Verification     Check required features:
     │                      ├── VK_KHR_synchronization2
     │                      ├── VK_KHR_timeline_semaphore
     │                      ├── VK_KHR_dynamic_rendering
     │                      └── VK_KHR_buffer_device_address
     ▼
7. Logical Device Creation  Create VkDevice with required features enabled
     │                      └── Queues: Graphics (1), Transfer (1), Compute (1)
     ▼
8. volkLoadDevice           Load device-level Vulkan functions
     ▼
9. VMA Allocator            VMA::CreateAllocator(device, physical_device, instance)
     ▼
10. Descriptor Resources    Create descriptor pool, set layout, pipeline layout
     ▼
11. Command Pools           Create triple-buffered command pools
     ▼
12. Swapchain              Create VkSwapchainKHR (MAILBOX present mode)
     ▼
13. Synchronization        Create timeline semaphores for frame pipelining
```

### Why This Order

- **volkInitialize first**: Loads the Vulkan loader before any API calls
- **Validation before instance**: Validation layer names must be passed to `VkInstanceCreateInfo`
- **volkLoadInstance after VkInstance**: Required to load instance-level functions like `vkEnumeratePhysicalDevices`
- **Feature verification before device**: Avoids creating a device without required extensions
- **volkLoadDevice after VkDevice**: Enables device-level functions like `vkCreateBuffer`
- **VMA after device**: Allocator wraps the VkDevice
- **Descriptors before command pools**: Command buffers bind descriptor sets from the pool

## Vulkan 1.3 Extensions

The backend requires these Vulkan 1.3 core features and extensions:

| Extension | Purpose | Alternative |
|-----------|---------|-------------|
| **VK_KHR_synchronization2** | Modern pipeline barriers with stage/access masks | Legacy `VkPipelineBarrier` |
| **VK_KHR_timeline_semaphore** | CPU wait on GPU timeline values | Legacy `VkFence` |
| **VK_KHR_dynamic_rendering** | Render passes without VkRenderPass objects | VkRenderPass (legacy) |
| **VK_KHR_buffer_device_address** | Bindless buffer access via device addresses | Traditional descriptor binding |
| **VK_KHR_win32_surface** | Windows swapchain support | Wayland/X11 on Linux |

**volk dynamic dispatch**: The backend uses `volk` (https://github.com/zeux/volk) for dynamic Vulkan function loading. This means the engine doesn't link against `vulkan-1.lib` — instead, Vulkan is discovered at runtime. This enables graceful fallback if a required extension is unavailable.

## Descriptor Layout

Descriptors are organized to match DX12 register semantics and enable automatic HLSL→SPIRV compilation:

```
┌─────────────────────────────────────────────────────────────┐
│ VkDescriptorSet (one per frame-in-flight)                  │
├─────────────────────────────────────────────────────────────┤
│ Binding 0-15   (SAMPLED_IMAGE)  ← t0-t15 (texture slots)   │
│ Binding 16-31  (SAMPLER)        ← s0-s15 (sampler slots)   │
│ Binding 32-47  (UNIFORM_BUFFER) ← b0-b15 (cbuffer slots)   │
│ Push Constants (128 bytes)      ← packed shader data        │
└─────────────────────────────────────────────────────────────┘
```

Descriptor creation flow:

1. **Buffer creation**: Device allocates VMA buffer, creates `VkBufferView` (if applicable), writes descriptor to set
2. **Texture creation**: Device allocates VMA image, creates `VkImageView`, writes SRV descriptor to set
3. **Sampler creation**: Device creates `VkSampler`, writes to descriptor set
4. **Per-frame flush**: Before each render, `FlushDescriptorSet()` updates the descriptor set for the current frame

### Why Per-Frame Descriptors?

Vulkan synchronization requires that descriptor writes don't race with in-flight rendering. The backend maintains one descriptor set per frame-in-flight (typically 3), rotating between them. Each frame updates only its own descriptor set, eliminating synchronization hazards.

## Command List Implementation

Command recording uses triple-buffered command pools to avoid command buffer reuse while GPU rendering is still in flight:

```cpp
// Pseudo-code: frame-to-frame rotation
class CommandListRotation {
    VkCommandPool pools[3];
    VkCommandBuffer buffers[3];
    uint32_t currentFrame = 0;

    void Begin() {
        uint32_t poolIndex = currentFrame % 3;
        vkResetCommandPool(device, pools[poolIndex], 0);
        vkBeginCommandBuffer(buffers[poolIndex], ...);
        currentFrame++;
    }

    void Flush() {
        // Record any pending descriptor updates to command buffer
        FlushDescriptorSet();
    }
};
```

### Per-Frame Descriptor Set Flushing

Descriptor writes (textures, buffers, samplers) may accumulate across multiple `SetDescriptor()` calls. The `FlushDescriptorSet()` function batches these updates:

```cpp
void VulkanDevice::FlushDescriptorSet() {
    if (pendingDescriptorWrites.empty()) return;

    VkWriteDescriptorSet writes[MAX_DESCRIPTOR_WRITES];
    // ... populate writes[] from pending updates ...

    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    pendingDescriptorWrites.clear();
}
```

This is called before each draw command to ensure descriptors are up-to-date.

## Swapchain

The swapchain uses MAILBOX present mode for lowest-latency presentation:

```cpp
VkPresentModeKHR presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
// Equivalent to DX12 DXGI_SWAP_EFFECT_FLIP_DISCARD with no vsync
```

| DX12 Equivalent | Vulkan | Behavior |
|---|---|---|
| FLIP_DISCARD, no vsync | MAILBOX | Drop frames if rendering slower than display |
| FLIP_DISCARD, vsync on | FIFO | Block until display is ready |

Surface creation uses `VK_KHR_win32_surface` on Windows; requires `hinstance` and `hwnd` from the window system.

## Fence / Synchronization

The backend uses **timeline semaphores** (not traditional fences) for efficient CPU/GPU synchronization:

```cpp
// Pseudo-code: wait for frame N to complete
uint64_t frameValue = currentFrameNumber;
vkWaitSemaphores(device, &waitInfo, TIMEOUT);  // Blocks until GPU reaches frameValue
```

**Why timeline semaphores?**

| Mechanism | Pros | Cons |
|---|---|---|
| Traditional `VkFence` | Simple, one-per-frame | Must reset between uses, binary only |
| **Timeline Semaphore** (chosen) | Monotonic, no reset, queryable | Requires `VK_KHR_timeline_semaphore` |

The implementation maintains a single timeline semaphore per frame-in-flight, incrementing its value on each submission. This eliminates the fence reset overhead and enables more flexible synchronization patterns.

## Y-Axis Flip

Vulkan's NDC (Normalized Device Coordinates) have Y pointing **down** (screen-space), while DX12's Y points **up**. The backend automatically flips the viewport:

```cpp
VkViewport viewport = {
    x, y,
    width,
    -height,  // Negative height flips Y
    minDepth, maxDepth
};
```

This ensures shaders using DX12-style transforms (e.g., `float4(position, 1.0)`) work unmodified in Vulkan.

## Shader Pipeline Integration

The shader system auto-detects the active RHI backend and produces SPIR-V for Vulkan:

```cpp
// Pseudo-code: shader compilation
ShaderOutputFormat format = (GetActiveRHIBackend() == RHIBackend::Vulkan)
    ? ShaderOutputFormat::SPIRV
    : ShaderOutputFormat::DXIL;

// Compiler is routed to DXC with -spirv and binding shifts
CompileShader(source, format, ...);
```

See [spirv-compilation.md](spirv-compilation.md) for the full SPIR-V pipeline.

## Bindless Disabled

The Vulkan backend operates in **non-bindless mode** (`IsBindless() returns false`). This means:

- Renderers use the SM5.0 codepath (no `ResourceDescriptorHeap[]`)
- Shaders access resources via named descriptor set bindings (e.g., `layout(set=0, binding=0) uniform sampler2D albedo`)
- **No performance penalty**: Vulkan's descriptor indexing can be enabled in a future iteration without any API changes

The reason for this conservative approach is that SM6.6 bindless shader bytecode (DXIL) cannot be directly transpiled to SPIR-V — the descriptor indexing patterns differ. Enabling bindless would require maintaining separate shader source with conditional compilation (`#ifdef VULKAN`), which violates the engine's design principle of single-source HLSL.

## Performance

Benchmark on NVIDIA RTX 4090 (Milestone 1.3):

| Demo | Frame Rate | Frame Time |
|------|-----------|-----------|
| triangle | 5187 FPS | 0.19ms |
| textured_quad | 5334 FPS | 0.19ms |
| camera_test | 332 FPS | 3.01ms |
| mesh_test | 331 FPS | 3.02ms |

These match the DX12 backend performance, validating that the RHI abstraction has zero overhead.

## Vulkan Compatibility Notes

### Minimum Version: Vulkan 1.3

UzEngine requires Vulkan 1.3 (released Sept 2022). This includes:

- `VK_KHR_dynamic_rendering` (core in 1.3)
- `VK_KHR_synchronization2` (core in 1.3)
- `VK_KHR_timeline_semaphore` (core in 1.3)
- `VK_KHR_buffer_device_address` (core in 1.2, widely available)

Older GPUs with only Vulkan 1.0/1.1 support are not supported. The `feature verification` step during initialization returns early if required extensions are unavailable.

### volk Function Pointers

Dynamic loading via volk requires careful initialization ordering:

```cpp
// ❌ WRONG — vkGetPhysicalDeviceProperties not loaded yet
volkInitialize();
vkGetPhysicalDeviceProperties(physicalDevice, &props);

// ✅ CORRECT — load instance functions, then use them
volkInitialize();
volkLoadInstance(instance);
vkGetPhysicalDeviceProperties(physicalDevice, &props);
```

### Descriptor Update Rules

Descriptor sets must not be in-flight during descriptor updates. The backend enforces this by:

1. Maintaining per-frame descriptor sets (rotation)
2. Calling `FlushDescriptorSet()` before each render
3. Synchronizing before descriptor updates via `WaitIdle()` if necessary

## Rules

1. **NEVER** include `vulkan.h` outside `runtime/engine/rhi/vulkan/`
2. **ALWAYS** call `volkLoadInstance()` after creating VkInstance
3. **ALWAYS** call `volkLoadDevice()` after creating VkDevice
4. **ALWAYS** flush descriptor sets before drawing** — call `FlushDescriptorSet()` before the first draw of each frame
5. **ALWAYS** respect frame-in-flight boundaries** — don't reuse command buffers while the GPU is rendering
6. **NEVER** update descriptors while a descriptor set is in-flight** — use frame-in-flight rotation to avoid hazards
7. **ALWAYS** verify required extensions in feature verification step — return early if unavailable
