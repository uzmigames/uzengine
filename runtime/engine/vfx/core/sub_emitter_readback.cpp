/// GPU->CPU readback for sub-emitter particle trigger flags.
///
/// Implements triple-buffered readback of compacted triggered particle data.
/// See sub_emitter_readback.h for full architecture documentation.

#include "sub_emitter_readback.h"

#include "engine/rhi/rhi.h"
#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Compact compute shader — HLSL source
// ---------------------------------------------------------------------------

/// Scans SubEmitterFlags[], gathers triggered particle state into a compact
/// structured buffer via InterlockedAdd, capped at cbMaxReadback entries.
///
/// Thread group size of 64 balances occupancy across all GPU vendors.
/// Each thread handles one particle index; threads beyond cbAliveCount exit early.
static constexpr const char* kCompactShaderCode = R"hlsl(
// --- SubEmitterCompact CS ---
// Scans per-particle flags and appends triggered particle state into a
// compact structured buffer for CPU readback. Capped at cbMaxReadback entries.

struct SubEmitterGPUPayload {
    float3 position; float _pad0;
    float3 velocity; float age;
    float4 color;
};

Buffer<uint>                             SubEmitterFlags  : register(t0);
Buffer<float3>                           Positions        : register(t1);
Buffer<float3>                           Velocities       : register(t2);
Buffer<float4>                           Colors           : register(t3);
Buffer<float>                            Ages             : register(t4);

RWStructuredBuffer<SubEmitterGPUPayload> CompactPayloads  : register(u0);
RWBuffer<uint>                           CompactCount     : register(u1);

cbuffer CompactCB : register(b0) {
    uint cbAliveCount;
    uint cbMaxReadback;
    uint2 _cbPad;
};

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= cbAliveCount) return;

    uint flag = SubEmitterFlags[idx];
    if (flag == 0u) return;

    // Atomically reserve a slot in the compact buffer.
    uint slot;
    InterlockedAdd(CompactCount[0], 1u, slot);

    // Cap: discard if we have already reached the readback limit.
    if (slot >= cbMaxReadback) return;

    SubEmitterGPUPayload payload;
    payload.position = Positions[idx];
    payload._pad0    = 0.0f;
    payload.velocity = Velocities[idx];
    payload.age      = Ages[idx];
    payload.color    = Colors[idx];

    CompactPayloads[slot] = payload;
}
)hlsl";

// ---------------------------------------------------------------------------
// GetCompactShaderCode
// ---------------------------------------------------------------------------

std::string_view SubEmitterReadbackBuffer::GetCompactShaderCode() noexcept {
    return kCompactShaderCode;
}

// ---------------------------------------------------------------------------
// Create
// ---------------------------------------------------------------------------

