/// UzEngine Pipeline Library — Geometry pipelines (ForwardPBR, GBuffer, Lighting, Debug, DepthPrepass).

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
// Pipeline Getters
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetForwardPBR() {
    return GetForwardPBR(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetForwardPBR(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetForwardPBR called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    auto& cached = bindless ? m_forwardPBRBindless : m_forwardPBR;
    if (cached.IsValid()) {
        return cached;
    }

    if (!EnsureMeshShaders() || !EnsureForwardPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = m_meshPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA16Float;
    desc.renderTargetCount                = 1;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "ForwardPBR_Bindless" : "ForwardPBR";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    cached = CreateCachedPipeline(desc);
    if (!cached.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ForwardPBR pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ForwardPBR pipeline (bindless={})", bindless);
    }

    return cached;
}

RHIPipelineHandle PipelineLibrary::GetGBuffer() {
    return GetGBuffer(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetGBuffer(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBuffer called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    auto& cached = bindless ? m_gbufferBindless : m_gbuffer;
    if (cached.IsValid()) {
        return cached;
    }

    if (!EnsureMeshShaders() || !EnsureGBufferPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = m_gbufferPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    // G-Buffer render targets — must match GBuffer::Create() texture formats exactly
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;    // albedo+AO
    desc.renderTargetFormats[1]           = RHIFormat::RGBA8Unorm;    // octNormal+metallic+roughness
    desc.renderTargetFormats[2]           = RHIFormat::RGBA32Float;   // world-space position (debug)
    desc.renderTargetFormats[3]           = RHIFormat::RGBA16Float;   // HDR emissive color
    desc.renderTargetCount                = 4;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "GBuffer_Bindless" : "GBuffer";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    cached = CreateCachedPipeline(desc);
    if (!cached.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBuffer pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBuffer pipeline (bindless={})", bindless);
    }

    return cached;
}

RHIPipelineHandle PipelineLibrary::GetLighting() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetLighting called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_lighting.IsValid()) {
        return m_lighting;
    }

    if (!EnsureFullscreenShaders() || !EnsureLightingPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_lightingPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;  // HDR output (tonemapped separately)
    desc.renderTargetCount        = 1;
    desc.debugName                = "DeferredLighting";

    // Fullscreen passes never use vertex input layout
    m_lighting = CreateCachedPipeline(desc);
    if (!m_lighting.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create Lighting pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created Lighting pipeline");
    }

    return m_lighting;
}

RHIPipelineHandle PipelineLibrary::GetGBufferDebug() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBufferDebug called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_gbufferDebug.IsValid()) {
        return m_gbufferDebug;
    }

    if (!EnsureFullscreenShaders() || !EnsureGBufferDebugPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_gbufferDebugPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "GBufferDebug";

    m_gbufferDebug = CreateCachedPipeline(desc);
    if (!m_gbufferDebug.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBufferDebug pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBufferDebug pipeline");
    }

    return m_gbufferDebug;
}

RHIPipelineHandle PipelineLibrary::GetDebugView() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDebugView called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_debugView.IsValid()) {
        return m_debugView;
    }

    if (!EnsureFullscreenShaders() || !EnsureDebugViewPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_debugViewPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "DebugView";

    m_debugView = CreateCachedPipeline(desc);
    if (!m_debugView.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DebugView pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DebugView pipeline");
    }

    return m_debugView;
}

// ─────────────────────────────────────────────────────────────────────────────

// Depth Prepass Pipeline
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetDepthPrepass() {
    return GetDepthPrepass(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetDepthPrepass(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDepthPrepass called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    auto& cached = bindless ? m_depthPrepassBindless : m_depthPrepass;
    if (cached.IsValid()) {
        return cached;
    }

    // The depth prepass reuses the shadow depth vertex shader — it transforms
    // vertices to clip space with only the model + viewProj matrices. No pixel
    // shader is needed since we only write depth.
    if (!EnsureShadowShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_shadowVS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    // No depth bias — unlike the shadow pass, the depth prepass writes to the
    // GBuffer depth and does not need bias to prevent self-shadowing.
    /// @source UE5 DepthRendering.cpp — FDepthPassMeshProcessor uses no depth bias
    desc.rasterizer.slopeScaledDepthBias  = 0.0f;
    desc.rasterizer.depthBias             = 0.0f;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.depthStencil.depthFunc           = RHICompareFunc::LessEqual;
    desc.renderTargetCount                = 0;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "DepthPrepass_Bindless" : "DepthPrepass";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    cached = CreateCachedPipeline(desc);
    if (!cached.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DepthPrepass pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DepthPrepass pipeline (bindless={})", bindless);
    }

    return cached;
}

RHIPipelineHandle PipelineLibrary::GetDepthPrepassInstanced() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDepthPrepassInstanced called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_depthPrepassInstanced.IsValid()) {
        return m_depthPrepassInstanced;
    }

    if (!EnsureInstancedShadowShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_instancedShadowVS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    // No depth bias — same as non-instanced depth prepass.
    desc.rasterizer.slopeScaledDepthBias  = 0.0f;
    desc.rasterizer.depthBias             = 0.0f;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.depthStencil.depthFunc           = RHICompareFunc::LessEqual;
    desc.renderTargetCount                = 0;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = "DepthPrepass_Instanced";

    m_depthPrepassInstanced = CreateCachedPipeline(desc);
    if (!m_depthPrepassInstanced.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DepthPrepass_Instanced pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DepthPrepass_Instanced pipeline");
    }

    return m_depthPrepassInstanced;
}

// ─────────────────────────────────────────────────────────────────────────────

} // namespace uz
