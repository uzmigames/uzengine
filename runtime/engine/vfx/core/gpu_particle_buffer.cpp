/// GPU Particle Buffer -- Allocation and teardown of SoA ping-pong storage.

#include "gpu_particle_buffer.h"

#include "engine/rhi/rhi.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

UZ_DEFINE_LOG_CATEGORY(VFX, Trace);

namespace uz {

// ---------------------------------------------------------------------------
// ParticleAttributeSet
// ---------------------------------------------------------------------------

bool ParticleAttributeSet::IsValid() const noexcept
{
    return positions.IsValid() && velocities.IsValid() &&
           ages.IsValid() && lifespans.IsValid() &&
           sizes.IsValid() && colors.IsValid() &&
           rotations.IsValid() && subImageIndices.IsValid();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Approximate total bytes per particle including both sets and shared buffers.
constexpr uint32_t kApproxBytesPerParticle = 128;

/// Creates a structured buffer with UAV access for VFX compute shaders.
///
/// # Parameters
/// - `device`: RHI device for allocation.
/// - `sizeBytes`: Total buffer size in bytes.
/// - `stride`: Element stride for structured buffer SRV/UAV views.
/// - `debugName`: GPU debug label (visible in PIX/RenderDoc).
///
/// # Returns
/// Valid handle on success, invalid handle on failure.
RHIBufferHandle CreateVFXBuffer(IRHIDevice* device, uint64_t sizeBytes,
                                uint32_t stride, const char* debugName)
{
    RHIBufferDesc desc{};
    desc.sizeBytes = sizeBytes;
    desc.stride = stride;
    desc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
    desc.memory = RHIMemoryType::Default;
    desc.debugName = debugName;
    return device->CreateBuffer(desc);
}

/// Creates an indirect argument buffer (UAV + IndirectArgs usage).
///
/// Written by compute shaders, consumed by DrawIndexedIndirect / DispatchIndirect.
RHIBufferHandle CreateIndirectArgsBuffer(IRHIDevice* device, uint64_t sizeBytes,
                                         const char* debugName)
{
    RHIBufferDesc desc{};
    desc.sizeBytes = sizeBytes;
    desc.stride = 0;
    desc.usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs;
    desc.memory = RHIMemoryType::Default;
    desc.debugName = debugName;
    return device->CreateBuffer(desc);
}

/// Formats a VFX debug name into a thread-local buffer.
///
/// Returns a pointer valid until the next call on the same thread.
const char* FormatDebugName(const char* attribute, const char* suffix)
{
    static thread_local char buf[128];
    std::snprintf(buf, sizeof(buf), "VFX_%s_%s", attribute, suffix);
    return buf;
}

/// Allocates all seven attribute buffers for one ParticleAttributeSet.
///
/// # Returns
/// `true` if all buffers were allocated successfully.
bool CreateAttributeSet(IRHIDevice* device, ParticleAttributeSet& set,
                        uint32_t capacity, const char* suffix)
{
    const auto cap = static_cast<uint64_t>(capacity);

    // Positions and velocities: HLSL uses RWStructuredBuffer<float4> (stride 16),
    // NOT float3 (stride 12). The .w component is unused but the structured buffer
    // stride must match the HLSL template type exactly.
    set.positions  = CreateVFXBuffer(device, cap * 16, 16, FormatDebugName("Positions", suffix));
    set.velocities = CreateVFXBuffer(device, cap * 16, 16, FormatDebugName("Velocities", suffix));
    set.ages       = CreateVFXBuffer(device, cap * 4,  4,  FormatDebugName("Ages", suffix));
    set.lifespans  = CreateVFXBuffer(device, cap * 4,  4,  FormatDebugName("Lifespans", suffix));
    set.sizes      = CreateVFXBuffer(device, cap * 4,  4,  FormatDebugName("Sizes", suffix));
    set.colors     = CreateVFXBuffer(device, cap * 16, 16, FormatDebugName("Colors", suffix));
    set.rotations       = CreateVFXBuffer(device, cap * 4,  4,  FormatDebugName("Rotations", suffix));
    set.subImageIndices = CreateVFXBuffer(device, cap * 4,  4,  FormatDebugName("SubImageIndices", suffix));

    return set.IsValid();
}

/// Destroys all attribute buffers in a set and resets handles to invalid.
void DestroyAttributeSet(IRHIDevice* device, ParticleAttributeSet& set)
{
    if (set.positions.IsValid())  device->DestroyBuffer(set.positions);
    if (set.velocities.IsValid()) device->DestroyBuffer(set.velocities);
    if (set.ages.IsValid())       device->DestroyBuffer(set.ages);
    if (set.lifespans.IsValid())  device->DestroyBuffer(set.lifespans);
    if (set.sizes.IsValid())      device->DestroyBuffer(set.sizes);
    if (set.colors.IsValid())     device->DestroyBuffer(set.colors);
    if (set.rotations.IsValid())       device->DestroyBuffer(set.rotations);
    if (set.subImageIndices.IsValid()) device->DestroyBuffer(set.subImageIndices);
    if (set.persistentIDs.IsValid())   device->DestroyBuffer(set.persistentIDs);
    set = {};
}

}  // namespace

// ---------------------------------------------------------------------------
// GPUParticleBuffer::Create
// ---------------------------------------------------------------------------

bool GPUParticleBuffer::Create(IRHIDevice* device, uint32_t particleCapacity)
{
    UZ_CHECKF(device != nullptr, "GPUParticleBuffer::Create: device is null");
    UZ_CHECKF(particleCapacity > 0, "GPUParticleBuffer::Create: capacity must be > 0");
    UZ_CHECKF(!IsValid(), "GPUParticleBuffer::Create: already allocated, call Destroy() first");

    capacity = particleCapacity;
    readIndex = 0;
    writeIndex = 1;

    // Allocate both ping-pong attribute sets
    if (!CreateAttributeSet(device, sets[0], capacity, "SetA")) {
        UZ_LOG(VFX, Error, "Failed to allocate particle attribute set A: capacity={}", capacity);
        Destroy(device);
        return false;
    }

    if (!CreateAttributeSet(device, sets[1], capacity, "SetB")) {
        UZ_LOG(VFX, Error, "Failed to allocate particle attribute set B: capacity={}", capacity);
        Destroy(device);
        return false;
    }

    const auto cap = static_cast<uint64_t>(capacity);

    // Shared: alive count (single uint32, modified by GPU atomics).
    // Extra `CopySrc` usage is required so the simulation pass can copy
    // it into the CPU readback ring each frame.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer
                   | RHIBufferUsage::UnorderedAccess
                   | RHIBufferUsage::CopySrc;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = "VFX_AliveCount";
        aliveCount = device->CreateBuffer(desc);
    }

