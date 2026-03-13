/// Staging Batch implementation.
///
/// Groups multiple small uploads into a single copy command list submission
/// to reduce copy queue overhead. Uses a persistently mapped upload-heap
/// staging buffer with 256-byte aligned bump allocation.

#include "staging_batch.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

UZ_DEFINE_STAT(IO, StagingBatchSubmissions);

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool StagingBatch::Init(IRHIDevice* device, uint64_t maxStagingBytes) {
    UZ_PROFILE_FUNCTION();

    if (!device) {
        UZ_LOG(Renderer, Error, "StagingBatch::Init: device is null");
        return false;
    }

    if (maxStagingBytes == 0) {
        UZ_LOG(Renderer, Error, "StagingBatch::Init: maxStagingBytes must be > 0");
        return false;
    }

    m_device = device;
    m_maxStagingBytes = maxStagingBytes;

    // Create a single upload-heap buffer for staging data
    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = maxStagingBytes;
    bufDesc.usage = RHIBufferUsage::CopySrc;
    bufDesc.memory = RHIMemoryType::Upload;
    bufDesc.debugName = "StagingBatch_UploadBuffer";

    m_stagingBuffer = m_device->CreateBuffer(bufDesc);
    if (!m_stagingBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "StagingBatch::Init: failed to create staging buffer ({:.1f} MB)",
               static_cast<double>(maxStagingBytes) / (1024.0 * 1024.0));
        m_device = nullptr;
        return false;
    }

    m_mappedPtr = m_device->MapBuffer(m_stagingBuffer);
    if (!m_mappedPtr) {
        UZ_LOG(Renderer, Error, "StagingBatch::Init: failed to map staging buffer");
        m_device->DestroyBuffer(m_stagingBuffer);
        m_stagingBuffer = RHIBufferHandle::Invalid();
        m_device = nullptr;
        return false;
    }

    m_stagingOffset = 0;
    m_batchActive = false;
    m_entries.clear();

    UZ_LOG(Renderer, Info, "StagingBatch initialized: staging={:.1f} MB",
           static_cast<double>(maxStagingBytes) / (1024.0 * 1024.0));

    return true;
}

void StagingBatch::Shutdown() {
    UZ_PROFILE_FUNCTION();

    if (m_device && m_stagingBuffer.IsValid()) {
        m_device->UnmapBuffer(m_stagingBuffer);
        m_device->DestroyBuffer(m_stagingBuffer);
    }

    m_device = nullptr;
    m_stagingBuffer = RHIBufferHandle::Invalid();
    m_mappedPtr = nullptr;
    m_maxStagingBytes = 0;
    m_stagingOffset = 0;
    m_batchActive = false;
    m_entries.clear();

    UZ_LOG(Renderer, Info, "StagingBatch shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void StagingBatch::BeginBatch() {
    m_entries.clear();
    m_stagingOffset = 0;
    m_batchActive = true;
}

bool StagingBatch::AddBufferUpload(RHIBufferHandle dest, const void* srcData,
                                    uint64_t sizeBytes, uint64_t destOffset) {
    if (!m_batchActive) {
        UZ_LOG(Renderer, Error, "StagingBatch::AddBufferUpload: no active batch (call BeginBatch first)");
        return false;
    }

    if (!srcData || sizeBytes == 0) {
        UZ_LOG(Renderer, Error, "StagingBatch::AddBufferUpload: invalid srcData or sizeBytes=0");
        return false;
    }

    const uint64_t alignedSize = AlignTo256(sizeBytes);
    if (m_stagingOffset + alignedSize > m_maxStagingBytes) {
        UZ_LOG(Renderer, Warning,
               "StagingBatch::AddBufferUpload: staging buffer full "
               "(need {} bytes, have {} remaining)",
               alignedSize, m_maxStagingBytes - m_stagingOffset);
        return false;
    }

    // Copy source data into the staging buffer
    auto* dst = static_cast<uint8_t*>(m_mappedPtr) + m_stagingOffset;
    std::memcpy(dst, srcData, sizeBytes);

    StagingUploadEntry entry{};
    entry.destBuffer = dest;
    entry.srcOffsetInStaging = m_stagingOffset;
    entry.destOffset = destOffset;
    entry.sizeBytes = sizeBytes;
    entry.isTexture = false;

    m_entries.push_back(entry);
    m_stagingOffset += alignedSize;

    return true;
}

bool StagingBatch::AddTextureUpload(RHITextureHandle dest, const void* srcData,
                                     uint64_t sizeBytes, uint32_t subresource) {
    if (!m_batchActive) {
        UZ_LOG(Renderer, Error, "StagingBatch::AddTextureUpload: no active batch (call BeginBatch first)");
        return false;
    }

    if (!srcData || sizeBytes == 0) {
        UZ_LOG(Renderer, Error, "StagingBatch::AddTextureUpload: invalid srcData or sizeBytes=0");
        return false;
    }

    const uint64_t alignedSize = AlignTo256(sizeBytes);
    if (m_stagingOffset + alignedSize > m_maxStagingBytes) {
        UZ_LOG(Renderer, Warning,
               "StagingBatch::AddTextureUpload: staging buffer full "
               "(need {} bytes, have {} remaining)",
               alignedSize, m_maxStagingBytes - m_stagingOffset);
        return false;
    }

    auto* dst = static_cast<uint8_t*>(m_mappedPtr) + m_stagingOffset;
    std::memcpy(dst, srcData, sizeBytes);

    StagingUploadEntry entry{};
    entry.destTexture = dest;
    entry.srcOffsetInStaging = m_stagingOffset;
    entry.sizeBytes = sizeBytes;
    entry.subresource = subresource;
    entry.isTexture = true;

    m_entries.push_back(entry);
    m_stagingOffset += alignedSize;

    return true;
}

uint64_t StagingBatch::SubmitBatch() {
    UZ_PROFILE_FUNCTION();

    m_batchActive = false;

    if (m_entries.empty()) {
        return 0;
    }

    // Create a copy command list for batch submission
    IRHICommandList* copyList = m_device->CreateCommandList(RHIQueueType::Copy);
    if (!copyList) {
        UZ_LOG(Renderer, Error, "StagingBatch::SubmitBatch: failed to create copy command list");
        m_entries.clear();
        return 0;
    }

    copyList->Begin();

    for (const auto& entry : m_entries) {
        if (entry.isTexture) {
            // Texture uploads use CopyBufferToTexture.
            // Row pitch is unknown here without texture desc query — pass sizeBytes
            // as row pitch (correct for single-row textures, approximate otherwise).
            // Full row-pitch calculation requires texture width * bytesPerTexel.
            copyList->CopyBufferToTexture(
                m_stagingBuffer, entry.srcOffsetInStaging,
                static_cast<uint32_t>(entry.sizeBytes),  // srcRowPitch approximation
                entry.destTexture, entry.subresource, 0);
        } else {
            copyList->CopyBuffer(
                m_stagingBuffer, entry.srcOffsetInStaging,
                entry.destBuffer, entry.destOffset,
                entry.sizeBytes);
        }
    }

    copyList->End();
    copyList->Execute();

    const uint64_t fenceValue = m_device->SignalCopyQueue();

    m_device->DestroyCommandList(copyList);

    UZ_STAT_INCREMENT(IO, StagingBatchSubmissions);

    UZ_LOG(Renderer, Trace,
           "StagingBatch: submitted {} uploads ({:.1f} KB) → fence={}",
           m_entries.size(),
           static_cast<double>(m_stagingOffset) / 1024.0,
           fenceValue);

    m_entries.clear();
    return fenceValue;
}

} // namespace uz
