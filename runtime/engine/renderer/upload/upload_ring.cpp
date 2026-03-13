/// Upload ring buffer implementation.
///
/// Single persistent upload-heap buffer, partitioned into N equal regions
/// (one per in-flight frame). Each frame bump-allocates within its region.

#include "upload_ring.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

UZ_DEFINE_STAT(IO, UploadRingBytesUsed);
UZ_DEFINE_STAT(IO, UploadRingAllocations);

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool UploadRing::Init(IRHIDevice* device, uint32_t frameCount, uint32_t bytesPerFrame) {
    UZ_PROFILE_FUNCTION();

    if (!device || frameCount == 0 || bytesPerFrame == 0) {
        UZ_LOG(RHI, Error, "UploadRing::Init: invalid arguments (device={}, frameCount={}, bytesPerFrame={})",
               static_cast<const void*>(device), frameCount, bytesPerFrame);
        return false;
    }

    m_device = device;
    m_frameCount = frameCount;
    m_bytesPerFrame = bytesPerFrame;
    m_totalSize = frameCount * bytesPerFrame;

    RHIBufferDesc desc{};
    desc.sizeBytes = m_totalSize;
    desc.usage = RHIBufferUsage::CopySrc;
    desc.memory = RHIMemoryType::Upload;
    desc.debugName = "UploadRingBuffer";

    m_buffer = m_device->CreateBuffer(desc);
    if (!m_buffer.IsValid()) {
        UZ_LOG(RHI, Error, "UploadRing::Init: failed to create backing buffer ({} bytes)", m_totalSize);
        return false;
    }

    m_mappedBase = static_cast<uint8_t*>(m_device->MapBuffer(m_buffer));
    if (!m_mappedBase) {
        UZ_LOG(RHI, Error, "UploadRing::Init: failed to map backing buffer");
        m_device->DestroyBuffer(m_buffer);
        m_buffer = {};
        return false;
    }

    UZ_LOG(RHI, Info, "UploadRing: initialized {} frames x {} bytes = {} bytes total",
           frameCount, bytesPerFrame, m_totalSize);
    return true;
}

void UploadRing::Shutdown() {
    if (!m_buffer.IsValid()) return;

    m_device->UnmapBuffer(m_buffer);
    m_device->DestroyBuffer(m_buffer);
    m_buffer = {};
    m_mappedBase = nullptr;
    m_bytesPerFrame = 0;
    m_frameCount = 0;
    m_currentFrame = 0;
    m_frameOffset = 0;
    m_totalSize = 0;
    m_device = nullptr;

    UZ_LOG(RHI, Info, "UploadRing: shutdown");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Operations
// ─────────────────────────────────────────────────────────────────────────────

void UploadRing::BeginFrame(uint32_t frameIndex) {
    m_currentFrame = frameIndex % m_frameCount;
    m_frameOffset = 0;
}

UploadRing::Allocation UploadRing::Allocate(uint32_t sizeBytes, uint32_t alignment) {
    UZ_PROFILE_FUNCTION();

    if (sizeBytes == 0) {
        UZ_LOG(RHI, Warning, "UploadRing::Allocate: sizeBytes is 0");
        return {};
    }

    // Align the current offset up
    const uint32_t alignMask = alignment - 1;
    const uint32_t alignedOffset = (m_frameOffset + alignMask) & ~alignMask;

    // Check if allocation fits within this frame's budget
    if (alignedOffset + sizeBytes > m_bytesPerFrame) {
        UZ_LOG(RHI, Error,
               "UploadRing::Allocate: frame budget exhausted (requested={}, aligned_offset={}, budget={})",
               sizeBytes, alignedOffset, m_bytesPerFrame);
        return {};
    }

    // Compute absolute offset into the full buffer
    const uint32_t frameBase = m_currentFrame * m_bytesPerFrame;
    const uint32_t absoluteOffset = frameBase + alignedOffset;

    Allocation alloc{};
    alloc.cpuPtr = m_mappedBase + absoluteOffset;
    alloc.bufferHandle = m_buffer;
    alloc.offsetBytes = absoluteOffset;

    // Advance the bump pointer past this allocation
    m_frameOffset = alignedOffset + sizeBytes;

    UZ_STAT_INCREMENT(IO, UploadRingAllocations);
    UZ_STAT_INCREMENT(IO, UploadRingBytesUsed, static_cast<int64_t>(sizeBytes));

    return alloc;
}

uint32_t UploadRing::GetRemainingBytes() const {
    return m_bytesPerFrame - m_frameOffset;
}

} // namespace uz
