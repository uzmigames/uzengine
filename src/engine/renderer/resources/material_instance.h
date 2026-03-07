#pragma once

// material_instance.h -- Lightweight material parameter override layer.
//
// A MaterialInstance references a parent Material and selectively overrides
// scalar, vector, and texture parameters. Parameters not overridden fall
// through to the parent. This follows UE5's UMaterialInstanceDynamic pattern:
// no shader recompilation, just parameter swaps.
//
// RULE RHI-1: No backend types above rhi.h

#include "engine/renderer/resources/material.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ============================================================================
// MaterialInstance -- per-object parameter overrides over a parent Material.
// ============================================================================

struct MaterialInstance {
    uint32_t    id   = 0;        // Unique instance ID (assigned by manager)
    std::string name;            // Human-readable name (e.g. "Gold_Scratched")
    Material*   parent = nullptr;

    // Override maps. Only parameters present here differ from the parent.
    std::unordered_map<std::string, float>              scalar_overrides;
    std::unordered_map<std::string, std::vector<float>> vector_overrides;
    std::unordered_map<std::string, std::string>        texture_overrides;      // asset paths
    std::unordered_map<std::string, RhiTexture>         texture_handle_overrides;

    // Resolved getters: check override first, then fall back to parent.

    [[nodiscard]] float get_scalar(const char* key, float fallback) const {
        auto it = scalar_overrides.find(key);
        if (it != scalar_overrides.end()) return it->second;
        if (parent) {
            auto pit = parent->parameters.find(key);
            if (pit != parent->parameters.end()) return pit->second;
        }
        return fallback;
    }

    [[nodiscard]] const std::vector<float>* get_vector(const char* key) const {
        auto it = vector_overrides.find(key);
        if (it != vector_overrides.end()) return &it->second;
        if (parent) {
            auto pit = parent->vec_parameters.find(key);
            if (pit != parent->vec_parameters.end()) return &pit->second;
        }
        return nullptr;
    }

    [[nodiscard]] RhiTexture get_texture(const char* key) const {
        auto it = texture_handle_overrides.find(key);
        if (it != texture_handle_overrides.end()) return it->second;
        if (parent) {
            auto pit = parent->texture_handles.find(key);
            if (pit != parent->texture_handles.end()) return pit->second;
        }
        return RHI_NULL;
    }

    // Set a scalar override.
    void set_scalar(const std::string& key, float value) {
        scalar_overrides[key] = value;
    }

    // Set a vector override.
    void set_vector(const std::string& key, std::vector<float> value) {
        vector_overrides[key] = std::move(value);
    }

    // Clear a scalar override (reverts to parent value).
    void clear_scalar(const std::string& key) {
        scalar_overrides.erase(key);
    }

    // Clear a vector override.
    void clear_vector(const std::string& key) {
        vector_overrides.erase(key);
    }

    // Clear a texture override.
    void clear_texture(const std::string& key) {
        texture_overrides.erase(key);
        texture_handle_overrides.erase(key);
    }

    // Check if a parameter is overridden.
    [[nodiscard]] bool has_scalar_override(const char* key) const {
        return scalar_overrides.count(key) > 0;
    }
    [[nodiscard]] bool has_vector_override(const char* key) const {
        return vector_overrides.count(key) > 0;
    }
    [[nodiscard]] bool has_texture_override(const char* key) const {
        return texture_handle_overrides.count(key) > 0;
    }
};

// ============================================================================
// MaterialInstanceManager -- creates, caches, and resolves material instances.
// ============================================================================

class MaterialInstanceManager {
public:
    MaterialInstanceManager();
    ~MaterialInstanceManager();

    MaterialInstanceManager(const MaterialInstanceManager&) = delete;
    MaterialInstanceManager& operator=(const MaterialInstanceManager&) = delete;

    /// Create a new material instance from a parent material.
    /// Returns the instance pointer (owned by manager).
    MaterialInstance* create(const std::string& name, Material* parent);

    /// Get an instance by its unique ID.
    [[nodiscard]] MaterialInstance* get(uint32_t id) const;

    /// Get an instance by name.
    [[nodiscard]] MaterialInstance* find(const std::string& name) const;

    /// Destroy an instance by ID.
    void destroy(uint32_t id);

    /// Get all instances (for serialization / editor listing).
    [[nodiscard]] const std::unordered_map<uint32_t, MaterialInstance*>& all() const;

    /// Set a texture override with automatic async loading.
    /// Queues the texture for async decode and stores the handle.
    void set_texture_override(MaterialInstance* inst,
                              const std::string& key,
                              const std::string& asset_path);

    /// Clear all instances.
    void clear();

    /// Returns the number of active instances.
    [[nodiscard]] uint32_t count() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
