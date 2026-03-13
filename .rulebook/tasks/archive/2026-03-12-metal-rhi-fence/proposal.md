# Proposal: Metal RHI Fence (MTLSharedEvent)

## Why

CPU/GPU synchronization is required for frame pipelining, resource readback, and shutdown. Metal uses `MTLSharedEvent` (timeline semaphore equivalent) for monotonic fence values — the same concept as DX12's `ID3D12Fence` and Vulkan's `VkSemaphore` (timeline). This is the simplest Metal RHI component and should be implemented first alongside resources.

## What Changes

### New Files

1. **`metal_fence.h`** — `MetalFence` class implementing `IRHIFence` (4 methods)
2. **`metal_fence.cpp`** — Implementation using `MTLSharedEvent`

### Metal API Mapping

| IRHIFence Method | Metal API | Notes |
|---|---|---|
| `Signal(value)` | `MTLCommandBuffer::encodeSignalEvent(event, value)` | Encoded into command buffer |
| `WaitCPU(value)` | `MTLSharedEvent::notifyListener(block, value)` + dispatch_semaphore | Blocks calling thread |
| `IsComplete(value)` | `event.signaledValue >= value` | Non-blocking poll |
| `GetCompletedValue()` | `event.signaledValue` | Last completed GPU value |

### Design Decisions

- **MTLSharedEvent over MTLFence**: `MTLFence` is intra-encoder only. `MTLSharedEvent` supports cross-queue and CPU/GPU sync with monotonic values — matching our `IRHIFence` contract exactly.
- **Listener-based WaitCPU**: Metal doesn't have a blocking wait API. Implementation uses `MTLSharedEventListener` with a `dispatch_semaphore_t` to create a blocking wait from the async notification.

## Impact

- Affected specs: `docs/engine/03-rhi/15-metal-backend.md`
- Affected code: `runtime/engine/rhi/metal/metal_fence.h/.cpp` (new)
- Breaking change: NO
- User benefit: CPU/GPU synchronization for Metal backend
- Dependencies: `metal-rhi-resources` (log category, metal-cpp headers)
