/// UzEngine Pipeline Library — Implementation.

#include "pipeline_library.h"

#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "rhi_pso_cache.h"
#include "shader_manager.h"
#include "vertex.h"
#include "vertex_layout.h"

#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

UZ_DEFINE_STAT(RHI, PsoCacheHits);
UZ_DEFINE_STAT(RHI, PsoCacheMisses);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

PipelineLibrary::~PipelineLibrary() {
    if (m_initialized) {
        Shutdown();
    }
}

bool PipelineLibrary::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            const std::string& psoCacheDir) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "PipelineLibrary::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::Init failed: device is null");
        return false;
    }

    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::Init failed: shaderMgr is null");
        return false;
    }

    m_device = device;
    m_shaderMgr = shaderMgr;
    m_psoCache.Init();

    if (!psoCacheDir.empty()) {
        m_psoDiskCache = std::make_unique<PsoDiskCache>(psoCacheDir);
        UZ_LOG(Renderer, Info, "PipelineLibrary: PSO disk cache enabled at '{}'", psoCacheDir);
    }

    m_initialized = true;

    UZ_LOG(Renderer, Info, "PipelineLibrary initialized (PSOs will be created lazily)");
    return true;
}

void PipelineLibrary::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Destroy cached pipelines
    auto destroyPipeline = [this](RHIPipelineHandle& h) {
        if (h.IsValid()) {
            m_device->DestroyPipeline(h);
            h = RHIPipelineHandle::Invalid();
        }
    };

    destroyPipeline(m_forwardPBR);
    destroyPipeline(m_forwardPBRBindless);
    destroyPipeline(m_gbuffer);
    destroyPipeline(m_gbufferBindless);
    destroyPipeline(m_lighting);
    destroyPipeline(m_gbufferDebug);
    destroyPipeline(m_debugView);
    destroyPipeline(m_shadowDepth);
    destroyPipeline(m_shadowDepthBindless);
    destroyPipeline(m_gbufferInstanced);
    destroyPipeline(m_shadowDepthInstanced);
    destroyPipeline(m_skyAtmosphere);
    destroyPipeline(m_tonemap);
    destroyPipeline(m_heightFog);
    destroyPipeline(m_transmittanceLut);
    destroyPipeline(m_skyCubemapCapture);
    destroyPipeline(m_skyCubemapCapturePSPipeline);
    destroyPipeline(m_depthPrepass);
    destroyPipeline(m_depthPrepassBindless);
    destroyPipeline(m_depthPrepassInstanced);
    destroyPipeline(m_hizPyramid);

    // Destroy cached shaders
    auto destroyShader = [this](RHIShaderHandle& h) {
        if (h.IsValid()) {
            m_device->DestroyShader(h);
            h = RHIShaderHandle::Invalid();
        }
    };

    destroyShader(m_meshVS);
    destroyShader(m_meshPS);
    destroyShader(m_fullscreenVS);
    destroyShader(m_gbufferPS);
    destroyShader(m_lightingPS);
    destroyShader(m_gbufferDebugPS);
    destroyShader(m_debugViewPS);
    destroyShader(m_instancedMeshVS);
    destroyShader(m_instancedShadowVS);
    destroyShader(m_shadowVS);
    destroyShader(m_skyAtmospherePS);
    destroyShader(m_tonemapPS);
    destroyShader(m_heightFogPS);
    destroyShader(m_transmittanceLutCS);
    destroyShader(m_skyCubemapCaptureCS);
    destroyShader(m_skyCubemapCapturePSShader);
    destroyShader(m_hizPyramidCS);

    m_psoCache.Shutdown();
    m_psoDiskCache.reset();

    m_device = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "PipelineLibrary shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Compilation Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureMeshShaders() {
    if (!m_meshVS.IsValid()) {
        m_meshVS = m_shaderMgr->CreateShader(EngineShader::MeshVS, RHIShaderStage::Vertex);
        if (!m_meshVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MeshVS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureFullscreenShaders() {
    if (!m_fullscreenVS.IsValid()) {
        m_fullscreenVS = m_shaderMgr->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        if (!m_fullscreenVS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile FullscreenVS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureGBufferPixelShader() {
    if (!m_gbufferPS.IsValid()) {
        m_gbufferPS = m_shaderMgr->CreateShader(EngineShader::GbufferPS, RHIShaderStage::Pixel);
        if (!m_gbufferPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GbufferPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureLightingPixelShader() {
    if (!m_lightingPS.IsValid()) {
        m_lightingPS = m_shaderMgr->CreateShader(EngineShader::DeferredLightingPS, RHIShaderStage::Pixel);
        if (!m_lightingPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DeferredLightingPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureGBufferDebugPixelShader() {
    if (!m_gbufferDebugPS.IsValid()) {
        m_gbufferDebugPS = m_shaderMgr->CreateShader(EngineShader::GbufferDebugPS, RHIShaderStage::Pixel);
        if (!m_gbufferDebugPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GbufferDebugPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureDebugViewPixelShader() {
    if (!m_debugViewPS.IsValid()) {
        m_debugViewPS = m_shaderMgr->CreateShader(EngineShader::DebugViewPS, RHIShaderStage::Pixel);
        if (!m_debugViewPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DebugViewPS");
            return false;
        }
    }
    return true;
}

bool PipelineLibrary::EnsureForwardPixelShader() {
    if (!m_meshPS.IsValid()) {
        m_meshPS = m_shaderMgr->CreateShader(EngineShader::MeshPS, RHIShaderStage::Pixel);
        if (!m_meshPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MeshPS");
            return false;
        }
    }
    return true;
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
    desc.renderTargetCount                = 3;
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
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    // UE5 parity: slope-scaled depth bias in rasterizer state.
    // Source: ShadowRendering.cpp:38 — CSMSlopeScaleDepthBias default 3.0
    // UE5 parity: CSMSlopeScaleDepthBias=3.0 (ShadowRendering.cpp:38)
    // UE5 parity: CSMDepthBias=10.0 applied as rasterizer constant bias (ShadowRendering.cpp:32)
    desc.rasterizer.slopeScaledDepthBias  = 3.0f;
    desc.rasterizer.depthBias             = 10.0f;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    // Shadow maps use standard-Z (0=near, 1=far). LessEqual matches UE5 CF_LessEqual
    // (ShadowDepthRendering.cpp:760) — avoids self-shadow acne on coplanar surfaces.
    desc.depthStencil.depthFunc           = RHICompareFunc::LessEqual;
    desc.renderTargetCount                = 0;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "ShadowDepth_Bindless" : "ShadowDepth";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
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
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetFormats[1]           = RHIFormat::RGBA8Unorm;
    desc.renderTargetFormats[2]           = RHIFormat::RGBA32Float;
    desc.renderTargetCount                = 3;
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
    desc.rasterizer.cullMode              = RHICullMode::Back;
    desc.rasterizer.frontCounterClockwise = false;
    // UE5 parity: same bias as non-instanced shadow PSO
    desc.rasterizer.slopeScaledDepthBias  = 3.0f;
    desc.rasterizer.depthBias             = 10.0f;
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
    // Shadow maps use standard-Z (0=near, 1=far). LessEqual matches UE5 CF_LessEqual
    // (ShadowDepthRendering.cpp:760) — avoids self-shadow acne on coplanar surfaces.
    desc.depthStencil.depthFunc           = RHICompareFunc::LessEqual;
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
// Sky Atmosphere Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSkyAtmospherePixelShader() {
    if (!m_skyAtmospherePS.IsValid()) {
        m_skyAtmospherePS = m_shaderMgr->CreateShader(EngineShader::SkyAtmospherePS, RHIShaderStage::Pixel);
        if (!m_skyAtmospherePS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SkyAtmospherePS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSkyAtmosphere() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSkyAtmosphere called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_skyAtmosphere.IsValid()) {
        return m_skyAtmosphere;
    }

    if (!EnsureFullscreenShaders() || !EnsureSkyAtmospherePixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_skyAtmospherePS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "SkyAtmosphere";

    m_skyAtmosphere = CreateCachedPipeline(desc);
    if (!m_skyAtmosphere.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SkyAtmosphere pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SkyAtmosphere pipeline");
    }

    return m_skyAtmosphere;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tonemap Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureTonemapPixelShader() {
    if (!m_tonemapPS.IsValid()) {
        m_tonemapPS = m_shaderMgr->CreateShader(EngineShader::TonemapPS, RHIShaderStage::Pixel);
        if (!m_tonemapPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile TonemapPS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetTonemap() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTonemap called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_tonemap.IsValid()) {
        return m_tonemap;
    }

    if (!EnsureFullscreenShaders() || !EnsureTonemapPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_tonemapPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount        = 1;
    desc.debugName                = "Tonemap";

    m_tonemap = CreateCachedPipeline(desc);
    if (!m_tonemap.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create Tonemap pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created Tonemap pipeline");
    }

    return m_tonemap;
}

// ─────────────────────────────────────────────────────────────────────────────
// Height Fog Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureHeightFogPixelShader() {
    if (!m_heightFogPS.IsValid()) {
        m_heightFogPS = m_shaderMgr->CreateShader(EngineShader::HeightFogPS, RHIShaderStage::Pixel);
        if (!m_heightFogPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile HeightFogPS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetHeightFog() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetHeightFog called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_heightFog.IsValid()) {
        return m_heightFog;
    }

    if (!EnsureFullscreenShaders() || !EnsureHeightFogPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_heightFogPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "HeightFog";

    // Alpha blending: fog color * fogFactor + scene * (1 - fogFactor)
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::Zero;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;

    m_heightFog = CreateCachedPipeline(desc);
    if (!m_heightFog.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create HeightFog pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created HeightFog pipeline");
    }

    return m_heightFog;
}

// ─────────────────────────────────────────────────────────────────────────────
// Transmittance LUT Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureTransmittanceLutShader() {
    if (m_transmittanceLutCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::TransmittanceLutCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile TransmittanceLutCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "transmittance_lut_cs";

    m_transmittanceLutCS = m_device->CreateShader(shaderDesc);
    if (!m_transmittanceLutCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TransmittanceLutCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetTransmittanceLut() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTransmittanceLut called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_transmittanceLut.IsValid()) {
        return m_transmittanceLut;
    }

    if (!EnsureTransmittanceLutShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_transmittanceLutCS;
    desc.debugName     = "TransmittanceLUT";

    m_transmittanceLut = m_device->CreateComputePipeline(desc);
    if (!m_transmittanceLut.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TransmittanceLUT pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created TransmittanceLUT pipeline");
    }

    return m_transmittanceLut;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Scattering LUT Compute Pipeline

bool PipelineLibrary::EnsureMultiScatterLutShader() {
    if (m_multiScatterLutCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::MultiScatterLutCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MultiScatterLutCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "multi_scatter_lut_cs";

    m_multiScatterLutCS = m_device->CreateShader(shaderDesc);
    if (!m_multiScatterLutCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MultiScatterLutCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetMultiScatterLut() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetMultiScatterLut called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_multiScatterLut.IsValid()) {
        return m_multiScatterLut;
    }

    if (!EnsureMultiScatterLutShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_multiScatterLutCS;
    desc.debugName     = "MultiScatterLUT";

    m_multiScatterLut = m_device->CreateComputePipeline(desc);
    if (!m_multiScatterLut.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MultiScatterLUT pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created MultiScatterLUT pipeline");
    }

    return m_multiScatterLut;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky Cubemap Capture Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSkyCubemapCaptureShader() {
    if (m_skyCubemapCaptureCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SkyCubemapCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SkyCubemapCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "sky_cubemap_cs";

    m_skyCubemapCaptureCS = m_device->CreateShader(shaderDesc);
    if (!m_skyCubemapCaptureCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SkyCubemapCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSkyCubemapCapture() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSkyCubemapCapture called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_skyCubemapCapture.IsValid()) {
        return m_skyCubemapCapture;
    }

    if (!EnsureSkyCubemapCaptureShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_skyCubemapCaptureCS;
    desc.debugName     = "SkyCubemapCapture";

    m_skyCubemapCapture = m_device->CreateComputePipeline(desc);
    if (!m_skyCubemapCapture.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SkyCubemapCapture pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SkyCubemapCapture pipeline");
    }

    return m_skyCubemapCapture;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky Cubemap Capture Pixel Shader Pipeline (SM5 fallback)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSkyCubemapCapturePSShader() {
    if (m_skyCubemapCapturePSShader.IsValid()) {
        return true;
    }

    m_skyCubemapCapturePSShader = m_shaderMgr->CreateShader(EngineShader::SkyCubemapPS, RHIShaderStage::Pixel);
    if (!m_skyCubemapCapturePSShader.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SkyCubemapPS");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSkyCubemapCapturePS() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSkyCubemapCapturePS called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_skyCubemapCapturePSPipeline.IsValid()) {
        return m_skyCubemapCapturePSPipeline;
    }

    if (!EnsureFullscreenShaders() || !EnsureSkyCubemapCapturePSShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_skyCubemapCapturePSShader;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "SkyCubemapCapturePS";

    m_skyCubemapCapturePSPipeline = CreateCachedPipeline(desc);
    if (!m_skyCubemapCapturePSPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SkyCubemapCapturePS pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SkyCubemapCapturePS pipeline");
    }

    return m_skyCubemapCapturePSPipeline;
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
// Hi-Z Pyramid Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureHiZPyramidShader() {
    if (m_hizPyramidCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::HizPyramidCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile HizPyramidCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "hiz_pyramid_cs";

    m_hizPyramidCS = m_device->CreateShader(shaderDesc);
    if (!m_hizPyramidCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create HizPyramidCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetHiZPyramid() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetHiZPyramid called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_hizPyramid.IsValid()) {
        return m_hizPyramid;
    }

    if (!EnsureHiZPyramidShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_hizPyramidCS;
    desc.debugName     = "HiZPyramid";

    m_hizPyramid = m_device->CreateComputePipeline(desc);
    if (!m_hizPyramid.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create HiZPyramid pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created HiZPyramid pipeline");
    }

    return m_hizPyramid;
}

// ─────────────────────────────────────────────────────────────────────────────
// DFS Object Culling Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureDfsCullObjectsShader() {
    if (m_dfsCullObjectsCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DfsCullObjectsCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DfsCullObjectsCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "CSMain";
    shaderDesc.debugName    = "dfs_cull_objects_cs";

    m_dfsCullObjectsCS = m_device->CreateShader(shaderDesc);
    if (!m_dfsCullObjectsCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DfsCullObjectsCS shader");
        return false;
    }
    return true;
}

/// @source UE5 DistanceFieldShadowing.cpp:196-210 — CullObjectsForShadow pipeline creation
RHIPipelineHandle PipelineLibrary::GetDfsCullObjects() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDfsCullObjects called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_dfsCullObjects.IsValid()) {
        return m_dfsCullObjects;
    }

    if (!EnsureDfsCullObjectsShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_dfsCullObjectsCS;
    desc.debugName     = "DFS_CullObjects";

    m_dfsCullObjects = m_device->CreateComputePipeline(desc);
    if (!m_dfsCullObjects.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DFS_CullObjects pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DFS_CullObjects pipeline");
    }

    return m_dfsCullObjects;
}

// ─────────────────────────────────────────────────────────────────────────────
// DFS Cone Tracing Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureDfsConeTraceShader() {
    if (m_dfsConeTraceCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DfsConeTraceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DfsConeTraceCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "CSMain";
    shaderDesc.debugName    = "dfs_cone_trace_cs";

    m_dfsConeTraceCS = m_device->CreateShader(shaderDesc);
    if (!m_dfsConeTraceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DfsConeTraceCS shader");
        return false;
    }
    return true;
}

/// @source UE5 DistanceFieldShadowing.cpp:380-420 — DistanceFieldShadowing pipeline creation
RHIPipelineHandle PipelineLibrary::GetDfsConeTrace() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDfsConeTrace called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_dfsConeTrace.IsValid()) {
        return m_dfsConeTrace;
    }

    if (!EnsureDfsConeTraceShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_dfsConeTraceCS;
    desc.debugName     = "DFS_ConeTrace";

    m_dfsConeTrace = m_device->CreateComputePipeline(desc);
    if (!m_dfsConeTrace.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DFS_ConeTrace pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DFS_ConeTrace pipeline");
    }

    return m_dfsConeTrace;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAA Resolve Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureTaaResolveShader() {
    if (m_taaResolveCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::TaaResolveCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile TaaResolveCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "taa_resolve_cs";

    m_taaResolveCS = m_device->CreateShader(shaderDesc);
    if (!m_taaResolveCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TaaResolveCS shader");
        return false;
    }
    return true;
}

/// @source UE5 TemporalAA.cpp:395-396 — IMPLEMENT_GLOBAL_SHADER(FTemporalAACS, ..., "MainCS", SF_Compute)
RHIPipelineHandle PipelineLibrary::GetTaaResolve() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTaaResolve called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_taaResolve.IsValid()) {
        return m_taaResolve;
    }

    if (!EnsureTaaResolveShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_taaResolveCS;
    desc.debugName     = "TAA_Resolve";

    m_taaResolve = m_device->CreateComputePipeline(desc);
    if (!m_taaResolve.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TAA_Resolve pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created TAA_Resolve pipeline");
    }

    return m_taaResolve;
}

// ─────────────────────────────────────────────────────────────────────────────
// PSO Cache Integration
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle PipelineLibrary::CreateCachedPipeline(const RHIGraphicsPipelineDesc& desc) {
    PsoCacheKey key = ComputePsoCacheKey(desc);

    // In-memory cache check
    auto cached = m_psoCache.Lookup(key);
    if (cached.IsValid()) {
        UZ_STAT_INCREMENT(RHI, PsoCacheHits);
        UZ_LOG(Renderer, Debug, "PsoCache hit for '{}'", desc.debugName ? desc.debugName : "<unnamed>");
        return cached;
    }

    // Disk cache check — load blob if available
    std::optional<std::vector<uint8_t>> diskBlob;
    RHIGraphicsPipelineDesc descWithBlob = desc;

    if (m_psoDiskCache) {
        diskBlob = m_psoDiskCache->Lookup(key.hash);
        if (diskBlob && !diskBlob->empty()) {
            descWithBlob.cachedBlobData = diskBlob->data();
            descWithBlob.cachedBlobSize = diskBlob->size();
            UZ_LOG(Renderer, Debug, "PsoDiskCache hit for '{}' ({} bytes)",
                   desc.debugName ? desc.debugName : "<unnamed>", diskBlob->size());
        }
    }

    auto handle = m_device->CreateGraphicsPipeline(descWithBlob);
    if (handle.IsValid()) {
        m_psoCache.Insert(key, handle);
        UZ_STAT_INCREMENT(RHI, PsoCacheMisses);

        // Store blob to disk if we didn't load one (or if load was stale and
        // the driver compiled from scratch anyway)
        if (m_psoDiskCache && !diskBlob) {
            auto blob = m_device->GetPipelineCachedBlob(handle);
            if (!blob.empty()) {
                m_psoDiskCache->Store(key.hash, blob.data(), blob.size());
            }
        }

        UZ_LOG(Renderer, Debug, "PsoCache miss for '{}' — created and cached", desc.debugName ? desc.debugName : "<unnamed>");
    }
    return handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Warmup — Pre-create all standard pipelines
// ─────────────────────────────────────────────────────────────────────────────

uint32_t PipelineLibrary::WarmupAll() {
    UZ_PROFILE_FUNCTION();

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::WarmupAll called before Init");
        return 0;
    }

    const bool bindless = m_device->IsBindless();
    uint32_t count = 0;

    auto warmup = [&](RHIPipelineHandle h, const char* name) {
        if (h.IsValid()) {
            ++count;
        } else {
            UZ_LOG(Renderer, Warning, "WarmupAll: failed to create '{}'", name);
        }
    };

    warmup(GetForwardPBR(bindless), "ForwardPBR");
    warmup(GetGBuffer(bindless), "GBuffer");
    warmup(GetLighting(), "Lighting");
    warmup(GetGBufferDebug(), "GBufferDebug");
    warmup(GetDebugView(), "DebugView");
    warmup(GetShadowDepth(bindless), "ShadowDepth");
    warmup(GetSkyAtmosphere(), "SkyAtmosphere");
    warmup(GetHeightFog(), "HeightFog");
    warmup(GetTransmittanceLut(), "TransmittanceLUT");
    warmup(GetSkyCubemapCapture(), "SkyCubemapCapture");
    warmup(GetSkyCubemapCapturePS(), "SkyCubemapCapturePS");
    warmup(GetDepthPrepass(bindless), "DepthPrepass");
    warmup(GetHiZPyramid(), "HiZPyramid");
    warmup(GetDfsCullObjects(), "DFS_CullObjects");
    warmup(GetDfsConeTrace(), "DFS_ConeTrace");

    // SM5 variants if device is bindless (need both paths)
    if (bindless) {
        warmup(GetForwardPBR(false), "ForwardPBR_SM5");
        warmup(GetGBuffer(false), "GBuffer_SM5");
        warmup(GetShadowDepth(false), "ShadowDepth_SM5");
        warmup(GetDepthPrepass(false), "DepthPrepass_SM5");
    }

    UZ_LOG(Renderer, Info, "PipelineLibrary: warmed up {} pipelines", count);
    return count;
}

} // namespace uz