bool SubEmitterReadbackBuffer::Create(IRHIDevice* device, uint32_t capacity)
{
    UZ_CHECKF(device != nullptr,
              "SubEmitterReadbackBuffer::Create: device is null");
    UZ_CHECKF(capacity > 0,
              "SubEmitterReadbackBuffer::Create: capacity must be > 0");
    UZ_CHECKF(!IsValid(),
              "SubEmitterReadbackBuffer::Create: already allocated, call Destroy() first");

    particleCapacity = capacity;
    currentWriteIndex = 0;
    framesSinceCreate = 0;

    // --- Flag buffer (Default heap, UAV + CopySrc) --------------------------
    // One uint32 per particle. The simulation CS writes flags here.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(capacity) * sizeof(uint32_t);
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::StructuredBuffer
                       | RHIBufferUsage::UnorderedAccess
                       | RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_SubEmitterFlags";

        flagBuffer = device->CreateBuffer(desc);
        if (!flagBuffer.IsValid()) {
            UZ_LOG(VFX, Error,
                   "SubEmitterReadbackBuffer: failed to allocate flag buffer "
                   "({} particles, {} bytes)",
                   capacity, desc.sizeBytes);
            Destroy(device);
            return false;
        }
    }

    // --- Compact payload buffer (Default heap, UAV + CopySrc) ---------------
    // Dense array of triggered particle data, max kMaxReadbackParticles entries.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(kMaxReadbackParticles)
                       * sizeof(SubEmitterGPUPayload);
        desc.stride    = sizeof(SubEmitterGPUPayload);
        desc.usage     = RHIBufferUsage::StructuredBuffer
                       | RHIBufferUsage::UnorderedAccess
                       | RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_SubEmitterCompactPayloads";

        compactBuffer = device->CreateBuffer(desc);
        if (!compactBuffer.IsValid()) {
            UZ_LOG(VFX, Error,
                   "SubEmitterReadbackBuffer: failed to allocate compact payload buffer "
                   "({} entries, {} bytes)",
                   kMaxReadbackParticles, desc.sizeBytes);
            Destroy(device);
            return false;
        }
    }

    // --- Compact count buffer (Default heap, UAV + CopySrc) -----------------
    // Single uint32 atomic counter, incremented by InterlockedAdd in the compact CS.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::StructuredBuffer
                       | RHIBufferUsage::UnorderedAccess
                       | RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "VFX_SubEmitterCompactCount";

        compactCount = device->CreateBuffer(desc);
        if (!compactCount.IsValid()) {
            UZ_LOG(VFX, Error,
                   "SubEmitterReadbackBuffer: failed to allocate compact count buffer");
            Destroy(device);
            return false;
        }
    }

    // --- Readback payload buffers (Readback heap, CopyDst) ------------------
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(kMaxReadbackParticles)
                       * sizeof(SubEmitterGPUPayload);
        desc.stride    = sizeof(SubEmitterGPUPayload);
        desc.usage     = RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Readback;
        desc.debugName = "VFX_SubEmitterReadbackPayload";

        readbackPayloadBuffers[i] = device->CreateBuffer(desc);
        if (!readbackPayloadBuffers[i].IsValid()) {
            UZ_LOG(VFX, Error,
                   "SubEmitterReadbackBuffer: failed to allocate readback payload buffer "
                   "[slot {}]", i);
            Destroy(device);
            return false;
        }
    }

    // --- Readback count buffers (Readback heap, CopyDst) --------------------
    for (uint32_t i = 0; i < kBufferCount; ++i) {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Readback;
        desc.debugName = "VFX_SubEmitterReadbackCount";

        readbackCountBuffers[i] = device->CreateBuffer(desc);
        if (!readbackCountBuffers[i].IsValid()) {
            UZ_LOG(VFX, Error,
                   "SubEmitterReadbackBuffer: failed to allocate readback count buffer "
                   "[slot {}]", i);
            Destroy(device);
            return false;
        }
    }

    const uint64_t flagBytes     = static_cast<uint64_t>(capacity) * sizeof(uint32_t);
    const uint64_t compactBytes  = static_cast<uint64_t>(kMaxReadbackParticles)
                                 * sizeof(SubEmitterGPUPayload);
    const uint64_t readbackBytes = compactBytes * kBufferCount
                                 + sizeof(uint32_t) * kBufferCount;
    const uint64_t totalBytes    = flagBytes + compactBytes + sizeof(uint32_t)
                                 + readbackBytes;

    UZ_LOG(VFX, Info,
           "SubEmitterReadbackBuffer created: capacity={}, maxReadback={}, "
           "totalGPUMemory={} bytes (flags={}, compact={}, readback={})",
           capacity, kMaxReadbackParticles, totalBytes,
           flagBytes, compactBytes + sizeof(uint32_t), readbackBytes);

    return true;
}

// ---------------------------------------------------------------------------
// Destroy
// ---------------------------------------------------------------------------

void SubEmitterReadbackBuffer::Destroy(IRHIDevice* device)
{
    if (!device) return;

    if (flagBuffer.IsValid()) {
        device->DestroyBuffer(flagBuffer);
        flagBuffer = RHIBufferHandle::Invalid();
    }

    if (compactBuffer.IsValid()) {
        device->DestroyBuffer(compactBuffer);
        compactBuffer = RHIBufferHandle::Invalid();
    }

    if (compactCount.IsValid()) {
        device->DestroyBuffer(compactCount);
        compactCount = RHIBufferHandle::Invalid();
    }

    for (uint32_t i = 0; i < kBufferCount; ++i) {
        if (readbackPayloadBuffers[i].IsValid()) {
            device->DestroyBuffer(readbackPayloadBuffers[i]);
            readbackPayloadBuffers[i] = RHIBufferHandle::Invalid();
        }
        if (readbackCountBuffers[i].IsValid()) {
            device->DestroyBuffer(readbackCountBuffers[i]);
            readbackCountBuffers[i] = RHIBufferHandle::Invalid();
        }
    }

    particleCapacity  = 0;
    currentWriteIndex = 0;
    framesSinceCreate = 0;
}

// ---------------------------------------------------------------------------
// IsValid
// ---------------------------------------------------------------------------

