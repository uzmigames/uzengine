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
#include <cstdio>     // std::snprintf — used by GetParticleMeshPSO debug-name builder
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
    , m_particleMeshVS(other.m_particleMeshVS)
    , m_particleMeshPS(other.m_particleMeshPS)
    , m_particleMeshShadersFailed(other.m_particleMeshShadersFailed)
    , m_particleMeshCache(std::move(other.m_particleMeshCache))
{
    other.m_device = nullptr;
    other.m_shaderPipeline = nullptr;
    other.m_initialized = false;
    other.m_meshVS = RHIShaderHandle::Invalid();
    other.m_particleMeshVS = RHIShaderHandle::Invalid();
    other.m_particleMeshPS = RHIShaderHandle::Invalid();
    other.m_particleMeshShadersFailed = false;
}

MaterialPipeline& MaterialPipeline::operator=(MaterialPipeline&& other) noexcept {
    if (this != &other) {
        Shutdown();
        m_device = other.m_device;
        m_shaderPipeline = other.m_shaderPipeline;
        m_initialized = other.m_initialized;
        m_meshVS = other.m_meshVS;
        m_cache = std::move(other.m_cache);
        m_particleMeshVS = other.m_particleMeshVS;
        m_particleMeshPS = other.m_particleMeshPS;
        m_particleMeshShadersFailed = other.m_particleMeshShadersFailed;
        m_particleMeshCache = std::move(other.m_particleMeshCache);

        other.m_device = nullptr;
        other.m_shaderPipeline = nullptr;
        other.m_initialized = false;
        other.m_meshVS = RHIShaderHandle::Invalid();
        other.m_particleMeshVS = RHIShaderHandle::Invalid();
        other.m_particleMeshPS = RHIShaderHandle::Invalid();
        other.m_particleMeshShadersFailed = false;
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

    // ── Particle-mesh resolver cache ──
    // PSOs first, then the shared VS/PS. Same destruction order used by
    // PipelineLibrary::Shutdown for symmetry — the underlying RHI does
    // not require any specific order, but keeping VS/PS alive while PSOs
    // exist keeps validation tools happy on backends that walk reference
    // graphs.
    for (auto& [key, pso] : m_particleMeshCache) {
        if (pso.IsValid()) {
            m_device->DestroyPipeline(pso);
        }
    }
    m_particleMeshCache.clear();

    if (m_particleMeshVS.IsValid()) {
        m_device->DestroyShader(m_particleMeshVS);
        m_particleMeshVS = RHIShaderHandle::Invalid();
    }
    if (m_particleMeshPS.IsValid()) {
        m_device->DestroyShader(m_particleMeshPS);
        m_particleMeshPS = RHIShaderHandle::Invalid();
    }
    m_particleMeshShadersFailed = false;

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
                                  + (bindless ? "_Bindless" : "_SM5")
                                  + (graph.domain == MaterialDomain::Decal ? "_MeshDecal" : "");
            auto desc = BuildGBufferPipelineDesc(
                m_meshVS, cached.pixelShader, bindless, graph.twoSided,
                graph.domain == MaterialDomain::Decal, debugName.c_str());
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
                          + (bindless ? "_Bindless" : "_SM5")
                          + (graph.domain == MaterialDomain::Decal ? "_MeshDecal" : "");
    auto psoDesc = BuildGBufferPipelineDesc(
        m_meshVS, psHandle, bindless, graph.twoSided,
        graph.domain == MaterialDomain::Decal, debugName.c_str());

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
    bool bindless, bool twoSided, bool meshDecal, const char* debugName) const
{
    // Must match PipelineLibrary::GetGBuffer() exactly — same RT formats,
    // depth format, rasterizer state, and depth-stencil configuration.
    // See pipeline_geometry.cpp lines 87-102.
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader  = vs;
    desc.pixelShader   = ps;
    desc.topology      = RHIPrimitiveTopology::TriangleList;

    // Rasterizer — back-face cull, CCW front face (DX12 standard, matching
    // GBuffer) unless the graph is two-sided. `twoSided` is part of the graph's
    // content hash, so a two-sided graph owns its own cached PSO; the draw
    // command's `twoSided`/flag bit 3 (scene_sync) only reached the shadow and
    // distance-field consumers before — the GBuffer PSO still culled.
    desc.rasterizer.cullMode              = twoSided ? RHICullMode::None : RHICullMode::Back;

    // Depth — enabled, write enabled (deferred geometry pass writes depth)
    desc.depthStencil.depthEnable = true;
    desc.depthStencil.depthWrite  = true;

    // Mesh decal (a Decal-domain graph on a mesh): drawn after the opaque geometry over the finished
    // depth, blending its albedo by the opacity the pixel shader writes in the albedo target's alpha
    // (SrcAlpha / InvSrcAlpha on the colour; Zero / One on the alpha keeps the AO underneath); the
    // normal, position and emissive targets are kept (Zero / One), and the depth is not written.
    if (meshDecal) {
        desc.depthStencil.depthWrite = false;
        for (uint32_t i = 0; i < 4; ++i) {
            auto& b       = desc.renderTargetBlend[i];
            b.blendEnable = true;
            b.srcColor    = RHIBlendFactor::Zero;
            b.dstColor    = RHIBlendFactor::One;
            b.colorOp     = RHIBlendOp::Add;
            b.srcAlpha    = RHIBlendFactor::Zero;
            b.dstAlpha    = RHIBlendFactor::One;
            b.alphaOp     = RHIBlendOp::Add;
        }
        desc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
        desc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
    }

    // G-Buffer render targets — matches GBuffer::Create() and PipelineLibrary::GetGBuffer()
    desc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;    // albedo + AO
    desc.renderTargetFormats[1] = RHIFormat::RGBA16Float;   // octNormal + metallic + roughness
    desc.renderTargetFormats[2] = RHIFormat::RGBA32Float;   // world-space position (debug)
    desc.renderTargetFormats[3] = RHIFormat::RGBA16Float;   // HDR emissive color
    desc.renderTargetCount      = 4;
    desc.depthStencilFormat     = RHIFormat::D32Float;

    desc.debugName = debugName;

    // SM5 path requires vertex input layout; SM6.6 bindless omits it.
    // `desc.inputElements` points into `m_standardLayout` (a member): a local
    // `VertexLayoutResult` stores its elements inline, so pointing into one
    // left the returned descriptor dangling for the caller's
    // CreateGraphicsPipeline.
    if (!bindless) {
        desc.inputElements     = m_standardLayout.elements;
        desc.inputElementCount = m_standardLayout.count;
        desc.vertexStride      = m_standardLayout.stride;
    }

    return desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle-Mesh PSO Resolver
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Packs `(shadingModel, blendMode, twoSided, layout)` into a 64-bit cache
/// key. Each field occupies its own byte so the bit-pattern is trivially
/// reversible during debugging — no hash collisions are possible because
/// every input maps to a unique 32-bit pattern in `[0, 2^32)`.
///
/// Layout (LSB → MSB):
///   bits  0..7  : `MeshVertexLayout` (max 256 layouts)
///   bits  8..15 : `twoSided` (0 or 1, but reserved as a full byte)
///   bits 16..23 : `EBlendMode` (max 256, currently 5)
///   bits 24..31 : `EShadingModel` (max 256, currently 10)
///   bits 32..63 : reserved (zero)
///
/// Why no hashing: the input space is small (≤ 2^32), so a direct bit-pack
/// is faster, deterministic, and debug-readable in `std::format("{:016X}")`.
[[nodiscard]] uint64_t MakeParticleMeshKey(const ParticleMaterialDesc& desc,
                                            MeshVertexLayout layout) noexcept {
    const uint64_t l = static_cast<uint64_t>(layout)              & 0xFFull;
    const uint64_t t = static_cast<uint64_t>(desc.twoSided ? 1 : 0) & 0xFFull;
    const uint64_t b = static_cast<uint64_t>(desc.blendMode)      & 0xFFull;
    const uint64_t s = static_cast<uint64_t>(desc.shadingModel)   & 0xFFull;
    return (s << 24) | (b << 16) | (t << 8) | l;
}

/// Vertex layout descriptor for `MeshVertexLayout::VfxMesh_PosNormalUV`.
/// Matches `VfxMeshVertex` in `vfx_procedural_mesh.h` and
/// `particle_mesh_vs.hlsl`'s VS input struct (Position + Normal + UV,
/// stride 32). Returned by reference so the call site has stable
/// pointer-to-elements lifetime for the entire process.
[[nodiscard]] const RHIInputElementDesc* GetVfxMeshPosNormalUVLayout(uint32_t& outCount,
                                                                      uint32_t& outStride) noexcept {
    static const RHIInputElementDesc kElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float, 0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,  0, 24 },
    };
    outCount  = 3;
    outStride = 32;
    return kElements;
}

} // namespace

