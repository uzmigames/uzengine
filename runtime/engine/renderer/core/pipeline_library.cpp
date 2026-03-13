/// UzEngine Pipeline Library — Implementation.

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
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

PipelineLibrary::~PipelineLibrary() {
    if (m_initialized) {
        Shutdown();
    }
}

bool PipelineLibrary::Init(IRHIDevice* device, ShaderManager* shaderMgr) {
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

    cached = m_device->CreateGraphicsPipeline(desc);
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
    // G-Buffer render targets match GBuffer class formats
    desc.renderTargetFormats[0]           = RHIFormat::RGBA8Unorm;   // albedo
    desc.renderTargetFormats[1]           = RHIFormat::RGBA16Float;  // normals
    desc.renderTargetFormats[2]           = RHIFormat::RGBA8Unorm;   // metallic/roughness
    desc.renderTargetCount                = 3;
    desc.depthStencilFormat               = RHIFormat::D32Float;
    desc.debugName                        = bindless ? "GBuffer_Bindless" : "GBuffer";

    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    cached = m_device->CreateGraphicsPipeline(desc);
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
    m_lighting = m_device->CreateGraphicsPipeline(desc);
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

    m_gbufferDebug = m_device->CreateGraphicsPipeline(desc);
    if (!m_gbufferDebug.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GBufferDebug pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GBufferDebug pipeline");
    }

    return m_gbufferDebug;
}

} // namespace uz
