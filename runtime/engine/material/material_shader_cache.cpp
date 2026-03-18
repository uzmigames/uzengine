/// UzEngine Material Shader Cache — Implementation
///
/// Content-addressed compilation cache for material graphs. Uses xxHash64
/// content hashes as keys to avoid redundant HLSL translation.

#include "material_shader_cache.h"

#include "../logging/log.h"

#include <filesystem>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Static sentinel
// ─────────────────────────────────────────────────────────────────────────────

const MaterialShaderCache::CompiledMaterial MaterialShaderCache::s_invalidEntry = {};

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

void MaterialShaderCache::Init(const std::string& cacheDir) {
    cacheDir_ = cacheDir;

    // Create cache directory if it does not exist (for diagnostic HLSL dumps).
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);
    if (ec) {
        UZ_LOG(Material, Warning, "MaterialShaderCache: could not create cache dir '{}': {}",
               cacheDir_, ec.message());
    }

    UZ_LOG(Material, Info, "MaterialShaderCache initialized (dir='{}')", cacheDir_);
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileGraph
// ─────────────────────────────────────────────────────────────────────────────

const MaterialShaderCache::CompiledMaterial&
MaterialShaderCache::CompileGraph(const MaterialGraph& graph) {
    const uint64_t hash = graph.ComputeContentHash();

    // Check in-memory cache first.
    auto it = cache_.find(hash);
    if (it != cache_.end()) {
        UZ_LOG(Material, Debug, "MaterialShaderCache: cache hit for hash {:016X}", hash);
        return it->second;
    }

    // Cache miss — validate the graph before compilation.
    std::string validationError;
    if (!graph.Validate(validationError)) {
        UZ_LOG(Material, Error, "MaterialShaderCache: graph validation failed: {}", validationError);
        return s_invalidEntry;
    }

    // Compile via HlslMaterialTranslator.
    HlslMaterialTranslator translator;
    MaterialCompileResult result = translator.Compile(graph);

    CompiledMaterial entry;
    entry.contentHash = hash;
    entry.valid = result.success;

    if (result.success) {
        entry.generatedHlsl = result.hlslSource;
        entry.compileResult = std::move(result);
        UZ_LOG(Material, Info,
               "MaterialShaderCache: compiled graph '{}' (hash={:016X}, {} params, {} textures)",
               graph.name, hash, entry.compileResult.parameterCount,
               entry.compileResult.textureSlotCount);
    } else {
        entry.compileResult = std::move(result);
        UZ_LOG(Material, Error, "MaterialShaderCache: compilation failed for '{}': {}",
               graph.name, entry.compileResult.errorMessage);
    }

    auto [insertIt, inserted] = cache_.emplace(hash, std::move(entry));
    return insertIt->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// IsCached
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialShaderCache::IsCached(uint64_t contentHash) const {
    auto it = cache_.find(contentHash);
    return it != cache_.end() && it->second.valid;
}

// ─────────────────────────────────────────────────────────────────────────────
// ClearCache
// ─────────────────────────────────────────────────────────────────────────────

void MaterialShaderCache::ClearCache() {
    const size_t count = cache_.size();
    cache_.clear();
    UZ_LOG(Material, Info, "MaterialShaderCache: cleared {} entries", count);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetHlsl
// ─────────────────────────────────────────────────────────────────────────────

const std::string* MaterialShaderCache::GetHlsl(uint64_t contentHash) const {
    auto it = cache_.find(contentHash);
    if (it != cache_.end() && it->second.valid) {
        return &it->second.generatedHlsl;
    }
    return nullptr;
}

} // namespace uz
