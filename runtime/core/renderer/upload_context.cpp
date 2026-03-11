/// GPU upload context implementation.
///
/// Creates temporary upload-heap buffers for staging CPU data, then copies
/// to default-heap destinations via command list CopyBuffer commands.

#include "upload_context.h"
#include "log.h"

#include <cstring>

namespace uz {

bool UploadContext::UploadBuffer(IRHICommandList* cmdList, RHIBufferHandle dst,
                                 const void* data, uint64_t sizeBytes) {
    if (!m_device || !cmdList || !data || sizeBytes == 0) {
        UZ_LOG(Renderer, Error, "UploadBuffer: invalid arguments");
        return false;
    }

    // Create a staging buffer in upload heap
    RHIBufferDesc stagingDesc{};
    stagingDesc.sizeBytes = sizeBytes;
    stagingDesc.usage = RHIBufferUsage::CopySrc;
    stagingDesc.memory = RHIMemoryType::Upload;
    stagingDesc.debugName = "StagingBuffer";

    RHIBufferHandle staging = m_device->CreateBuffer(stagingDesc);
    if (!staging.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadBuffer: failed to create staging buffer ({} bytes)", sizeBytes);
        return false;
    }

    // Map and copy CPU data into staging buffer
    void* mapped = m_device->MapBuffer(staging);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "UploadBuffer: failed to map staging buffer");
        m_device->DestroyBuffer(staging);
        return false;
    }
    std::memcpy(mapped, data, sizeBytes);

    // Record GPU copy: staging → destination
    cmdList->CopyBuffer(staging, 0, dst, 0, sizeBytes);

    // Keep staging alive until GPU copy completes
    m_stagingBuffers.push_back(staging);
    return true;
}

void UploadContext::ReleaseStaging() {
    for (auto& buf : m_stagingBuffers) {
        m_device->DestroyBuffer(buf);
    }
    m_stagingBuffers.clear();
}

} // namespace uz