bool MaterialPipeline::EnsureParticleMeshShaders() {
    if (m_particleMeshShadersFailed) {
        // Sticky failure — already logged once, do not flood subsequent frames.
        return false;
    }
    if (m_particleMeshVS.IsValid() && m_particleMeshPS.IsValid()) {
        return true;
    }

    // Same compilation path as `MaterialPipeline::Init` uses for `m_meshVS`:
    // LoadBytecodeFromSource via the shared `ShaderPipeline` (DXC). We
    // intentionally do NOT call `PipelineLibrary::EnsureParticleMeshShaders`
    // because that lives in a separate translation unit and uses a
    // different `ShaderManager` instance — duplicating the compile here
    // (≈ a few hundred bytes of DXIL) keeps `MaterialPipeline` self-contained.

    if (!m_particleMeshVS.IsValid()) {
        auto vsBytecode = m_shaderPipeline->LoadBytecodeFromSource(
            EngineShaders::GetName(EngineShader::ParticleMeshVS),
            EngineShaders::GetSource(EngineShader::ParticleMeshVS));
        if (vsBytecode.empty()) {
            UZ_LOG(Renderer, Error,
                   "MaterialPipeline: failed to compile ParticleMeshVS — "
                   "particle-mesh PSO resolver disabled for the rest of this run");
            m_particleMeshShadersFailed = true;
            return false;
        }

        RHIShaderDesc vsDesc{};
        vsDesc.bytecode     = vsBytecode.data();
        vsDesc.bytecodeSize = vsBytecode.size();
        vsDesc.stage        = RHIShaderStage::Vertex;
        vsDesc.entryPoint   = "main";
        vsDesc.debugName    = "MaterialPipeline_ParticleMeshVS";

        m_particleMeshVS = m_device->CreateShader(vsDesc);
        if (!m_particleMeshVS.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "MaterialPipeline: failed to create ParticleMeshVS handle");
            m_particleMeshShadersFailed = true;
            return false;
        }
    }

    if (!m_particleMeshPS.IsValid()) {
        auto psBytecode = m_shaderPipeline->LoadBytecodeFromSource(
            EngineShaders::GetName(EngineShader::ParticleMeshPS),
            EngineShaders::GetSource(EngineShader::ParticleMeshPS));
        if (psBytecode.empty()) {
            UZ_LOG(Renderer, Error,
                   "MaterialPipeline: failed to compile ParticleMeshPS — "
                   "particle-mesh PSO resolver disabled for the rest of this run");
            m_particleMeshShadersFailed = true;
            return false;
        }

        RHIShaderDesc psDesc{};
        psDesc.bytecode     = psBytecode.data();
        psDesc.bytecodeSize = psBytecode.size();
        psDesc.stage        = RHIShaderStage::Pixel;
        psDesc.entryPoint   = "main";
        psDesc.debugName    = "MaterialPipeline_ParticleMeshPS";

        m_particleMeshPS = m_device->CreateShader(psDesc);
        if (!m_particleMeshPS.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "MaterialPipeline: failed to create ParticleMeshPS handle");
            m_particleMeshShadersFailed = true;
            return false;
        }
    }

    return true;
}

