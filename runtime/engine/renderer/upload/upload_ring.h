/// UzEngine Upload Ring Buffer — Triple-Buffered Staging Suballocator
///
/// Provides a persistent, triple-buffered ring buffer for GPU upload staging.
/// Replaces per-upload staging buffer allocation with a single large mapped
/// buffer that is suballocated per frame via bump allocation.
///
/// Each frame gets a fixed-size region. `BeginFrame()` resets the write offset
/// for the current frame's region (safe because the swapchain fence guarantees
/// frame N-frameCount has completed). `Allocate()` bumps within that region.
///
/// # Thread Safety
/// NOT thread-safe. All calls must originate from the render thread.
///
/// # Ownership
/// The ring does NOT own a command list. Callers pass their own command list
/// when recording copy commands from the ring's staging memory.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

/// Persistent triple-buffered ring buffer for GPU upload staging.
///
/// Replaces per-upload staging buffer allocation with a single large
/// mapped buffer. Suballocates contiguous regions per frame, reusing
/// memory after the GPU fence confirms the copy is complete.
///
/// # Usage
/// ```cpp
/// UploadRing ring;
/// ring.Init(device, 3, 4 * 1024 * 1024); // 3 frames, 4MB per frame
///
/// ring.BeginFrame(frameIndex);
///
/// auto alloc = ring.Allocate(textureDataSize, 512);
/// memcpy(alloc.cpuPtr, pixels, textureDataSize);
/// cmdList->CopyBufferToTexture(alloc.bufferHandle, alloc.offsetBytes, dstTexture, ...);
/// ```
class UploadRing {
public:
    UploadRing() = default;
    ~UploadRing() = default;

    // Non-copyable, non-movable
    UploadRing(const UploadRing&) = delete;
    UploadRing& operator=(const UploadRing&) = delete;
    UploadRing(UploadRing&&) = delete;
    UploadRing& operator=(UploadRing&&) = delete;

    /// Result of a suballocation from the upload ring.
    ///
    /// Contains a CPU-writable pointer for memcpy, the backing buffer handle
    /// for recording copy commands, and the byte offset into that buffer.
    /// Valid only for the current frame — the region is recycled on the next
    /// `BeginFrame()` for this frame index.
    struct Allocation {
        void* cpuPtr = nullptr;              ///< CPU-writable mapped pointer
        RHIBufferHandle bufferHandle;        ///< Backing upload-heap buffer handle
        uint32_t offsetBytes = 0;            ///< Byte offset into the backing buffer

        /// Returns true if the allocation succeeded.
        [[nodiscard]] bool IsValid() const { return cpuPtr != nullptr; }
    };

    /// Initializes the ring with a persistent upload-heap buffer.
    ///
    /// Creates a single buffer of size `frameCount * bytesPerFrame` in upload
    /// heap memory and maps it persistently. The buffer remains mapped for the
    /// entire lifetime of the ring.
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer creation. Must outlive this ring.
    /// * `frameCount` — Number of in-flight frames (typically 2-3).
    /// * `bytesPerFrame` — Maximum upload bytes per frame (e.g., 4MB).
    ///
    /// # Returns
    /// `true` if the backing buffer was created and mapped successfully.
    /// `false` on buffer creation or mapping failure (logs error).
    bool Init(IRHIDevice* device, uint32_t frameCount, uint32_t bytesPerFrame);

    /// Releases the backing buffer and resets all state.
    ///
    /// Safe to call multiple times or on an uninitialized ring (no-op).
    void Shutdown();

    /// Begins a new frame by resetting the bump offset for this frame's region.
    ///
    /// No GPU fence wait is needed here — the swapchain's present fence
    /// guarantees that frame `(frameIndex - frameCount)` has completed,
    /// making this frame's region safe to overwrite.
    ///
    /// # Arguments
    /// * `frameIndex` — Current frame index. Used modulo `frameCount`.
    void BeginFrame(uint32_t frameIndex);

    /// Suballocates from the current frame's region.
    ///
    /// Uses a bump allocator within the frame's fixed region. Aligns the
    /// current offset up to the requested alignment before allocating.
    ///
    /// # Arguments
    /// * `sizeBytes` — Number of bytes to allocate. Must be > 0.
    /// * `alignment` — Alignment in bytes (default 256, suitable for DX12
    ///   texture row pitch requirements). Must be a power of two.
    ///
    /// # Returns
    /// Valid allocation with `cpuPtr`, `bufferHandle`, and `offsetBytes`.
    /// Returns invalid allocation (`cpuPtr == nullptr`) if the frame budget
    /// is exhausted or `sizeBytes` is 0.
    [[nodiscard]] Allocation Allocate(uint32_t sizeBytes, uint32_t alignment = 256);

    /// Returns remaining bytes available in the current frame's budget.
    [[nodiscard]] uint32_t GetRemainingBytes() const;

    /// Returns the per-frame budget in bytes.
    [[nodiscard]] uint32_t GetBytesPerFrame() const { return m_bytesPerFrame; }

    /// Returns bytes allocated so far this frame.
    [[nodiscard]] uint32_t GetBytesUsedThisFrame() const { return m_frameOffset; }

    /// Returns true if the ring has been initialized successfully.
    [[nodiscard]] bool IsInitialized() const { return m_buffer.IsValid(); }

private:
    IRHIDevice* m_device = nullptr;
    RHIBufferHandle m_buffer;           ///< Single large upload-heap buffer
    uint8_t* m_mappedBase = nullptr;    ///< Persistently mapped pointer to buffer start
    uint32_t m_bytesPerFrame = 0;       ///< Budget per frame
    uint32_t m_frameCount = 0;          ///< Number of in-flight frames
    uint32_t m_currentFrame = 0;        ///< Current frame index (modulo frameCount)
    uint32_t m_frameOffset = 0;         ///< Current bump offset within frame's region
    uint32_t m_totalSize = 0;           ///< Total buffer size (frameCount * bytesPerFrame)
};

} // namespace uz
