/// CBRingAllocator implementation.
///
/// Suballocates 256-byte-aligned constant buffer slots from a single large
/// upload-heap buffer. Triple-buffered to avoid GPU/CPU synchronization stalls.

#include "constant_buffers.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

CBRingAllocator::~CBRingAllocator() {
    Shutdown();
}

bool CBRingAllocator::Init(IRHIDevice* device, uint32_t framesInFlight, uint64_t perFrameBytes) {
    UZ_PROFILE_FUNCTION();
    if (!device || framesInFlight == 0 || perFrameBytes == 0) {
        UZ_LOG(Renderer, Error, "CBRingAllocator::Init: invalid arguments");
        return false;
    }

    m_device = device;
    m_framesInFlight = framesInFlight;
    m_perFrameBytes = (perFrameBytes + 255) & ~255ULL;

    uint64_t totalBytes = m_perFrameBytes * framesInFlight;

    RHIBufferDesc desc{};
    desc.sizeBytes = totalBytes;
    desc.usage = RHIBufferUsage::ConstantBuffer;
    desc.memory = RHIMemoryType::Upload;
    desc.debugName = "CBRingBuffer";

    m_buffer = device->CreateBuffer(desc);
    if (!m_buffer.IsValid()) {
        UZ_LOG(Renderer, Error, "CBRingAllocator: failed to create backing buffer ({} bytes)", totalBytes);
        return false;
    }

    m_mappedBase = static_cast<uint8_t*>(device->MapBuffer(m_buffer));
    if (!m_mappedBase) {
        UZ_LOG(Renderer, Error, "CBRingAllocator: failed to map backing buffer");
        device->DestroyBuffer(m_buffer);
        m_buffer = {};
        return false;
    }

    m_gpuBaseAddr = device->GetBufferGPUAddress(m_buffer);
    // DX11 returns 0 for GPU address — that's OK, we use handle-based binding instead

    UZ_LOG(Renderer, Info, "CBRingAllocator: {} frames x {} bytes = {} total",
           framesInFlight, m_perFrameBytes, totalBytes);
    return true;
}

void CBRingAllocator::Shutdown() {
    if (m_device && m_buffer.IsValid()) {
        m_device->DestroyBuffer(m_buffer);
    }
    m_buffer = {};
    m_mappedBase = nullptr;
    m_gpuBaseAddr = 0;
    m_device = nullptr;
}

void CBRingAllocator::BeginFrame(uint32_t frameIndex) {
    m_currentFrame = frameIndex % m_framesInFlight;
    m_currentOffset = 0;
}

CBAllocation CBRingAllocator::Allocate(uint64_t sizeBytes) {
    UZ_PROFILE_FUNCTION();
    uint64_t alignedSize = (sizeBytes + 255) & ~255ULL;
    if (m_currentOffset + alignedSize > m_perFrameBytes) {
        UZ_LOG(Renderer, Error, "CBRingAllocator: frame region exhausted ({}/{} bytes)",
               m_currentOffset + alignedSize, m_perFrameBytes);
        return {nullptr, 0};
    }

    uint64_t frameBase = m_currentFrame * m_perFrameBytes;
    uint64_t offset = frameBase + m_currentOffset;

    CBAllocation alloc;
    alloc.cpuPtr = m_mappedBase + offset;
    alloc.gpuAddr = m_gpuBaseAddr + offset;
    alloc.bufferHandle = m_buffer;
    alloc.offsetBytes = offset;

    m_currentOffset += alignedSize;
    return alloc;
}

} // namespace uz
