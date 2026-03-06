#pragma once

// shader_library.h -- Named shader registry with PSO caching.
//
// Provides a centralized registry where shaders are registered by name
// (e.g. "basic", "pbr_gbuffer", "depth_only") with their embedded HLSL
// source. The renderer requests pipelines by shader name + pipeline desc,
// and the library returns a cached PSO handle (creating it on first use).
//
// Usage:
//   ShaderLibrary lib;
//   lib.register_builtin_shaders();
//   RhiPipeline pso = lib.get_or_create_pipeline("basic", base_desc);
//
// Thread safety: NOT thread-safe. Access from render thread only.

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/rhi.h"

namespace uz {

// ---------------------------------------------------------------------------
// ShaderProgram — a VS+PS pair with embedded source
// ---------------------------------------------------------------------------
struct ShaderProgram {
    const char* vs_source = nullptr;
    size_t      vs_size   = 0;
    const char* ps_source = nullptr;
    size_t      ps_size   = 0;
    const char* vs_entry  = "VSMain";
    const char* ps_entry  = "PSMain";
};

// ---------------------------------------------------------------------------
// ShaderLibrary — named shader registry + PSO cache
// ---------------------------------------------------------------------------
class ShaderLibrary {
public:
    ShaderLibrary()  = default;
    ~ShaderLibrary();

    // Non-copyable.
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    /// Register all built-in shaders (basic, fullscreen, depth_only, etc.).
    /// Called once during renderer init.
    void register_builtin_shaders();

    /// Register a shader program by name. Overwrites if name exists.
    void register_shader(const std::string& name, const ShaderProgram& program);

    /// Look up a shader program by name. Returns nullptr if not found.
    [[nodiscard]] const ShaderProgram* get(const std::string& name) const;

    /// Get or create a pipeline for the named shader with the given desc.
    /// The desc's vertex_shader/pixel_shader fields are filled automatically
    /// from the registered shader. Returns RHI_NULL on failure.
    ///
    /// The pipeline is cached per (shader_name + pipeline_desc_hash).
    RhiPipeline get_or_create_pipeline(const std::string& shader_name,
                                        RhiPipelineDesc desc);

    /// List all registered shader names (sorted).
    [[nodiscard]] std::vector<std::string> list() const;

    /// Destroy all cached pipelines (call before RHI shutdown).
    void shutdown();

private:
    struct Entry {
        ShaderProgram program;
        // Cached pipelines keyed by pipeline desc hash.
        std::unordered_map<uint64_t, RhiPipeline> pipelines;
    };

    std::unordered_map<std::string, Entry> shaders_;

    /// Compute a hash of the pipeline desc (excluding shader fields).
    [[nodiscard]] static uint64_t hash_pipeline_desc(const RhiPipelineDesc& desc);
};

} // namespace uz