    // Readback ring (3 slots). Each holds a single uint32 in a
    // CPU-visible Readback heap. The simulation pass's Kick copies the
    // current `aliveCount` here; the CPU polls the oldest slot each
    // frame to get the authoritative live particle count.
    for (uint32_t i = 0; i < kAliveReadbackRingSize; ++i) {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Readback;
        desc.debugName = "VFX_AliveCount_Readback";
        aliveCountReadback[i] = device->CreateBuffer(desc);
    }
    readbackWriteIndex    = 0;
    readbackHasKickedOnce = false;
    gpuAliveCount         = 0;

    // Shared: free list stack (capacity entries, each uint32 particle index)
    freeList = CreateVFXBuffer(device, cap * sizeof(uint32_t), sizeof(uint32_t),
                               "VFX_FreeList");

    // Shared: free list head pointer (single uint32 atomic, top-of-stack index)
    freeListHead = CreateVFXBuffer(device, sizeof(uint32_t), sizeof(uint32_t),
                                   "VFX_FreeListHead");

    // Shared: particle ID to buffer index mapping (capacity entries)
    idToIndex = CreateVFXBuffer(device, cap * sizeof(uint32_t), sizeof(uint32_t),
                                "VFX_IdToIndex");

    // Shared: indirect draw arguments (5 x uint32 = 20 bytes)
    indirectDrawArgs = CreateIndirectArgsBuffer(device, sizeof(RHIDrawIndexedIndirectArgs),
                                                "VFX_IndirectDrawArgs");

    // Shared: indirect dispatch arguments (3 x uint32 = 12 bytes)
    indirectDispatchArgs = CreateIndirectArgsBuffer(device, sizeof(RHIDispatchIndirectArgs),
                                                    "VFX_IndirectDispatchArgs");

