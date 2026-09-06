/// UzEngine Pipeline Library — Shadow pipelines (shadow depth, instanced variants).

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "shader_manager.h"
#include "vertex.h"
#include "vertex_layout.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Depth Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureShadowShaders() {
    if (!m_shadowVS.IsValid()) {
        m_shadowVS = m_shaderMgr->CreateShader(EngineShader::ShadowDepthVS, RHIShaderStage::Vertex);
        if (!m_shadowVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ShadowDepthVS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetShadowDepth() {
    return GetShadowDepth(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetShadowDepth(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetShadowDepth called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    auto& cached = bindless ? m_shadowDepthBindless : m_shadowDepth;
    if (cached.IsValid()) {
        return cached;
    }

    if (!EnsureShadowShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_shadowVS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back; // Shadow casters — cull back faces
    // standard: slope-scaled depth bias in rasterizer state.
    // Source: ShadowRendering.cpp:38 — CSMSlopeScaleDepthBias default 3.0
    // standard: CSMSlopeScaleDepthBias=3.0 (ShadowRendering.cpp:38)
    // standard: CSMDepthBias=10.0 applied as rasterizer constant bias (ShadowRendering.cpp:32)
    desc.rasterizer.slopeScaledDepthBias  = 3.0f;
    desc.rasterizer.depthBias             = 10.0f;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    // Shadow maps use STANDARD-Z (0=near, 1=far) — orthographic light projection via
    // XMMatrixOrthographicOffCenterLH produces standard-Z naturally.
    // Less comparison: fragment closer to light (smaller Z) overwrites farther fragments.
    // Clear value kShadowDepthClear=1.0 (far plane). SCF_Less for SampleCmpLevelZero.
    // @ref shadow_cascade.cpp:295 — standard-Z documentation
    desc.depthStencil.depthFunc           = RHICompareFunc::Less;
    desc.renderTargetCount                = 0;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "ShadowDepth_Bindless" : "ShadowDepth";

    const auto layout = VertexLayout::Standard();  // hoisted: desc.inputElements references it
    // (must outlive the pipeline creation call below)
    if (!bindless) {

        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    cached = CreateCachedPipeline(desc);
    if (!cached.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ShadowDepth pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ShadowDepth pipeline (bindless={})", bindless);
    }

    return cached;
}

// ─────────────────────────────────────────────────────────────────────────────
// Instanced Pipelines — GPU Instancing variants (UZ_INSTANCED=1)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureInstancedMeshShaders() {
    // GPU instancing requires bindless (StructuredBuffer vertex/instance fetch via
    // ResourceDescriptorHeap). On non-bindless (SM6.0/DX11), instancing is not
    // available — the engine falls back to per-object draw calls.
    if (!m_device->IsBindless()) return false;

    if (!m_instancedMeshVS.IsValid()) {
        m_instancedMeshVS = m_shaderMgr->CreateShaderWithDefines(
            EngineShader::MeshVS, RHIShaderStage::Vertex, "UZ_INSTANCED=1");
        if (!m_instancedMeshVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MeshVS (instanced)");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureInstancedShadowShaders() {
    if (!m_device->IsBindless()) return false;

    if (!m_instancedShadowVS.IsValid()) {
        m_instancedShadowVS = m_shaderMgr->CreateShaderWithDefines(
            EngineShader::ShadowDepthVS, RHIShaderStage::Vertex, "UZ_INSTANCED=1");
        if (!m_instancedShadowVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ShadowDepthVS (instanced)");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetGBufferInstanced() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBufferInstanced called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_gbufferInstanced.IsValid()) {
        return m_gbufferInstanced;
    }

    if (!EnsureInstancedMeshShaders() || !EnsureGBufferPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_instancedMeshVS;
    desc.pixelShader   = m_gbufferPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back; // Opaque instanced geometry — cull back faces
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;   // octNormal+metallic+roughness
    desc.renderTargetFormats[2]           = RHIFormat::RGBA32Float;
    desc.renderTargetFormats[3]           = RHIFormat::RGBA16Float;   // HDR emissive color
    desc.renderTargetCount                = 4;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = "GBuffer_Instanced";

    m_gbufferInstanced = CreateCachedPipeline(desc);
    if (!m_gbufferInstanced.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBuffer_Instanced pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBuffer_Instanced pipeline");
    }

    return m_gbufferInstanced;
}

RHIPipelineHandle PipelineLibrary::GetShadowDepthInstanced() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetShadowDepthInstanced called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_shadowDepthInstanced.IsValid()) {
        return m_shadowDepthInstanced;
    }

    if (!EnsureInstancedShadowShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_instancedShadowVS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back; // Shadow casters — cull back faces
    // standard: same bias as non-instanced shadow PSO
    desc.rasterizer.slopeScaledDepthBias  = 3.0f;
    desc.rasterizer.depthBias             = 10.0f;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    // Shadow maps use STANDARD-Z (0=near, 1=far) — same as non-instanced path.
    desc.depthStencil.depthFunc           = RHICompareFunc::Less;
    desc.renderTargetCount                = 0;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = "ShadowDepth_Instanced";

    m_shadowDepthInstanced = CreateCachedPipeline(desc);
    if (!m_shadowDepthInstanced.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ShadowDepth_Instanced pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ShadowDepth_Instanced pipeline");
    }

    return m_shadowDepthInstanced;
}

// ─────────────────────────────────────────────────────────────────────────────

} // namespace uz
