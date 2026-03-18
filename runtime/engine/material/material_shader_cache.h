/// UzEngine Material Shader Cache — Content-Addressed Cache for Compiled Material Graphs
///
/// Caches the result of compiling a MaterialGraph through the HlslMaterialTranslator.
/// Uses the graph's content hash (xxHash64) as the cache key, so identical graphs
/// always produce a cache hit regardless of expression insertion order or file path.
///
/// This is a **source-level cache only** — it stores generated HLSL source and
/// metadata (parameter/texture slot info). Actual DXC compilation and PSO creation
/// happen in the renderer layer when the material is first bound to a draw call.
///
/// @source UE5 MaterialShaderMap.h — FMaterialShaderMap (content-addressed shader cache)
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the game/render thread.
///
/// # Examples
/// ```cpp
/// MaterialShaderCache cache;
/// cache.Init("cache/materials");
///
/// MaterialGraph graph;
/// // ... build graph ...
///
/// const auto& compiled = cache.CompileGraph(graph);
/// if (compiled.valid) {
///     // Use compiled.generatedHlsl and compiled.compileResult
/// }
/// ```

#pragma once

#include "compiler/hlsl_material_translator.h"
#include "material_graph.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MaterialShaderCache
// ─────────────────────────────────────────────────────────────────────────────

/// Content-addressed cache for compiled material graphs.
///
/// @source UE5 MaterialShaderMap.h — FMaterialShaderMap
/// Stores generated HLSL source and MaterialCompileResult metadata, keyed by
/// the graph's content hash. On cache hit (same topology + parameters), returns
/// the previously compiled result without re-running the translator.
class MaterialShaderCache {
public:
    /// Result of compiling a material graph, stored in the cache.
    struct CompiledMaterial {
        uint64_t contentHash = 0;          ///< xxHash64 of the source graph
        std::string generatedHlsl;         ///< The ComputeMaterialOutputs() HLSL function
        MaterialCompileResult compileResult; ///< Full compilation metadata
        bool valid = false;                ///< True if compilation succeeded
    };

    /// Initialize the cache with an optional directory for future disk persistence.
    ///
    /// The directory is created if it does not exist. Currently only used for
    /// diagnostic HLSL dumps (writing generated source to disk for inspection).
    ///
    /// # Parameters
    /// - `cacheDir`: Directory path for cache artifacts. Default "cache/materials".
    void Init(const std::string& cacheDir = "cache/materials");

    /// Compile a MaterialGraph or retrieve from cache if already compiled.
    ///
    /// 1. Computes the graph's content hash via MaterialGraph::ComputeContentHash()
    /// 2. Checks the in-memory cache for a matching hash
    /// 3. On miss: runs HlslMaterialTranslator::Compile(), stores the result
    /// 4. Returns a const reference to the cached CompiledMaterial
    ///
    /// The returned reference is stable until ClearCache() is called.
    ///
    /// # Parameters
    /// - `graph`: The material graph to compile. Must pass Validate().
    ///
    /// # Returns
    /// Reference to the cached compilation result. Check `.valid` for success.
    [[nodiscard]] const CompiledMaterial& CompileGraph(const MaterialGraph& graph);

    /// Check if a graph with the given content hash is already in the cache.
    ///
    /// # Parameters
    /// - `contentHash`: xxHash64 content hash from MaterialGraph::ComputeContentHash().
    ///
    /// # Returns
    /// True if the hash maps to a valid cached compilation result.
    [[nodiscard]] bool IsCached(uint64_t contentHash) const;

    /// Remove all entries from the in-memory cache.
    ///
    /// After calling this, all subsequent CompileGraph() calls will re-run
    /// the translator. Existing references to CompiledMaterial become invalid.
    void ClearCache();

    /// Retrieve the generated HLSL source for a cached content hash.
    ///
    /// Useful for debugging and diagnostic dump of generated shader code.
    ///
    /// # Parameters
    /// - `contentHash`: xxHash64 content hash to look up.
    ///
    /// # Returns
    /// Pointer to the HLSL string if found, nullptr otherwise.
    [[nodiscard]] const std::string* GetHlsl(uint64_t contentHash) const;

    /// Returns the number of entries currently in the cache.
    [[nodiscard]] size_t GetCacheSize() const noexcept { return cache_.size(); }

private:
    std::string cacheDir_;
    std::unordered_map<uint64_t, CompiledMaterial> cache_;

    /// Sentinel entry returned when compilation fails and no cache entry exists.
    static const CompiledMaterial s_invalidEntry;
};

} // namespace uz
