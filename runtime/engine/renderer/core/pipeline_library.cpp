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
    destroyPipeline(m_shadowDepth);
    destroyPipeline(m_shadowDepthBindless);

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
    destroyShader(m_shadowVS);

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
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;
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
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
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
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
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
    desc.depthStencil.depthEnable         = true;
    desc.depthStencil.depthWrite          = true;
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
    warmup(GetShadowDepth(bindless), "ShadowDepth");

    // SM5 variants if device is bindless (need both paths)
    if (bindless) {
        warmup(GetForwardPBR(false), "ForwardPBR_SM5");
        warmup(GetGBuffer(false), "GBuffer_SM5");
        warmup(GetShadowDepth(false), "ShadowDepth_SM5");
    }

    UZ_LOG(Renderer, Info, "PipelineLibrary: warmed up {} pipelines", count);
    return count;
}

} // namespace uz
