#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "engine/renderer/rhi/rhi.h"

namespace uz {

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

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
