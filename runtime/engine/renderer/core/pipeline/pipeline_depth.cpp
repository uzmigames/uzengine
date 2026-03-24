/// UzEngine Pipeline Library — Depth pipelines (Hi-Z pyramid, GPU occlusion culling).

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "shader_manager.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

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
// GPU Frustum + HiZ Occlusion Culling Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureGpuCullShader() {
    if (m_gpuCullCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GpuCullCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GpuCullCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "gpu_cull_cs";

    m_gpuCullCS = m_device->CreateShader(shaderDesc);
    if (!m_gpuCullCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GpuCullCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetGpuCull() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGpuCull called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_gpuCull.IsValid()) {
        return m_gpuCull;
    }

    if (!EnsureGpuCullShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_gpuCullCS;
    desc.debugName     = "GpuCull_FrustumHiZ";

    m_gpuCull = m_device->CreateComputePipeline(desc);
    if (!m_gpuCull.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GpuCull pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GpuCull pipeline");
    }

    return m_gpuCull;
}

} // namespace uz
