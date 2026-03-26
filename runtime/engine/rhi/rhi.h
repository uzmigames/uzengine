#pragma once

/// UzEngine Rendering Hardware Interface — Abstract Device, Command List, SwapChain, Fence.
///
/// This header defines the backend-agnostic GPU abstraction layer for UzEngine.
/// Application code uses only these pure virtual interfaces — never DX12, Vulkan,
/// or Metal types directly.
///
/// # Architecture
/// ```
/// Application → IRHIDevice       → DX12Device   (or VulkanDevice, MetalDevice)
///             → IRHICommandList   → DX12CommandList
///             → IRHISwapChain     → DX12SwapChain
///             → IRHIFence         → DX12Fence
/// ```
///
/// # Resource Model
/// GPU resources are referenced via typed generational handles (`RHIHandle<Tag>`).
/// The backend owns the actual GPU objects and maps handles to them internally.
/// Handles prevent use-after-free via generation counters — stale handles are
/// safely detected and rejected.
///
/// # Bindless (SM6.6)
/// Resources get a persistent descriptor index in a global CBV_SRV_UAV heap.
/// Shaders access resources via `SetDescriptorIndex()` push constants, avoiding
/// traditional descriptor table binding.
///
/// # Threading
/// - `IRHIDevice` methods (Create/Destroy) are thread-safe (internally synchronized).
/// - `IRHICommandList` is NOT thread-safe — one recording thread per list.
/// - `IRHISwapChain::Present` must be called from the thread that created it.
///
/// # Lifetime
/// All interfaces returned by `IRHIDevice` (command lists, fences, swap chains)
/// are owned by the caller and must be destroyed via the corresponding
/// `Destroy*()` method before the device is destroyed.

// Umbrella header — includes all RHI interface sub-headers.
// rhi_device.h transitively includes rhi_fence.h, rhi_command_list.h, rhi_swapchain.h.
#include "rhi_device.h"
