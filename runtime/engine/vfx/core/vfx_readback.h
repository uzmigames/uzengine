#pragma once

/// VFX GPU Readback Framework -- unified GPU-to-CPU readback with ring-buffered staging.
///
/// Manages a ring of staging buffers with N-frame latency, dispatches copy
/// commands, and invokes typed callbacks when data is available on the CPU.
/// Replaces ad-hoc readback in individual modules and serves as the foundation
/// for dynamic AABB, death events, gameplay queries, etc.
///
/// # Design
/// A ring of `kRingSize` staging frames, each with a large READBACK-heap buffer
/// and a bump allocator. Readback requests within a frame suballocate from the
/// same staging buffer. At EndFrame(), CopyBufferRegion commands are issued for
/// all requests. At ProcessCallbacks() (N frames later), the oldest staging
/// buffer is mapped, callbacks are invoked, and the buffer is unmapped.
///
/// # Thread Safety
/// Not thread-safe. Intended for single-threaded main/render thread use.

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace uz {

class IRHICommandList;
class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// ReadbackRequest -- individual GPU→CPU copy request
// ─────────────────────────────────────────────────────────────────────────────

/// A single GPU→CPU readback request queued within a frame.
///
/// The callback is invoked on the main thread when the GPU data becomes
/// available (after `ringSize` frames). The data pointer passed to the
/// callback is valid only for the duration of the callback invocation.
struct ReadbackRequest {
    /// Source GPU buffer to copy from.
    RHIBufferHandle src = RHIBufferHandle::Invalid();

    /// Byte offset into the source buffer to start copying from.
    uint32_t offset = 0;

    /// Number of bytes to copy.
    uint32_t size = 0;

    /// Byte offset into the staging buffer where this request's data is stored.
    uint32_t stagingOffset = 0;

    /// Callback invoked when the data is ready on the CPU.
    /// Parameters: pointer to mapped data (valid only during callback), byte size.
    std::function<void(const void* data, uint32_t size)> callback;
};

// ─────────────────────────────────────────────────────────────────────────────
// StagingFrame -- per-ring-slot staging buffer with bump allocator
// ─────────────────────────────────────────────────────────────────────────────

/// One slot in the ring of staging buffers.
///
/// Each frame's readback requests are batched into a single READBACK-heap
/// buffer via bump allocation. The fence value tracks when the GPU has
/// finished writing to this slot's data.
struct StagingFrame {
    /// READBACK-heap staging buffer (e.g., 64KB).
    RHIBufferHandle buffer = RHIBufferHandle::Invalid();

    /// Current bump-allocator offset (bytes used so far this frame).
    uint32_t usedBytes = 0;

    /// Total capacity of the staging buffer in bytes.
    uint32_t capacityBytes = 0;

    /// Fence value signaled after the CopyBufferRegion commands for this
    /// frame's requests have completed on the GPU.
    uint64_t fenceValue = 0;

    /// Requests queued for this frame.
    std::vector<ReadbackRequest> requests;
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXReadbackManager -- ring-buffered GPU readback manager
// ─────────────────────────────────────────────────────────────────────────────

/// Unified GPU→CPU readback manager for the VFX system.
///
/// # Lifecycle
/// 1. `Create(device, maxRequestsPerFrame, ringSize)` -- allocate staging buffers.
/// 2. Each frame:
///    a. `BeginFrame(frameIndex)` -- advance ring, prepare current slot.
///    b. `RequestReadback(src, offset, size, callback)` -- queue requests.
///    c. `EndFrame(cmd, fenceValue)` -- issue CopyBufferRegion commands.
///    d. `ProcessCallbacks(device)` -- map oldest slot, invoke callbacks, unmap.
/// 3. `Destroy(device)` -- release all staging buffers.
///
/// # Capacity
/// Default staging buffer: 64KB per ring slot. Requests beyond capacity are
/// dropped with a VFX warning log. Total VRAM overhead: ringSize * 64KB.
class VFXReadbackManager {
public:
    VFXReadbackManager() = default;
    ~VFXReadbackManager() = default;

    // Non-copyable, movable.
    VFXReadbackManager(const VFXReadbackManager&) = delete;
    VFXReadbackManager& operator=(const VFXReadbackManager&) = delete;
    VFXReadbackManager(VFXReadbackManager&&) = default;
    VFXReadbackManager& operator=(VFXReadbackManager&&) = default;

    /// Allocates the ring of staging buffers.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    /// - `maxRequestsPerFrame`: Maximum readback requests per frame. Default 64.
    /// - `ringSize`: Number of staging frames in the ring. Default 3.
    /// - `stagingCapacityBytes`: Per-slot staging buffer size. Default 65536 (64KB).
    ///
    /// # Returns
    /// `true` on success, `false` if any staging buffer allocation fails.
    [[nodiscard]] bool Create(IRHIDevice* device,
                              uint32_t maxRequestsPerFrame = 64,
                              uint32_t ringSize = 3,
                              uint32_t stagingCapacityBytes = 65536);

    /// Releases all staging buffers and resets state.
    void Destroy(IRHIDevice* device);

    /// Advances the ring and prepares the current frame's staging slot.
    /// Call at the start of each frame before queueing any readback requests.
    void BeginFrame(uint64_t frameIndex);

    /// Queues a GPU→CPU readback request for the current frame.
    ///
    /// The callback will be invoked `ringSize` frames later when the data
    /// is available on the CPU. If the staging buffer is full or the per-frame
    /// request limit is reached, the request is dropped with a warning.
    ///
    /// # Parameters
    /// - `src`: Source GPU buffer handle.
    /// - `byteOffset`: Offset into the source buffer.
    /// - `byteSize`: Number of bytes to read back.
    /// - `callback`: Function to call with the mapped data.
    ///
    /// # Returns
    /// `true` if the request was queued, `false` if dropped.
    [[nodiscard]] bool RequestReadback(
        RHIBufferHandle src,
        uint32_t byteOffset,
        uint32_t byteSize,
        std::function<void(const void* data, uint32_t size)> callback);

    /// Issues CopyBufferRegion commands for all requests queued this frame.
    /// Call after all RequestReadback() calls and before command list submission.
    ///
    /// # Parameters
    /// - `cmd`: Command list to record copy commands on.
    /// - `fenceValue`: Fence value that will be signaled after these copies complete.
    void EndFrame(IRHICommandList* cmd, uint64_t fenceValue);

    /// Maps the oldest completed staging slot, invokes callbacks, and unmaps.
    /// Non-blocking: if the oldest frame's fence has not been signaled yet,
    /// processing is skipped and data arrives next frame instead of stalling.
    ///
    /// # Parameters
    /// - `device`: RHI device for mapping/unmapping the staging buffer.
    /// - `completedFenceValue`: The most recent fence value signaled by the GPU.
    void ProcessCallbacks(IRHIDevice* device, uint64_t completedFenceValue);

    /// Returns true if the manager has been successfully created.
    [[nodiscard]] bool IsValid() const noexcept { return !ring_.empty(); }

private:
    std::vector<StagingFrame> ring_;
    uint32_t ringSize_ = 0;
    uint32_t currentSlot_ = 0;
    uint32_t maxRequestsPerFrame_ = 64;
    uint64_t frameIndex_ = 0;
};

} // namespace uz
