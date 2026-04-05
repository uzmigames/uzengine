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

    // ForwardPBR uses Unlit shading model as a key differentiator from GBuffer
    // (both are Opaque blend mode, but ForwardPBR is the single-pass forward path).
    const ShaderPermutationKey key(EShadingModel::Unlit, EBlendMode::Opaque, bindless);
    const auto cached = m_permutationCache.Lookup(key);
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
    desc.rasterizer.cullMode              = RHICullMode::Back; // Opaque geometry — cull back faces to halve overdraw
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

    auto handle = CreateCachedPipeline(desc);
    if (handle.IsValid()) {
        m_permutationCache.Insert(key, handle);
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ForwardPBR pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ForwardPBR pipeline (bindless={})", bindless);
    }

    return handle;
}

RHIPipelineHandle PipelineLibrary::GetGBuffer() {
    return GetGBuffer(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetGBuffer(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBuffer called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    const ShaderPermutationKey key(EShadingModel::DefaultLit, EBlendMode::Opaque, bindless);
    const auto cached = m_permutationCache.Lookup(key);
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
    desc.rasterizer.cullMode              = RHICullMode::Back; // Opaque geometry — cull back faces to halve overdraw
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    // G-Buffer render targets — must match GBuffer::Create() texture formats exactly
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;    // albedo+AO
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;   // octNormal+metallic+roughness
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

    auto handle = CreateCachedPipeline(desc);
    if (handle.IsValid()) {
        m_permutationCache.Insert(key, handle);
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBuffer pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBuffer pipeline (bindless={})", bindless);
    }

    return handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer Masked Pipeline (alpha test, two-sided)
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetGBufferMasked() {
    return GetGBufferMasked(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetGBufferMasked(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBufferMasked called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    const ShaderPermutationKey key(EShadingModel::DefaultLit, EBlendMode::Masked, bindless);
    const auto cached = m_permutationCache.Lookup(key);
    if (cached.IsValid()) {
        return cached;
    }

    if (!EnsureMeshShaders() || !EnsureGBufferPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    // Masked materials use the same GBuffer pixel shader as opaque — the alpha
    // test is driven by the blendMode field in PerObjectCB (shader reads blendMode
    // and discards when opacity < alphaClipThreshold). The PSO differs only in
    // rasterizer state: CullMode::None for two-sided masked foliage/fences.
    //
    // Note: [earlydepthstencil] is stripped when the compiler detects `discard`
    // in the shader, so depth is correctly written only for surviving fragments.
    // Two-sided rendering for masked materials: disable back-face culling.
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = m_gbufferPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::None; // Two-sided for masked materials (foliage, fences)
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;    // albedo+AO
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;   // octNormal+metallic+roughness
    desc.renderTargetFormats[2]           = RHIFormat::RGBA32Float;   // world-space position
    desc.renderTargetFormats[3]           = RHIFormat::RGBA16Float;   // HDR emissive
    desc.renderTargetCount                = 4;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "GBufferMasked_Bindless" : "GBufferMasked";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    auto handle = CreateCachedPipeline(desc);
    if (handle.IsValid()) {
        m_permutationCache.Insert(key, handle);
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBufferMasked pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBufferMasked pipeline (bindless={})", bindless);
    }

    return handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Translucent Forward Pipeline (SrcAlpha/InvSrcAlpha, depth read no write)
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetTranslucent() {
    return GetTranslucent(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetTranslucent(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTranslucent called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    const ShaderPermutationKey key(EShadingModel::DefaultLit, EBlendMode::Translucent, bindless);
    const auto cached = m_permutationCache.Lookup(key);
    if (cached.IsValid()) {
        return cached;
    }

    if (!EnsureMeshShaders() || !EnsureTranslucentPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    // Translucent objects use the translucent pixel shader which handles
    // glass/transmission (smID==9) with Fresnel-based alpha and standard
    // translucent materials with matOpacity alpha. Rendered after deferred
    // lighting into the HDR scene color with alpha blending.
    //
    // SrcAlpha/InvSrcAlpha blend, no depth write for translucent.
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = m_translucentPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back; // Front faces only — back faces cause Fresnel artifacts on glass
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = false;  // Read depth, do NOT write
    // Premultiplied alpha blend (One, InvSrcAlpha):
    //   final.rgb = src.rgb + dst.rgb * (1 - src.a)
    // The pixel shader pre-multiplies color by alpha (Fresnel for glass).
    // This avoids HDR color bleeding at low alpha (SrcAlpha * HDR >> 1.0).
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA16Float;  // HDR scene color
    desc.renderTargetCount                = 1;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "Translucent_Bindless" : "Translucent";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    auto handle = CreateCachedPipeline(desc);
    if (handle.IsValid()) {
        m_permutationCache.Insert(key, handle);
        UZ_LOG(Renderer, Info, "PipelineLibrary: created Translucent pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create Translucent pipeline (bindless={})", bindless);
    }

    return handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Additive Forward Pipeline (One/One, depth read no write)
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetAdditive() {
    return GetAdditive(m_device->IsBindless());
}

RHIPipelineHandle PipelineLibrary::GetAdditive(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetAdditive called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    const ShaderPermutationKey key(EShadingModel::DefaultLit, EBlendMode::Additive, bindless);
    const auto cached = m_permutationCache.Lookup(key);
    if (cached.IsValid()) {
        return cached;
    }

    if (!EnsureMeshShaders() || !EnsureForwardPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    // Additive blending adds the fragment color to the existing scene color.
    // Used for particle effects, laser beams, glow effects, etc.
    // No sorting required since the blend operation is commutative.
    //
    // One/One additive blend for additive materials.
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = m_meshPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::None; // Two-sided for additive effects (particles, glow)
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = false;  // Read depth, do NOT write
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA16Float;  // HDR scene color
    desc.renderTargetCount                = 1;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "Additive_Bindless" : "Additive";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    auto handle = CreateCachedPipeline(desc);
    if (handle.IsValid()) {
        m_permutationCache.Insert(key, handle);
        UZ_LOG(Renderer, Info, "PipelineLibrary: created Additive pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create Additive pipeline (bindless={})", bindless);
    }

    return handle;
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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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

RHIPipelineHandle PipelineLibrary::GetPixelDebugCompute() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetPixelDebugCompute called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_pixelDebugCompute.IsValid()) {
        return m_pixelDebugCompute;
    }

    if (!EnsurePixelDebugComputeShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_pixelDebugCS;
    desc.debugName     = "PixelDebugCompute";

    m_pixelDebugCompute = m_device->CreateComputePipeline(desc);
    if (!m_pixelDebugCompute.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create PixelDebugCompute pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created PixelDebugCompute pipeline");
    }

    return m_pixelDebugCompute;
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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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
    desc.rasterizer.cullMode              = RHICullMode::Back; // Opaque depth — cull back faces
    // No depth bias — unlike the shadow pass, the depth prepass writes to the
    // GBuffer depth and does not need bias to prevent self-shadowing.
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
    desc.rasterizer.cullMode              = RHICullMode::Back; // Opaque depth — cull back faces
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