    if (!aliveCount.IsValid() || !freeList.IsValid() || !freeListHead.IsValid() ||
        !idToIndex.IsValid() || !indirectDrawArgs.IsValid() ||
        !indirectDispatchArgs.IsValid()) {
        UZ_LOG(VFX, Error, "Failed to allocate shared particle buffers: capacity={}", capacity);
        Destroy(device);
        return false;
    }

    // Allocate sort buffers (sortKeys + sortIndices, power-of-two padded).
    if (!sort.Create(device, capacity)) {
        UZ_LOG(VFX, Error, "Failed to allocate particle sort buffers: capacity={}", capacity);
        Destroy(device);
        return false;
    }

    // Mark for deferred initialization — the GPU bookkeeping buffers (freeList,
    // freeListHead, aliveCount) contain garbage until InitializeData() copies
    // the correct initial values from staging buffers.
    needsInitialization = true;

    const uint64_t estimatedKB = (cap * kApproxBytesPerParticle) / 1024;
    UZ_LOG(VFX, Info, "GPU particle buffer created: capacity={}, memory~{}KB",
           capacity, estimatedKB);
    return true;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::CreateMeshBuffers
// ---------------------------------------------------------------------------

bool GPUParticleBuffer::CreateMeshBuffers(IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr, "GPUParticleBuffer::CreateMeshBuffers: device is null");
    UZ_CHECKF(IsValid(), "GPUParticleBuffer::CreateMeshBuffers: base buffers not allocated");
    UZ_CHECKF(!hasMeshBuffers, "GPUParticleBuffer::CreateMeshBuffers: already allocated");

    const auto cap = static_cast<uint64_t>(capacity);

    // Instance transforms: one float4x4 (64 bytes) per particle.
    meshInstanceTransforms = CreateVFXBuffer(device, cap * 64, 64,
                                              "VFX_MeshInstanceTransforms");

    // Instance count: single uint32 atomic counter.
    meshInstanceCount = CreateVFXBuffer(device, sizeof(uint32_t), sizeof(uint32_t),
                                         "VFX_MeshInstanceCount");

    // Mesh indirect draw args: DrawIndexedInstancedIndirect (5 x uint32 = 20 bytes).
    meshIndirectDrawArgs = CreateIndirectArgsBuffer(device,
                                                     sizeof(RHIDrawIndexedIndirectArgs),
                                                     "VFX_MeshIndirectDrawArgs");

    if (!meshInstanceTransforms.IsValid() || !meshInstanceCount.IsValid() ||
        !meshIndirectDrawArgs.IsValid()) {
        UZ_LOG(VFX, Error,
               "Failed to allocate mesh instance buffers: capacity={}", capacity);
        DestroyMeshBuffers(device);
        return false;
    }

    hasMeshBuffers = true;

