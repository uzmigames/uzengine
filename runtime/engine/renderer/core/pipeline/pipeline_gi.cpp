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
// Screen-space ReSTIR GI Compute Pipelines (phase C, r.GI.ReSTIR)
// ─────────────────────────────────────────────────────────────────────────────

/// Shader + pipeline of one ReSTIR GI pass, the SSGI pattern above with the two handles passed in.
static RHIPipelineHandle CreateSsgiRestirPipeline(IRHIDevice* device, ShaderManager* shaderMgr, bool initialized,
                                                  EngineShader shader, const char* shaderName, const char* pipelineName,
                                                  RHIShaderHandle& shaderHandle, RHIPipelineHandle& pipeline) {
    if (!initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSsgiRestir* called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (pipeline.IsValid()) return pipeline;
    if (!shaderHandle.IsValid()) {
        auto bytecode = shaderMgr->GetBytecode(shader);
        if (bytecode.empty()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile {}", shaderName);
            return RHIPipelineHandle::Invalid();
        }
        RHIShaderDesc shaderDesc{};
        shaderDesc.bytecode     = bytecode.data();
        shaderDesc.bytecodeSize = bytecode.size();
        shaderDesc.stage        = RHIShaderStage::Compute;
        shaderDesc.entryPoint   = "main";
        shaderDesc.debugName    = shaderName;
        shaderHandle = device->CreateShader(shaderDesc);
        if (!shaderHandle.IsValid()) {
            UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create {} shader", shaderName);
            return RHIPipelineHandle::Invalid();
        }
    }
    RHIComputePipelineDesc desc{};
    desc.computeShader = shaderHandle;
    desc.debugName     = pipelineName;
    pipeline = device->CreateComputePipeline(desc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create {} pipeline", pipelineName);
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created {} pipeline", pipelineName);
    }
    return pipeline;
}

RHIPipelineHandle PipelineLibrary::GetSsgiRestirInitial() {
    return CreateSsgiRestirPipeline(m_device, m_shaderMgr, m_initialized, EngineShader::SsgiRestirInitialCS,
                                    "ssgi_restir_initial_cs", "SSGI_RestirInitial", m_ssgiRestirInitialCS, m_ssgiRestirInitial);
}

RHIPipelineHandle PipelineLibrary::GetSsgiRestirTemporal() {
    return CreateSsgiRestirPipeline(m_device, m_shaderMgr, m_initialized, EngineShader::SsgiRestirTemporalCS,
                                    "ssgi_restir_temporal_cs", "SSGI_RestirTemporal", m_ssgiRestirTemporalCS, m_ssgiRestirTemporal);
}

RHIPipelineHandle PipelineLibrary::GetSsgiRestirSpatial() {
    return CreateSsgiRestirPipeline(m_device, m_shaderMgr, m_initialized, EngineShader::SsgiRestirSpatialCS,
                                    "ssgi_restir_spatial_cs", "SSGI_RestirSpatial", m_ssgiRestirSpatialCS, m_ssgiRestirSpatial);
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

uint64_t PipelineLibrary::GetMeshSdfBakeShaderHash() {
    if (!m_shaderMgr) return 0;
    const auto bytecode = m_shaderMgr->GetBytecode(EngineShader::MeshSdfBakeCS);
    if (bytecode.empty()) return 0;
    uint64_t hash = 1469598103934665603ull;
    for (const uint8_t byte : bytecode) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
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
// DDGI Probe Update Compute Pipeline (DXR inline ray tracing)
// ─────────────────────────────────────────────────────────────────────────────
//
// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
//                     Irradiance Probes" — runtime swap with the SDF path
// @ref Microsoft DirectX-Specs "Inline Raytracing (DXR 1.1)" — RayQuery<>

bool PipelineLibrary::EnsureDdgiProbeUpdateShader() {
    if (m_ddgiProbeUpdateCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DdgiProbeUpdateCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DdgiProbeUpdateCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ddgi_probe_update_cs";

    m_ddgiProbeUpdateCS = m_device->CreateShader(shaderDesc);
    if (!m_ddgiProbeUpdateCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DdgiProbeUpdateCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetDdgiProbeUpdate() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDdgiProbeUpdate called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_ddgiProbeUpdate.IsValid()) {
        return m_ddgiProbeUpdate;
    }

    if (!EnsureDdgiProbeUpdateShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ddgiProbeUpdateCS;
    desc.debugName     = "DDGI_ProbeUpdate";

    m_ddgiProbeUpdate = m_device->CreateComputePipeline(desc);
    if (!m_ddgiProbeUpdate.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DDGI_ProbeUpdate pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DDGI_ProbeUpdate pipeline");
    }

    return m_ddgiProbeUpdate;
}

// ─────────────────────────────────────────────────────────────────────────────
// DDGI Probe Trace Compute Pipeline (block B.6: one thread per probe ray)
// ─────────────────────────────────────────────────────────────────────────────

bool PipelineLibrary::EnsureDdgiProbeTraceShader() {
    if (m_ddgiProbeTraceCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DdgiProbeTraceCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DdgiProbeTraceCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ddgi_probe_trace_cs";

    m_ddgiProbeTraceCS = m_device->CreateShader(shaderDesc);
    if (!m_ddgiProbeTraceCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DdgiProbeTraceCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetDdgiProbeTrace() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDdgiProbeTrace called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_ddgiProbeTrace.IsValid()) {
        return m_ddgiProbeTrace;
    }

    if (!EnsureDdgiProbeTraceShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ddgiProbeTraceCS;
    desc.debugName     = "DDGI_ProbeTrace";

    m_ddgiProbeTrace = m_device->CreateComputePipeline(desc);
    if (!m_ddgiProbeTrace.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DDGI_ProbeTrace pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DDGI_ProbeTrace pipeline");
    }

    return m_ddgiProbeTrace;
}

// ─────────────────────────────────────────────────────────────────────────────
// DDGI Atlas Border Fill Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────
//
// Fills the 1-texel octahedral border of every probe tile from the tile's interior after the
// probe update (Cigolle 2014 identifications), and clears both atlases once at creation. The
// deferred lighting samples the tiles bilinearly and reaches the border for every normal within
// half a texel of a tile edge; until this pass existed those texels were never written
// (followup-01, H-55: 0.57-0.72x with identical stored energy).

bool PipelineLibrary::EnsureDdgiBorderFillShader() {
    if (m_ddgiBorderFillCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::DdgiBorderFillCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile DdgiBorderFillCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "ddgi_border_fill_cs";

    m_ddgiBorderFillCS = m_device->CreateShader(shaderDesc);
    if (!m_ddgiBorderFillCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DdgiBorderFillCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetDdgiBorderFill() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetDdgiBorderFill called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_ddgiBorderFill.IsValid()) {
        return m_ddgiBorderFill;
    }
    if (!EnsureDdgiBorderFillShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_ddgiBorderFillCS;
    desc.debugName     = "DDGI_BorderFill";

    m_ddgiBorderFill = m_device->CreateComputePipeline(desc);
    if (!m_ddgiBorderFill.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create DDGI_BorderFill pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created DDGI_BorderFill pipeline");
    }
    return m_ddgiBorderFill;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hardware Ray-Traced Reflections Compute Pipeline (stage 1, opt-in)
// ─────────────────────────────────────────────────────────────────────────────
//
// `rt_reflection_cs.hlsl`: one mirror ray per pixel the screen trace left without a confident
// hit, inline RayQuery against the TLAS, the hit shaded from the global distance field + sun the
// way the DDGI writer shades its probe rays. Created lazily by RegisterRTReflectionPass, so with
// `r.RayTracing.Reflections` off the shader is never compiled.

bool PipelineLibrary::EnsureRtReflectionShader() {
    if (m_rtReflectionCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::RtReflectionCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile RtReflectionCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "rt_reflection_cs";

    m_rtReflectionCS = m_device->CreateShader(shaderDesc);
    if (!m_rtReflectionCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create RtReflectionCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetRtReflection() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetRtReflection called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_rtReflection.IsValid()) {
        return m_rtReflection;
    }

    if (!EnsureRtReflectionShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_rtReflectionCS;
    desc.debugName     = "RT_Reflections";

    m_rtReflection = m_device->CreateComputePipeline(desc);
    if (!m_rtReflection.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create RT_Reflections pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created RT_Reflections pipeline");
    }
    return m_rtReflection;
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface Radiance Cache Resolve Compute Pipeline (Phase D stage 1, opt-in)
// ─────────────────────────────────────────────────────────────────────────────
//
// `surface_cache_resolve_cs.hlsl`: one thread per hash entry, folds the frame's fixed-point
// accumulators into the per-frame EMA of the resolved record, ages and evicts cells (stale, or
// inside a distance-field change box), or clears the table. Created lazily by the resolve pass, so
// with `r.GI.SurfaceCache` off the shader is never compiled.

bool PipelineLibrary::EnsureSurfaceCacheResolveShader() {
    if (m_surfaceCacheResolveCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::SurfaceCacheResolveCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile SurfaceCacheResolveCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "surface_cache_resolve_cs";

    m_surfaceCacheResolveCS = m_device->CreateShader(shaderDesc);
    if (!m_surfaceCacheResolveCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SurfaceCacheResolveCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetSurfaceCacheResolve() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetSurfaceCacheResolve called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    if (m_surfaceCacheResolve.IsValid()) {
        return m_surfaceCacheResolve;
    }

    if (!EnsureSurfaceCacheResolveShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_surfaceCacheResolveCS;
    desc.debugName     = "SurfaceCache_Resolve";

    m_surfaceCacheResolve = m_device->CreateComputePipeline(desc);
    if (!m_surfaceCacheResolve.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create SurfaceCache_Resolve pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created SurfaceCache_Resolve pipeline");
    }
    return m_surfaceCacheResolve;
}

// ─────────────────────────────────────────────────────────────────────────────
// Radiance Cache Probe Side Compute Pipeline
// ─────────────────────────────────────────────────────────────────────────────
//
// The nearest mesh triangle to every radiance-cache probe, over the instances the global distance
// field was composited from: the side of that face the probe lies on and the direction from the face
// to the probe. The trace classifies and relocates probes with it when
// r.GI.RadianceCache.ProbeClassification = 1 (followup-01 candidate); the distance field alone is
// unsigned and biased, and cannot tell the two sides of a wall apart.

bool PipelineLibrary::EnsureProbeSideShader() {
    if (m_probeSideCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ProbeSideCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ProbeSideCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "probe_side_cs";

    m_probeSideCS = m_device->CreateShader(shaderDesc);
    if (!m_probeSideCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ProbeSideCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetProbeSide() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetProbeSide called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_probeSide.IsValid()) {
        return m_probeSide;
    }
    if (!EnsureProbeSideShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_probeSideCS;
    desc.debugName     = "RadianceCache_ProbeSide";

    m_probeSide = m_device->CreateComputePipeline(desc);
    if (!m_probeSide.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create RadianceCache_ProbeSide pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created RadianceCache_ProbeSide pipeline");
    }
    return m_probeSide;
}

bool PipelineLibrary::EnsureProbeReachShader() {
    if (m_probeReachCS.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ProbeReachCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to compile ProbeReachCS");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "probe_reach_cs";

    m_probeReachCS = m_device->CreateShader(shaderDesc);
    if (!m_probeReachCS.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create ProbeReachCS shader");
        return false;
    }
    return true;
}

RHIPipelineHandle PipelineLibrary::GetProbeReach() {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "PipelineLibrary::GetProbeReach called before Init()");
        return RHIPipelineHandle::Invalid();
    }
    if (m_probeReach.IsValid()) {
        return m_probeReach;
    }
    if (!EnsureProbeReachShader()) {
        return RHIPipelineHandle::Invalid();
    }

    RHIComputePipelineDesc desc{};
    desc.computeShader = m_probeReachCS;
    desc.debugName     = "RadianceCache_ProbeReach";

    m_probeReach = m_device->CreateComputePipeline(desc);
    if (!m_probeReach.IsValid()) {
        UZ_LOG(Renderer, Error, "PipelineLibrary: failed to create RadianceCache_ProbeReach pipeline");
    } else {
        UZ_LOG(Renderer, Info, "PipelineLibrary: created RadianceCache_ProbeReach pipeline");
    }
    return m_probeReach;
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
