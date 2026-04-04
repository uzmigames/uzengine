/// UzEngine Pipeline Library — Post-process pipelines (tonemap, velocity, SSR, color grading, motion blur, DOF, FXAA, CAS).

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
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
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
// Velocity Buffer Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureVelocityPixelShader() {
    if (m_velocityPS.IsFailed()) return false;
    if (!m_velocityPS.IsValid()) {
        m_velocityPS = m_shaderMgr->CreateShader(EngineShader::VelocityPS, RHIShaderStage::Pixel);
        if (!m_velocityPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile VelocityPS");
            m_velocityPS = RHIShaderHandle::Failed();
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetVelocityPass() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetVelocityPass called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    // Return cached handle (valid or permanently failed — never retry failed creations)
    if (m_velocityPass != RHIPipelineHandle::Invalid()) {
        return m_velocityPass;
    }

    if (!EnsureFullscreenShaders() || !EnsureVelocityPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader                = m_fullscreenVS;
    desc.pixelShader                 = m_velocityPS;
    desc.topology                    = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode         = RHICullMode::None; // Fullscreen triangle — no geometry to cull
    desc.depthStencil.depthEnable    = false;
    desc.depthStencil.depthWrite     = false;
    desc.renderTargetFormats[0]      = RHIFormat::RG16Float;
    desc.renderTargetCount           = 1;
    desc.debugName                   = "VelocityBuffer";

    // No blending — velocity is written directly for every pixel.
    desc.renderTargetBlend[0].blendEnable = false;

    m_velocityPass = CreateCachedPipeline(desc);
    if (!m_velocityPass.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create VelocityBuffer pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created VelocityBuffer pipeline");
    }

    return m_velocityPass;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSR Trace Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsrTraceShader() {
    if (m_ssrTraceCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsrTraceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsrTraceCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssr_trace_cs";

    m_ssrTraceCS = m_device->CreateShader(shaderDesc);
    if (!m_ssrTraceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsrTraceCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsrTrace() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsrTrace called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_ssrTrace.IsValid()) {
        return m_ssrTrace;
    }

    if (!EnsureSsrTraceShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssrTraceCS;
    desc.debugName     = "SSR_Trace";

    m_ssrTrace = m_device->CreateComputePipeline(desc);
    if (!m_ssrTrace.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSR_Trace pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSR_Trace pipeline");
    }

    return m_ssrTrace;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSR Resolve Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsrResolveShader() {
    if (m_ssrResolveCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsrResolveCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsrResolveCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssr_resolve_cs";

    m_ssrResolveCS = m_device->CreateShader(shaderDesc);
    if (!m_ssrResolveCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsrResolveCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsrResolve() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsrResolve called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_ssrResolve.IsValid()) {
        return m_ssrResolve;
    }

    if (!EnsureSsrResolveShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssrResolveCS;
    desc.debugName     = "SSR_Resolve";

    m_ssrResolve = m_device->CreateComputePipeline(desc);
    if (!m_ssrResolve.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSR_Resolve pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSR_Resolve pipeline");
    }

    return m_ssrResolve;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSR Temporal Accumulation Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsrTemporalShader() {
    if (m_ssrTemporalCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsrTemporalCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsrTemporalCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssr_temporal_cs";

    m_ssrTemporalCS = m_device->CreateShader(shaderDesc);
    if (!m_ssrTemporalCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsrTemporalCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsrTemporal() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsrTemporal called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_ssrTemporal.IsValid()) {
        return m_ssrTemporal;
    }

    if (!EnsureSsrTemporalShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssrTemporalCS;
    desc.debugName     = "SSR_Temporal";

    m_ssrTemporal = m_device->CreateComputePipeline(desc);
    if (!m_ssrTemporal.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSR_Temporal pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSR_Temporal pipeline");
    }

    return m_ssrTemporal;
}

// ─────────────────────────────────────────────────────────────────────────────
// Color Grading LUT Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureColorGradingLutShader() {
    if (m_colorGradingLutCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ColorGradingLutCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: ColorGradingLutCS bytecode empty");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ColorGradingLutCS";

    m_colorGradingLutCS = m_device->CreateShader(shaderDesc);
    if (!m_colorGradingLutCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ColorGradingLutCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetColorGradingLut() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetColorGradingLut called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_colorGradingLut.IsValid()) {
        return m_colorGradingLut;
    }

    if (!EnsureColorGradingLutShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_colorGradingLutCS;
    desc.debugName     = "ColorGradingLUT";

    m_colorGradingLut = m_device->CreateComputePipeline(desc);
    if (!m_colorGradingLut.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ColorGradingLUT pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created ColorGradingLUT pipeline");
    }

    return m_colorGradingLut;
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion Blur Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureMotionBlurShader() {
    if (m_motionBlurCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::MotionBlurCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: MotionBlurCS bytecode empty");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "MotionBlurCS";

    m_motionBlurCS = m_device->CreateShader(shaderDesc);
    if (!m_motionBlurCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MotionBlurCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetMotionBlur() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetMotionBlur called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_motionBlur.IsValid()) {
        return m_motionBlur;
    }

    if (!EnsureMotionBlurShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_motionBlurCS;
    desc.debugName     = "MotionBlur";

    m_motionBlur = m_device->CreateComputePipeline(desc);
    if (!m_motionBlur.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MotionBlur pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created MotionBlur pipeline");
    }

    return m_motionBlur;
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion Blur Tile Max Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureMotionBlurTileMaxShader() {
    if (m_motionBlurTileMaxCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::MotionblurTilemaxCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: MotionblurTilemaxCS bytecode empty");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "MotionBlurTileMaxCS";

    m_motionBlurTileMaxCS = m_device->CreateShader(shaderDesc);
    if (!m_motionBlurTileMaxCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MotionBlurTileMaxCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetMotionBlurTileMax() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetMotionBlurTileMax called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_motionBlurTileMax.IsValid()) {
        return m_motionBlurTileMax;
    }

    if (!EnsureMotionBlurTileMaxShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_motionBlurTileMaxCS;
    desc.debugName     = "MotionBlurTileMax";

    m_motionBlurTileMax = m_device->CreateComputePipeline(desc);
    if (!m_motionBlurTileMax.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MotionBlurTileMax pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created MotionBlurTileMax pipeline");
    }

    return m_motionBlurTileMax;
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion Blur Neighbor Max Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureMotionBlurNeighborMaxShader() {
    if (m_motionBlurNeighborMaxCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::MotionblurNeighbormaxCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: MotionblurNeighbormaxCS bytecode empty");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "MotionBlurNeighborMaxCS";

    m_motionBlurNeighborMaxCS = m_device->CreateShader(shaderDesc);
    if (!m_motionBlurNeighborMaxCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MotionBlurNeighborMaxCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetMotionBlurNeighborMax() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetMotionBlurNeighborMax called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_motionBlurNeighborMax.IsValid()) {
        return m_motionBlurNeighborMax;
    }

    if (!EnsureMotionBlurNeighborMaxShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_motionBlurNeighborMaxCS;
    desc.debugName     = "MotionBlurNeighborMax";

    m_motionBlurNeighborMax = m_device->CreateComputePipeline(desc);
    if (!m_motionBlurNeighborMax.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MotionBlurNeighborMax pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created MotionBlurNeighborMax pipeline");
    }

    return m_motionBlurNeighborMax;
}

// ─────────────────────────────────────────────────────────────────────────────
// DOF Setup Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureDofSetupShader() {
    if (m_dofSetupCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DofSetupCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: DofSetupCS bytecode empty");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "DofSetupCS";

    m_dofSetupCS = m_device->CreateShader(shaderDesc);
    if (!m_dofSetupCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DofSetupCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetDofSetup() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDofSetup called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_dofSetup.IsValid()) {
        return m_dofSetup;
    }

    if (!EnsureDofSetupShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_dofSetupCS;
    desc.debugName     = "DOF_Setup";

    m_dofSetup = m_device->CreateComputePipeline(desc);
    if (!m_dofSetup.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DOF_Setup pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DOF_Setup pipeline");
    }

    return m_dofSetup;
}

// ─────────────────────────────────────────────────────────────────────────────
// DOF Blur Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureDofBlurShader() {
    if (m_dofBlurCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DofBlurCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: DofBlurCS bytecode empty");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "DofBlurCS";

    m_dofBlurCS = m_device->CreateShader(shaderDesc);
    if (!m_dofBlurCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DofBlurCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetDofBlur() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDofBlur called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_dofBlur.IsValid()) {
        return m_dofBlur;
    }

    if (!EnsureDofBlurShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_dofBlurCS;
    desc.debugName     = "DOF_Blur";

    m_dofBlur = m_device->CreateComputePipeline(desc);
    if (!m_dofBlur.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DOF_Blur pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DOF_Blur pipeline");
    }

    return m_dofBlur;
}

// ─────────────────────────────────────────────────────────────────────────────
// FXAA 3.11 Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureFxaaPixelShader() {
    if (!m_fxaaPS.IsValid()) {
        m_fxaaPS = m_shaderMgr->CreateShader(EngineShader::FxaaPS, RHIShaderStage::Pixel);
        if (!m_fxaaPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile FxaaPS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetFxaa() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetFxaa called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_fxaa.IsValid()) {
        return m_fxaa;
    }

    if (!EnsureFullscreenShaders() || !EnsureFxaaPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_fxaaPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount        = 1;
    desc.debugName                = "FXAA_3.11";

    m_fxaa = CreateCachedPipeline(desc);
    if (!m_fxaa.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create FXAA pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created FXAA pipeline");
    }

    return m_fxaa;
}

// ─────────────────────────────────────────────────────────────────────────────
// CAS (Contrast Adaptive Sharpening) Pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// @source AMD FidelityFX CAS — ffx_cas.h
bool PipelineLibrary::EnsureCasSharpenPixelShader() {
    if (!m_casSharpenPS.IsValid()) {
        m_casSharpenPS = m_shaderMgr->CreateShader(EngineShader::CasSharpenPS, RHIShaderStage::Pixel);
        if (!m_casSharpenPS.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile CasSharpenPS");
            return false;
        }
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetCasSharpen() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetCasSharpen called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_casSharpen.IsValid()) {
        return m_casSharpen;
    }

    if (!EnsureFullscreenShaders() || !EnsureCasSharpenPixelShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = m_fullscreenVS;
    desc.pixelShader   = m_casSharpenPS;
    desc.topology      = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode      = RHICullMode::None; // Fullscreen triangle — no geometry to cull
    desc.depthStencil.depthEnable = false;
    desc.depthStencil.depthWrite  = false;
    desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount        = 1;
    desc.debugName                = "CAS_Sharpen";

    m_casSharpen = CreateCachedPipeline(desc);
    if (!m_casSharpen.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create CAS_Sharpen pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created CAS_Sharpen pipeline");
    }

    return m_casSharpen;
}

} // namespace uz
