/// TLASManager Implementation — Per-frame TLAS rebuild from draw commands.

#include "tlas_manager.h"
#include "core/gbuffer/draw_list.h"
#include "core/cvar.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "log.h"

#include <cstring>

namespace uz {

static auto* cv_TlasReuse = UZ_CVAR_BOOL("r.RayTracing.TLAS.Reuse", true,
    "Skip the TLAS build when the instance descriptors (transforms, BLAS addresses) are unchanged "
    "and refit in place when only their contents changed (block B.6). 0 rebuilds every frame, as "
    "before, for an A/B on one binary.");

bool TLASManager::Init(IRHIDevice* device, uint32_t maxInstances) {
    m_device = device;
    m_maxInstances = maxInstances;

    if (!m_device->SupportsRayTracing()) {
        UZ_LOG(Renderer, Info, "TLASManager: RT not supported, skipping TLAS creation");
        return false;
    }

    // Create TLAS
    RHITLASDesc tlasDesc = {};
    tlasDesc.maxInstanceCount = maxInstances;
    tlasDesc.buildFlags       = RHIASBuildFlags::PreferFastTrace | RHIASBuildFlags::AllowUpdate;
    tlasDesc.debugName        = "SceneTLAS";

    m_tlasHandle = m_device->CreateTLAS(tlasDesc);
    if (!m_tlasHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "TLASManager: Failed to create TLAS");
        return false;
    }

    // Create instance buffer (upload heap for CPU write each frame)
    // D3D12_RAYTRACING_INSTANCE_DESC is 64 bytes per instance
    RHIBufferDesc instBufDesc = {};
    instBufDesc.sizeBytes = static_cast<uint64_t>(maxInstances) * 64;
    instBufDesc.usage     = RHIBufferUsage::ShaderBindingTable; // generic upload buffer
    instBufDesc.memory    = RHIMemoryType::Upload;
    instBufDesc.debugName = "TLAS_InstanceBuffer";

    m_instanceBuffer = m_device->CreateBuffer(instBufDesc);
    if (!m_instanceBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "TLASManager: Failed to create instance buffer");
        m_device->DestroyTLAS(m_tlasHandle);
        m_tlasHandle = RHITLASHandle::Invalid();
        return false;
    }

    UZ_LOG(Renderer, Info, "TLASManager: Initialized with max {} instances", maxInstances);
    return true;
}

