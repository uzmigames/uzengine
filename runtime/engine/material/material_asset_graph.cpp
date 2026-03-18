/// UzEngine Graph-Based Material Asset — Implementation
///
/// Loads .uzmat JSON files, deserializes them into a MaterialGraph, and
/// compiles via the MaterialShaderCache.

#include "material_asset_graph.h"

#include "../logging/log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LoadFromFile
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialAssetGraph::LoadFromFile(const std::string& path, MaterialShaderCache& cache) {
    filePath_ = path;

    // Load and parse the .uzmat JSON into the graph.
    std::string parseError;
    if (!graph_.LoadFromFile(path, &parseError)) {
        UZ_LOG(Material, Error, "MaterialAssetGraph: failed to load '{}': {}", path, parseError);
        compiled_ = {};
        generatedHlsl_.clear();
        contentHash_ = 0;
        return false;
    }

    UZ_LOG(Material, Info, "MaterialAssetGraph: loaded '{}' ({} expressions, domain={}, shading={})",
           path, graph_.GetExpressions().size(),
           MaterialDomainToString(graph_.domain),
           MaterialShadingModelToString(graph_.shadingModel));

    // Compile the graph (or retrieve from cache if content hash matches).
    return Recompile(cache);
}

// ─────────────────────────────────────────────────────────────────────────────
// Recompile
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialAssetGraph::Recompile(MaterialShaderCache& cache) {
    const auto& compiled = cache.CompileGraph(graph_);

    contentHash_ = compiled.contentHash;

    if (compiled.valid) {
        compiled_ = compiled.compileResult;
        generatedHlsl_ = compiled.generatedHlsl;
        UZ_LOG(Material, Info,
               "MaterialAssetGraph: '{}' compiled successfully (hash={:016X}, {} params, {} textures)",
               graph_.name, contentHash_,
               compiled_.parameterCount, compiled_.textureSlotCount);
        return true;
    }

    // Compilation failed — preserve error info but clear generated HLSL.
    compiled_ = compiled.compileResult;
    generatedHlsl_.clear();
    UZ_LOG(Material, Error, "MaterialAssetGraph: '{}' compilation failed: {}",
           graph_.name, compiled_.errorMessage);
    return false;
}

} // namespace uz
