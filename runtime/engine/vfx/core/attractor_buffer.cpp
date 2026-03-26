/// VFX Attractor Buffer — GPU structured buffer for per-frame attractor parameters.

#include "attractor_buffer.h"

#include "engine/rhi/rhi.h"
#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// AttractorBuffer::Create
// ---------------------------------------------------------------------------

bool AttractorBuffer::Create(IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr, "AttractorBuffer::Create: device is null");
    UZ_CHECKF(!IsValid(), "AttractorBuffer::Create: already allocated, call Destroy() first");

    RHIBufferDesc desc{};
    desc.sizeBytes = static_cast<uint64_t>(kMaxAttractors) * sizeof(AttractorData);
    desc.stride    = sizeof(AttractorData);
    desc.usage     = RHIBufferUsage::StructuredBuffer;
    desc.memory    = RHIMemoryType::Upload;
    desc.debugName = "VFX_AttractorBuffer";

    buffer = device->CreateBuffer(desc);
    if (!buffer.IsValid()) {
        UZ_LOG(VFX, Error,
               "AttractorBuffer: failed to allocate GPU buffer "
               "({}x{} = {} bytes)",
               kMaxAttractors, sizeof(AttractorData),
               static_cast<uint64_t>(kMaxAttractors) * sizeof(AttractorData));
        return false;
    }

    // Zero-initialise the CPU mirror so that unused slots never hold stale data.
    std::memset(attractors, 0, sizeof(attractors));
    activeCount = 0;

    UZ_LOG(VFX, Info,
           "AttractorBuffer created: maxAttractors={}, bufferBytes={}",
           kMaxAttractors,
           static_cast<uint64_t>(kMaxAttractors) * sizeof(AttractorData));
    return true;
}

// ---------------------------------------------------------------------------
// AttractorBuffer::Destroy
// ---------------------------------------------------------------------------

void AttractorBuffer::Destroy(IRHIDevice* device)
{
    if (!device) return;

    if (buffer.IsValid()) {
        device->DestroyBuffer(buffer);
        buffer = RHIBufferHandle::Invalid();
    }

    std::memset(attractors, 0, sizeof(attractors));
    activeCount = 0;
}

// ---------------------------------------------------------------------------
// AttractorBuffer::IsValid
// ---------------------------------------------------------------------------

bool AttractorBuffer::IsValid() const noexcept
{
    return buffer.IsValid();
}

// ---------------------------------------------------------------------------
// AttractorBuffer::AddAttractor
// ---------------------------------------------------------------------------

bool AttractorBuffer::AddAttractor(const AttractorData& data)
{
    if (activeCount >= kMaxAttractors) {
        UZ_LOG(VFX, Warning,
               "AttractorBuffer::AddAttractor: capacity exhausted (max={}), "
               "attractor discarded",
               kMaxAttractors);
        return false;
    }

    attractors[activeCount] = data;
    ++activeCount;
    return true;
}

// ---------------------------------------------------------------------------
// AttractorBuffer::Clear
// ---------------------------------------------------------------------------

void AttractorBuffer::Clear() noexcept
{
    activeCount = 0;
}

// ---------------------------------------------------------------------------
// AttractorBuffer::UploadToGPU
// ---------------------------------------------------------------------------

void AttractorBuffer::UploadToGPU(IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr, "AttractorBuffer::UploadToGPU: device is null");
    UZ_CHECKF(IsValid(), "AttractorBuffer::UploadToGPU: buffer not allocated");

    void* mapped = device->MapBuffer(buffer);
    if (!mapped) {
        UZ_LOG(VFX, Error, "AttractorBuffer::UploadToGPU: MapBuffer returned null");
        return;
    }

    // Copy the active attractors then zero the remaining slots.
    // This ensures the shader never reads stale attractor data from a previous
    // frame when activeCount has decreased.
    const uint64_t activeBytes = static_cast<uint64_t>(activeCount) * sizeof(AttractorData);
    const uint64_t totalBytes  = static_cast<uint64_t>(kMaxAttractors) * sizeof(AttractorData);

    std::memcpy(mapped, attractors, activeBytes);

    if (activeBytes < totalBytes) {
        std::memset(static_cast<uint8_t*>(mapped) + activeBytes, 0,
                    totalBytes - activeBytes);
    }

    device->UnmapBuffer(buffer);
}

} // namespace uz
