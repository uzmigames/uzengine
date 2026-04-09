/// UzEngine Material Pipeline — Implementation
///
/// Compiles material graphs into GBuffer PSOs via the standard shader pipeline.
/// Uses the same GBuffer RT formats and rasterizer state as PipelineLibrary::GetGBuffer()
/// to ensure seamless interop at draw time.
///

#include "material_pipeline.h"

#include "engine/material/material_graph.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"
#include "rhi.h"
#include "shader_pipeline.h"
#include "vertex_layout.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

namespace {

/// Marker string in gbuffer_ps.hlsl that gets replaced with generated material HLSL.
constexpr const char* kMaterialBodyInclude = "#include \"material_body.hlsli\"";

/// Defines injected when compiling material graph pixel shaders.
/// UZ_MATERIAL_GRAPH=1 activates the material graph code path in gbuffer_ps.hlsl.
constexpr const char* kMaterialGraphDefines = "UZ_MATERIAL_GRAPH=1";

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MaterialPipeline::~MaterialPipeline() {
    Shutdown();
}

MaterialPipeline::MaterialPipeline(MaterialPipeline&& other) noexcept
    : m_device(other.m_device)
    , m_shaderPipeline(other.m_shaderPipeline)
    , m_initialized(other.m_initialized)
    , m_meshVS(other.m_meshVS)
    , m_cache(std::move(other.m_cache))
{
    other.m_device = nullptr;
    other.m_shaderPipeline = nullptr;
    other.m_initialized = false;
    other.m_meshVS = RHIShaderHandle::Invalid();
}

MaterialPipeline& MaterialPipeline::operator=(MaterialPipeline&& other) noexcept {
    if (this != &other) {
        Shutdown();
        m_device = other.m_device;
        m_shaderPipeline = other.m_shaderPipeline;
        m_initialized = other.m_initialized;
        m_meshVS = other.m_meshVS;
        m_cache = std::move(other.m_cache);

        other.m_device = nullptr;
        other.m_shaderPipeline = nullptr;
        other.m_initialized = false;
        other.m_meshVS = RHIShaderHandle::Invalid();
    }
    return *this;
}

bool MaterialPipeline::Init(IRHIDevice* device, ShaderPipeline* shaderPipeline) {
    if (!device) {
        UZ_LOG(Renderer, Error, "MaterialPipeline::Init: device is null");
        return false;
    }
    if (!shaderPipeline) {
        UZ_LOG(Renderer, Error, "MaterialPipeline::Init: shaderPipeline is null");
        return false;
    }

    m_device = device;
    m_shaderPipeline = shaderPipeline;

    // Compile the mesh vertex shader once — reused by all material PSOs.
    // This is the same VS used by PipelineLibrary::GetGBuffer().
    auto vsBytecode = m_shaderPipeline->LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::MeshVS),
        EngineShaders::GetSource(EngineShader::MeshVS));

    if (vsBytecode.empty()) {
        UZ_LOG(Renderer, Error, "MaterialPipeline::Init: failed to compile mesh vertex shader");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode     = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage        = RHIShaderStage::Vertex;
    vsDesc.entryPoint   = "main";
    vsDesc.debugName    = "MaterialPipeline_MeshVS";

    m_meshVS = m_device->CreateShader(vsDesc);
    if (!m_meshVS.IsValid()) {
        UZ_LOG(Renderer, Error, "MaterialPipeline::Init: failed to create mesh VS shader handle");
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "MaterialPipeline initialized");
    return true;
}

void MaterialPipeline::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Destroy all cached shader handles and PSOs
    for (auto& [hash, entry] : m_cache) {
        if (entry.pixelShader.IsValid()) {
            m_device->DestroyShader(entry.pixelShader);
        }
        if (entry.pipelineBindless.IsValid()) {
            m_device->DestroyPipeline(entry.pipelineBindless);
        }
        if (entry.pipelineSM5.IsValid()) {
            m_device->DestroyPipeline(entry.pipelineSM5);
        }
    }
    m_cache.clear();

    if (m_meshVS.IsValid()) {
        m_device->DestroyShader(m_meshVS);
        m_meshVS = RHIShaderHandle::Invalid();
    }

    m_device = nullptr;
    m_shaderPipeline = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "MaterialPipeline shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// PSO compilation
// ─────────────────────────────────────────────────────────────────────────────

RHIPipelineHandle MaterialPipeline::GetOrCreatePSO(const MaterialGraph& graph) {
    return GetOrCreatePSO(graph, m_device->IsBindless());
}