void TLASManager::Rebuild(const DrawCommand* drawCommands, uint32_t drawCount,
                           const GPUMesh* meshes, uint32_t meshCount,
                           BLASManager& blasMgr, IRHICommandList* cmdList) {
    if (!m_tlasHandle.IsValid() || !m_instanceBuffer.IsValid() || !drawCommands || !cmdList) {
        return;
    }

    // Map instance buffer
    auto* mapped = static_cast<uint8_t*>(m_device->MapBuffer(m_instanceBuffer));
    if (!mapped) {
        UZ_LOG(Renderer, Error, "TLASManager: Failed to map instance buffer");
        return;
    }

    uint32_t instanceCount = 0;
    uint64_t hash = 1469598103934665603ull;   // FNV-1a 64 offset basis
    for (uint32_t i = 0; i < drawCount && instanceCount < m_maxInstances; ++i) {
        const auto& cmd = drawCommands[i];

        if (!IsGIOccluder(cmd) || cmd.meshId >= meshCount) continue;
        const auto& mesh = meshes[cmd.meshId];
        if (!mesh.IsValid()) continue;

        // Get or build BLAS for this mesh
        RHIBLASHandle blasHandle = blasMgr.GetOrBuildBLAS(cmd.meshId, mesh, cmdList);
        if (!blasHandle.IsValid()) continue;

        uint64_t blasAddress = m_device->GetBLASGPUAddress(blasHandle);
        if (blasAddress == 0) continue;

        // Fill D3D12_RAYTRACING_INSTANCE_DESC (64 bytes, tightly packed)
        // Layout: float3x4 Transform (48 bytes) + InstanceID:24 + InstanceMask:8 (4 bytes)
        //       + InstanceContributionToHitGroupIndex:24 + Flags:8 (4 bytes)
        //       + AccelerationStructure (8 bytes) = 64 bytes total
        uint8_t* inst = mapped + instanceCount * 64;
        std::memset(inst, 0, 64);

        // Mat4 uses row vectors (translation in _41/_42/_43). DXR's 3x4
        // multiplies column vectors, so transpose the entire affine transform,
        // including the rotation/scale basis, not just its translation.
        WriteInstanceTransform(reinterpret_cast<float*>(inst), cmd.worldMatrix);

        // InstanceID (24 bits) | InstanceMask (8 bits) at byte offset 48
        uint32_t idAndMask = (cmd.meshId & 0x00FFFFFF) | (0xFF << 24);
        std::memcpy(inst + 48, &idAndMask, 4);

        // InstanceContributionToHitGroupIndex (24 bits) | Flags (8 bits) at byte offset 52
        uint32_t hitAndFlags = 0; // hit group 0, no special flags
        std::memcpy(inst + 52, &hitAndFlags, 4);

        // AccelerationStructure GPU VA at byte offset 56
        std::memcpy(inst + 56, &blasAddress, 8);

        // The descriptor as the build reads it (transform, id/mask, flags, BLAS address), plus the
        // BLAS handle's slot and generation: a BLAS rebuilt into a reused pool slot can land at the
        // same GPU address with different geometry, and the TLAS must not keep its old bounds.
        for (uint32_t b = 0; b < 64; ++b) {
            hash ^= static_cast<uint64_t>(inst[b]);
            hash *= 1099511628211ull;
        }
        hash ^= static_cast<uint64_t>(blasHandle.index);
        hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(blasHandle.generation);
        hash *= 1099511628211ull;

        ++instanceCount;
    }

    m_device->UnmapBuffer(m_instanceBuffer);
    const uint32_t previousCount = m_lastInstanceCount;
    const uint64_t previousHash  = m_lastInstanceHash;
    m_lastInstanceCount = instanceCount;
    m_lastInstanceHash  = hash;

    if (instanceCount == 0) return;

    // Block B.6: the same descriptors build the same tree, so an unchanged set keeps the TLAS
    // of the previous frame (no build, no barrier). A set of the same size is refit in place.
    const bool reuse = cv_TlasReuse->value != 0.0f && m_built;
    if (reuse && instanceCount == previousCount && hash == previousHash) {
        ++m_skips;
        if (!m_skipLogged) {
            m_skipLogged = true;
            UZ_LOG(Renderer, Info,
                   "TLASManager: instance descriptors unchanged ({} instances, hash {:016x}); build skipped (full builds so far {}, refits {})",
                   instanceCount, hash, m_fullBuilds, m_refits);
        }
        return;
    }
    const bool refit = reuse && instanceCount == previousCount
                    && m_refitsSinceBuild < kRefitsBeforeRebuild;

    // Build (or refit) the TLAS
    cmdList->BuildTLAS(m_tlasHandle, m_instanceBuffer, instanceCount,
                       RHIBufferHandle::Invalid(), refit);
    if (refit) {
        ++m_refits;
        ++m_refitsSinceBuild;
    } else {
        ++m_fullBuilds;
        m_refitsSinceBuild = 0;
    }
    m_built = true;

    // UAV barrier after TLAS build
    cmdList->UAVBarrierAS();
}

uint32_t TLASManager::GetTLASSRVIndex() const {
    if (!m_device || !m_tlasHandle.IsValid()) return UINT32_MAX;
    return m_device->GetTLASSRVIndex(m_tlasHandle);
}

void TLASManager::Shutdown() {
    if (m_device && (m_fullBuilds || m_refits || m_skips)) {
        UZ_LOG(Renderer, Info, "TLASManager: builds full={} refit={} skipped={} (last {} instances)",
               m_fullBuilds, m_refits, m_skips, m_lastInstanceCount);
    }
    if (m_device) {
        if (m_instanceBuffer.IsValid()) {
            m_device->DestroyBuffer(m_instanceBuffer);
        }
        if (m_tlasHandle.IsValid()) {
            m_device->DestroyTLAS(m_tlasHandle);
        }
    }
    m_instanceBuffer = RHIBufferHandle::Invalid();
    m_tlasHandle     = RHITLASHandle::Invalid();
    m_device         = nullptr;
    m_lastInstanceCount = 0;
    m_lastInstanceHash = 0;
    m_fullBuilds = m_refits = m_skips = m_refitsSinceBuild = 0;
    m_built = false;
    m_skipLogged = false;
}

} // namespace uz
