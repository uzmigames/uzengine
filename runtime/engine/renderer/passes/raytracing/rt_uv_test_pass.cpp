/// RT UV Test Pass — Implementation.
///
/// Compiles rt_uv_test_lib.hlsl as lib_6_6, creates RTPSO, builds shader
/// tables, and dispatches rays to write a UV gradient to a UAV texture.
///
/// @ref Microsoft DXR 1.0 Spec — DispatchRays, D3D12_STATE_OBJECT_DESC.

#include "rt_uv_test_pass.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/shader/shader_manager.h"
#include "engine/renderer/raytracing/shader_table_builder.h"
#include "engine/shader/engine_shaders.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

bool RTUVTestPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                         uint32_t width, uint32_t height)
{
    if (m_initialized) return true;
    if (!device || !shaderMgr) return false;

    m_device = device;
    m_width  = width;
    m_height = height;

    // ── Step 1: Compile the RT library shader (lib_6_6) ──────────────────
    m_libShader = shaderMgr->CreateShader(EngineShader::RtUvTestLib,
                                           RHIShaderStage::Library);
    if (!m_libShader.IsValid()) {
        UZ_LOG(Renderer, Error, "RTUVTestPass: failed to compile rt_uv_test_lib.hlsl");
        return false;
    }

    // ── Step 2: Create the RTPSO ─────────────────────────────────────────
    RHIRTHitGroupDesc hitGroup{};
    hitGroup.hitGroupName      = L"HitGroup";
    hitGroup.closestHitExport  = L"ClosestHit";

    RHIRTPipelineDesc psoDesc{};
    psoDesc.libraryShaders     = &m_libShader;
    psoDesc.libraryCount       = 1;
    psoDesc.hitGroups          = &hitGroup;
    psoDesc.hitGroupCount      = 1;
    psoDesc.maxPayloadSizeInBytes   = sizeof(float) * 4;  // RayPayload { float4 color }
    psoDesc.maxAttributeSizeInBytes = sizeof(float) * 2;  // BuiltInTriangleIntersectionAttributes
    psoDesc.maxRecursionDepth  = 1;

    m_rtPipeline = device->CreateRaytracingPipeline(psoDesc);
    if (!m_rtPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "RTUVTestPass: failed to create RTPSO");
        device->DestroyShader(m_libShader);
        m_libShader = RHIShaderHandle::Invalid();
        return false;
    }

    // ── Step 3: Build shader tables ──────────────────────────────────────
    ShaderTableBuilder builder;
    builder.AddRayGenShader(L"RayGen");
    builder.AddMissShader(L"Miss");
    builder.AddHitGroup(L"HitGroup");

    auto tables = builder.Build(device, m_rtPipeline);
    if (!tables.rayGenBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "RTUVTestPass: failed to build shader tables");
        device->DestroyRaytracingPipeline(m_rtPipeline);
        m_rtPipeline = RHIRTPipelineHandle::Invalid();
        device->DestroyShader(m_libShader);
        m_libShader = RHIShaderHandle::Invalid();
        return false;
    }

    m_rayGenSBT     = tables.rayGenBuffer;
    m_missSBT       = tables.missBuffer;
    m_hitGroupSBT   = tables.hitGroupBuffer;
    m_rayGenRange   = tables.rayGenRange;
    m_missRange     = tables.missRange;
    m_hitGroupRange = tables.hitGroupRange;

    // ── Step 4: Create output UAV texture (RGBA8Unorm) ───────────────────
    {
        RHITextureDesc texDesc{};
        texDesc.width   = width;
        texDesc.height  = height;
        texDesc.format  = RHIFormat::RGBA8Unorm;
        texDesc.usage   = RHITextureUsage::ShaderResource
                        | RHITextureUsage::UnorderedAccess;
        texDesc.debugName = "RTUVTestOutput";

        m_outputTexture = device->CreateTexture(texDesc);
        if (!m_outputTexture.IsValid()) {
            UZ_LOG(Renderer, Error, "RTUVTestPass: failed to create output texture");
            Shutdown(device);
            return false;
        }

        m_outputSrv = device->GetTextureSRVIndex(m_outputTexture);
        m_outputUav = device->GetTextureUAVIndex(m_outputTexture);
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info,
           "RTUVTestPass: initialized ({}x{}, RTPSO valid, SBT built, UAV={})",
           width, height, m_outputUav);
    return true;
}

void RTUVTestPass::Execute(IRHICommandList* cmd, uint32_t tlasSrvIndex)
{
    if (!m_initialized || !cmd) return;
    UZ_PROFILE_SCOPE("RTUVTestPass::Execute");

    // Transition output to UAV
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_outputTexture;
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // DispatchRays — the RHI binds the RTPSO internally, sets the global
    // root signature, and issues the DXR dispatch. The shader reads g_Output
    // (u0) via ResourceDescriptorHeap[outputUavIdx] — for now we use the
    // DXR local root signature pattern where the UAV is bound at register(u0)
    // in the global descriptor heap.
    //
    // Root param binding for RT:
    //   The RT shader uses `register(u0)` and `register(t0)` which map to
    //   the global root signature's descriptor tables. We set them before dispatch.
    cmd->SetDescriptorTable(4, m_outputUav);   // u0 table at root param 4
    if (tlasSrvIndex != UINT32_MAX) {
        cmd->SetDescriptorTable(2, tlasSrvIndex); // t0 table at root param 2
    }

    RHIDispatchRaysDesc dispatchDesc{};
    dispatchDesc.rayGenShaderTable = m_rayGenRange;
    dispatchDesc.missShaderTable   = m_missRange;
    dispatchDesc.hitGroupTable     = m_hitGroupRange;
    dispatchDesc.width             = m_width;
    dispatchDesc.height            = m_height;
    dispatchDesc.depth             = 1;

    cmd->DispatchRays(m_rtPipeline, dispatchDesc);

    // UAV barrier to ensure writes complete before SRV read
    cmd->UAVBarrierAS();

    // Transition back to SRV for compositing
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_outputTexture;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }
}

void RTUVTestPass::Shutdown(IRHIDevice* device)
{
    if (!device) return;

    if (m_outputTexture.IsValid()) {
        device->DestroyTexture(m_outputTexture);
        m_outputTexture = {};
    }

    ShaderTableBuilder::ShaderTableBuffers tables{
        m_rayGenSBT, m_missSBT, m_hitGroupSBT,
        m_rayGenRange, m_missRange, m_hitGroupRange};
    ShaderTableBuilder::DestroyTables(device, tables);
    m_rayGenSBT = {};
    m_missSBT = {};
    m_hitGroupSBT = {};

    if (m_rtPipeline.IsValid()) {
        device->DestroyRaytracingPipeline(m_rtPipeline);
        m_rtPipeline = RHIRTPipelineHandle::Invalid();
    }

    if (m_libShader.IsValid()) {
        device->DestroyShader(m_libShader);
        m_libShader = RHIShaderHandle::Invalid();
    }

    m_initialized = false;
    m_device = nullptr;
}

} // namespace uz