RHIPipelineHandle MaterialPipeline::GetOrCreatePSO(const MaterialGraph& graph, bool bindless) {
    UZ_PROFILE_SCOPE("MaterialPipeline::GetOrCreatePSO");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "MaterialPipeline::GetOrCreatePSO called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    const uint64_t hash = graph.ComputeContentHash();

    // ── Cache lookup ──
    auto it = m_cache.find(hash);
    if (it != m_cache.end()) {
        auto& cached = it->second;
        auto& pso = bindless ? cached.pipelineBindless : cached.pipelineSM5;
        if (pso.IsValid()) {
            return pso;
        }
        // The pixel shader is compiled, but this bindless variant hasn't been
        // created yet. Create only the PSO for this variant.
        if (cached.pixelShader.IsValid()) {
            std::string debugName = "MatGraph_" + std::to_string(hash)
                                  + (bindless ? "_Bindless" : "_SM5");
            auto desc = BuildGBufferPipelineDesc(
                m_meshVS, cached.pixelShader, bindless, debugName.c_str());
            pso = m_device->CreateGraphicsPipeline(desc);
            if (!pso.IsValid()) {
                UZ_LOG(Renderer, Error,
                       "MaterialPipeline: PSO creation failed for cached shader "
                       "(hash={:016X}, bindless={})", hash, bindless);
            }
            return pso;
        }
    }

    // ── Validate graph ──
    std::string validationError;
    if (!graph.Validate(validationError)) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: graph validation failed (hash={:016X}): {}",
               hash, validationError);
        return RHIPipelineHandle::Invalid();
    }

    // ── Step 1: Translate graph to HLSL ──
    HlslMaterialTranslator translator;
    auto compileResult = translator.Compile(graph);
    if (!compileResult.success || compileResult.hlslSource.empty()) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: HLSL translation failed (hash={:016X}): {}",
               hash, compileResult.errorMessage);
        return RHIPipelineHandle::Invalid();
    }

    // ── Step 2: Build combined PS source ──
    std::string combinedSource = BuildCombinedPixelShaderSource(compileResult.hlslSource);
    if (combinedSource.empty()) {
        return RHIPipelineHandle::Invalid();
    }

    // ── Step 3: Compile via ShaderPipeline (DXC) ──
    std::string shaderName = "gbuffer_matgraph_" + std::to_string(hash) + "_ps.hlsl";
    auto bytecode = m_shaderPipeline->LoadBytecodeFromSource(
        shaderName, combinedSource, kMaterialGraphDefines);

    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: DXC compilation failed (hash={:016X})", hash);
        return RHIPipelineHandle::Invalid();
    }

    // ── Step 4: Create pixel shader handle ──
    RHIShaderDesc psDesc{};
    psDesc.bytecode     = bytecode.data();
    psDesc.bytecodeSize = bytecode.size();
    psDesc.stage        = RHIShaderStage::Pixel;
    psDesc.entryPoint   = "main";
    psDesc.debugName    = shaderName.c_str();

    RHIShaderHandle psHandle = m_device->CreateShader(psDesc);
    if (!psHandle.IsValid()) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: shader handle creation failed (hash={:016X})", hash);
        return RHIPipelineHandle::Invalid();
    }

    // ── Step 5: Create GBuffer PSO ──
    std::string debugName = "MatGraph_" + std::to_string(hash)
                          + (bindless ? "_Bindless" : "_SM5");
    auto psoDesc = BuildGBufferPipelineDesc(
        m_meshVS, psHandle, bindless, debugName.c_str());

    RHIPipelineHandle pso = m_device->CreateGraphicsPipeline(psoDesc);
    if (!pso.IsValid()) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: PSO creation failed (hash={:016X}, bindless={})",
               hash, bindless);
        m_device->DestroyShader(psHandle);
        return RHIPipelineHandle::Invalid();
    }

    UZ_LOG(Renderer, Info,
           "MaterialPipeline: compiled material PSO "
           "(hash={:016X}, params={}, textures={}, bindless={})",
           hash, compileResult.parameterCount,
           compileResult.textureSlotCount, bindless);

    // ── Step 6: Cache the result ──
    CachedMaterialPSO cached{};
    cached.pixelShader    = psHandle;
    cached.compileResult  = std::move(compileResult);
    cached.generatedHlsl  = cached.compileResult.hlslSource;
    if (bindless) {
        cached.pipelineBindless = pso;
    } else {
        cached.pipelineSM5 = pso;
    }
    m_cache[hash] = std::move(cached);

    return pso;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache queries
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialPipeline::HasCachedPSO(uint64_t materialHash) const {
    auto it = m_cache.find(materialHash);
    if (it == m_cache.end()) {
        return false;
    }
    const auto& entry = it->second;
    return entry.pipelineBindless.IsValid() || entry.pipelineSM5.IsValid();
}

