#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "engine/renderer/rhi/rhi.h"

namespace uz {

struct CompiledGraphShader;  // Forward declaration for graph material support.

// ============================================================================
// Material — properties and shader assignment for a renderable surface.
// ============================================================================

struct Material {
    std::string name;
    std::string shader_name;

    // Scalar and vector parameters.
    std::unordered_map<std::string, float> parameters;
    std::unordered_map<std::string, std::vector<float>> vec_parameters;

    // Texture assignments (asset paths).
    std::unordered_map<std::string, std::string> textures;

    // Resolved RHI handles (populated by MaterialManager).
    std::unordered_map<std::string, RhiTexture> texture_handles;

    // Shader graph material support.
    RhiPipeline graph_pso = RHI_NULL;  // Graph-specific PSO (RHI_NULL for standard materials).
    const CompiledGraphShader* graph_compiled = nullptr;  // Cbuffer layout metadata.
    bool is_graph_material() const { return graph_pso != RHI_NULL; }
};

// ============================================================================
// MaterialManager — loading, caching, and hot-reloading of .uzmat files.
// ============================================================================

class MaterialManager {
public:
    MaterialManager();
    ~MaterialManager();

    // Non-copyable.
    MaterialManager(const MaterialManager&) = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;

    /// Load a material from a .uzmat file. Returns a pointer to the cached
    /// material, or nullptr if loading fails.
    Material* load(const std::string& path);

    /// Get a cached material by its original load path.
    Material* get(const std::string& path) const;

    /// Reload a material from disk.
    bool reload(const std::string& path);

    /// Unload all materials and textures.
    void clear();

    /// Re-resolve texture handles from the texture cache.
    /// Returns true if any material's handles were updated.
    bool refresh_textures();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// ============================================================================
// Free functions — incremental texture loading (called by renderer each frame)
// ============================================================================

/// Create 1x1 placeholder textures. Call once after RHI init.
void texture_init_placeholders();

/// Decode + upload up to `budget` queued textures. Returns true if any loaded.
bool texture_tick(int budget);

/// Returns true if there are textures queued for loading.
bool texture_has_queued();

/// Clear all cached textures and pending work. For testing.
void texture_clear_all();

/// Queue a texture for async loading and return its handle (placeholder until
/// the real data arrives). Same handle is reused if already cached.
/// Used by MaterialInstance for texture overrides.
RhiTexture texture_get_or_queue(const std::string& path);

} // namespace uz
