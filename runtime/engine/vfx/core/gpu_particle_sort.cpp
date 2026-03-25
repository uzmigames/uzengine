/// GPU Particle Sort — Allocation and dispatch of bitonic merge sort buffers.
///
/// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort

#include "gpu_particle_sort.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/memory/cb_ring_allocator.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"
#include "engine/profiling/profiler.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ── Internal helpers ────────────────────────────────────────────────────────

namespace {

/// Root parameter slot for the sort constant buffer (b0).
constexpr uint32_t kSlotSortCB = 0;

/// Creates a structured buffer with UAV access for sort data.
///
/// # Parameters
/// - `device`: RHI device for allocation.
/// - `elementCount`: Number of uint32 elements.
/// - `debugName`: GPU debug label (visible in PIX/RenderDoc).
///
/// # Returns
/// Valid handle on success, invalid handle on failure.
RHIBufferHandle CreateSortBuffer(IRHIDevice* device, uint32_t elementCount,
                                  const char* debugName)
{
    RHIBufferDesc desc{};
    desc.sizeBytes = static_cast<uint64_t>(elementCount) * sizeof(uint32_t);
    desc.stride = sizeof(uint32_t);
    desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
    desc.memory = RHIMemoryType::Default;
    desc.debugName = debugName;
    return device->CreateBuffer(desc);
}

/// Calculates ceil(count / groupSize). Returns 0 if count is 0.
uint32_t DivRoundUp(uint32_t count, uint32_t groupSize)
{
    if (count == 0) return 0;
    return (count + groupSize - 1) / groupSize;
}

/// Emits a UAV barrier on the sort key and index buffers.
///
/// Ensures writes from one dispatch are visible to the next dispatch that
/// reads/writes the same buffers.
void EmitSortUAVBarriers(IRHICommandList* cmd, const GPUParticleSort& sort)
{
    RHIBarrierDesc barriers[2] = {};

    barriers[0].buffer      = sort.sortKeys;
    barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
    barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;

    barriers[1].buffer      = sort.sortIndices;
    barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
    barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;

    cmd->ResourceBarrier(barriers, 2);
}

/// Fills a VFXSortConstants struct with the given parameters, allocates from
/// the CB ring, uploads the data, and binds it to root parameter slot 0.
///
/// # Parameters
/// - `cmd`: Command list to bind the CBV on.
/// - `cbRing`: CB ring allocator for per-dispatch constant upload.
/// - `cb`: Fully filled VFXSortConstants struct to upload.
///
/// # Returns
/// `true` if the allocation succeeded and the CBV was bound.
/// `false` if the CB ring is full (logs an error).
bool UploadAndBindSortCB(IRHICommandList* cmd, CBRingAllocator& cbRing,
                          const VFXSortConstants& cb)
{
    auto alloc = cbRing.Allocate(sizeof(VFXSortConstants));
    if (!alloc.cpuPtr) {
        UZ_LOG(VFX, Error, "GPUParticleSort: CB ring allocation failed (80 bytes)");
        return false;
    }

    std::memcpy(alloc.cpuPtr, &cb, sizeof(VFXSortConstants));

    cmd->SetRootCBV(kSlotSortCB,
                    alloc.bufferHandle,
                    alloc.offsetBytes,
                    alloc.cpuPtr,
                    alloc.sizeBytes);
    return true;
}

} // namespace

// ── GPUParticleSort::Create ─────────────────────────────────────────────────

bool GPUParticleSort::Create(IRHIDevice* device, uint32_t particleCapacity)
{
    UZ_CHECKF(device != nullptr, "GPUParticleSort::Create: device is null");
    UZ_CHECKF(particleCapacity > 0, "GPUParticleSort::Create: capacity must be > 0");
    UZ_CHECKF(!IsValid(), "GPUParticleSort::Create: already allocated, call Destroy() first");

    capacity = particleCapacity;
    paddedCount = NextPowerOfTwo(particleCapacity);

    sortKeys = CreateSortBuffer(device, paddedCount, "VFX_SortKeys");
    if (!sortKeys.IsValid()) {
        UZ_LOG(VFX, Error, "GPUParticleSort: failed to allocate sort keys buffer: "
               "paddedCount={}", paddedCount);
        Destroy(device);
        return false;
    }

    sortIndices = CreateSortBuffer(device, paddedCount, "VFX_SortIndices");
    if (!sortIndices.IsValid()) {
        UZ_LOG(VFX, Error, "GPUParticleSort: failed to allocate sort indices buffer: "
               "paddedCount={}", paddedCount);
        Destroy(device);
        return false;
    }

    const uint64_t totalBytes = static_cast<uint64_t>(paddedCount) * sizeof(uint32_t) * 2;
    UZ_LOG(VFX, Info, "GPU particle sort created: capacity={}, paddedCount={}, memory={}KB",
           capacity, paddedCount, totalBytes / 1024);
    return true;
}

// ── GPUParticleSort::Destroy ────────────────────────────────────────────────

void GPUParticleSort::Destroy(IRHIDevice* device)
{
    if (!device) return;

    if (sortKeys.IsValid())   device->DestroyBuffer(sortKeys);
    if (sortIndices.IsValid()) device->DestroyBuffer(sortIndices);

    sortKeys = RHIBufferHandle::Invalid();
    sortIndices = RHIBufferHandle::Invalid();
    capacity = 0;
    paddedCount = 0;
}

// ── GPUParticleSort::IsValid ────────────────────────────────────────────────

