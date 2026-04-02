/// UzEngine Pipeline Library — Global illumination pipelines (DFS, SSGI, GDF, mesh SDF, radiance cache).

#include "pipeline_library.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "shader_manager.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

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
// SSGI Previous Frame Color Reduction Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsgiPrevColorReduceShader() {
    if (m_ssgiPrevColorReduceCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::PrevColorReduceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile PrevColorReduceCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "prev_color_reduce_cs";

    m_ssgiPrevColorReduceCS = m_device->CreateShader(shaderDesc);
    if (!m_ssgiPrevColorReduceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create PrevColorReduceCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsgiPrevColorReduce() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsgiPrevColorReduce called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_ssgiPrevColorReduce.IsValid()) return m_ssgiPrevColorReduce;
    if (!EnsureSsgiPrevColorReduceShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssgiPrevColorReduceCS;
    desc.debugName     = "SSGI_PrevColorReduce";

    m_ssgiPrevColorReduce = m_device->CreateComputePipeline(desc);
    if (!m_ssgiPrevColorReduce.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSGI_PrevColorReduce pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSGI_PrevColorReduce pipeline");
    }
    return m_ssgiPrevColorReduce;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSGI Trace Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsgiTraceShader() {
    if (m_ssgiTraceCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsgiTraceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsgiTraceCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssgi_trace_cs";

    m_ssgiTraceCS = m_device->CreateShader(shaderDesc);
    if (!m_ssgiTraceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsgiTraceCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsgiTrace() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsgiTrace called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_ssgiTrace.IsValid()) return m_ssgiTrace;
    if (!EnsureSsgiTraceShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssgiTraceCS;
    desc.debugName     = "SSGI_Trace";

    m_ssgiTrace = m_device->CreateComputePipeline(desc);
    if (!m_ssgiTrace.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSGI_Trace pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSGI_Trace pipeline");
    }
    return m_ssgiTrace;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSGI Spatial Denoise Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsgiSpatialDenoiseShader() {
    if (m_ssgiSpatialDenoiseCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsgiSpatialDenoiseCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsgiSpatialDenoiseCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssgi_spatial_denoise_cs";

    m_ssgiSpatialDenoiseCS = m_device->CreateShader(shaderDesc);
    if (!m_ssgiSpatialDenoiseCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsgiSpatialDenoiseCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsgiSpatialDenoise() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsgiSpatialDenoise called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_ssgiSpatialDenoise.IsValid()) return m_ssgiSpatialDenoise;
    if (!EnsureSsgiSpatialDenoiseShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssgiSpatialDenoiseCS;
    desc.debugName     = "SSGI_SpatialDenoise";

    m_ssgiSpatialDenoise = m_device->CreateComputePipeline(desc);
    if (!m_ssgiSpatialDenoise.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSGI_SpatialDenoise pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSGI_SpatialDenoise pipeline");
    }
    return m_ssgiSpatialDenoise;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSGI Temporal Accumulation Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsgiTemporalAccumShader() {
    if (m_ssgiTemporalAccumCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsgiTemporalAccumCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsgiTemporalAccumCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssgi_temporal_accum_cs";

    m_ssgiTemporalAccumCS = m_device->CreateShader(shaderDesc);
    if (!m_ssgiTemporalAccumCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsgiTemporalAccumCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsgiTemporalAccum() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsgiTemporalAccum called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_ssgiTemporalAccum.IsValid()) return m_ssgiTemporalAccum;
    if (!EnsureSsgiTemporalAccumShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssgiTemporalAccumCS;
    desc.debugName     = "SSGI_TemporalAccum";

    m_ssgiTemporalAccum = m_device->CreateComputePipeline(desc);
    if (!m_ssgiTemporalAccum.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSGI_TemporalAccum pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSGI_TemporalAccum pipeline");
    }
    return m_ssgiTemporalAccum;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSGI Bilateral Upsample Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSsgiBilateralUpsampleShader() {
    if (m_ssgiBilateralUpsampleCS.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SsgiUpsampleCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SsgiUpsampleCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ssgi_upsample_cs";

    m_ssgiBilateralUpsampleCS = m_device->CreateShader(shaderDesc);
    if (!m_ssgiBilateralUpsampleCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SsgiUpsampleCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSsgiBilateralUpsample() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsgiBilateralUpsample called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_ssgiBilateralUpsample.IsValid()) return m_ssgiBilateralUpsample;
    if (!EnsureSsgiBilateralUpsampleShader()) return RHIPipelineHandle::Invalid();

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ssgiBilateralUpsampleCS;
    desc.debugName     = "SSGI_BilateralUpsample";

    m_ssgiBilateralUpsample = m_device->CreateComputePipeline(desc);
    if (!m_ssgiBilateralUpsample.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SSGI_BilateralUpsample pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SSGI_BilateralUpsample pipeline");
    }
    return m_ssgiBilateralUpsample;
}

// ─────────────────────────────────────────────────────────────────────────────
// GDF Composite Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureGdfCompositeShader() {
    if (m_gdfCompositeCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GlobalSdfCompositeCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GlobalSdfCompositeCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "global_sdf_composite_cs";

    m_gdfCompositeCS = m_device->CreateShader(shaderDesc);
    if (!m_gdfCompositeCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GlobalSdfCompositeCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetGdfComposite() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetGdfComposite called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_gdfComposite.IsValid()) {
        return m_gdfComposite;
    }

    if (!EnsureGdfCompositeShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_gdfCompositeCS;
    desc.debugName     = "GDF_Composite";

    m_gdfComposite = m_device->CreateComputePipeline(desc);
    if (!m_gdfComposite.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create GDF_Composite pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GDF_Composite pipeline");
    }

    return m_gdfComposite;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh SDF Bake Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureMeshSdfBakeShader() {
    if (m_meshSdfBakeCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::MeshSdfBakeCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile MeshSdfBakeCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "mesh_sdf_bake_cs";

    m_meshSdfBakeCS = m_device->CreateShader(shaderDesc);
    if (!m_meshSdfBakeCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MeshSdfBakeCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetMeshSdfBake() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetMeshSdfBake called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_meshSdfBake.IsValid()) {
        return m_meshSdfBake;
    }

    if (!EnsureMeshSdfBakeShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_meshSdfBakeCS;
    desc.debugName     = "MeshSDF_Bake";

    m_meshSdfBake = m_device->CreateComputePipeline(desc);
    if (!m_meshSdfBake.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create MeshSDF_Bake pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created MeshSDF_Bake pipeline");
    }

    return m_meshSdfBake;
}

// ─────────────────────────────────────────────────────────────────────────────
// Radiance Cache Trace Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureRadianceCacheTraceShader() {
    if (m_radianceCacheTraceCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::RadianceCacheTraceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile RadianceCacheTraceCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "radiance_cache_trace_cs";

    m_radianceCacheTraceCS = m_device->CreateShader(shaderDesc);
    if (!m_radianceCacheTraceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create RadianceCacheTraceCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetRadianceCacheTrace() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetRadianceCacheTrace called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_radianceCacheTrace.IsValid()) {
        return m_radianceCacheTrace;
    }

    if (!EnsureRadianceCacheTraceShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_radianceCacheTraceCS;
    desc.debugName     = "RadianceCache_Trace";

    m_radianceCacheTrace = m_device->CreateComputePipeline(desc);
    if (!m_radianceCacheTrace.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create RadianceCache_Trace pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created RadianceCache_Trace pipeline");
    }

    return m_radianceCacheTrace;
}

// ─────────────────────────────────────────────────────────────────────────────
// SVGF Reproject Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSvgfReprojectShader() {
    if (m_svgfReprojectCS.IsValid()) return true;
    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SvgfReprojectCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SvgfReprojectCS");
        return false;
    }
    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "svgf_reproject_cs";
    m_svgfReprojectCS = m_device->CreateShader(sd);
    return m_svgfReprojectCS.IsValid();
}

RHIPipelineHandle PipelineLibrary::GetSvgfReproject() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_svgfReproject.IsValid()) return m_svgfReproject;
    if (!EnsureSvgfReprojectShader()) return RHIPipelineHandle::Invalid();
    RHIComputePipelineDesc desc{};
    desc.computeShader = m_svgfReprojectCS;
    desc.debugName     = "SVGF_Reproject";
    m_svgfReproject = m_device->CreateComputePipeline(desc);
    if (m_svgfReproject.IsValid())
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SVGF_Reproject pipeline");
    return m_svgfReproject;
}

// ─────────────────────────────────────────────────────────────────────────────
// SVGF Filter Moments Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSvgfFilterMomentsShader() {
    if (m_svgfFilterMomentsCS.IsValid()) return true;
    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SvgfFilterMomentsCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SvgfFilterMomentsCS");
        return false;
    }
    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "svgf_filter_moments_cs";
    m_svgfFilterMomentsCS = m_device->CreateShader(sd);
    return m_svgfFilterMomentsCS.IsValid();
}

RHIPipelineHandle PipelineLibrary::GetSvgfFilterMoments() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_svgfFilterMoments.IsValid()) return m_svgfFilterMoments;
    if (!EnsureSvgfFilterMomentsShader()) return RHIPipelineHandle::Invalid();
    RHIComputePipelineDesc desc{};
    desc.computeShader = m_svgfFilterMomentsCS;
    desc.debugName     = "SVGF_FilterMoments";
    m_svgfFilterMoments = m_device->CreateComputePipeline(desc);
    if (m_svgfFilterMoments.IsValid())
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SVGF_FilterMoments pipeline");
    return m_svgfFilterMoments;
}

// ─────────────────────────────────────────────────────────────────────────────
// SVGF A-Trous Wavelet Filter Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureSvgfAtrousShader() {
    if (m_svgfAtrousCS.IsValid()) return true;
    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SvgfAtrousCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SvgfAtrousCS");
        return false;
    }
    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "svgf_atrous_cs";
    m_svgfAtrousCS = m_device->CreateShader(sd);
    return m_svgfAtrousCS.IsValid();
}

RHIPipelineHandle PipelineLibrary::GetSvgfAtrous() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_svgfAtrous.IsValid()) return m_svgfAtrous;
    if (!EnsureSvgfAtrousShader()) return RHIPipelineHandle::Invalid();
    RHIComputePipelineDesc desc{};
    desc.computeShader = m_svgfAtrousCS;
    desc.debugName     = "SVGF_ATrous";
    m_svgfAtrous = m_device->CreateComputePipeline(desc);
    if (m_svgfAtrous.IsValid())
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SVGF_ATrous pipeline");
    return m_svgfAtrous;
}

// ─────────────────────────────────────────────────────────────────────────────
// God Rays Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureGodRaysShader() {
    if (m_godRaysCS.IsValid()) return true;
    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GodRaysCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile GodRaysCS");
        return false;
    }
    RHIShaderDesc sd{};
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "god_rays_cs";
    m_godRaysCS = m_device->CreateShader(sd);
    return m_godRaysCS.IsValid();
}

RHIPipelineHandle PipelineLibrary::GetGodRays() {
    if (!m_initialized) return RHIPipelineHandle::Invalid();
    if (m_godRays.IsValid()) return m_godRays;
    if (!EnsureGodRaysShader()) return RHIPipelineHandle::Invalid();
    RHIComputePipelineDesc desc{};
    desc.computeShader = m_godRaysCS;
    desc.debugName     = "GodRays";
    m_godRays = m_device->CreateComputePipeline(desc);
    if (m_godRays.IsValid())
        UZ_LOG(Renderer, Info, "PipelineLibrary: created GodRays pipeline");
    return m_godRays;
}

} // namespace uz
