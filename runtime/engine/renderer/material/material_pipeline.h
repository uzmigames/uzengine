/// UzEngine Material Pipeline — PSO Compilation for Material Graph Shaders
///
/// Compiles material graphs into GPU-ready pipeline state objects by:
/// 1. Translating the graph to HLSL via HlslMaterialTranslator
/// 2. Injecting the generated HLSL into the gbuffer_ps template
/// 3. Compiling the combined source via ShaderPipeline (DXC)
/// 4. Creating a graphics PSO matching the standard GBuffer layout
/// 5. Caching the result by material content hash
///
/// Each material is compiled into its own shader map containing all
/// required shader permutations. UzEngine simplifies this to a single
/// GBuffer PS variant per material graph, since we use a fixed deferred
/// pipeline with bindless resources.
///
/// # Ownership
/// Owns all compiled PS shader handles and PSO handles in the cache.
/// Does NOT own the device, shader pipeline, or pipeline library — all
/// must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render/init thread.

#pragma once

#include "rhi_types.h"
#include "hlsl_material_translator.h"
#include "material_types.h"  // EShadingModel, EBlendMode (sibling header)
#include "vertex_layout.h"   // VertexLayoutResult (owned input layout for SM5 PSOs)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class IRHIDevice;
class ShaderPipeline;
class MaterialGraph;

// ─────────────────────────────────────────────────────────────────────────────
// MeshVertexLayout — vertex stream layouts compatible with the particle-mesh VS
// ─────────────────────────────────────────────────────────────────────────────

/// Identifies the vertex input layout consumed by the particle-mesh vertex
/// shader. The mesh-particle VS in `particle_mesh_vs.hlsl` indexes a
/// `VfxMeshVertex` (Position float3 + Normal float3 + UV float2, stride 32)
/// via `SV_VertexID`, so today there is exactly one valid layout. The enum
/// is kept open so future layouts (skinned, packed-normal, vertex colours)
/// can extend the resolver cache key without touching the call site.
enum class MeshVertexLayout : uint8_t {
    /// Standard `VfxMeshVertex`: Position (RGB32F) + Normal (RGB32F) + UV (RG32F),
    /// 32-byte stride. Matches the `.uzmesh` LOD0 import path used by VFX.
    VfxMesh_PosNormalUV = 0,

    Count = 1, ///< Total layout count (not a valid layout).
};

// ─────────────────────────────────────────────────────────────────────────────
// ParticleMaterialDesc — minimal descriptor for the particle-mesh PSO resolver
// ─────────────────────────────────────────────────────────────────────────────

/// Cache-key fields extracted from a material asset (`.uzmat`) at batch-build
/// time. Only the four fields referenced by the spec scenarios participate in
/// the PSO cache key; everything else is parameter data fed through bindless
/// SRVs / per-instance constants.
///
/// The descriptor is a value type — copying it is cheap (16 bytes total). The
/// caller (`frame_particles.cpp`) populates one per emitter and forwards it
/// to `MaterialPipeline::GetParticleMeshPSO`.
///
/// `materialPathHash` is **not** part of the PSO cache key — two materials
/// with identical (shadingModel, blendMode, twoSided, layout) tuples MUST
/// share a single PSO. The hash is forwarded to the warning-dedup map so
/// duplicate "material failed to load" warnings can be suppressed per
/// (path × emitter) pair.
struct ParticleMaterialDesc {
    /// Material shading model (DefaultLit, Unlit, Subsurface, ...).
    /// Drives the PSO cache key but does NOT yet select different shader
    /// permutations — the particle-mesh PS treats every model as DefaultLit
    /// today. Extending shading-model support is a future task; the cache
    /// key already routes correctly so swapping in a permuted PS later is
    /// a single-line change.
    EShadingModel shadingModel = EShadingModel::DefaultLit;

