/// UzEngine Material Pipeline — PSO Compilation for Material Graph Shaders
///
/// Compiles material graphs into GPU-ready pipeline state objects by:
/// 1. Translating the graph to HLSL via HlslMaterialTranslator
/// 2. Injecting the generated HLSL into the gbuffer_ps template
/// 3. Compiling the combined source via ShaderPipeline (DXC)
/// 4. Creating a graphics PSO matching the standard GBuffer layout
/// 5. Caching the result by material content hash
///
/// @source UE5 MaterialShaderMap.cpp — FMaterialShaderMap::Compile()
/// UE5 compiles each material into its own shader map containing all
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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class IRHIDevice;
class ShaderPipeline;
class MaterialGraph;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialPipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Compiles material graphs into cached GBuffer PSOs.
///
/// @source UE5 MaterialShaderMap.cpp — FMaterialShaderMap::Compile()
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
    /// * `debugName` — Debug label for the PSO.
    [[nodiscard]] RHIGraphicsPipelineDesc BuildGBufferPipelineDesc(
        RHIShaderHandle vs, RHIShaderHandle ps,
        bool bindless, const char* debugName) const;

    IRHIDevice* m_device = nullptr;
    ShaderPipeline* m_shaderPipeline = nullptr;
    bool m_initialized = false;

    /// Cached mesh vertex shader handle — compiled once and reused for all material PSOs.
    RHIShaderHandle m_meshVS = RHIShaderHandle::Invalid();

    /// Map of material content hash -> compiled PSO + metadata.
    std::unordered_map<uint64_t, CachedMaterialPSO> m_cache;
};

} // namespace uz
