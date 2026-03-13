/// UzEngine Staging Batch — Groups small uploads into batched copy submissions.
///
/// Instead of issuing one copy command per small resource upload, this batches
/// multiple small uploads into a single staging buffer region and submits
/// them as one copy command list, reducing copy queue overhead.
///
/// # Usage
/// ```cpp
/// StagingBatch batch;
/// batch.Init(device, 16 * 1024 * 1024); // 16 MB staging buffer
///
/// batch.BeginBatch();
/// batch.AddBufferUpload(destBuffer1, srcData1, size1, destOffset1);
/// batch.AddBufferUpload(destBuffer2, srcData2, size2, destOffset2);
/// batch.AddTextureUpload(destTexture1, srcData3, size3, subresource);
/// uint64_t fenceValue = batch.SubmitBatch();
///
/// // Later: device->GraphicsWaitForCopy(fenceValue);
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Render thread only.

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;

/// A single pending upload within a batch.
///
/// Tracks the source offset in the staging buffer and the destination
/// resource (either a buffer or texture, never both).
struct StagingUploadEntry {
    RHIBufferHandle destBuffer = RHIBufferHandle::Invalid();    ///< Destination buffer (invalid for texture uploads).
    RHITextureHandle destTexture = RHITextureHandle::Invalid(); ///< Destination texture (invalid for buffer uploads).
    uint64_t srcOffsetInStaging = 0;  ///< Byte offset into the staging buffer where source data lives.
    uint64_t destOffset = 0;          ///< Byte offset in the destination buffer (unused for textures).
    uint64_t sizeBytes = 0;           ///< Number of bytes to copy.
    uint32_t subresource = 0;         ///< Subresource index for texture uploads.
    bool isTexture = false;           ///< True if this entry targets a texture, false for buffers.
};

/// Batches multiple small uploads into a single copy queue submission.
///
/// Allocates a single upload-heap staging buffer at init time and bump-allocates
/// within it for each `AddBufferUpload` / `AddTextureUpload` call. All pending
/// copies are recorded into one copy command list and submitted together via
/// `SubmitBatch()`.
///
/// # Alignment
/// Each upload is aligned to 256 bytes within the staging buffer to satisfy
/// DX12 `CopyBufferRegion` alignment requirements.
///
/// # Capacity
/// If the staging buffer is full, `Add*Upload` returns `false`. The caller
/// should `SubmitBatch()` to flush, then `BeginBatch()` to start a new batch.
///
/// # Lifecycle
/// 1. `Init()` — creates the staging buffer
/// 2. `BeginBatch()` — resets write offset and pending entries
/// 3. `Add*Upload()` — copies data into staging and records the entry
/// 4. `SubmitBatch()` — records and executes copy commands, returns fence value
/// 5. `Shutdown()` — destroys the staging buffer
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.
class StagingBatch {
public:
    StagingBatch() = default;
    ~StagingBatch() = default;

    StagingBatch(const StagingBatch&) = delete;
    StagingBatch& operator=(const StagingBatch&) = delete;
    StagingBatch(StagingBatch&&) = delete;
    StagingBatch& operator=(StagingBatch&&) = delete;

    /// Initializes the staging batch with a device and maximum staging buffer size.
    ///
    /// Creates an upload-heap buffer of `maxStagingBytes` and persistently maps it.
    ///
    /// # Arguments
    /// * `device` — RHI device pointer (must remain valid for the lifetime of this object).
    /// * `maxStagingBytes` — Size of the staging buffer in bytes (default 16 MB).
    ///
    /// # Returns
    /// `true` on success, `false` if device is null or buffer creation fails.
    bool Init(IRHIDevice* device, uint64_t maxStagingBytes = 16 * 1024 * 1024);

    /// Shuts down the staging batch, unmapping and destroying the staging buffer.
    void Shutdown();

    /// Begins a new batch. Resets pending entries and staging offset.
    ///
    /// Must be called before any `Add*Upload` calls.
    void BeginBatch();

    /// Adds a buffer-to-buffer upload to the current batch.
    ///
    /// Copies `srcData` into the staging buffer at the current offset and records
    /// a copy entry. The staging offset advances by `sizeBytes` rounded up to 256.
    ///
    /// # Arguments
    /// * `dest` — Destination buffer handle.
    /// * `srcData` — CPU pointer to the source data (must remain valid until memcpy completes).
    /// * `sizeBytes` — Number of bytes to upload.
    /// * `destOffset` — Byte offset in the destination buffer (default 0).
    ///
    /// # Returns
    /// `true` if the upload fits in the staging buffer, `false` if full.
    [[nodiscard]] bool AddBufferUpload(RHIBufferHandle dest, const void* srcData,
                                       uint64_t sizeBytes, uint64_t destOffset = 0);

    /// Adds a texture upload to the current batch.
    ///
    /// Copies `srcData` into the staging buffer and records a texture copy entry.
    ///
    /// # Arguments
    /// * `dest` — Destination texture handle.
    /// * `srcData` — CPU pointer to the source pixel data.
    /// * `sizeBytes` — Number of bytes to upload.
    /// * `subresource` — Subresource index (mip level * arraySize + arraySlice).
    ///
    /// # Returns
    /// `true` if the upload fits in the staging buffer, `false` if full.
    [[nodiscard]] bool AddTextureUpload(RHITextureHandle dest, const void* srcData,
                                        uint64_t sizeBytes, uint32_t subresource = 0);

    /// Submits all pending uploads as a single copy queue batch.
    ///
    /// Creates a copy command list, records all pending copy commands, executes
    /// the list, and signals the copy queue fence.
    ///
    /// # Returns
    /// Fence value for synchronization (pass to `device->GraphicsWaitForCopy()`).
    /// Returns 0 if there are no pending entries.
    uint64_t SubmitBatch();

    /// Returns the number of pending uploads in the current batch.
    [[nodiscard]] uint32_t GetPendingCount() const { return static_cast<uint32_t>(m_entries.size()); }

    /// Returns bytes used in the current batch's staging buffer.
    [[nodiscard]] uint64_t GetStagingBytesUsed() const { return m_stagingOffset; }

    /// Returns true if initialized with a valid device and staging buffer.
    [[nodiscard]] bool IsInitialized() const { return m_device != nullptr; }

private:
    /// Aligns a byte offset up to the nearest 256-byte boundary.
    ///
    /// DX12 requires `CopyBufferRegion` offsets to be 256-byte aligned.
    [[nodiscard]] static constexpr uint64_t AlignTo256(uint64_t offset) {
        return (offset + 255) & ~static_cast<uint64_t>(255);
    }

    IRHIDevice* m_device = nullptr;
    uint64_t m_maxStagingBytes = 0;
    uint64_t m_stagingOffset = 0;       ///< Current write position in staging buffer.
    RHIBufferHandle m_stagingBuffer = RHIBufferHandle::Invalid();
    void* m_mappedPtr = nullptr;        ///< Persistently mapped staging buffer pointer.
    std::vector<StagingUploadEntry> m_entries;
    bool m_batchActive = false;
};

} // namespace uz
