/// UzEngine GPU Upload Context
///
/// Manages staging (upload-heap) buffers for transferring CPU data to
/// GPU default-heap resources. Creates temporary upload buffers, copies
/// CPU data into them, and records CopyBuffer commands on a command list.
///
/// # Ownership
/// The `UploadContext` holds staging buffers alive until `ReleaseStaging()`
/// is called. The caller MUST wait for the GPU copy to complete (via fence)
/// before calling `ReleaseStaging()`, or the GPU will read freed memory.
///
/// # Thread Safety
/// NOT thread-safe. Use from the thread that owns the command list.

#pragma once

#include "rhi.h"
#include "rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// UploadContext
// ─────────────────────────────────────────────────────────────────────────────

/// Records staging buffer uploads and tracks temporary resources for cleanup.
///
/// Usage:
/// ```cpp
/// UploadContext upload(device);
/// upload.UploadBuffer(cmdList, dstBuffer, data, size);
/// // ... record more uploads ...
/// cmdList->End();
/// cmdList->Execute();
/// device->WaitIdle();          // Wait for GPU copy to finish
/// upload.ReleaseStaging();     // Free staging buffers
/// ```
class UploadContext {
public:
    /// Creates an upload context bound to a device for staging buffer creation.
    ///
    /// # Arguments
    /// * `device` - RHI device used to create staging buffers. Must outlive
    ///   this context. Must not be null.
    explicit UploadContext(IRHIDevice* device) : m_device(device) {}

    /// Uploads CPU data to a GPU default-heap buffer via an intermediate staging buffer.
    ///
    /// Creates a temporary upload-heap buffer, copies `sizeBytes` from `data`
    /// into it via `memcpy`, then records a `CopyBuffer` GPU command from
    /// staging to `dst` on the provided command list.
    ///
    /// # Arguments
    /// * `cmdList` - Command list to record the copy command on. Must be in recording state.
    /// * `dst` - Destination buffer handle (must be default-heap with CopyDst usage).
    /// * `data` - Pointer to CPU source data. Must be valid for `sizeBytes` bytes.
    /// * `sizeBytes` - Number of bytes to upload. Must be > 0.
    ///
    /// # Returns
    /// `true` if the staging buffer was created and the copy was recorded.
    /// `false` on invalid arguments or buffer creation failure.
    ///
    /// # Errors
    /// Logs `Renderer::Error` on failure with specific reason.
    bool UploadBuffer(IRHICommandList* cmdList, RHIBufferHandle dst,
                      const void* data, uint64_t sizeBytes);

    /// Uploads CPU pixel data to a GPU default-heap texture via a staging buffer.
    ///
    /// Creates a temporary upload-heap buffer sized for 256-byte row alignment,
    /// copies pixel data with proper row pitch, then records a `CopyBufferToTexture`
    /// GPU command from staging to `dst` on the provided command list.
    ///
    /// # Arguments
    /// * `cmdList` - Command list to record the copy command on. Must be in recording state.
    /// * `dst` - Destination texture handle (must be default-heap with CopyDst usage).
    /// * `data` - Pointer to tightly-packed CPU pixel data (no padding between rows).
    /// * `sizeBytes` - Total size of pixel data in bytes.
    /// * `mipLevel` - Target mip level (default 0).
    /// * `arraySlice` - Target array slice (default 0).
    ///
    /// # Returns
    /// `true` if the staging buffer was created and the copy was recorded.
    /// `false` on invalid arguments, unsupported format, or staging buffer creation failure.
    ///
    /// # Errors
    /// Logs `Renderer::Error` on failure with specific reason.
    bool UploadTexture(IRHICommandList* cmdList, RHITextureHandle dst,
                       const void* data, uint64_t sizeBytes,
                       uint32_t mipLevel = 0, uint32_t arraySlice = 0);

    /// Releases all staging buffers held by this context.
    ///
    /// # Safety
    /// The GPU MUST have finished all copy commands before calling this.
    /// Typically achieved by executing the command list and waiting on a fence.
    void ReleaseStaging();

    /// Returns the number of staging buffers awaiting release.
    [[nodiscard]] uint32_t GetStagingCount() const { return static_cast<uint32_t>(m_stagingBuffers.size()); }

private:
    IRHIDevice*                    m_device = nullptr;
    std::vector<RHIBufferHandle>   m_stagingBuffers;
};

} // namespace uz
