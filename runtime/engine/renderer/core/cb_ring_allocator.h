/// UzEngine CB Ring Allocator — Triple-Buffered Constant Buffer Upload Ring
///
/// Provides a ring-buffer allocator for per-frame constant buffer uploads.
/// Pre-allocates a large upload-heap buffer and suballocates 256-byte-aligned
/// slots per frame. Triple-buffered to avoid GPU/CPU synchronization stalls.
///
/// Split from `constant_buffers.h` for single-responsibility: CB struct layouts
/// live in `constant_buffers.h`, allocation logic lives here.

#pragma once

#include "rhi_types.h"
#include <cstdint>

namespace uz {

class IRHIDevice;

/// Result of a constant buffer suballocation from `CBRingAllocator`.
///
/// Holds both CPU-writable and GPU-visible pointers to the same memory region,
/// allowing the caller to memcpy data and immediately bind the GPU address as
/// a root CBV. Valid only for the current frame — the region is recycled on
/// the next `BeginFrame` call for this frame index.
///
/// # Thread Safety
/// Not thread-safe. Produced and consumed on the render thread only.
///
/// # Examples
/// ```cpp
/// auto alloc = ring.Allocate(sizeof(PerFrameCB));
/// memcpy(alloc.cpuPtr, &cbData, sizeof(PerFrameCB));
/// cmdList->SetGraphicsRootConstantBufferView(1, alloc.gpuAddr);
/// ```
struct CBAllocation {
    void*           cpuPtr;      ///< CPU-writable pointer (upload heap mapped memory)
    uint64_t        gpuAddr;     ///< GPU virtual address for root CBV binding (DX12/Vulkan)
    RHIBufferHandle bufferHandle; ///< Backing buffer handle (for DX11 handle-based binding)
    uint64_t        offsetBytes; ///< Byte offset into the backing buffer
};

/// Ring-buffer allocator for per-frame constant buffer uploads.
///
/// Pre-allocates a large upload-heap buffer and suballocates 256-byte-aligned
/// slots per frame. Triple-buffered (or N-buffered) so the GPU can read from
/// a completed frame's region while the CPU writes into the current frame's
/// region without synchronization stalls.
///
/// Typical lifetime: created once at renderer init, `BeginFrame` each frame,
/// `Allocate` for each CB upload, destroyed at renderer shutdown.
///
/// # Thread Safety
/// NOT thread-safe. All calls must originate from the render thread.
///
/// # Examples
/// ```cpp
/// CBRingAllocator ring;
/// ring.Init(device, 3, 64 * 1024);  // 3 frames, 64KB each
/// ring.BeginFrame(frameIndex);
/// auto alloc = ring.Allocate(sizeof(PerFrameCB));
/// memcpy(alloc.cpuPtr, &perFrame, sizeof(PerFrameCB));
/// ```
class CBRingAllocator {
public:
    CBRingAllocator() = default;
    ~CBRingAllocator();

    /// Initializes the ring buffer with the given number of frames and per-frame capacity.
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer creation. Must outlive this allocator.
    /// * `framesInFlight` — Number of frames to triple-buffer (typically 3).
    /// * `perFrameBytes` — Maximum bytes per frame (rounded up to 256-byte alignment).
    ///
    /// # Returns
    /// `true` if the backing buffer was created successfully.
    bool Init(IRHIDevice* device, uint32_t framesInFlight, uint64_t perFrameBytes);

    /// Releases the backing buffer.
    void Shutdown();

    /// Resets the write offset for the current frame's region.
    ///
    /// # Arguments
    /// * `frameIndex` — Current frame index (0, 1, 2, ...). Used modulo framesInFlight.
    void BeginFrame(uint32_t frameIndex);

    /// Suballocates a 256-byte-aligned block from the current frame's region.
    ///
    /// # Arguments
    /// * `sizeBytes` — Number of bytes needed (will be rounded up to 256).
    ///
    /// # Returns
    /// A `CBAllocation` with CPU write pointer and GPU virtual address.
    /// Returns `{nullptr, 0}` if the frame's region is full.
    [[nodiscard]] CBAllocation Allocate(uint64_t sizeBytes);

private:
    IRHIDevice*    m_device = nullptr;
    RHIBufferHandle m_buffer;
    uint8_t*       m_mappedBase = nullptr;
    uint64_t       m_gpuBaseAddr = 0;
    uint64_t       m_perFrameBytes = 0;
    uint32_t       m_framesInFlight = 0;
    uint32_t       m_currentFrame = 0;
    uint64_t       m_currentOffset = 0;
};

} // namespace uz