RHIGraphicsPipelineDesc MaterialPipeline::BuildParticleMeshPipelineDesc(
    const ParticleMaterialDesc& desc, MeshVertexLayout layout,
    const char* debugName) const
{
    // The render-pass output is the HDR scene-colour target plus the depth
    // buffer; the formats MUST match `PipelineLibrary::GetParticleMeshOpaque`
    // exactly or DX12 will reject the PSO at draw time. See
    // `pipeline_vfx.cpp:752-754` for the canonical RT layout.
    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader = m_particleMeshVS;
    pipeDesc.pixelShader  = m_particleMeshPS;
    pipeDesc.topology     = RHIPrimitiveTopology::TriangleList;

    // ── Vertex layout selection ────────────────────────────────────────────
    // Today there is exactly one layout (`VfxMesh_PosNormalUV`); a switch
    // statement is overkill but it documents the extension point for future
    // skinned/packed variants without forcing a follow-up edit.
    switch (layout) {
        case MeshVertexLayout::VfxMesh_PosNormalUV:
        default: {
            uint32_t count   = 0;
            uint32_t stride  = 0;
            const auto* elem = GetVfxMeshPosNormalUVLayout(count, stride);
            pipeDesc.inputElements     = elem;
            pipeDesc.inputElementCount = count;
            pipeDesc.vertexStride      = stride;
            break;
        }
    }

    // ── Rasterizer (cull mode honours `twoSided`) ─────────────────────────
    pipeDesc.rasterizer.cullMode =
        desc.twoSided ? RHICullMode::None : RHICullMode::Back;

    // ── Depth-stencil state — driven by blend mode ─────────────────────────
    // Reversed-Z: nearer fragments compare GREATER. Opaque writes depth so
    // particles occlude each other; translucent / additive read-only so
    // back-to-front draw order is the only thing that decides blending.
    pipeDesc.depthStencil.depthEnable = true;
    pipeDesc.depthStencil.depthFunc   = RHICompareFunc::Greater;
    pipeDesc.depthStencil.depthWrite  =
        (desc.blendMode == EBlendMode::Opaque ||
         desc.blendMode == EBlendMode::Masked);

    // ── Blend state — distinct configuration per `EBlendMode` ─────────────
    auto& rt0 = pipeDesc.renderTargetBlend[0];
    switch (desc.blendMode) {
        case EBlendMode::Opaque:
        case EBlendMode::Masked:
            // No blending — fragments either fully overwrite (Opaque) or
            // are discarded by alpha test in the PS (Masked). The PSO is
            // identical either way today; the cache key still routes
            // Masked to its own slot so a future masked-specific PS can
            // be hot-swapped in without churning the call site.
            rt0.blendEnable = false;
            break;
        case EBlendMode::Translucent:
            // Standard SrcAlpha / InvSrcAlpha alpha compositing.
            rt0.blendEnable = true;
            rt0.srcColor    = RHIBlendFactor::SrcAlpha;
            rt0.dstColor    = RHIBlendFactor::InvSrcAlpha;
            rt0.colorOp     = RHIBlendOp::Add;
            rt0.srcAlpha    = RHIBlendFactor::One;
            rt0.dstAlpha    = RHIBlendFactor::InvSrcAlpha;
            rt0.alphaOp     = RHIBlendOp::Add;
            break;
        case EBlendMode::Additive:
            // One / One additive — used for sparks, glow, fire embers.
            rt0.blendEnable = true;
            rt0.srcColor    = RHIBlendFactor::SrcAlpha; // pre-mult by particle alpha
            rt0.dstColor    = RHIBlendFactor::One;
            rt0.colorOp     = RHIBlendOp::Add;
            rt0.srcAlpha    = RHIBlendFactor::Zero;
            rt0.dstAlpha    = RHIBlendFactor::One;
            rt0.alphaOp     = RHIBlendOp::Add;
            break;
        case EBlendMode::Modulate:
            // DestColor / Zero multiplicative — rarely used for mesh
            // particles but accepted for parity with the EBlendMode enum.
            rt0.blendEnable = true;
            rt0.srcColor    = RHIBlendFactor::DstColor;
            rt0.dstColor    = RHIBlendFactor::Zero;
            rt0.colorOp     = RHIBlendOp::Add;
            rt0.srcAlpha    = RHIBlendFactor::DstAlpha;
            rt0.dstAlpha    = RHIBlendFactor::Zero;
            rt0.alphaOp     = RHIBlendOp::Add;
            break;
        case EBlendMode::Count:
        default:
            // Out-of-range value — treat as Opaque. The caller is expected
            // to validate ahead of time, but defending here means a corrupt
            // material descriptor never produces a PSO with undefined blend.
            rt0.blendEnable = false;
            break;
    }

    // ── Render target formats ──────────────────────────────────────────────
    pipeDesc.renderTargetFormats[0] = RHIFormat::RGBA16Float; // HDR scene color
    pipeDesc.renderTargetCount      = 1;
    pipeDesc.depthStencilFormat     = RHIFormat::D32Float;
    pipeDesc.debugName              = debugName;

    return pipeDesc;
}

