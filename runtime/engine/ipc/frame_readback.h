/// Triple-buffered GPU-to-CPU frame readback for H.264 encoding.
///
/// Manages 3 readback buffers that rotate each frame:
/// - Buffer N:   GPU copies the backbuffer into it (this frame)
/// - Buffer N-1: GPU copy in flight (fence not signaled yet)
/// - Buffer N-2: Ready to read on CPU (fence signaled, safe to map)
///
/// This eliminates the `WaitIdle()` stall in the encode path. The first two
/// frames after initialization produce no output (pipeline is filling), then
/// every subsequent frame reads the oldest completed buffer with zero stall.
///
/// # Usage
/// ```cpp
/// FrameReadback readback;
/// readback.Init(device, width, height);
///
/// // Each frame, after Present:
/// cmdList->Begin();
/// readback.CaptureFrame(cmdList, swapChain);
/// cmdList->End();
/// cmdList->Execute();
/// readback.SignalFence(device);
///
/// // Read the oldest buffer (N-2) — no stall if ready:
/// if (readback.IsReadReady()) {
///     const uint8_t* data;
///     uint32_t stride;
///     readback.MapRead(&data, &stride);
///     // ... encode H.264 ...
///     readback.UnmapRead();
///     readback.AdvanceRead();
/// }
///
/// readback.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Call from the render/encode thread only.

#pragma once

#include <cstdint>
#include "rhi_types.h"   // RHITextureHandle for CaptureFromTexture()

namespace uz {

// Forward declarations — full definitions only in the .cpp via "rhi.h".
class IRHIDevice;
class IRHISwapChain;
class IRHICommandList;
class IRHIFence;

/// Triple-buffered GPU readback for low-latency frame capture.
///
/// Rotates through 3 readback buffers so that the CPU reads buffer N-2 while
/// the GPU writes buffer N. The two-frame latency is the minimum needed to
/// avoid any GPU stall — buffer N-1 may still be in flight.
///
/// The caller drives the rotation explicitly:
/// 1. `CaptureFrame()` — records the GPU copy into the current write buffer
/// 2. `SignalFence()` — signals the fence after the command list executes
/// 3. `IsReadReady()` — checks if the oldest buffer's fence has completed
/// 4. `MapRead()` / `UnmapRead()` — accesses the oldest buffer's pixels
/// 5. `AdvanceRead()` — advances the read index after encoding
class FrameReadback {
public:
    FrameReadback();
    ~FrameReadback();

    // Non-copyable, non-movable — owns GPU resources via raw device pointer.
    FrameReadback(const FrameReadback&) = delete;
    FrameReadback& operator=(const FrameReadback&) = delete;
    FrameReadback(FrameReadback&&) = delete;
    FrameReadback& operator=(FrameReadback&&) = delete;

    /// Initialize 3 readback buffers and 3 fences for the given dimensions.
    ///
    /// Row pitch is aligned to 256 bytes (DX12 requirement, harmless on other backends).
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer/fence creation.
    /// * `width`  — Backbuffer width in pixels.
    /// * `height` — Backbuffer height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` if any buffer or fence creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, uint32_t width, uint32_t height);

    /// Release all readback buffers and fences. Safe to call if not initialized.
    void Shutdown();

    /// Record GPU commands to copy the current backbuffer (in Present state)
    /// into the current write buffer. Call AFTER Present.
    ///
    /// Transitions: Present -> CopySrc -> Present.
    /// Advances the write index internally.
    ///
    /// # Arguments
    /// * `cmdList`   — Command list to record the copy on.
    /// * `swapChain` — Swap chain providing the current backbuffer.
    void CaptureFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain);

    /// Record GPU commands to copy the given texture (already in CopySrc state)
    /// into the current write buffer. The caller is responsible for barrier
    /// transitions before and after this call.
    ///
    /// This is the preferred capture path — avoids reading from the swapchain
    /// backbuffer (which rotates after Present and may cause DEVICE_REMOVED).
    ///
    /// Advances the write index internally.
    ///
    /// # Arguments
    /// * `cmdList`    — Command list in recording state.
    /// * `srcTexture` — Texture to copy from (must be in CopySrc state).
    void CaptureFromTexture(IRHICommandList* cmdList, RHITextureHandle srcTexture);

    /// Signal the fence for the most recently written buffer.
    ///
    /// Must be called AFTER `cmdList->Execute()` so the GPU signals the fence
    /// when the copy completes. The fence is signaled on the GPU timeline via
    /// `IRHIFence::Signal()`.
    ///
    /// # Arguments
    /// * `device` — Not used (fence Signal is self-contained), reserved for
    ///   future queue-specific signaling.
    void SignalFence();

    /// Check if the oldest buffer (N-2) has completed its GPU copy.
    ///
    /// Returns `false` during the first two frames (pipeline filling) and
    /// whenever the GPU hasn't finished the oldest copy yet.
    ///
    /// # Returns
    /// `true` if `MapRead()` can be called without stalling.
    [[nodiscard]] bool IsReadReady() const;

    /// Map the oldest readback buffer for CPU read access.
    ///
    /// Only call after `IsReadReady()` returns `true`. The returned pointer
    /// is valid until `UnmapRead()` is called.
    ///
    /// # Arguments
    /// * `outData`   — Receives pointer to the first row of RGBA pixel data.
    /// * `outStride` — Receives the row pitch in bytes (256-byte aligned).
    ///
    /// # Returns
    /// `true` on success, `false` if mapping fails or arguments are null.
    [[nodiscard]] bool MapRead(const uint8_t** outData, uint32_t* outStride) const;

    /// Unmap the oldest readback buffer after reading. Pairs with `MapRead()`.
    void UnmapRead() const;

    /// Advance the read index after encoding is complete.
    ///
    /// Call after `MapRead()` + encode + `UnmapRead()`. This makes the buffer
    /// available for future GPU writes.
    void AdvanceRead();

    /// Returns `true` if `Init()` has been called successfully.
    [[nodiscard]] bool IsInitialized() const;

    /// Returns the backbuffer width this readback was initialized for.
    [[nodiscard]] uint32_t GetWidth() const;

    /// Returns the backbuffer height this readback was initialized for.
    [[nodiscard]] uint32_t GetHeight() const;

private:
    /// Number of readback buffers in the ring. 3 allows reading N-2 while
    /// the GPU writes N, with N-1 in flight.
    static constexpr uint32_t kBufferCount = 3;

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
