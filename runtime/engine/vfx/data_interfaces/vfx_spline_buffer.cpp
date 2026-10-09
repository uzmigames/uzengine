/// VFX Spline Data Buffer — implementation.

#include "vfx_spline_buffer.h"
#include "vfx_spline_di.h"

#include "engine/rhi/rhi.h"
#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplineBuffer::Create
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSplineBuffer::Create(IRHIDevice* device, uint32_t requestedCapacity)
{
    UZ_CHECKF(device != nullptr, "VfxSplineBuffer::Create: device is null");
    UZ_CHECKF(!IsValid(),
              "VfxSplineBuffer::Create: already allocated, call Destroy() first");

    // Clamp the capacity to a sane range. < 2 makes no spline; the upper
    // bound is documented in the header — anything beyond that should
    // switch to a binary-search HLSL helper rather than grow this buffer.
    capacity = std::clamp(requestedCapacity, 2u, kVfxSplineMaxResolution);

    RHIBufferDesc desc{};
    desc.sizeBytes = static_cast<uint64_t>(capacity) * sizeof(VfxSplinePoint);
    desc.stride    = sizeof(VfxSplinePoint);
    desc.usage     = RHIBufferUsage::StructuredBuffer;
    desc.memory    = RHIMemoryType::Upload;
    desc.debugName = "VFX_SplineBuffer";

    buffer = device->CreateBuffer(desc);
    if (!buffer.IsValid()) {
        UZ_LOG(VFX, Error,
               "VfxSplineBuffer: failed to allocate GPU buffer "
               "(capacity={}, bytes={})",
               capacity, desc.sizeBytes);
        capacity = 0;
        return false;
    }

    pointCount  = 0;
    totalLength = 0.0f;

    UZ_LOG(VFX, Info,
           "VfxSplineBuffer created: capacity={}, bytes={}",
           capacity, desc.sizeBytes);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplineBuffer::Destroy
// ─────────────────────────────────────────────────────────────────────────────

void VfxSplineBuffer::Destroy(IRHIDevice* device)
{
    if (!device) return;

    if (buffer.IsValid()) {
        device->DestroyBuffer(buffer);
        buffer = RHIBufferHandle::Invalid();
    }

    capacity    = 0;
    pointCount  = 0;
    totalLength = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplineBuffer::IsValid
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSplineBuffer::IsValid() const noexcept
{
    return buffer.IsValid() && capacity > 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplineBuffer::GetBindlessSrvIndex
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxSplineBuffer::GetBindlessSrvIndex(IRHIDevice* device) const noexcept
{
    if (!IsValid() || !device) return 0xFFFFFFFFu;
    return device->GetBufferSRVIndex(buffer);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxSplineBuffer::RebuildAndUpload
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSplineBuffer::RebuildAndUpload(const VfxSpline& spline,
                                        IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr,
              "VfxSplineBuffer::RebuildAndUpload: device is null");

    if (!IsValid()) {
        UZ_LOG(VFX, Error,
               "VfxSplineBuffer::RebuildAndUpload: buffer not created");
        return false;
    }

    // Empty / degenerate spline — bake nothing, mark count zero so the
    // shader skips its sample reads.
    if (spline.controlPoints.size() < 2) {
        pointCount  = 0;
        totalLength = 0.0f;

        // Zero the buffer so stale GPU reads don't pull last frame's data
        // when the author shrinks a spline below the minimum control count.
        if (void* mapped = device->MapBuffer(buffer)) {
            std::memset(mapped, 0,
                        static_cast<size_t>(capacity) * sizeof(VfxSplinePoint));
            device->UnmapBuffer(buffer);
        }
        return true;
    }

    // Force a fresh arc-length build so the bake reflects the current
    // control points. `BuildArcLengthTable` is mutable so we can call it
    // through the const reference.
    spline.BuildArcLengthTable();

    // Choose the bake resolution. The arc-length table holds
    // `arcLengthSamples + 1` entries (one extra for the t=1 bookend).
    // Cap by buffer capacity so we don't overrun the GPU allocation.
    const uint32_t numSamples = std::min<uint32_t>(
        static_cast<uint32_t>(spline.arcLengthTable.size()), capacity);

    // Bake every sample as a `VfxSplinePoint`. The arc-length entry is
    // the cumulative distance from sample[0] (already what the LUT
    // stores). The position + tangent come from the parameter-uniform
    // `Sample(t)` rather than `SampleUniform`, because the LUT itself
    // was filled by parameter-uniform sampling.
    void* mapped = device->MapBuffer(buffer);
    if (!mapped) {
        UZ_LOG(VFX, Error,
               "VfxSplineBuffer::RebuildAndUpload: MapBuffer returned null");
        return false;
    }

    auto* dst = static_cast<VfxSplinePoint*>(mapped);
    const float invDenom = (numSamples > 1)
        ? (1.0f / static_cast<float>(numSamples - 1))
        : 0.0f;

    for (uint32_t i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) * invDenom;
        const VfxSplineSample sample = spline.Sample(t);

        VfxSplinePoint& p = dst[i];
        p.positionX = sample.position.x;
        p.positionY = sample.position.y;
        p.positionZ = sample.position.z;
        p.arcLength = (i < spline.arcLengthTable.size())
                    ? spline.arcLengthTable[i] : spline.totalLength;
        p.tangentX  = sample.tangent.x;
        p.tangentY  = sample.tangent.y;
        p.tangentZ  = sample.tangent.z;
        p._pad      = 0.0f;
    }

    // Zero the trailing slots so the shader never sees stale data when
    // the spline shrinks between frames.
    if (numSamples < capacity) {
        std::memset(dst + numSamples, 0,
                    static_cast<size_t>(capacity - numSamples)
                    * sizeof(VfxSplinePoint));
    }

    device->UnmapBuffer(buffer);

    pointCount  = numSamples;
    totalLength = spline.totalLength;
    return true;
}

} // namespace uz
