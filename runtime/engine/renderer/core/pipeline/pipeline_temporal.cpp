/// UzEngine Pipeline Library — Temporal anti-aliasing and temporal super-resolution pipelines.

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "shader_manager.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

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
// TSR — Dilate Velocity Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureTsrDilateVelocityShader() {
    if (m_tsrDilateVelocityCS.IsValid()) return true;

    m_tsrDilateVelocityCS = m_shaderMgr->CreateShaderWithDefines(
        EngineShader::TsrResolveCS, RHIShaderStage::Compute, "TSR_PASS_DILATE_VELOCITY=1");
    if (!m_tsrDilateVelocityCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TSR DilateVelocity shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetTsrDilateVelocity() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTsrDilateVelocity called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_tsrDilateVelocity.IsValid()) return m_tsrDilateVelocity;
    if (!EnsureTsrDilateVelocityShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_tsrDilateVelocityCS;
    desc.debugName = "TSR_DilateVelocity";
    m_tsrDilateVelocity = m_device->CreateComputePipeline(desc);
    if (!m_tsrDilateVelocity.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TSR_DilateVelocity pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created TSR_DilateVelocity pipeline");
    }
    return m_tsrDilateVelocity;
}

// ─────────────────────────────────────────────────────────────────────────────
// TSR — Reject Shading Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureTsrRejectShadingShader() {
    if (m_tsrRejectShadingCS.IsValid()) return true;

    m_tsrRejectShadingCS = m_shaderMgr->CreateShaderWithDefines(
        EngineShader::TsrResolveCS, RHIShaderStage::Compute, "TSR_PASS_REJECT_SHADING=1");
    if (!m_tsrRejectShadingCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TSR RejectShading shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetTsrRejectShading() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTsrRejectShading called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_tsrRejectShading.IsValid()) return m_tsrRejectShading;
    if (!EnsureTsrRejectShadingShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_tsrRejectShadingCS;
    desc.debugName = "TSR_RejectShading";
    m_tsrRejectShading = m_device->CreateComputePipeline(desc);
    if (!m_tsrRejectShading.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TSR_RejectShading pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created TSR_RejectShading pipeline");
    }
    return m_tsrRejectShading;
}

// ─────────────────────────────────────────────────────────────────────────────
// TSR — Update History Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureTsrUpdateHistoryShader() {
    if (m_tsrUpdateHistoryCS.IsValid()) return true;

    m_tsrUpdateHistoryCS = m_shaderMgr->CreateShaderWithDefines(
        EngineShader::TsrResolveCS, RHIShaderStage::Compute, "TSR_PASS_UPDATE_HISTORY=1");
    if (!m_tsrUpdateHistoryCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TSR UpdateHistory shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetTsrUpdateHistory() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetTsrUpdateHistory called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_tsrUpdateHistory.IsValid()) return m_tsrUpdateHistory;
    if (!EnsureTsrUpdateHistoryShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_tsrUpdateHistoryCS;
    desc.debugName = "TSR_UpdateHistory";
    m_tsrUpdateHistory = m_device->CreateComputePipeline(desc);
    if (!m_tsrUpdateHistory.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create TSR_UpdateHistory pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created TSR_UpdateHistory pipeline");
    }
    return m_tsrUpdateHistory;
}

} // namespace uz
