/// UzEngine Pipeline Library — Atmosphere pipelines (sky, height fog, transmittance LUT, multi-scatter, cubemap).

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
// Aerial Perspective Volume Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureAPVolumeShader() {
    if (m_apVolumeCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::AerialPerspectiveVolumeCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile AerialPerspectiveVolumeCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "aerial_perspective_volume_cs";

    m_apVolumeCS = m_device->CreateShader(shaderDesc);
    if (!m_apVolumeCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create AerialPerspectiveVolumeCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetAerialPerspectiveVolume() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetAerialPerspectiveVolume called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_apVolume.IsValid()) {
        return m_apVolume;
    }

    if (!EnsureAPVolumeShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_apVolumeCS;
    desc.debugName     = "AerialPerspectiveVolume";

    m_apVolume = m_device->CreateComputePipeline(desc);
    if (!m_apVolume.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create AerialPerspectiveVolume pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created AerialPerspectiveVolume pipeline");
    }

    return m_apVolume;
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

} // namespace uz