    /// Alpha blending mode (Opaque, Masked, Translucent, Additive, Modulate).
    /// Each non-Modulate value maps to a distinct PSO with the corresponding
    /// blend state and depth-write configuration. Modulate is treated as
    /// Translucent for now (same blend factors are not exposed by the existing
    /// PS); the spec only mandates Opaque/Translucent/Additive/Masked.
    EBlendMode blendMode = EBlendMode::Opaque;

    /// When true, the PSO disables back-face culling. Used by translucent
    /// shards / glass / foliage where the camera can see both faces of a
    /// thin mesh particle.
    bool twoSided = false;

    /// xxHash64 / FNV-1a hash of the material's VFS path. Used purely for
    /// warning-dedup keying when the resolve path falls back to the engine
    /// opaque PSO. Zero is a valid sentinel for "no path / inline material".
    uint64_t materialPathHash = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialPipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles material graphs into cached GBuffer PSOs.
///
///
/// Each material graph produces a unique pixel shader that replaces the
/// standard gbuffer_ps.hlsl material evaluation. The pipeline caches
/// compiled PSOs by material content hash (xxHash64), so identical
/// graphs are never recompiled.
///
/// # Usage
/// ```cpp
/// MaterialPipeline matPipeline;
/// matPipeline.Init(device, &shaderPipeline);
///
/// // At draw time:
/// auto pso = matPipeline.GetOrCreatePSO(materialGraph);
/// if (pso.IsValid()) {
///     cmdList->SetPipeline(pso);
///     // ... draw with material graph ...
/// }
/// ```
class MaterialPipeline {
public:
    MaterialPipeline() = default;
    ~MaterialPipeline();

    MaterialPipeline(const MaterialPipeline&) = delete;
    MaterialPipeline& operator=(const MaterialPipeline&) = delete;
    MaterialPipeline(MaterialPipeline&&) noexcept;
    MaterialPipeline& operator=(MaterialPipeline&&) noexcept;

    /// Initializes the material pipeline with device and shader compilation references.
    ///
    /// # Arguments
    /// * `device` — RHI device for shader/PSO creation. Must outlive this object.
    /// * `shaderPipeline` — Shader compilation pipeline (DXC). Must outlive this object.
    ///
    /// # Returns
    /// `true` on success, `false` if arguments are null.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderPipeline* shaderPipeline);

    /// Releases all cached shader handles and PSOs.
    ///
    /// Safe to call multiple times or without prior `Init()`.
    void Shutdown();

    /// Compiles a material graph into a GBuffer PSO, or returns a cached one.
    ///
    /// The compilation pipeline:
    /// 1. Computes graph content hash via MaterialGraph::ComputeContentHash()
    /// 2. Returns cached PSO on hash hit
    /// 3. On miss: translates graph to HLSL via HlslMaterialTranslator
    /// 4. Retrieves gbuffer_ps.hlsl source from EngineShaders
    /// 5. Replaces `#include "material_body.hlsli"` with the generated HLSL
    /// 6. Compiles the combined source with UZ_MATERIAL_GRAPH=1 define
    /// 7. Creates RHI shader handle from the compiled bytecode
    /// 8. Creates GBuffer-compatible graphics PSO (4 RT + D32 depth)
    /// 9. Caches the PSO, compile result, and generated HLSL
    ///
    /// # Arguments
    /// * `graph` — Material graph to compile. Must pass Validate().
    /// * `bindless` — If true, creates PSO without vertex input layout (SM6.6 path).
    ///
    /// # Returns
    /// Valid pipeline handle on success, invalid handle on failure (errors logged).
    [[nodiscard]] RHIPipelineHandle GetOrCreatePSO(const MaterialGraph& graph, bool bindless);

    /// Overload that uses the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetOrCreatePSO(const MaterialGraph& graph);

    /// Checks if a PSO for the given material hash exists in cache.
    ///
    /// # Arguments
    /// * `materialHash` — xxHash64 content hash from MaterialGraph::ComputeContentHash().
    [[nodiscard]] bool HasCachedPSO(uint64_t materialHash) const;