bool GPUParticleSort::IsValid() const noexcept
{
    return sortKeys.IsValid() && sortIndices.IsValid() && paddedCount > 0;
}

// ── GPUParticleSort::DispatchSort ───────────────────────────────────────────

void GPUParticleSort::DispatchSort(IRHIDevice* device,
                                    IRHICommandList* cmd,
                                    RHIPipelineHandle pipeline,
                                    CBRingAllocator& cbRing,
                                    uint32_t aliveCount,
                                    const float* cameraPos,
                                    const float* cameraForward,
                                    uint32_t positionsSrvIndex)
{
    UZ_CHECKF(device != nullptr, "GPUParticleSort::DispatchSort: device is null");
    UZ_CHECKF(cmd != nullptr, "GPUParticleSort::DispatchSort: cmd is null");
    UZ_CHECKF(pipeline.IsValid(), "GPUParticleSort::DispatchSort: pipeline is invalid");
    UZ_CHECKF(IsValid(), "GPUParticleSort::DispatchSort: sort buffers not allocated");
    UZ_CHECKF(cameraPos != nullptr, "GPUParticleSort::DispatchSort: cameraPos is null");
    UZ_CHECKF(cameraForward != nullptr, "GPUParticleSort::DispatchSort: cameraForward is null");

    if (aliveCount == 0) return;

    UZ_PROFILE_SCOPE("VFX::ParticleSort");

    const uint32_t groupCount = DivRoundUp(paddedCount, kSortGroupSize);

    // Query UAV descriptor indices for bindless access via ResourceDescriptorHeap[].
    // The RHI device manages the global descriptor heap and returns the index
    // assigned at buffer creation time.
    const uint32_t keysUavIdx    = device->GetBufferUAVIndex(sortKeys);
    const uint32_t indicesUavIdx = device->GetBufferUAVIndex(sortIndices);

    cmd->SetPipeline(pipeline);

    // ── Phase 0: local sort + key generation ────────────────────────────
    {
        VFXSortConstants cb{};
        cb.particleCount       = aliveCount;
        cb.sortPhase           = 0;
        cb.paddedCount         = paddedCount;
        cb._pad0               = 0;
        cb.cameraPosX          = cameraPos[0];
        cb.cameraPosY          = cameraPos[1];
        cb.cameraPosZ          = cameraPos[2];
        cb._pad1               = 0.0f;
        cb.cameraForwardX      = cameraForward[0];
        cb.cameraForwardY      = cameraForward[1];
        cb.cameraForwardZ      = cameraForward[2];
        cb._pad2               = 0.0f;
        cb.positionsSrvIndex   = positionsSrvIndex;
        cb.sortKeysUavIndex    = keysUavIdx;
        cb.sortIndicesUavIndex = indicesUavIdx;
        cb._pad3               = 0;
        cb.bitonicBlockSize    = 0;
        cb.bitonicSubBlockSize = 0;
        cb._pad4               = 0;
        cb._pad5               = 0;

        if (!UploadAndBindSortCB(cmd, cbRing, cb)) return;

        cmd->Dispatch(groupCount, 1, 1);
    }

    // UAV barrier between phase 0 and phase 1.
    EmitSortUAVBarriers(cmd, *this);

    // ── Phase 1: global bitonic merge ───────────────────────────────────
    // For each blockSize starting at 2 * SORT_GROUP_SIZE (the first merge
    // that spans thread group boundaries), iterate subBlockSize from
    // blockSize/2 down to 1. Each (blockSize, subBlockSize) pair is one
    // dispatch.
    //
    // The shader's comment documents this dispatch loop:
    //   for (blockSize = 512; blockSize <= paddedCount; blockSize <<= 1)
    //     for (subBlockSize = blockSize >> 1; subBlockSize > 0; subBlockSize >>= 1)
    //
    // @ref Batcher 1968 "Sorting Networks and Their Applications"
    for (uint32_t blockSize = kSortGroupSize * 2;
         blockSize <= paddedCount;
         blockSize <<= 1)
    {
        for (uint32_t subBlockSize = blockSize >> 1;
             subBlockSize > 0;
             subBlockSize >>= 1)
        {
            VFXSortConstants cb{};
            cb.particleCount       = aliveCount;
            cb.sortPhase           = 1;
            cb.paddedCount         = paddedCount;
            cb._pad0               = 0;
            cb.cameraPosX          = cameraPos[0];
            cb.cameraPosY          = cameraPos[1];
            cb.cameraPosZ          = cameraPos[2];
            cb._pad1               = 0.0f;
            cb.cameraForwardX      = cameraForward[0];
            cb.cameraForwardY      = cameraForward[1];
            cb.cameraForwardZ      = cameraForward[2];
            cb._pad2               = 0.0f;
            cb.positionsSrvIndex   = positionsSrvIndex;
            cb.sortKeysUavIndex    = keysUavIdx;
            cb.sortIndicesUavIndex = indicesUavIdx;
            cb._pad3               = 0;
            cb.bitonicBlockSize    = blockSize;
            cb.bitonicSubBlockSize = subBlockSize;
            cb._pad4               = 0;
            cb._pad5               = 0;

            if (!UploadAndBindSortCB(cmd, cbRing, cb)) return;

            cmd->Dispatch(groupCount, 1, 1);

            // UAV barrier between each global merge step so that the next
            // dispatch sees the writes from this one.
            EmitSortUAVBarriers(cmd, *this);
        }
    }
}

} // namespace uz