void MaterialPipeline::InvalidatePSO(uint64_t materialHash) {
    auto it = m_cache.find(materialHash);
    if (it == m_cache.end()) {
        return;
    }

    auto& entry = it->second;
    if (entry.pixelShader.IsValid()) {
        m_device->DestroyShader(entry.pixelShader);
    }
    if (entry.pipelineBindless.IsValid()) {
        m_device->DestroyPipeline(entry.pipelineBindless);
    }
    if (entry.pipelineSM5.IsValid()) {
        m_device->DestroyPipeline(entry.pipelineSM5);
    }
    m_cache.erase(it);

    UZ_LOG(Renderer, Info, "MaterialPipeline: invalidated PSO (hash={:016X})", materialHash);
}

const MaterialCompileResult* MaterialPipeline::GetCompileResult(uint64_t materialHash) const {
    auto it = m_cache.find(materialHash);
    if (it == m_cache.end()) {
        return nullptr;
    }
    return &it->second.compileResult;
}

const std::string* MaterialPipeline::GetGeneratedHlsl(uint64_t materialHash) const {
    auto it = m_cache.find(materialHash);
    if (it == m_cache.end()) {
        return nullptr;
    }
    return &it->second.generatedHlsl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string MaterialPipeline::BuildCombinedPixelShaderSource(const std::string& materialHlsl) const {
    UZ_PROFILE_SCOPE("MaterialPipeline::BuildCombinedPSSource");

    // Retrieve the gbuffer_ps.hlsl engine shader source
    std::string gbufferSource = EngineShaders::GetSource(EngineShader::GbufferPS);
    if (gbufferSource.empty()) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: EngineShaders::GetSource(GbufferPS) returned empty source");
        return {};
    }

    // Find the #include "material_body.hlsli" marker and replace it with the
    // generated material HLSL. This is the same approach used by a production engine's material
    // compilation, where each material graph produces a unique PS variant.
    auto pos = gbufferSource.find(kMaterialBodyInclude);
    if (pos == std::string::npos) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline: gbuffer_ps.hlsl does not contain '{}' — "
               "cannot inject material body",
               kMaterialBodyInclude);
        return {};
    }

    // Replace the include directive with the actual generated HLSL.
    // The generated code contains the ComputeMaterialOutputs() function that
    // the #ifdef UZ_MATERIAL_GRAPH path in gbuffer_ps.hlsl calls.
    gbufferSource.replace(pos, std::strlen(kMaterialBodyInclude), materialHlsl);

    return gbufferSource;
}

RHIGraphicsPipelineDesc MaterialPipeline::BuildGBufferPipelineDesc(
    RHIShaderHandle vs, RHIShaderHandle ps,
    bool bindless, const char* debugName) const
{
    // Must match PipelineLibrary::GetGBuffer() exactly — same RT formats,
    // depth format, rasterizer state, and depth-stencil configuration.
    // See pipeline_geometry.cpp lines 87-102.
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = vs;
    desc.pixelShader   = ps;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // Rasterizer — back-face cull, CCW front face (DX12 standard, matching GBuffer)
    desc.rasterizer.cullMode              = RHICullMode::Back;

    // Depth — enabled, write enabled (deferred geometry pass writes depth)
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = true;

    // G-Buffer render targets — matches GBuffer::Create() and PipelineLibrary::GetGBuffer()
    desc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;    // albedo + AO
    desc.renderTargetFormats[1] = RHIFormat::RGBA16Float;   // octNormal + metallic + roughness
    desc.renderTargetFormats[2] = RHIFormat::RGBA32Float;   // world-space position (debug)
    desc.renderTargetFormats[3] = RHIFormat::RGBA16Float;   // HDR emissive color
    desc.renderTargetCount      = 4;
    desc.depthStencilFormat     = RHIFormat::D32Float;

    desc.debugName = debugName;

    // SM5 path requires vertex input layout; SM6.6 bindless omits it
    if (!bindless) {
        const auto layout = VertexLayout::Standard();
        desc.inputElements     = layout.elements;
        desc.inputElementCount = layout.count;
        desc.vertexStride      = layout.stride;
    }

    return desc;
}

} // namespace uz
