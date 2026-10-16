/// UzEngine Pipeline Library — Geometry pipelines (ForwardPBR, GBuffer, Lighting, Debug, DepthPrepass).

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "cvar.h"
#include "rhi.h"
#include "shader_manager.h"
#include "vertex.h"
#include "vertex_layout.h"

// Diagnostic (followup-12, ledger H-45): 0 builds the forward translucent pipeline WITHOUT a depth test,
// so every rasterised translucent fragment reaches the pixel shader and the region dump. It exists to
// tell, at partly covered glass pixels, a fragment that was never rasterised from one hidden by opaque
// geometry. It changes the image and is never a shipping setting. Read when the pipeline is created.
static auto* cv_TranslucencyDepthTest = UZ_CVAR_INT("r.Translucency.DepthTest", 1,
    "Diagnostic: 0 = forward translucent pipeline without a depth test (changes the image)", 0, 1);

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

RHIPipelineHandle PipelineLibrary::GetVelocityObject(bool twoSided) {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    auto& pipeline = m_velocityObject[twoSided ? 1 : 0];
    if (pipeline != RHIPipelineHandle::Invalid()) return pipeline;
    if (!m_velocityObjectVS.IsValid())
        m_velocityObjectVS = m_shaderMgr->CreateShader(EngineShader::VelocityObjectVS, RHIShaderStage::Vertex);
    if (!m_velocityObjectPS.IsValid())
        m_velocityObjectPS = m_shaderMgr->CreateShader(EngineShader::VelocityObjectPS, RHIShaderStage::Pixel);
    if (!m_velocityObjectVS.IsValid() || !m_velocityObjectPS.IsValid()) {
        pipeline = RHIPipelineHandle::Failed();
        return pipeline;
    }
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader = m_velocityObjectVS;
    desc.pixelShader = m_velocityObjectPS;
    desc.topology = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode = twoSided ? RHICullMode::None : RHICullMode::Back;
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite = false;
    desc.depthStencil.depthFunc = RHICompareFunc::Equal;
    desc.depthStencilFormat = RHIFormat::D32Float;
    desc.renderTargetCount = 1;
    desc.renderTargetFormats[0] = RHIFormat::RG16Float;
    desc.debugName = twoSided ? "VelocityObject_TwoSided" : "VelocityObject";
    const auto layout = VertexLayout::Standard();
    if (!m_device->IsBindless()) {
        desc.inputElements = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride = layout.stride;
    }
    pipeline = CreateCachedPipeline(desc);
    return pipeline;
}

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

    // Hoisted out of the branch: `desc.inputElements` keeps a pointer into
    // `layout`, which must outlive the CreateCachedPipeline call below.
    const auto layout = VertexLayout::Standard();
    if (!bindless) {
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

    // Hoisted out of the branch: `desc.inputElements` keeps a pointer into
    // `layout`, which must outlive the CreateCachedPipeline call below.
    const auto layout = VertexLayout::Standard();
    if (!bindless) {
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
// GBuffer depth-only and Equal pipelines (block B.4c: the depth prepass)
// ─────────────────────────────────────────────────────────────────────────────
//
// The depth prepass is the GBuffer pass in a depth-only mode: the same vertex shader and constants
// as the colour pass, so the clip-space depths are the same bits, no pixel shader and no colour
// targets. The colour pass then draws standard opaque geometry with `Equal` and no depth write. The
// previous prepass used the shadow vertex shader (whose view-projection it never bound) with a
// `LessEqual` test against the reversed-Z clear: it drew nothing.

RHIPipelineHandle PipelineLibrary::GetGBufferDepthOnly(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBufferDepthOnly called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    RHIPipelineHandle& slot = m_gbufferDepthOnly[bindless ? 1 : 0];
    if (slot.IsValid()) {
        return slot;
    }
    if (!EnsureMeshShaders()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = RHIShaderHandle::Invalid();   // depth only
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;   // as the opaque GBuffer pipeline
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.depthStencil.depthFunc           = RHICompareFunc::Greater;   // reversed-Z, as the GBuffer
    desc.renderTargetCount                = 0;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "GBufferDepthOnly_Bindless" : "GBufferDepthOnly";

    const auto layout = VertexLayout::Standard();
    if (!bindless) {
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    slot = CreateCachedPipeline(desc);
    if (slot.IsValid()) {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBufferDepthOnly pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBufferDepthOnly pipeline (bindless={})", bindless);
    }
    return slot;
}

RHIPipelineHandle PipelineLibrary::GetGBufferEqual(bool bindless) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGBufferEqual called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    RHIPipelineHandle& slot = m_gbufferEqual[bindless ? 1 : 0];
    if (slot.IsValid()) {
        return slot;
    }
    if (!EnsureMeshShaders() || !EnsureGBufferPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_meshVS;
    desc.pixelShader   = m_gbufferPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = false;                   // the prepass wrote it
    desc.depthStencil.depthFunc           = RHICompareFunc::Equal;   // only the prepass's fragments
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;
    desc.renderTargetFormats[2]           = RHIFormat::RGBA32Float;
    desc.renderTargetFormats[3]           = RHIFormat::RGBA16Float;
    desc.renderTargetCount                = 4;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "GBufferEqual_Bindless" : "GBufferEqual";

    const auto layout = VertexLayout::Standard();
    if (!bindless) {
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    slot = CreateCachedPipeline(desc);
    if (slot.IsValid()) {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBufferEqual pipeline (bindless={})", bindless);
    } else {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBufferEqual pipeline (bindless={})", bindless);
    }
    return slot;
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

    // Hoisted out of the branch: `desc.inputElements` keeps a pointer into
    // `layout`, which must outlive the CreateCachedPipeline call below.
    const auto layout = VertexLayout::Standard();
    if (!bindless) {
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
    desc.depthStencil.depthEnable         = cv_TranslucencyDepthTest->value != 0;  // 1 unless diagnosing
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

    // Hoisted out of the branch: `desc.inputElements` keeps a pointer into
    // `layout`, which must outlive the CreateCachedPipeline call below.
    const auto layout = VertexLayout::Standard();
    if (!bindless) {
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

    // Hoisted out of the branch: `desc.inputElements` keeps a pointer into
    // `layout`, which must outlive the CreateCachedPipeline call below.
    const auto layout = VertexLayout::Standard();
    if (!bindless) {
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

// ─────────────────────────────────────────────────────────────────────────────
// Light cull compute pipeline (block B.7)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureLightCullShader() {
    if (m_lightCullCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ClusterAssignCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ClusterAssignCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "cluster_assign_cs";

    m_lightCullCS = m_device->CreateShader(shaderDesc);
    if (!m_lightCullCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ClusterAssignCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetLightCull() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetLightCull called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_lightCull.IsValid()) {
        return m_lightCull;
    }

    if (!EnsureLightCullShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_lightCullCS;
    desc.debugName     = "LightCull";

    m_lightCull = m_device->CreateComputePipeline(desc);
    if (!m_lightCull.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create LightCull pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created LightCull pipeline");
    }

    return m_lightCull;
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
    desc.renderTargetFormats[1]   = RHIFormat::RGBA16Float;  // Debug: direct lighting (Lo)
    desc.renderTargetFormats[2]   = RHIFormat::RGBA16Float;  // Debug: indirect lighting (ambient)
    desc.renderTargetFormats[3]   = RHIFormat::RGBA16Float;  // Debug: shadow/AO mask
    desc.renderTargetCount        = 4;
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

RHIPipelineHandle PipelineLibrary::GetGpuSkinning() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGpuSkinning called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_gpuSkinningPipeline.IsValid()) {
        return m_gpuSkinningPipeline;
    }

    if (!EnsureGpuSkinningShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_gpuSkinningCS;
    desc.debugName     = "GpuSkinning";

    m_gpuSkinningPipeline = m_device->CreateComputePipeline(desc);
    if (!m_gpuSkinningPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GpuSkinning pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GpuSkinning pipeline");
    }

    return m_gpuSkinningPipeline;
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

// ─────────────────────────────────────────────────────────────────────────────
// Path Tracer Pipeline (Step 1 — fullscreen magenta)
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::GetPathTracer() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetPathTracer called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_pathTracer.IsValid()) {
        return m_pathTracer;
    }

    if (!EnsureFullscreenShaders() || !EnsurePathTracerPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_pathTracerPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA32Float; // accum (Step 6)
    desc.renderTargetFormats[1]   = RHIFormat::RGBA16Float; // hdrSceneColor
    desc.renderTargetCount        = 2;
    desc.debugName                = "PathTracer";

    m_pathTracer = CreateCachedPipeline(desc);
    if (!m_pathTracer.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create PathTracer pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created PathTracer pipeline");
    }

    return m_pathTracer;
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

} // namespace uz