RHIPipelineHandle MaterialPipeline::GetParticleMeshPSO(const ParticleMaterialDesc& desc,
                                                       MeshVertexLayout layout)
{
    UZ_PROFILE_SCOPE("MaterialPipeline::GetParticleMeshPSO");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline::GetParticleMeshPSO called before Init()");
        return RHIPipelineHandle::Invalid();
    }

    // Out-of-range layout is treated as the default to keep the cache
    // bounded; an explicit assert during development surfaces broken call
    // sites without aborting release builds.
    if (static_cast<uint32_t>(layout) >= static_cast<uint32_t>(MeshVertexLayout::Count)) {
        UZ_LOG(Renderer, Warning,
               "MaterialPipeline::GetParticleMeshPSO: layout={} is out of range "
               "(Count={}) — falling back to VfxMesh_PosNormalUV",
               static_cast<uint32_t>(layout),
               static_cast<uint32_t>(MeshVertexLayout::Count));
        layout = MeshVertexLayout::VfxMesh_PosNormalUV;
    }

    // ── Cache lookup ─────────────────────────────────────────────────────
    const uint64_t key = MakeParticleMeshKey(desc, layout);
    if (auto it = m_particleMeshCache.find(key); it != m_particleMeshCache.end()) {
        return it->second;
    }

    // ── Lazy-compile the shared particle-mesh VS / PS ────────────────────
    if (!EnsureParticleMeshShaders()) {
        // Sticky failure — already logged. Cache the invalid handle so we
        // do not retry the (failing) shader compile on every call.
        m_particleMeshCache[key] = RHIPipelineHandle::Invalid();
        return RHIPipelineHandle::Invalid();
    }

    // ── Build + create the PSO ────────────────────────────────────────────
    char debugName[96];
    std::snprintf(debugName, sizeof(debugName),
                  "ParticleMeshPSO_sm%u_bm%u_ts%u_layout%u",
                  static_cast<uint32_t>(desc.shadingModel),
                  static_cast<uint32_t>(desc.blendMode),
                  desc.twoSided ? 1u : 0u,
                  static_cast<uint32_t>(layout));

    const RHIGraphicsPipelineDesc pipeDesc =
        BuildParticleMeshPipelineDesc(desc, layout, debugName);

    RHIPipelineHandle pso = m_device->CreateGraphicsPipeline(pipeDesc);
    if (!pso.IsValid()) {
        UZ_LOG(Renderer, Error,
               "MaterialPipeline::GetParticleMeshPSO: PSO creation failed for "
               "key={:016X} (shadingModel={}, blendMode={}, twoSided={}, layout={})",
               key,
               ShadingModelToString(desc.shadingModel),
               BlendModeToString(desc.blendMode),
               desc.twoSided ? "true" : "false",
               static_cast<uint32_t>(layout));
        // Cache the invalid handle so the failure logs once per (key) and
        // not every frame. The caller is expected to fall back to
        // `PipelineLibrary::GetParticleMeshOpaque()` on `IsValid() == false`.
        m_particleMeshCache[key] = RHIPipelineHandle::Invalid();
        return RHIPipelineHandle::Invalid();
    }

    UZ_LOG(Renderer, Info,
           "MaterialPipeline: created particle-mesh PSO key={:016X} "
           "(shadingModel={}, blendMode={}, twoSided={}, layout={})",
           key,
           ShadingModelToString(desc.shadingModel),
           BlendModeToString(desc.blendMode),
           desc.twoSided ? "true" : "false",
           static_cast<uint32_t>(layout));

    m_particleMeshCache[key] = pso;
    return pso;
}

} // namespace uz