bool SubEmitterReadbackBuffer::IsValid() const noexcept
{
    return flagBuffer.IsValid()
        && compactBuffer.IsValid()
        && compactCount.IsValid()
        && readbackPayloadBuffers[0].IsValid()
        && readbackCountBuffers[0].IsValid();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

RHIBufferHandle SubEmitterReadbackBuffer::GetFlagBuffer() const noexcept
{
    return flagBuffer;
}

RHIBufferHandle SubEmitterReadbackBuffer::GetCompactBuffer() const noexcept
{
    return compactBuffer;
}

RHIBufferHandle SubEmitterReadbackBuffer::GetCompactCount() const noexcept
{
    return compactCount;
}

// ---------------------------------------------------------------------------
// GetReadIndex
// ---------------------------------------------------------------------------

uint32_t SubEmitterReadbackBuffer::GetReadIndex() const noexcept
{
    // The read index is the slot written 2 frames ago. With triple buffering
    // (indices 0, 1, 2) and a rotating write index, the oldest completed
    // slot is always (currentWriteIndex + 1) % kBufferCount.
    //
    // Example rotation for write index progression 0->1->2->0->1:
    //   writeIndex=0 -> readIndex=1 (written 2 frames ago when write was at 1)
    //   writeIndex=1 -> readIndex=2
    //   writeIndex=2 -> readIndex=0
    return (currentWriteIndex + 1) % kBufferCount;
}

// ---------------------------------------------------------------------------
// IssueCopy
// ---------------------------------------------------------------------------

void SubEmitterReadbackBuffer::IssueCopy(IRHICommandList* cmdList)
{
    UZ_CHECKF(cmdList != nullptr,
              "SubEmitterReadbackBuffer::IssueCopy: cmdList is null");
    UZ_CHECKF(IsValid(),
              "SubEmitterReadbackBuffer::IssueCopy: buffer not allocated");

    const uint32_t slot = currentWriteIndex;

    // Copy compact payload buffer -> readback payload buffer for this slot.
    const uint64_t payloadBytes = static_cast<uint64_t>(kMaxReadbackParticles)
                                * sizeof(SubEmitterGPUPayload);
    cmdList->CopyBuffer(compactBuffer, 0,
                        readbackPayloadBuffers[slot], 0,
                        payloadBytes);

    // Copy compact count -> readback count buffer for this slot.
    cmdList->CopyBuffer(compactCount, 0,
                        readbackCountBuffers[slot], 0,
                        sizeof(uint32_t));

    // Advance the write index for the next frame.
    currentWriteIndex = (currentWriteIndex + 1) % kBufferCount;

    // Track frames elapsed so GatherTriggered() knows when valid data exists.
    if (framesSinceCreate < kBufferCount) {
        ++framesSinceCreate;
    }
}

// ---------------------------------------------------------------------------
// GatherTriggered
// ---------------------------------------------------------------------------

SubEmitterReadback SubEmitterReadbackBuffer::GatherTriggered(IRHIDevice* device) const
{
    UZ_CHECKF(device != nullptr,
              "SubEmitterReadbackBuffer::GatherTriggered: device is null");
    UZ_CHECKF(IsValid(),
              "SubEmitterReadbackBuffer::GatherTriggered: buffer not allocated");

    SubEmitterReadback result;

    // During the first kBufferCount frames, the readback buffers do not yet
    // contain valid GPU-written data. Return empty to avoid reading garbage.
    if (framesSinceCreate < kBufferCount) {
        return result;
    }

    const uint32_t readSlot = GetReadIndex();

    // --- Read the triggered particle count ----------------------------------
    void* countMapped = device->MapBuffer(readbackCountBuffers[readSlot]);
    if (!countMapped) {
        UZ_LOG(VFX, Error,
               "SubEmitterReadbackBuffer::GatherTriggered: "
               "MapBuffer failed for count buffer [slot {}]",
               readSlot);
        return result;
    }

    uint32_t triggeredCount = 0;
    std::memcpy(&triggeredCount, countMapped, sizeof(uint32_t));
    device->UnmapBuffer(readbackCountBuffers[readSlot]);

    // Clamp to the maximum in case of GPU counter overflow (should not happen
    // with the InterlockedAdd cap, but defensive programming).
    triggeredCount = std::min(triggeredCount, kMaxReadbackParticles);

    if (triggeredCount == 0) {
        return result;
    }

    // --- Read the triggered particle payloads -------------------------------
    void* payloadMapped = device->MapBuffer(readbackPayloadBuffers[readSlot]);
    if (!payloadMapped) {
        UZ_LOG(VFX, Error,
               "SubEmitterReadbackBuffer::GatherTriggered: "
               "MapBuffer failed for payload buffer [slot {}]",
               readSlot);
        return result;
    }

    const auto* gpuPayloads = static_cast<const SubEmitterGPUPayload*>(payloadMapped);

    result.flaggedParticles.reserve(triggeredCount);

    for (uint32_t i = 0; i < triggeredCount; ++i) {
        const SubEmitterGPUPayload& src = gpuPayloads[i];

        EventPayload payload;
        payload.position = Vec3{src.posX, src.posY, src.posZ};
        payload.velocity = Vec3{src.velX, src.velY, src.velZ};
        payload.color    = Vec4{src.colorR, src.colorG, src.colorB, src.colorA};
        payload.age      = src.age;

        result.flaggedParticles.push_back(payload);
    }

    device->UnmapBuffer(readbackPayloadBuffers[readSlot]);

    UZ_LOG(VFX, Debug,
           "SubEmitterReadbackBuffer::GatherTriggered: "
           "read {} triggered particles from readback slot {}",
           triggeredCount, readSlot);

    return result;
}

} // namespace uz
