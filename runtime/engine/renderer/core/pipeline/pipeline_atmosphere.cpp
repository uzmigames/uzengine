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
    desc.depthStencilFormat       = RHIFormat::D32Float; // DSV may be bound (sky+depth clear pass)
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
// Single Scattering 3D LUT Compute Pipeline (Bruneton 2017)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSingleScatterLutShader() {
    if (m_singleScatterLutCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SingleScatteringLutCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SingleScatteringLutCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "single_scattering_lut_cs";

    m_singleScatterLutCS = m_device->CreateShader(shaderDesc);
    if (!m_singleScatterLutCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SingleScatteringLutCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSingleScatterLut() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSingleScatterLut called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_singleScatterLut.IsValid()) {
        return m_singleScatterLut;
    }

    if (!EnsureSingleScatterLutShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_singleScatterLutCS;
    desc.debugName     = "SingleScatterLUT";

    m_singleScatterLut = m_device->CreateComputePipeline(desc);
    if (!m_singleScatterLut.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SingleScatterLUT pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SingleScatterLUT pipeline");
    }

    return m_singleScatterLut;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scattering Density Compute Pipeline (Bruneton 2017)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureScatteringDensityShader() {
    if (m_scatteringDensityCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ScatteringDensityCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ScatteringDensityCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "scattering_density_cs";

    m_scatteringDensityCS = m_device->CreateShader(shaderDesc);
    if (!m_scatteringDensityCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ScatteringDensityCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetScatteringDensity() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetScatteringDensity called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_scatteringDensity.IsValid()) {
        return m_scatteringDensity;
    }

    if (!EnsureScatteringDensityShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_scatteringDensityCS;
    desc.debugName     = "ScatteringDensity";

    m_scatteringDensity = m_device->CreateComputePipeline(desc);
    if (!m_scatteringDensity.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ScatteringDensity pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ScatteringDensity pipeline");
    }

    return m_scatteringDensity;
}

// ─────────────────────────────────────────────────────────────────────────────
// Indirect Scattering Compute Pipeline (Bruneton 2017)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureIndirectScatteringShader() {
    if (m_indirectScatteringCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::IndirectScatteringCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile IndirectScatteringCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "indirect_scattering_cs";

    m_indirectScatteringCS = m_device->CreateShader(shaderDesc);
    if (!m_indirectScatteringCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create IndirectScatteringCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetIndirectScattering() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetIndirectScattering called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_indirectScattering.IsValid()) {
        return m_indirectScattering;
    }

    if (!EnsureIndirectScatteringShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_indirectScatteringCS;
    desc.debugName     = "IndirectScattering";

    m_indirectScattering = m_device->CreateComputePipeline(desc);
    if (!m_indirectScattering.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create IndirectScattering pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created IndirectScattering pipeline");
    }

    return m_indirectScattering;
}

// ─────────────────────────────────────────────────────────────────────────────
// Irradiance LUT Compute Pipeline (Bruneton 2017)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureIrradianceLutShader() {
    if (m_irradianceLutCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::IrradianceLutCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile IrradianceLutCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "irradiance_lut_cs";

    m_irradianceLutCS = m_device->CreateShader(shaderDesc);
    if (!m_irradianceLutCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create IrradianceLutCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetIrradianceLut() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetIrradianceLut called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_irradianceLut.IsValid()) {
        return m_irradianceLut;
    }

    if (!EnsureIrradianceLutShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_irradianceLutCS;
    desc.debugName     = "IrradianceLUT";

    m_irradianceLut = m_device->CreateComputePipeline(desc);
    if (!m_irradianceLut.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create IrradianceLUT pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created IrradianceLUT pipeline");
    }

    return m_irradianceLut;
}

// ─────────────────────────────────────────────────────────────────────────────

} // namespace uz
