// graph_material_loader.h -- Bridge between shader graph and material system.
//
// Detects the "graph:" prefix in material paths, compiles .uzshader files
// via ShaderGraphManager + HlslTranslator, registers compiled HLSL in
// ShaderLibrary, and creates a Material with extracted parameters.
//
// HLSL string lifetime: stored internally so ShaderProgram const char*
// pointers remain valid until the loader is destroyed.

#pragma once

#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/shader_library.h"
#include "engine/renderer/shader_graph/shader_graph_manager.h"
#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"

#include <string>
#include <unordered_map>

namespace uz {

// Prefix used in material_path to indicate a shader graph material.
inline constexpr const char* kGraphPrefix = "graph:";
inline constexpr size_t kGraphPrefixLen = 6;  // strlen("graph:")

/// Returns true if the path starts with "graph:".
inline bool is_graph_material_path(const std::string& path) {
    return path.size() > kGraphPrefixLen &&
           path.compare(0, kGraphPrefixLen, kGraphPrefix) == 0;
}

/// Strip the "graph:" prefix from a material path.
inline std::string strip_graph_prefix(const std::string& path) {
    return path.substr(kGraphPrefixLen);
}

/// One entry in the graph shader's cbuffer layout.
struct GraphCbufferEntry {
    std::string name;
    uint32_t offset = 0;       // Byte offset in cbuffer.
    uint32_t size   = 4;       // 4 = float, 8 = float2, 12 = float3, 16 = float4.
    float    default_val[4] = {0.f, 0.f, 0.f, 0.f};
};

/// Compiled HLSL source with stable string storage.
struct CompiledGraphShader {
    std::string vs_hlsl;
    std::string ps_hlsl;
    std::string shader_name;  // Unique name for ShaderLibrary registration.

    // Cbuffer layout metadata (sorted alphabetically, matches HLSL).
    std::vector<GraphCbufferEntry> cbuffer_layout;
    uint32_t cbuffer_size = 0;

    /// Fill a cbuffer data buffer from Material params. Returns byte count.
    uint32_t fill_cbuffer(const Material& mat, void* dst, uint32_t max_bytes) const;
};

class GraphMaterialLoader {
public:
    GraphMaterialLoader() = default;

    /// Compile a .uzshader file, register its shader in ShaderLibrary, and
    /// create a Material in MaterialManager. Returns the Material pointer,
    /// or nullptr on failure.
    ///
    /// @param graph_path  The .uzshader file path (without "graph:" prefix).
    /// @param shader_lib  ShaderLibrary to register compiled shaders into.
    /// @param mat_mgr     MaterialManager to insert the resulting Material into.
    Material* load_graph_material(const std::string& graph_path,
                                  ShaderLibrary& shader_lib,
                                  MaterialManager& mat_mgr);

    /// Check if a graph shader was already compiled.
    bool has_compiled(const std::string& graph_path) const;

    /// Get the compiled HLSL for a graph path (or nullptr).
    const CompiledGraphShader* get_compiled(const std::string& graph_path) const;

    /// Force recompile a graph (e.g., after graph mutation).
    Material* recompile(const std::string& graph_path,
                        ShaderLibrary& shader_lib,
                        MaterialManager& mat_mgr);

    /// Access the internal ShaderGraphManager.
    sg::ShaderGraphManager& graph_manager() { return graphMgr_; }

private:
    /// Compile the graph and store HLSL strings.
    bool compile_graph(const std::string& graph_path);

    /// Extract material parameters from the shader graph nodes.
    void extract_params(const sg::ShaderGraph& graph, Material& mat) const;

    /// Build cbuffer layout metadata from graph param nodes.
    void build_cbuffer_layout(const sg::ShaderGraph& graph,
                              CompiledGraphShader& compiled) const;

    /// Generate a unique shader name from the file path.
    static std::string make_shader_name(const std::string& graph_path);

    sg::ShaderGraphManager graphMgr_;

    // Stable HLSL string storage (ShaderProgram.vs_source/ps_source point here).
    std::unordered_map<std::string, CompiledGraphShader> compiled_;

    // Material cache for graph-based materials (keyed by "graph:" + path).
    std::unordered_map<std::string, Material> compiled_materials_;
};

} // namespace uz
