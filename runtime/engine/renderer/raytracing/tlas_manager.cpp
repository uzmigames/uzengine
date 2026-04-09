/// TLASManager Implementation — Per-frame TLAS rebuild from draw commands.

#include "tlas_manager.h"
#include "core/gbuffer/draw_list.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "log.h"

#include <cstring>

namespace uz {

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
    for (uint32_t i = 0; i < drawCount && instanceCount < m_maxInstances; ++i) {
        const auto& cmd = drawCommands[i];

        if (cmd.meshId >= meshCount) continue;
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

        // Transform: DXR expects a 3x4 row-major affine matrix.
        // Mat4 wraps XMFLOAT4X4 (row-major in memory: _RC notation).
        // DXR instance transform is row-major 3x4, so we copy rows 0-2 directly.
        const auto& xm = cmd.worldMatrix.m;
        auto* t = reinterpret_cast<float*>(inst);
        // Row 0
        t[0]  = xm._11; t[1]  = xm._12; t[2]  = xm._13; t[3]  = xm._14;
        // Row 1
        t[4]  = xm._21; t[5]  = xm._22; t[6]  = xm._23; t[7]  = xm._24;
        // Row 2
        t[8]  = xm._31; t[9]  = xm._32; t[10] = xm._33; t[11] = xm._34;

        // InstanceID (24 bits) | InstanceMask (8 bits) at byte offset 48
        uint32_t idAndMask = (cmd.meshId & 0x00FFFFFF) | (0xFF << 24);
        std::memcpy(inst + 48, &idAndMask, 4);

        // InstanceContributionToHitGroupIndex (24 bits) | Flags (8 bits) at byte offset 52
        uint32_t hitAndFlags = 0; // hit group 0, no special flags
        std::memcpy(inst + 52, &hitAndFlags, 4);

        // AccelerationStructure GPU VA at byte offset 56
        std::memcpy(inst + 56, &blasAddress, 8);

        ++instanceCount;
    }

    m_device->UnmapBuffer(m_instanceBuffer);
    m_lastInstanceCount = instanceCount;

    if (instanceCount == 0) return;

    // Build the TLAS
    cmdList->BuildTLAS(m_tlasHandle, m_instanceBuffer, instanceCount,
                       RHIBufferHandle::Invalid(), false);

    // UAV barrier after TLAS build
    cmdList->UAVBarrierAS();
}

uint32_t TLASManager::GetTLASSRVIndex() const {
    if (!m_device || !m_tlasHandle.IsValid()) return UINT32_MAX;
    return m_device->GetTLASSRVIndex(m_tlasHandle);
}

void TLASManager::Shutdown() {
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
}

} // namespace uz
