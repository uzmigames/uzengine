/// Editor Grid Renderer Implementation
///
/// Fullscreen triangle + procedural XY plane grid. The pixel shader
/// reconstructs world position by intersecting a camera ray with Z=0,
/// then draws anti-aliased grid lines with distance fade.
/// Coordinate system: X-forward, Y-right, Z-up.

#include "editor_grid.h"

#include "camera.h"
#include "constant_buffers.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "shader_pipeline.h"

#include <cstring>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(EditorGrid, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Grid CB — matches the HLSL cbuffer at b1
// ─────────────────────────────────────────────────────────────────────────────

struct GridCB {
    Mat4 invViewProj;
    Mat4 viewProj;
    Vec4 cameraPos;     // xyz = position, w = unused
    Vec4 gridParams;    // x = minorSpacing, y = majorInterval, z = fadeDistance, w = unused
};

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool EditorGrid::Init(IRHIDevice* device, ShaderPipeline& pipeline,
                      RHIFormat rtvFormat, RHIFormat dsvFormat) {
    m_device = device;

    // Compile embedded shaders (no VFS mount needed)
    auto vsBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GridVS),
        EngineShaders::GetSource(EngineShader::GridVS));
    if (vsBytecode.empty()) {
        UZ_LOG(EditorGrid, Error, "Failed to compile grid vertex shader");
        return false;
    }

    auto psBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GridPS),
        EngineShaders::GetSource(EngineShader::GridPS));
    if (psBytecode.empty()) {
        UZ_LOG(EditorGrid, Error, "Failed to compile grid pixel shader");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.debugName = "grid_vs";
    m_vs = device->CreateShader(vsDesc);

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.debugName = "grid_ps";
    m_ps = device->CreateShader(psDesc);

    // Pipeline — alpha blending enabled, depth test read-only (no write)
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader = m_vs;
    desc.pixelShader = m_ps;
    desc.renderTargetFormats[0] = rtvFormat;
    desc.renderTargetCount = 1;
    desc.depthStencilFormat = dsvFormat;

    // Depth: test only, no write — grid must not occlude geometry at Z=0
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite = false;
    desc.depthStencil.depthFunc = RHICompareFunc::LessEqual;

    // Rasterizer: no culling for fullscreen tri
    desc.rasterizer.cullMode = RHICullMode::None;

    // Alpha blending: srcAlpha * src + (1-srcAlpha) * dst
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
    desc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

    desc.debugName = "EditorGridPipeline";

    m_pipeline = device->CreateGraphicsPipeline(desc);
    if (!m_pipeline.IsValid()) {
        UZ_LOG(EditorGrid, Error, "Failed to create grid pipeline");
        return false;
    }

    UZ_LOG(EditorGrid, Info, "Editor grid initialized (spacing={}, major={}, fade={})",
           m_minorSpacing, m_majorInterval, m_fadeDistance);
    return true;
}

void EditorGrid::Shutdown() {
    m_device = nullptr;
    UZ_LOG(EditorGrid, Info, "Editor grid shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

void EditorGrid::Draw(IRHICommandList* cmdList, CBRingAllocator& cbRing, const Camera& camera) {
    Mat4 vp = camera.GetViewProjection();
    Mat4 invVP = vp.inverse();
    Vec3 camPos = camera.GetPosition();

    GridCB cb{};
    cb.invViewProj = invVP;
    cb.viewProj = vp;
    cb.cameraPos = {camPos.x, camPos.y, camPos.z, 0.f};
    cb.gridParams = {m_minorSpacing, static_cast<float>(m_majorInterval), m_fadeDistance, 0.f};

    CBAllocation alloc = cbRing.Allocate(sizeof(GridCB));
    std::memcpy(alloc.cpuPtr, &cb, sizeof(GridCB));

    cmdList->SetPipeline(m_pipeline);
    cmdList->SetRootCBV(1, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr, sizeof(GridCB));
    cmdList->Draw(3); // Fullscreen triangle — 3 vertices, no VB
}

} // namespace uz
