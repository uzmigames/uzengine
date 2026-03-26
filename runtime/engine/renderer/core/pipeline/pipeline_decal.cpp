/// UzEngine Pipeline Library — Decal pipelines.
///
/// Provides lazily-created PSOs for the DBuffer decal system:
///   - DecalRender: decal_vs + decal_ps, 3 RT alpha blend, front-face cull
///   - DecalApply: decal_apply_cs, fullscreen compute merge
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"

#include "pipeline_library.h"

#include "engine_shaders.h"
#include "engine/rhi/rhi.h"
#include "log.h"
#include "shader_manager.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Shader compilation helpers
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureDecalShaders() {
    if (!m_decalVS.IsValid()) {
        m_decalVS = m_shaderMgr->CreateShader(EngineShader::DecalVS, RHIShaderStage::Vertex);
        if (!m_decalVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DecalVS");
            return false;
        }
    }
    if (!m_decalPS.IsValid()) {
        m_decalPS = m_shaderMgr->CreateShader(EngineShader::DecalPS, RHIShaderStage::Pixel);
        if (!m_decalPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DecalPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureDecalApplyShader() {
    if (!m_decalApplyCS.IsValid()) {
        m_decalApplyCS = m_shaderMgr->CreateShader(
            EngineShader::DecalApplyCS, RHIShaderStage::Compute);
        if (!m_decalApplyCS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DecalApplyCS");
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetDecalRender — 3 RT alpha blend, front-face cull, no depth
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetDecalRender() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDecalRender called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_decalRender.IsValid()) {
        return m_decalRender;
    }

    if (!EnsureDecalShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_decalVS;
    desc.pixelShader   = m_decalPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // Front-face culling: render back faces of the decal OBB so the decal
    // is visible when the camera is outside the box. When the camera enters
    // the OBB, all back faces are behind the camera and the decal disappears
    // gracefully (correct behavior — camera is inside the decal volume).
    desc.rasterizer.cullMode              = RHICullMode::Front;
    desc.rasterizer.frontCounterClockwise = false;

    // No depth test or write — depth is read as an SRV in the pixel shader.
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;

    // Alpha blending on all 3 DBuffer render targets: SrcAlpha / InvSrcAlpha.
    // This allows multiple overlapping decals to blend correctly by priority order.
    for (uint32_t i = 0; i < 3; ++i) {
        desc.renderTargetBlend[i].blendEnable = true;
        desc.renderTargetBlend[i].srcColor    = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[i].dstColor    = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[i].colorOp     = RHIBlendOp::Add;
        desc.renderTargetBlend[i].srcAlpha    = RHIBlendFactor::One;
        desc.renderTargetBlend[i].dstAlpha    = RHIBlendFactor::InvSrcAlpha;
        desc.renderTargetBlend[i].alphaOp     = RHIBlendOp::Add;
    }

    desc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;  // DBuffer A
    desc.renderTargetFormats[1] = RHIFormat::RGBA8Unorm;  // DBuffer B
    desc.renderTargetFormats[2] = RHIFormat::RGBA8Unorm;  // DBuffer C
    desc.renderTargetCount      = 3;
    desc.depthStencilFormat     = RHIFormat::Unknown;      // No depth attachment
    desc.debugName              = "DecalRender";

    // No vertex input layout — the VS generates cube vertices from SV_VertexID.

    m_decalRender = CreateCachedPipeline(desc);
    if (!m_decalRender.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DecalRender pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DecalRender pipeline");
    }

    return m_decalRender;
}

// ─────────────────────────────────────────────────────────────────────────────
// GetDecalApply — compute pipeline (8x8x1)
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetDecalApply() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDecalApply called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_decalApply.IsValid()) {
        return m_decalApply;
    }

    if (!EnsureDecalApplyShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_decalApplyCS;
    desc.debugName     = "DecalApply";

    m_decalApply = m_device->CreateComputePipeline(desc);
    if (!m_decalApply.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DecalApply compute pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DecalApply compute pipeline");
    }

    return m_decalApply;
}

} // namespace uz