    /// Removes a cached PSO entry, forcing recompilation on next GetOrCreatePSO().
    ///
    /// The shader handle and PSO handle are destroyed via the device.
    /// Use when a material graph has been modified and needs recompilation.
    ///
    /// # Arguments
    /// * `materialHash` — xxHash64 content hash to invalidate.
    void InvalidatePSO(uint64_t materialHash);

    /// Returns the MaterialCompileResult for a previously compiled material graph.
    ///
    /// Contains parameter metadata (slot indices, names, defaults) and texture
    /// slot information needed for runtime material instance binding.
    ///
    /// # Arguments
    /// * `materialHash` — xxHash64 content hash to look up.
    ///
    /// # Returns
    /// Pointer to the compile result if found, nullptr otherwise.
    [[nodiscard]] const MaterialCompileResult* GetCompileResult(uint64_t materialHash) const;

    /// Returns the generated HLSL source for a cached material.
    ///
    /// Useful for debugging and diagnostic inspection of generated shaders.
    ///
    /// # Arguments
    /// * `materialHash` — xxHash64 content hash to look up.
    ///
    /// # Returns
    /// Pointer to the HLSL string if found, nullptr otherwise.
    [[nodiscard]] const std::string* GetGeneratedHlsl(uint64_t materialHash) const;

    /// Returns the number of cached material PSOs.
    [[nodiscard]] size_t GetCacheSize() const noexcept { return m_cache.size(); }

    /// Returns true if Init() has been called successfully.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    // ─────────────────────────────────────────────────────────────────────
    // Particle-mesh PSO resolver (VFX mesh-particle render path)
    // ─────────────────────────────────────────────────────────────────────

    /// Resolves a particle-mesh PSO from a material descriptor.
    ///
    /// The PSO is keyed by the tuple (`shadingModel`, `blendMode`, `twoSided`,
    /// `layout`). Two emitters whose materials share the same tuple are guaranteed
    /// to receive the **identical** `RHIPipelineHandle` (cache hit). The four
    /// supported `EBlendMode` values (Opaque, Masked, Translucent, Additive)
    /// map to distinct PSOs because they require different blend state and
    /// depth-write configurations.
    ///
    /// On any failure to compile the underlying particle-mesh shaders the
    /// method returns `RHIPipelineHandle::Invalid()`. The caller is expected
    /// to detect this and fall back to `PipelineLibrary::GetParticleMeshOpaque()`
    /// (with a one-shot warning logged at the call site) so the affected
    /// emitter still issues a draw.
    ///
    /// # Arguments
    /// * `desc`   — Material cache-key fields. See `ParticleMaterialDesc`.
    /// * `layout` — Vertex layout consumed by the particle-mesh VS.
    ///
    /// # Returns
    /// A valid `RHIPipelineHandle` on success, `RHIPipelineHandle::Invalid()`
    /// on shader compile / PSO creation failure (errors are logged exactly
    /// once per failure mode).
    ///
    /// # Thread Safety
    /// Must be called on the render/init thread, identical to
    /// `GetOrCreatePSO`.
    [[nodiscard]] RHIPipelineHandle GetParticleMeshPSO(const ParticleMaterialDesc& desc,
                                                       MeshVertexLayout layout);

    /// Returns the number of unique particle-mesh PSOs currently cached.
    /// Useful for tests asserting the cache-hit invariant.
    [[nodiscard]] size_t GetParticleMeshCacheSize() const noexcept {
        return m_particleMeshCache.size();
    }

private:
    /// Cached compilation result for a single material graph.
    struct CachedMaterialPSO {
        RHIPipelineHandle pipelineBindless;   ///< PSO for SM6.6 bindless path
        RHIPipelineHandle pipelineSM5;        ///< PSO for SM5 vertex-input path
        RHIShaderHandle pixelShader;          ///< Compiled material pixel shader
        MaterialCompileResult compileResult;  ///< Parameter/texture slot metadata
        std::string generatedHlsl;            ///< Generated HLSL for debugging
    };