    const uint64_t meshKB = (cap * 64 + sizeof(uint32_t) +
                             sizeof(RHIDrawIndexedIndirectArgs)) / 1024;
    UZ_LOG(VFX, Info,
           "GPU mesh instance buffers created: capacity={}, memory~{}KB",
           capacity, meshKB);
    return true;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::DestroyMeshBuffers
// ---------------------------------------------------------------------------

void GPUParticleBuffer::DestroyMeshBuffers(IRHIDevice* device)
{
    if (!device) return;

    if (meshInstanceTransforms.IsValid()) device->DestroyBuffer(meshInstanceTransforms);
    if (meshInstanceCount.IsValid())      device->DestroyBuffer(meshInstanceCount);
    if (meshIndirectDrawArgs.IsValid())   device->DestroyBuffer(meshIndirectDrawArgs);

    meshInstanceTransforms = {};
    meshInstanceCount = {};
    meshIndirectDrawArgs = {};
    hasMeshBuffers = false;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::CreatePersistentIDBuffers (phase 7.4.0)
// ---------------------------------------------------------------------------

bool GPUParticleBuffer::CreatePersistentIDBuffers(IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr,
              "GPUParticleBuffer::CreatePersistentIDBuffers: device is null");
    UZ_CHECKF(IsValid(),
              "GPUParticleBuffer::CreatePersistentIDBuffers: base buffers not allocated");
    UZ_CHECKF(!hasPersistentIDs,
              "GPUParticleBuffer::CreatePersistentIDBuffers: already allocated");

    const auto cap = static_cast<uint64_t>(capacity);

    // uint64 per slot, matching `FNiagaraEmitter::bRequiresPersistentIDs`.
    // Structured buffer stride = 8 bytes so HLSL `RWStructuredBuffer<uint64_t>`
    // (or paired uint2) indexes correctly.
    sets[0].persistentIDs = CreateVFXBuffer(
        device, cap * 8, 8, "VFX_PersistentIDs_SetA");
    sets[1].persistentIDs = CreateVFXBuffer(
        device, cap * 8, 8, "VFX_PersistentIDs_SetB");

    if (!sets[0].persistentIDs.IsValid() || !sets[1].persistentIDs.IsValid()) {
        UZ_LOG(VFX, Error,
               "CreatePersistentIDBuffers: allocation failed for capacity={}",
               capacity);
        if (sets[0].persistentIDs.IsValid()) {
            device->DestroyBuffer(sets[0].persistentIDs);
            sets[0].persistentIDs = {};
        }
        if (sets[1].persistentIDs.IsValid()) {
            device->DestroyBuffer(sets[1].persistentIDs);
            sets[1].persistentIDs = {};
        }
        return false;
    }

    hasPersistentIDs = true;
    const uint64_t idsKB = (cap * 8 * 2) / 1024;
    UZ_LOG(VFX, Info,
           "GPU persistent-ID buffers created: capacity={}, memory~{}KB",
           capacity, idsKB);
    return true;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::CreateDeathEventBuffers
// ---------------------------------------------------------------------------

bool GPUParticleBuffer::CreateDeathEventBuffers(IRHIDevice* device)
{
    UZ_CHECKF(device != nullptr, "GPUParticleBuffer::CreateDeathEventBuffers: device is null");
    UZ_CHECKF(IsValid(), "GPUParticleBuffer::CreateDeathEventBuffers: base buffers not allocated");
    UZ_CHECKF(!hasDeathEventBuffers, "GPUParticleBuffer::CreateDeathEventBuffers: already allocated");

    const auto cap = static_cast<uint64_t>(capacity);

    // Death event structured buffer: one ParticleDeathEvent (48 bytes) per particle.
    deathEventBuffer = CreateVFXBuffer(device,
                                        cap * sizeof(ParticleDeathEvent),
                                        sizeof(ParticleDeathEvent),
                                        "VFX_DeathEventBuffer");

    // Death event count: single uint32 atomic counter.
    deathEventCount = CreateVFXBuffer(device,
                                       sizeof(uint32_t),
                                       sizeof(uint32_t),
                                       "VFX_DeathEventCount");

    if (!deathEventBuffer.IsValid() || !deathEventCount.IsValid()) {
        UZ_LOG(VFX, Error,
               "Failed to allocate death event buffers: capacity={}", capacity);
        DestroyDeathEventBuffers(device);
        return false;
    }

    hasDeathEventBuffers = true;

    const uint64_t deathKB = (cap * sizeof(ParticleDeathEvent) + sizeof(uint32_t)) / 1024;
    UZ_LOG(VFX, Info,
           "GPU death event buffers created: capacity={}, memory~{}KB",
           capacity, deathKB);
    return true;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::DestroyDeathEventBuffers
// ---------------------------------------------------------------------------

void GPUParticleBuffer::DestroyDeathEventBuffers(IRHIDevice* device)
{
    if (!device) return;

    if (deathEventBuffer.IsValid()) device->DestroyBuffer(deathEventBuffer);
    if (deathEventCount.IsValid())  device->DestroyBuffer(deathEventCount);

    deathEventBuffer = {};
    deathEventCount = {};
    hasDeathEventBuffers = false;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::Destroy
// ---------------------------------------------------------------------------

void GPUParticleBuffer::Destroy(IRHIDevice* device)
{
    if (!device) return;

    // Destroy staging buffers if they were never cleaned up.
    DestroyStaging(device);

    DestroyAttributeSet(device, sets[0]);
    DestroyAttributeSet(device, sets[1]);

    // Destroy optional mesh instance buffers if present.
    DestroyMeshBuffers(device);

    // Destroy optional death event buffers if present.
    DestroyDeathEventBuffers(device);

    // Destroy sort buffers (sortKeys + sortIndices).
    sort.Destroy(device);

    if (aliveCount.IsValid())           device->DestroyBuffer(aliveCount);
    for (uint32_t i = 0; i < kAliveReadbackRingSize; ++i) {
        if (aliveCountReadback[i].IsValid()) {
            device->DestroyBuffer(aliveCountReadback[i]);
            aliveCountReadback[i] = {};
        }
    }
    readbackWriteIndex    = 0;
    readbackHasKickedOnce = false;
    gpuAliveCount         = 0;
    if (freeList.IsValid())             device->DestroyBuffer(freeList);
    if (freeListHead.IsValid())         device->DestroyBuffer(freeListHead);
    if (idToIndex.IsValid())            device->DestroyBuffer(idToIndex);
    if (indirectDrawArgs.IsValid())     device->DestroyBuffer(indirectDrawArgs);
    if (indirectDispatchArgs.IsValid()) device->DestroyBuffer(indirectDispatchArgs);

    aliveCount = {};
    freeList = {};
    freeListHead = {};
    idToIndex = {};
    indirectDrawArgs = {};
    indirectDispatchArgs = {};
    capacity = 0;
    readIndex = 0;
    writeIndex = 1;
    needsInitialization = false;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::InitializeData
// ---------------------------------------------------------------------------

bool GPUParticleBuffer::InitializeData(IRHIDevice* device, IRHICommandList* cmd)
{
    UZ_CHECKF(device != nullptr, "GPUParticleBuffer::InitializeData: device is null");
    UZ_CHECKF(cmd != nullptr, "GPUParticleBuffer::InitializeData: cmd is null");
    UZ_CHECKF(IsValid(), "GPUParticleBuffer::InitializeData: buffers not allocated");
    UZ_CHECKF(needsInitialization, "GPUParticleBuffer::InitializeData: already initialized");

    const auto cap = static_cast<uint64_t>(capacity);
    const uint64_t freeListSizeBytes = cap * sizeof(uint32_t);

    // ── Free list staging: fill with [0, 1, 2, ..., capacity-1] ─────────
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = freeListSizeBytes;
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_FreeList_Staging";
        freeListStaging = device->CreateBuffer(desc);

        if (!freeListStaging.IsValid()) {
            UZ_LOG(VFX, Error, "InitializeData: failed to create freeList staging buffer");
            DestroyStaging(device);
            return false;
        }

        void* mapped = device->MapBuffer(freeListStaging);
        if (!mapped) {
            UZ_LOG(VFX, Error, "InitializeData: failed to map freeList staging buffer");
            DestroyStaging(device);
            return false;
        }

        auto* data = static_cast<uint32_t*>(mapped);
        for (uint32_t i = 0; i < capacity; ++i) {
            data[i] = i;
        }
        device->UnmapBuffer(freeListStaging);

        cmd->CopyBuffer(freeListStaging, 0, freeList, 0, freeListSizeBytes);
    }

    // ── Free list head staging: initial value = capacity (all slots free) ──
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_FreeListHead_Staging";
        freeListHeadStaging = device->CreateBuffer(desc);

        if (!freeListHeadStaging.IsValid()) {
            UZ_LOG(VFX, Error, "InitializeData: failed to create freeListHead staging buffer");
            DestroyStaging(device);
            return false;
        }

        void* mapped = device->MapBuffer(freeListHeadStaging);
        if (!mapped) {
            UZ_LOG(VFX, Error, "InitializeData: failed to map freeListHead staging buffer");
            DestroyStaging(device);
            return false;
        }

        *static_cast<uint32_t*>(mapped) = capacity;
        device->UnmapBuffer(freeListHeadStaging);

        cmd->CopyBuffer(freeListHeadStaging, 0, freeListHead, 0, sizeof(uint32_t));
    }

    // ── Alive count staging: initial value = 0 (no particles alive) ─────
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::StructuredBuffer;
        desc.memory = RHIMemoryType::Upload;
        desc.debugName = "VFX_AliveCount_Staging";
        aliveCountStaging = device->CreateBuffer(desc);

        if (!aliveCountStaging.IsValid()) {
            UZ_LOG(VFX, Error, "InitializeData: failed to create aliveCount staging buffer");
            DestroyStaging(device);
            return false;
        }

        void* mapped = device->MapBuffer(aliveCountStaging);
        if (!mapped) {
            UZ_LOG(VFX, Error, "InitializeData: failed to map aliveCount staging buffer");
            DestroyStaging(device);
            return false;
        }

        *static_cast<uint32_t*>(mapped) = 0;
        device->UnmapBuffer(aliveCountStaging);

        cmd->CopyBuffer(aliveCountStaging, 0, aliveCount, 0, sizeof(uint32_t));
    }

    // ── Zero-initialize lifespan buffers in BOTH sets ──────────────────
    // Uninitialized Default heap memory may contain non-zero garbage. The update
    // shader checks (lifespan == 0.0f) to skip unspawned slots. If lifespan has a
    // non-zero garbage value, the shader treats it as a dead particle and pushes it
    // onto the free list, corrupting the free list head counter.
    {
        RHIBufferDesc zeroDesc{};
        zeroDesc.sizeBytes = static_cast<uint64_t>(capacity) * sizeof(float);
        zeroDesc.stride = sizeof(float);
        zeroDesc.usage = RHIBufferUsage::StructuredBuffer;
        zeroDesc.memory = RHIMemoryType::Upload;
        zeroDesc.debugName = "VFX_Zero_Staging";
        RHIBufferHandle zeroStaging = device->CreateBuffer(zeroDesc);

        if (zeroStaging.IsValid()) {
            void* mapped = device->MapBuffer(zeroStaging);
            if (mapped) {
                std::memset(mapped, 0, zeroDesc.sizeBytes);
                device->UnmapBuffer(zeroStaging);

                // Zero lifespans in both ping-pong sets
                cmd->CopyBuffer(zeroStaging, 0, sets[0].lifespans, 0, zeroDesc.sizeBytes);
                cmd->CopyBuffer(zeroStaging, 0, sets[1].lifespans, 0, zeroDesc.sizeBytes);
                // Zero ages in both sets
                cmd->CopyBuffer(zeroStaging, 0, sets[0].ages, 0, zeroDesc.sizeBytes);
                cmd->CopyBuffer(zeroStaging, 0, sets[1].ages, 0, zeroDesc.sizeBytes);
            }
            // Do NOT destroy zeroStaging here — the CopyBuffer commands are still
            // on the command list and reference this resource. Store it for later
            // cleanup (DestroyStaging will handle it after the GPU finishes).
            zeroStagingBuffer = zeroStaging;
        }
    }

    UZ_LOG(VFX, Info, "InitializeData: copied freeList ({} entries), freeListHead={}, aliveCount=0, zeroed lifespans/ages",
           capacity, capacity);

    // ── CopyDst → UAV barriers: GPU must see the copied data before compute ──
    // CopyBuffer implicitly promotes destinations to CopyDst state.
    // Transition to UAV so the spawn/update compute shaders can read/write.
    {
        const RHIBarrierDesc barriers[] = {
            { .buffer = freeList,              .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
            { .buffer = freeListHead,          .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
            { .buffer = aliveCount,            .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
            { .buffer = sets[0].lifespans,     .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
            { .buffer = sets[1].lifespans,     .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
            { .buffer = sets[0].ages,          .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
            { .buffer = sets[1].ages,          .stateBefore = RHIResourceState::CopyDst, .stateAfter = RHIResourceState::UnorderedAccess },
        };
        cmd->ResourceBarrier(barriers, 7);
    }

    needsInitialization = false;
    UZ_LOG(VFX, Info, "GPU particle buffer initialized: freeList[0..{}], head={}, aliveCount=0",
           capacity - 1, capacity);
    return true;
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::DestroyStaging
// ---------------------------------------------------------------------------

void GPUParticleBuffer::DestroyStaging(IRHIDevice* device)
{
    if (!device) return;

    if (freeListStaging.IsValid()) {
        device->DestroyBuffer(freeListStaging);
        freeListStaging = {};
    }

    if (freeListHeadStaging.IsValid()) {
        device->DestroyBuffer(freeListHeadStaging);
        freeListHeadStaging = {};
    }

    if (aliveCountStaging.IsValid()) {
        device->DestroyBuffer(aliveCountStaging);
        aliveCountStaging = {};
    }

    if (zeroStagingBuffer.IsValid()) {
        device->DestroyBuffer(zeroStagingBuffer);
        zeroStagingBuffer = {};
    }
}

// ---------------------------------------------------------------------------
// GPUParticleBuffer::UploadInitialState
// ---------------------------------------------------------------------------

void GPUParticleBuffer::UploadInitialState(IRHIDevice* device, IRHICommandList* cmd) {
    UZ_CHECKF(device != nullptr, "UploadInitialState: device is null");
    UZ_CHECKF(cmd != nullptr, "UploadInitialState: cmd is null");
    UZ_CHECKF(IsValid(), "UploadInitialState: buffer not allocated");

    const uint32_t cap = capacity;

    // DX12 buffers support implicit state promotion: Common → CopyDst is
    // auto-promoted, and CopyDst → UAV decays back to Common after execution.
    // We skip explicit Common→CopyDst barriers — DX12 handles the promotion.
    // After all copies, a single batch of CopyDst→UAV barriers ensures
    // visibility for compute dispatches.

    // Staging buffers stored for deferred destruction after GPU execution.
    RHIBufferHandle stagingBuffers[3] = {};

    // ── Free list: freeList[i] = i for all i ─────────────────────────
    {
        const uint64_t dataSize = static_cast<uint64_t>(cap) * sizeof(uint32_t);
        RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = dataSize;
        stagingDesc.usage     = RHIBufferUsage::CopySrc;
        stagingDesc.memory    = RHIMemoryType::Upload;
        stagingDesc.debugName = "VFX_FreeList_Staging";
        stagingBuffers[0] = device->CreateBuffer(stagingDesc);

        auto* mapped = static_cast<uint32_t*>(device->MapBuffer(stagingBuffers[0]));
        for (uint32_t i = 0; i < cap; ++i) {
            mapped[i] = i;
        }
        device->UnmapBuffer(stagingBuffers[0]);

        cmd->CopyBuffer(stagingBuffers[0], 0, freeList, 0, dataSize);
    }

    // ── Free list head: = capacity (all slots free) ──────────────────
    {
        RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = sizeof(uint32_t);
        stagingDesc.usage     = RHIBufferUsage::CopySrc;
        stagingDesc.memory    = RHIMemoryType::Upload;
        stagingDesc.debugName = "VFX_FreeListHead_Staging";
        stagingBuffers[1] = device->CreateBuffer(stagingDesc);

        auto* mapped = static_cast<uint32_t*>(device->MapBuffer(stagingBuffers[1]));
        mapped[0] = cap;
        device->UnmapBuffer(stagingBuffers[1]);

        cmd->CopyBuffer(stagingBuffers[1], 0, freeListHead, 0, sizeof(uint32_t));
    }

    // ── Alive count: = 0 ─────────────────────────────────────────────
    {
        RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = sizeof(uint32_t);
        stagingDesc.usage     = RHIBufferUsage::CopySrc;
        stagingDesc.memory    = RHIMemoryType::Upload;
        stagingDesc.debugName = "VFX_AliveCount_Staging";
        stagingBuffers[2] = device->CreateBuffer(stagingDesc);

        auto* mapped = static_cast<uint32_t*>(device->MapBuffer(stagingBuffers[2]));
        mapped[0] = 0;
        device->UnmapBuffer(stagingBuffers[2]);

        cmd->CopyBuffer(stagingBuffers[2], 0, aliveCount, 0, sizeof(uint32_t));
    }

    // Store staging handles for caller to destroy after WaitIdle().
    pendingStagingBuffers.clear();
    for (auto h : stagingBuffers) {
        if (h.IsValid()) pendingStagingBuffers.push_back(h);
    }

    // ── Zero-init lifespans in BOTH ping-pong sets ──────────────────────
    // Godot pattern: dispatch=capacity, dead slots do no-op via lifespan==0 check.
    // Without zero-init, unspawned slots have garbage lifespan → unpredictable behavior.
    for (int s = 0; s < 2; ++s) {
        if (sets[s].lifespans.IsValid()) {
            const uint64_t dataSize = cap * sizeof(float);
            RHIBufferDesc sd{};
            sd.sizeBytes = dataSize;
            sd.usage     = RHIBufferUsage::CopySrc;
            sd.memory    = RHIMemoryType::Upload;
            sd.debugName = "VFX_Lifespans_ZeroInit";
            auto staging = device->CreateBuffer(sd);
            if (staging.IsValid()) {
                auto* mapped = static_cast<float*>(device->MapBuffer(staging));
                std::memset(mapped, 0, static_cast<size_t>(dataSize));
                device->UnmapBuffer(staging);
                cmd->CopyBuffer(staging, 0, sets[s].lifespans, 0, dataSize);
                pendingStagingBuffers.push_back(staging);
            }
        }
    }

    // Single batch of CopyDst→UAV barriers after all copies.
    {
        RHIBarrierDesc barriers[3] = {};
        barriers[0].buffer = freeList;
        barriers[0].stateBefore = RHIResourceState::CopyDst;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].buffer = freeListHead;
        barriers[1].stateBefore = RHIResourceState::CopyDst;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[2].buffer = aliveCount;
        barriers[2].stateBefore = RHIResourceState::CopyDst;
        barriers[2].stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(barriers, 3);
    }

    // Lifespan buffers: CopyDst→UAV
    for (int s = 0; s < 2; ++s) {
        if (sets[s].lifespans.IsValid()) {
            RHIBarrierDesc b{};
            b.buffer      = sets[s].lifespans;
            b.stateBefore = RHIResourceState::CopyDst;
            b.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&b, 1);
        }
    }

    UZ_LOG(VFX, Info, "GPUParticleBuffer: initial state uploaded (freeList[0..{}], head={}, alive=0, lifespans zeroed)",
           cap - 1, cap);
}

void GPUParticleBuffer::TransitionAllToUAV(IRHICommandList* cmd) {
    UZ_CHECKF(cmd != nullptr, "TransitionAllToUAV: cmd is null");
    UZ_CHECKF(IsValid(), "TransitionAllToUAV: buffer not allocated");

    // Collect all valid buffer handles that need Common→UAV transition.
    RHIBarrierDesc barriers[32] = {};
    uint32_t count = 0;

    auto addBarrier = [&](RHIBufferHandle buf) {
        if (buf.IsValid() && count < 32) {
            barriers[count].buffer      = buf;
            barriers[count].stateBefore = RHIResourceState::Common;
            barriers[count].stateAfter  = RHIResourceState::UnorderedAccess;
            ++count;
        }
    };

    // Both attribute sets (read + write)
    for (int s = 0; s < 2; ++s) {
        addBarrier(sets[s].positions);
        addBarrier(sets[s].velocities);
        addBarrier(sets[s].ages);
        addBarrier(sets[s].lifespans);
        addBarrier(sets[s].sizes);
        addBarrier(sets[s].colors);
        addBarrier(sets[s].rotations);
    }

    // Shared bookkeeping buffers
    addBarrier(aliveCount);
    addBarrier(freeList);
    addBarrier(freeListHead);
    addBarrier(idToIndex);
    addBarrier(indirectDrawArgs);
    addBarrier(indirectDispatchArgs);

    // Sort buffers
    if (sort.IsValid()) {
        addBarrier(sort.sortKeys);
        addBarrier(sort.sortIndices);
    }

    if (count > 0) {
        cmd->ResourceBarrier(barriers, count);
    }

    UZ_LOG(VFX, Info, "GPUParticleBuffer: transitioned {} buffers Common→UAV", count);
}

// ───────────────────────────────────────────────────────────────────────────
// Alive-count CPU readback (phase 15.6)
// ───────────────────────────────────────────────────────────────────────────

void GPUParticleBuffer::KickAliveReadback(IRHICommandList* cmd) {
    if (cmd == nullptr) return;
    if (!aliveCount.IsValid()) return;

    const uint32_t slot = readbackWriteIndex;
    if (!aliveCountReadback[slot].IsValid()) return;

    // Caller has already barriered `aliveCount` from UAV → CopySrc and
    // will barrier it back after this copy. The readback buffer is in a
    // Readback heap (Common state) and accepts `CopyDst` without an
    // explicit barrier.
    cmd->CopyBuffer(aliveCount, 0,
                    aliveCountReadback[slot], 0, sizeof(uint32_t));

    readbackWriteIndex    = (slot + 1u) % kAliveReadbackRingSize;
    readbackHasKickedOnce = true;
}

void GPUParticleBuffer::PollAliveReadback(IRHIDevice* device) {
    if (device == nullptr) return;
    if (!readbackHasKickedOnce) return;

    // After Kick, `readbackWriteIndex` points to the slot that will be
    // written NEXT frame — which is the oldest slot in the ring, so its
    // current contents reflect the GPU value from N-1 frames ago. That
    // slot's write has had a full frame + GPU sync to complete.
    const uint32_t readSlot = readbackWriteIndex;
    if (!aliveCountReadback[readSlot].IsValid()) return;

    void* mapped = device->MapBuffer(aliveCountReadback[readSlot]);
    if (mapped != nullptr) {
        gpuAliveCount = *static_cast<uint32_t*>(mapped);
        device->UnmapBuffer(aliveCountReadback[readSlot]);
    }
}

}  // namespace uz
