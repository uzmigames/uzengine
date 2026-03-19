/// UzEngine Pipeline Library — Debug visualization pipeline (PassInspector vis).

#include "pipeline_library.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "rhi.h"
#include "log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Vis Fullscreen Pipeline (PassInspector)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureVisFullscreenPixelShader() {
    if (!m_visFullscreenPS.IsValid()) {
        m_visFullscreenPS = m_shaderMgr->CreateShader(EngineShader::VisFullscreenPS, RHIShaderStage::Pixel);
        if (!m_visFullscreenPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile VisFullscreenPS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetVisFullscreen() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetVisFullscreen called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_visFullscreen.IsValid()) {
        return m_visFullscreen;
    }

    if (!EnsureFullscreenShaders() || !EnsureVisFullscreenPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_visFullscreenPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount        = 1;
    desc.debugName                = "VisFullscreen";

    m_visFullscreen = CreateCachedPipeline(desc);
    if (!m_visFullscreen.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VisFullscreen pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created VisFullscreen pipeline");
    }

    return m_visFullscreen;
}

} // namespace uz
