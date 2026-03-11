#pragma once

/// GPU upload context for staging CPU data to default-heap resources.
///
/// Creates a temporary upload (staging) buffer, copies CPU data into it,
/// then records a GPU copy command from staging → destination buffer.
/// The caller must ensure the copy completes (via fence) before releasing
/// the staging buffer.

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
    explicit UploadContext(IRHIDevice* device) : m_device(device) {}

    /// Uploads CPU data to a GPU default-heap buffer via staging copy.
    ///
    /// Creates a temporary upload buffer, memcpy's data into it, then
    /// records a CopyBuffer command on the given command list.
    /// The staging buffer is held alive until ReleaseStaging() is called.
    bool UploadBuffer(IRHICommandList* cmdList, RHIBufferHandle dst,
                      const void* data, uint64_t sizeBytes);

    /// Releases all staging buffers. Call after the GPU copy completes.
    void ReleaseStaging();

    /// Returns the number of pending staging buffers.
    uint32_t GetStagingCount() const { return static_cast<uint32_t>(m_stagingBuffers.size()); }

private:
    IRHIDevice*                    m_device = nullptr;
    std::vector<RHIBufferHandle>   m_stagingBuffers;
};

} // namespace uz
