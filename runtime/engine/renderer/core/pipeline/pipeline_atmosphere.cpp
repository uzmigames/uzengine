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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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
// HDRI Skybox Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureHdriSkyboxPixelShader() {
    if (!m_hdriSkyboxPS.IsValid()) {
        m_hdriSkyboxPS = m_shaderMgr->CreateShader(EngineShader::SkyboxCubemapPS, RHIShaderStage::Pixel);
        if (!m_hdriSkyboxPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SkyboxCubemapPS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetHdriSkybox() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetHdriSkybox called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_hdriSkybox.IsValid()) {
        return m_hdriSkybox;
    }

    if (!EnsureFullscreenShaders() || !EnsureHdriSkyboxPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_hdriSkyboxPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "HdriSkybox";

    m_hdriSkybox = CreateCachedPipeline(desc);
    if (!m_hdriSkybox.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create HdriSkybox pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created HdriSkybox pipeline");
    }

    return m_hdriSkybox;
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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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
// Cloud Composite Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureCloudCompositePixelShader() {
    if (m_cloudCompositePS.IsValid()) {
        return true;
    }

    m_cloudCompositePS = m_shaderMgr->CreateShader(EngineShader::CloudCompositePS, RHIShaderStage::Pixel);
    if (!m_cloudCompositePS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile CloudCompositePS");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetCloudComposite() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetCloudComposite called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_cloudComposite.IsValid()) {
        return m_cloudComposite;
    }

    if (!EnsureFullscreenShaders() || !EnsureCloudCompositePixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_cloudCompositePS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA16Float;
    desc.renderTargetCount        = 1;
    desc.debugName                = "CloudComposite";

    // Premultiplied-alpha compositing:
    //   final.rgb = cloud.rgb * 1 + scene.rgb * cloud.a (transmittance)
    // Where cloud output is: rgb = cloud_color (premultiplied), a = transmittance
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;

    m_cloudComposite = CreateCachedPipeline(desc);
    if (!m_cloudComposite.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create CloudComposite pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created CloudComposite pipeline");
    }

    return m_cloudComposite;
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

    // sky_cubemap_ps.hlsl is an SM5 fallback — always uses register(t0),
    // never ResourceDescriptorHeap. On bindless, the CS path is used instead
    // and this pipeline is never needed. Skip creation to avoid E_INVALIDARG
    // from register(t0) conflicting with the DIRECTLY_INDEXED root sig.
    if (m_device->IsBindless()) {
        return false; // Bindless uses compute sky cubemap capture — PS not needed.
    }

    m_skyCubemapCapturePSShader = m_shaderMgr->CreateShader(
        EngineShader::SkyCubemapPS, RHIShaderStage::Pixel);
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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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
// Volumetric Fog Pipelines
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureVolumetricFogInjectShader() {
    if (m_volFogInjectCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::VolumetricFogInjectCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile VolumetricFogInjectCS");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode = bytecode.data(); sd.bytecodeSize = bytecode.size();
    sd.stage = RHIShaderStage::Compute; sd.entryPoint = "main";
    sd.debugName = "volumetric_fog_inject_cs";

    m_volFogInjectCS = m_device->CreateShader(sd);
    if (!m_volFogInjectCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VolumetricFogInjectCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetVolumetricFogInject() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_volFogInject.IsValid()) return m_volFogInject;
    if (!EnsureVolumetricFogInjectShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_volFogInjectCS;
    desc.debugName     = "VolumetricFogInject";

    m_volFogInject = m_device->CreateComputePipeline(desc);
    if (!m_volFogInject.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VolumetricFogInject pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created VolumetricFogInject pipeline");
    }
    return m_volFogInject;
}

bool PipelineLibrary::EnsureVolumetricFogScatterShader() {
    if (m_volFogScatterCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::VolumetricFogScatterCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile VolumetricFogScatterCS");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode = bytecode.data(); sd.bytecodeSize = bytecode.size();
    sd.stage = RHIShaderStage::Compute; sd.entryPoint = "main";
    sd.debugName = "volumetric_fog_scatter_cs";

    m_volFogScatterCS = m_device->CreateShader(sd);
    if (!m_volFogScatterCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VolumetricFogScatterCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetVolumetricFogScatter() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_volFogScatter.IsValid()) return m_volFogScatter;
    if (!EnsureVolumetricFogScatterShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_volFogScatterCS;
    desc.debugName     = "VolumetricFogScatter";

    m_volFogScatter = m_device->CreateComputePipeline(desc);
    if (!m_volFogScatter.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VolumetricFogScatter pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created VolumetricFogScatter pipeline");
    }
    return m_volFogScatter;
}

bool PipelineLibrary::EnsureVolumetricFogCompositeShader() {
    if (m_volFogCompositePS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::VolumetricFogCompositePS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile VolumetricFogCompositePS");
        return false;
    }

    RHIShaderDesc sd{};
    sd.bytecode = bytecode.data(); sd.bytecodeSize = bytecode.size();
    sd.stage = RHIShaderStage::Pixel; sd.entryPoint = "main";
    sd.debugName = "volumetric_fog_composite_ps";

    m_volFogCompositePS = m_device->CreateShader(sd);
    if (!m_volFogCompositePS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VolumetricFogCompositePS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetVolumetricFogComposite() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_volFogComposite.IsValid()) return m_volFogComposite;
    if (!EnsureFullscreenShaders() || !EnsureVolumetricFogCompositeShader())
        return RHIPipelineHandle::Invalid();

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_volFogCompositePS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None;
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    // Alpha blend: SrcAlpha / InvSrcAlpha (standard fog compositing)
    desc.renderTargetBlend[0].blendEnable = true;
    desc.renderTargetBlend[0].srcColor    = RHIBlendFactor::SrcAlpha;
    desc.renderTargetBlend[0].dstColor    = RHIBlendFactor::InvSrcAlpha;
    desc.renderTargetBlend[0].colorOp     = RHIBlendOp::Add;
    desc.renderTargetBlend[0].srcAlpha    = RHIBlendFactor::One;
    desc.renderTargetBlend[0].dstAlpha    = RHIBlendFactor::Zero;
    desc.renderTargetBlend[0].alphaOp     = RHIBlendOp::Add;
    desc.renderTargetFormats[0] = RHIFormat::RGBA16Float;
    desc.renderTargetCount      = 1;
    desc.debugName              = "VolumetricFogComposite";

    m_volFogComposite = CreateCachedPipeline(desc);
    if (!m_volFogComposite.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VolumetricFogComposite pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created VolumetricFogComposite pipeline");
    }
    return m_volFogComposite;
}

} // namespace uz