    /// Builds the combined pixel shader source by injecting material HLSL
    /// into the gbuffer_ps template.
    ///
    /// # Arguments
    /// * `materialHlsl` — Generated ComputeMaterialOutputs() HLSL function.
    ///
    /// # Returns
    /// Complete HLSL source ready for DXC compilation, or empty on failure.
    [[nodiscard]] std::string BuildCombinedPixelShaderSource(const std::string& materialHlsl) const;

    /// Creates a GBuffer-compatible graphics pipeline descriptor.
    ///
    /// Matches the exact render target formats and depth/rasterizer state used by
    /// PipelineLibrary::GetGBuffer() to ensure draw-time compatibility.
    ///
    /// # Arguments
    /// * `vs` — Vertex shader handle (mesh_vs).
    /// * `ps` — Pixel shader handle (compiled material PS).
    /// * `bindless` — If true, omits vertex input layout.
    /// * `twoSided` — Graph's two-sided flag: disables back-face culling.
    /// * `debugName` — Debug label for the PSO.
    ///
    /// The SM5 input layout points into `m_standardLayout` (a member), so the
    /// returned descriptor stays valid for the caller's CreateGraphicsPipeline.
    [[nodiscard]] RHIGraphicsPipelineDesc BuildGBufferPipelineDesc(
        RHIShaderHandle vs, RHIShaderHandle ps,
        bool bindless, bool twoSided, const char* debugName) const;

    IRHIDevice* m_device = nullptr;
    ShaderPipeline* m_shaderPipeline = nullptr;
    bool m_initialized = false;

    /// Cached mesh vertex shader handle — compiled once and reused for all material PSOs.
    RHIShaderHandle m_meshVS = RHIShaderHandle::Invalid();

    /// Standard vertex input layout, owned here so that the `inputElements`
    /// pointer handed out by BuildGBufferPipelineDesc() never refers to a
    /// destroyed local (`VertexLayoutResult` stores the elements inline).
    VertexLayoutResult m_standardLayout = VertexLayout::Standard();

    /// Map of material content hash -> compiled PSO + metadata.
    std::unordered_map<uint64_t, CachedMaterialPSO> m_cache;

    // ── Particle-mesh resolver state ─────────────────────────────────────────

    /// Lazy-compiled particle-mesh vertex shader handle. Compiled on the
    /// first call to `GetParticleMeshPSO`. Same source as
    /// `EngineShader::ParticleMeshVS` consumed by `PipelineLibrary`.
    RHIShaderHandle m_particleMeshVS = RHIShaderHandle::Invalid();

    /// Lazy-compiled particle-mesh pixel shader handle. Compiled on the
    /// first call to `GetParticleMeshPSO`. Same source as
    /// `EngineShader::ParticleMeshPS` consumed by `PipelineLibrary`.
    RHIShaderHandle m_particleMeshPS = RHIShaderHandle::Invalid();

    /// Sticky failure flag for the particle-mesh shader compile step.
    /// Once set, subsequent calls short-circuit to `Invalid()` without
    /// re-attempting compilation (mirrors `PipelineLibrary`'s
    /// `m_particleMeshOpaqueFailed` pattern to avoid log spam).
    bool m_particleMeshShadersFailed = false;

    /// Cache of compiled particle-mesh PSOs keyed by the packed
    /// (shadingModel, blendMode, twoSided, layout) tuple. The hash function
    /// is `MakeParticleMeshKey` defined in the .cpp.
    std::unordered_map<uint64_t, RHIPipelineHandle> m_particleMeshCache;

    /// Compiles `m_particleMeshVS` and `m_particleMeshPS` if they are not
    /// already valid. Returns false on failure (also sets the sticky
    /// failure flag).
    [[nodiscard]] bool EnsureParticleMeshShaders();

    /// Builds an `RHIGraphicsPipelineDesc` for a particle-mesh PSO variant.
    [[nodiscard]] RHIGraphicsPipelineDesc BuildParticleMeshPipelineDesc(
        const ParticleMaterialDesc& desc, MeshVertexLayout layout,
        const char* debugName) const;
};

} // namespace uz
