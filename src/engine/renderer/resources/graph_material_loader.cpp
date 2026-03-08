// graph_material_loader.cpp -- Shader graph → material bridge implementation.

#include "engine/renderer/resources/graph_material_loader.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/core/diag.h"
#include "engine/core/profiler.h"

#include <algorithm>
#include <cstring>

namespace uz {

// ---------------------------------------------------------------------------
// CompiledGraphShader::fill_cbuffer
// ---------------------------------------------------------------------------

uint32_t CompiledGraphShader::fill_cbuffer(
    const Material& mat, void* dst, uint32_t max_bytes) const
{
    if (cbuffer_size == 0 || !dst) return 0;
    uint32_t bytes = std::min(cbuffer_size, max_bytes);
    std::memset(dst, 0, bytes);

    for (const auto& entry : cbuffer_layout) {
        if (entry.offset + entry.size > bytes) continue;

        auto* out = static_cast<uint8_t*>(dst) + entry.offset;

        if (entry.size == 4) {
            // Scalar.
            auto it = mat.parameters.find(entry.name);
            float val = (it != mat.parameters.end()) ? it->second : entry.default_val[0];
            std::memcpy(out, &val, 4);
        } else {
            // Vector (8, 12, or 16 bytes).
            auto it = mat.vec_parameters.find(entry.name);
            if (it != mat.vec_parameters.end()) {
                uint32_t count = std::min(entry.size / 4, (uint32_t)it->second.size());
                std::memcpy(out, it->second.data(), count * 4);
            } else {
                std::memcpy(out, entry.default_val, entry.size);
            }
        }
    }
    return bytes;
}

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
    program.vs_entry  = "main";
    program.ps_entry  = "main";
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

    // Store in internal cache and link to compiled shader data.
    compiled_materials_[mat_key] = std::move(mat);
    compiled_materials_[mat_key].graph_compiled = &compiled_[graph_path];
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

    // Build cbuffer layout metadata (matches HlslTranslator's sorted output).
    build_cbuffer_layout(*managed->graph, compiled);

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

void GraphMaterialLoader::build_cbuffer_layout(
    const sg::ShaderGraph& graph, CompiledGraphShader& compiled) const
{
    compiled.cbuffer_layout.clear();
    compiled.cbuffer_size = 0;

    // Collect scalar and vector param nodes, matching HlslTranslator's sort order:
    // scalars sorted alphabetically first, then vectors sorted alphabetically.
    struct ScalarInfo { std::string name; float default_val; };
    struct VectorInfo { std::string name; float default_val[4]; uint32_t count; };
    std::vector<ScalarInfo> scalars;
    std::vector<VectorInfo> vectors;

    for (const auto& node : graph.nodes()) {
        if (!node) continue;
        if (node->type_name == "ScalarParam") {
            auto* s = static_cast<const sg::ScalarParamNode*>(node.get());
            scalars.push_back({s->param_name, s->default_val});
        } else if (node->type_name == "VectorParam") {
            auto* v = static_cast<const sg::VectorParamNode*>(node.get());
            uint32_t count = 4; // VectorParam always emits float4 in default usage.
            // Determine actual component count from connections (default to 4).
            VectorInfo vi{};
            vi.name = v->param_name;
            std::memcpy(vi.default_val, v->default_val, 16);
            vi.count = count;
            vectors.push_back(std::move(vi));
        }
    }

    std::sort(scalars.begin(), scalars.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(vectors.begin(), vectors.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });

    // HLSL cbuffer struct packing: members cannot straddle 16-byte boundaries.
    uint32_t offset = 0;

    auto align_to = [](uint32_t off, uint32_t alignment) -> uint32_t {
        return (off + alignment - 1) & ~(alignment - 1);
    };

    // Scalars first (each is float = 4 bytes).
    for (const auto& s : scalars) {
        // A float can straddle a 16-byte boundary only if it would cross one.
        uint32_t row_start = (offset / 16) * 16;
        if (offset + 4 > row_start + 16) {
            offset = row_start + 16; // Pad to next row.
        }
        GraphCbufferEntry entry;
        entry.name = s.name;
        entry.offset = offset;
        entry.size = 4;
        entry.default_val[0] = s.default_val;
        compiled.cbuffer_layout.push_back(std::move(entry));
        offset += 4;
    }

    // Vectors next.
    for (const auto& v : vectors) {
        uint32_t elem_size = v.count * 4;
        uint32_t align = (v.count >= 3) ? 16 : (v.count * 4);
        // HLSL rule: vector cannot straddle a 16-byte boundary.
        uint32_t row_start = (offset / 16) * 16;
        if (offset + elem_size > row_start + 16) {
            offset = row_start + 16;
        }
        offset = align_to(offset, 4); // At minimum 4-byte aligned.
        GraphCbufferEntry entry;
        entry.name = v.name;
        entry.offset = offset;
        entry.size = elem_size;
        std::memcpy(entry.default_val, v.default_val, 16);
        compiled.cbuffer_layout.push_back(std::move(entry));
        offset += elem_size;
    }

    // Round up to 16-byte alignment for cbuffer total size.
    compiled.cbuffer_size = align_to(offset, 16);
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
