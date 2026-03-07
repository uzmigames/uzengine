// graph_material_loader.cpp -- Shader graph → material bridge implementation.

#include "engine/renderer/resources/graph_material_loader.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/core/diag.h"
#include "engine/core/profiler.h"

#include <algorithm>

namespace uz {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Material* GraphMaterialLoader::load_graph_material(
    const std::string& graph_path,
    ShaderLibrary& shader_lib,
    MaterialManager& mat_mgr)
{
    UZ_PROFILE_ZONE("GraphMaterialLoader::load_graph_material");

    // Check if already compiled and cached in MaterialManager.
    const std::string mat_key = std::string(kGraphPrefix) + graph_path;
    if (Material* existing = mat_mgr.get(mat_key)) {
        return existing;
    }

    // Compile the graph to HLSL.
    if (!compile_graph(graph_path)) {
        return nullptr;
    }

    const auto& compiled = compiled_[graph_path];

    // Register the compiled shader in ShaderLibrary.
    ShaderProgram program;
    program.vs_source = compiled.vs_hlsl.c_str();
    program.vs_size   = compiled.vs_hlsl.size();
    program.ps_source = compiled.ps_hlsl.c_str();
    program.ps_size   = compiled.ps_hlsl.size();
    program.vs_entry  = "VSMain";
    program.ps_entry  = "PSMain";
    shader_lib.register_shader(compiled.shader_name, program);

    DIAG_FMT("GraphMaterialLoader: registered shader '%s' (VS: %zu bytes, PS: %zu bytes)",
             compiled.shader_name.c_str(),
             compiled.vs_hlsl.size(),
             compiled.ps_hlsl.size());

    // Build Material struct with extracted parameters.
    Material mat;
    mat.name = compiled.shader_name;
    mat.shader_name = compiled.shader_name;

    // Extract scalar/vector/texture params from graph nodes.
    auto* managed = graphMgr_.get(graph_path);
    if (managed && managed->graph) {
        extract_params(*managed->graph, mat);
    }

    // Insert into MaterialManager's cache using the graph: prefixed key.
    // We use the internal load_or_insert mechanism — since MaterialManager::load
    // only parses .uzmat files, we insert directly via get().
    // For now, we use a direct approach: the renderer will hold this Material.
    // TODO: Add MaterialManager::insert() for non-file materials.
    // For the MVP, return from our internal cache.
    compiled_materials_[mat_key] = std::move(mat);
    return &compiled_materials_[mat_key];
}

bool GraphMaterialLoader::has_compiled(const std::string& graph_path) const {
    return compiled_.count(graph_path) > 0;
}

const CompiledGraphShader* GraphMaterialLoader::get_compiled(
    const std::string& graph_path) const
{
    auto it = compiled_.find(graph_path);
    return (it != compiled_.end()) ? &it->second : nullptr;
}

Material* GraphMaterialLoader::recompile(
    const std::string& graph_path,
    ShaderLibrary& shader_lib,
    MaterialManager& mat_mgr)
{
    // Remove cached compilation to force recompile.
    compiled_.erase(graph_path);
    const std::string mat_key = std::string(kGraphPrefix) + graph_path;
    compiled_materials_.erase(mat_key);
    return load_graph_material(graph_path, shader_lib, mat_mgr);
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------

bool GraphMaterialLoader::compile_graph(const std::string& graph_path) {
    // Already compiled?
    if (compiled_.count(graph_path)) return true;

    // Load the .uzshader file (skip if already in the manager — preserves in-memory edits).
    if (!graphMgr_.has(graph_path)) {
        std::string loaded_path = graphMgr_.load(graph_path);
        if (loaded_path.empty()) {
            DIAG_FMT("GraphMaterialLoader: failed to load '%s'", graph_path.c_str());
            return false;
        }
    }

    auto* managed = graphMgr_.get(graph_path);
    if (!managed || !managed->graph) {
        DIAG_FMT("GraphMaterialLoader: no graph after loading '%s'", graph_path.c_str());
        return false;
    }

    // Compile to HLSL.
    sg::HlslTranslator translator;
    sg::CompileResult result = translator.compile(*managed->graph);

    if (!result.ok) {
        DIAG_FMT("GraphMaterialLoader: compilation failed for '%s':", graph_path.c_str());
        for (const auto& err : result.errors) {
            DIAG_FMT("  %s", err.c_str());
        }
        return false;
    }

    // Store with stable lifetime.
    CompiledGraphShader compiled;
    compiled.vs_hlsl = std::move(result.vertex_hlsl);
    compiled.ps_hlsl = std::move(result.pixel_hlsl);
    compiled.shader_name = make_shader_name(graph_path);
    compiled_[graph_path] = std::move(compiled);

    DIAG_FMT("GraphMaterialLoader: compiled '%s' -> shader '%s'",
             graph_path.c_str(), compiled_[graph_path].shader_name.c_str());
    return true;
}

void GraphMaterialLoader::extract_params(
    const sg::ShaderGraph& graph, Material& mat) const
{
    for (const auto& node : graph.nodes()) {
        if (!node) continue;

        if (node->type_name == "ScalarParam") {
            auto* scalar = static_cast<const sg::ScalarParamNode*>(node.get());
            mat.parameters[scalar->param_name] = scalar->default_val;
        }
        else if (node->type_name == "VectorParam") {
            auto* vec = static_cast<const sg::VectorParamNode*>(node.get());
            mat.vec_parameters[vec->param_name] = {
                vec->default_val[0], vec->default_val[1],
                vec->default_val[2], vec->default_val[3]
            };
        }
        else if (node->type_name == "TextureParam") {
            auto* tex = static_cast<const sg::TextureParamNode*>(node.get());
            // TextureParam nodes have a param_name but no file path at the graph
            // level — the actual texture path is set at the material/instance level.
            // Store an empty texture slot for the renderer to fill.
            mat.textures[tex->param_name] = "";
        }
    }
}

std::string GraphMaterialLoader::make_shader_name(const std::string& graph_path) {
    // Convert path to a valid shader name: "assets/shader_graphs/demo.uzshader"
    // -> "sg_demo"
    std::string name = graph_path;

    // Strip directory prefix.
    auto slash = name.rfind('/');
    if (slash == std::string::npos) slash = name.rfind('\\');
    if (slash != std::string::npos) name = name.substr(slash + 1);

    // Strip extension.
    auto dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(0, dot);

    // Prefix to avoid collisions with built-in shaders.
    return "sg_" + name;
}

} // namespace uz
