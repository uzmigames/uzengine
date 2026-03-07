// hlsl_translator.cpp -- HLSL code generator implementation.
//
// Walks from MaterialOutput backward through the graph, calling each node's
// compile() which invokes IShaderCompiler methods. Builds code chunks (SSA IR),
// then assembles the final VS/PS HLSL text.

#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"
#include "engine/renderer/shader_graph/shader_graph.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace uz::sg {

// ============================================================================
// Reset / helpers
// ============================================================================

void HlslTranslator::reset() {
    chunks_.clear();
    expr_cache_.clear();
    compilation_stack_.clear();
    scalar_params_.clear();
    vector_params_.clear();
    texture_params_.clear();
    errors_.clear();
    graph_ = nullptr;
    symbol_counter_ = 0;
    tex_register_ = 0;
    for (auto& c : output_chunks_) c = -1;
}

int32_t HlslTranslator::add_chunk(const std::string& expr, ValueType type, bool is_inline) {
    int32_t idx = static_cast<int32_t>(chunks_.size());
    CodeChunk chunk;
    chunk.index = idx;
    chunk.expr = expr;
    chunk.type = type;
    chunk.is_inline = is_inline;
    if (!is_inline) {
        chunk.var_name = create_symbol();
    }
    chunks_.push_back(std::move(chunk));
    return idx;
}

std::string HlslTranslator::get_code(int32_t index) const {
    if (index < 0 || index >= static_cast<int32_t>(chunks_.size())) return "0.0";
    auto& chunk = chunks_[index];
    return chunk.is_inline ? chunk.expr : chunk.var_name;
}

ValueType HlslTranslator::get_type(int32_t index) const {
    if (index < 0 || index >= static_cast<int32_t>(chunks_.size())) return ValueType::Float1;
    return chunks_[index].type;
}

std::string HlslTranslator::create_symbol() {
    char buf[32];
    snprintf(buf, sizeof(buf), "v%d", symbol_counter_++);
    return buf;
}

// ============================================================================
// compile() — main entry point
// ============================================================================

CompileResult HlslTranslator::compile(ShaderGraph& graph) {
    reset();
    graph_ = &graph;

    CompileResult result;

    // Find the MaterialOutput node.
    uint32_t out_id = graph.output_node_id();
    auto* out_node = graph.get_node(out_id);
    if (!out_node) {
        // Try to find it by type.
        for (auto& n : graph.nodes()) {
            if (n->type_name == "MaterialOutput") {
                out_node = n.get();
                out_id = n->id;
                break;
            }
        }
    }

    if (!out_node) {
        result.errors.push_back("No MaterialOutput node found");
        return result;
    }

    // Compile each connected input of MaterialOutput.
    // Pin order: BaseColor(0), Metallic(1), Roughness(2), Normal(3),
    //            Emission(4), AO(5), Opacity(6), Displacement(7)
    for (int32_t i = 0; i < out_node->input_count(); ++i) {
        auto& inp = out_node->get_input(i);
        if (inp.source_node_id != 0) {
            output_chunks_[i] = compile_input(inp.source_node_id, inp.source_output_index);
        }
    }

    if (!errors_.empty()) {
        result.errors = errors_;
        return result;
    }

    result.pixel_hlsl = assemble_pixel_shader(graph);
    result.vertex_hlsl = assemble_vertex_shader(graph);
    result.ok = true;
    result.errors = errors_;
    return result;
}

// ============================================================================
// IShaderCompiler — Constants
// ============================================================================

int32_t HlslTranslator::constant(float v) {
    char buf[64];
    // Format without trailing zeros but always with decimal point.
    if (v == static_cast<int>(v)) {
        snprintf(buf, sizeof(buf), "%.1f", v);
    } else {
        snprintf(buf, sizeof(buf), "%g", v);
    }
    return add_chunk(buf, ValueType::Float1, true);
}

int32_t HlslTranslator::constant_vec(const float* v, int32_t count) {
    std::string expr;
    ValueType type;
    switch (count) {
        case 1: type = ValueType::Float1; break;
        case 2: type = ValueType::Float2; expr = "float2("; break;
        case 3: type = ValueType::Float3; expr = "float3("; break;
        case 4: type = ValueType::Float4; expr = "float4("; break;
        default: return constant(0.f);
    }
    if (count == 1) return constant(v[0]);

    for (int i = 0; i < count; ++i) {
        if (i > 0) expr += ", ";
        char buf[64];
        if (v[i] == static_cast<int>(v[i])) {
            snprintf(buf, sizeof(buf), "%.1f", v[i]);
        } else {
            snprintf(buf, sizeof(buf), "%g", v[i]);
        }
        expr += buf;
    }
    expr += ")";
    return add_chunk(expr, type, true);
}

// ============================================================================
// IShaderCompiler — Arithmetic
// ============================================================================

int32_t HlslTranslator::add(int32_t a, int32_t b) {
    auto type = promote_type(get_type(a), get_type(b));
    if (!type) type = get_type(a);
    std::string expr = "(" + get_code(a) + " + " + get_code(b) + ")";
    return add_chunk(expr, type, false);
}

int32_t HlslTranslator::subtract(int32_t a, int32_t b) {
    auto type = promote_type(get_type(a), get_type(b));
    if (!type) type = get_type(a);
    std::string expr = "(" + get_code(a) + " - " + get_code(b) + ")";
    return add_chunk(expr, type, false);
}

int32_t HlslTranslator::multiply(int32_t a, int32_t b) {
    auto type = promote_type(get_type(a), get_type(b));
    if (!type) type = get_type(a);
    std::string expr = "(" + get_code(a) + " * " + get_code(b) + ")";
    return add_chunk(expr, type, false);
}

int32_t HlslTranslator::divide(int32_t a, int32_t b) {
    auto type = promote_type(get_type(a), get_type(b));
    if (!type) type = get_type(a);
    std::string expr = "(" + get_code(a) + " / " + get_code(b) + ")";
    return add_chunk(expr, type, false);
}

// ============================================================================
// IShaderCompiler — Math functions
// ============================================================================

int32_t HlslTranslator::lerp(int32_t a, int32_t b, int32_t alpha) {
    auto type = promote_type(get_type(a), get_type(b));
    if (!type) type = get_type(a);
    std::string expr = "lerp(" + get_code(a) + ", " + get_code(b) + ", " + get_code(alpha) + ")";
    return add_chunk(expr, type, false);
}

int32_t HlslTranslator::dot(int32_t a, int32_t b) {
    std::string expr = "dot(" + get_code(a) + ", " + get_code(b) + ")";
    return add_chunk(expr, ValueType::Float1, false);
}

int32_t HlslTranslator::normalize(int32_t v) {
    std::string expr = "normalize(" + get_code(v) + ")";
    return add_chunk(expr, get_type(v), false);
}

int32_t HlslTranslator::power(int32_t base, int32_t exp) {
    auto type = promote_type(get_type(base), get_type(exp));
    if (!type) type = get_type(base);
    std::string expr = "pow(" + get_code(base) + ", " + get_code(exp) + ")";
    return add_chunk(expr, type, false);
}

// ============================================================================
// IShaderCompiler — Texture
// ============================================================================

int32_t HlslTranslator::texture_sample(int32_t tex_param_chunk, int32_t uv) {
    std::string expr = get_code(tex_param_chunk) + ".Sample(g_Sampler, " + get_code(uv) + ")";
    return add_chunk(expr, ValueType::Float4, false);
}

// ============================================================================
// IShaderCompiler — Component operations
// ============================================================================

int32_t HlslTranslator::component_mask(int32_t v, const char* mask) {
    std::string expr = get_code(v) + "." + mask;

    // Determine output type from mask length.
    int mask_len = static_cast<int>(strlen(mask));
    ValueType type;
    switch (mask_len) {
        case 1: type = ValueType::Float1; break;
        case 2: type = ValueType::Float2; break;
        case 3: type = ValueType::Float3; break;
        case 4: type = ValueType::Float4; break;
        default: type = ValueType::Float1; break;
    }
    return add_chunk(expr, type, true);
}

int32_t HlslTranslator::append(int32_t a, int32_t b) {
    int ca = component_count(get_type(a));
    int cb = component_count(get_type(b));
    if (ca == 0) ca = 1;
    if (cb == 0) cb = 1;
    int total = ca + cb;
    if (total > 4) total = 4;

    ValueType type;
    switch (total) {
        case 1: type = ValueType::Float1; break;
        case 2: type = ValueType::Float2; break;
        case 3: type = ValueType::Float3; break;
        default: type = ValueType::Float4; break;
    }

    const char* tn = hlsl_type_name(type);
    std::string expr = std::string(tn ? tn : "float4") + "(" + get_code(a) + ", " + get_code(b) + ")";
    return add_chunk(expr, type, false);
}

// ============================================================================
// IShaderCompiler — Built-in inputs
// ============================================================================

int32_t HlslTranslator::texcoord(int32_t index) {
    char buf[64];
    snprintf(buf, sizeof(buf), "input.uv%d", index);
    return add_chunk(buf, ValueType::Float2, true);
}

int32_t HlslTranslator::time() {
    return add_chunk("g_Time", ValueType::Float1, true);
}

int32_t HlslTranslator::world_position() {
    return add_chunk("input.worldPos", ValueType::Float3, true);
}

int32_t HlslTranslator::world_normal() {
    return add_chunk("input.worldNormal", ValueType::Float3, true);
}

int32_t HlslTranslator::camera_position() {
    return add_chunk("g_CameraPos", ValueType::Float3, true);
}

// ============================================================================
// IShaderCompiler — Type promotion
// ============================================================================

int32_t HlslTranslator::promote(int32_t chunk, int32_t target_components) {
    auto src_type = get_type(chunk);
    int src_count = component_count(src_type);
    if (src_count == 0) src_count = 1;
    if (src_count == target_components) return chunk;

    ValueType target_type;
    switch (target_components) {
        case 1: target_type = ValueType::Float1; break;
        case 2: target_type = ValueType::Float2; break;
        case 3: target_type = ValueType::Float3; break;
        case 4: target_type = ValueType::Float4; break;
        default: return chunk;
    }

    // Scalar broadcast: float -> float3(x,x,x)
    if (src_count == 1) {
        const char* tn = hlsl_type_name(target_type);
        std::string code = get_code(chunk);
        std::string expr = std::string(tn) + "(";
        for (int i = 0; i < target_components; ++i) {
            if (i > 0) expr += ", ";
            expr += code;
        }
        expr += ")";
        return add_chunk(expr, target_type, true);
    }

    // Truncation: take first N components via swizzle.
    if (src_count > target_components) {
        const char swizzle[] = "xyzw";
        std::string mask(swizzle, target_components);
        return component_mask(chunk, mask.c_str());
    }

    // Extension: pad with zeros. float2 -> float3(v.xy, 0)
    const char* tn = hlsl_type_name(target_type);
    std::string expr = std::string(tn) + "(" + get_code(chunk);
    for (int i = src_count; i < target_components; ++i) {
        expr += ", 0.0";
    }
    expr += ")";
    return add_chunk(expr, target_type, true);
}

// ============================================================================
// IShaderCompiler — Parameters
// ============================================================================

int32_t HlslTranslator::scalar_param(const char* name, float default_val) {
    auto it = scalar_params_.find(name);
    if (it != scalar_params_.end()) return it->second.chunk;

    std::string expr = std::string("g_Material.") + name;
    int32_t idx = add_chunk(expr, ValueType::Float1, true);
    scalar_params_[name] = {default_val, idx};
    return idx;
}

int32_t HlslTranslator::vector_param(const char* name, const float* default_val, int32_t count) {
    auto it = vector_params_.find(name);
    if (it != vector_params_.end()) return it->second.chunk;

    ValueType type;
    switch (count) {
        case 1: type = ValueType::Float1; break;
        case 2: type = ValueType::Float2; break;
        case 3: type = ValueType::Float3; break;
        default: type = ValueType::Float4; count = 4; break;
    }

    std::string expr = std::string("g_Material.") + name;
    int32_t idx = add_chunk(expr, type, true);

    VectorParamInfo info{};
    for (int i = 0; i < count && i < 4; ++i)
        info.default_val[i] = default_val[i];
    info.count = count;
    info.chunk = idx;
    vector_params_[name] = info;
    return idx;
}

int32_t HlslTranslator::texture_param(const char* name) {
    auto it = texture_params_.find(name);
    if (it != texture_params_.end()) {
        return it->second;
    }

    // Texture declared as a global resource, referenced by name.
    std::string expr = std::string("g_Tex_") + name;
    int32_t idx = add_chunk(expr, ValueType::Texture2D, true);
    texture_params_[name] = idx;
    return idx;
}

// ============================================================================
// compile_input — recursive graph walk with caching + cycle detection
// ============================================================================

int32_t HlslTranslator::compile_input(uint32_t node_id, int32_t output_index) {
    // Cache key: (node_id << 32) | output_index
    uint64_t key = (static_cast<uint64_t>(node_id) << 32) | static_cast<uint32_t>(output_index);

    // Check cache.
    auto it = expr_cache_.find(key);
    if (it != expr_cache_.end()) return it->second;

    // Cycle detection.
    if (compilation_stack_.count(key)) {
        errors_.push_back("Cycle detected at node " + std::to_string(node_id));
        return constant(0.f);
    }
    compilation_stack_.insert(key);

    auto* node = graph_->get_node(node_id);
    if (!node) {
        errors_.push_back("Node not found: " + std::to_string(node_id));
        compilation_stack_.erase(key);
        return constant(0.f);
    }

    int32_t result = node->compile(*this, output_index);
    expr_cache_[key] = result;
    compilation_stack_.erase(key);
    return result;
}

// ============================================================================
// Pixel shader assembly
// ============================================================================

std::string HlslTranslator::assemble_pixel_shader(ShaderGraph& /*graph*/) {
    std::ostringstream ps;

    // Header.
    ps << "// Auto-generated pixel shader from UzEngine Shader Graph\n\n";

    // Sampler state.
    ps << "SamplerState g_Sampler : register(s0);\n\n";

    // Texture declarations.
    {
        int reg = 0;
        // Sort texture params for deterministic output.
        std::vector<std::string> tex_names;
        for (auto& [name, _] : texture_params_) tex_names.push_back(name);
        std::sort(tex_names.begin(), tex_names.end());
        for (auto& name : tex_names) {
            ps << "Texture2D g_Tex_" << name << " : register(t" << reg++ << ");\n";
        }
        if (!tex_names.empty()) ps << "\n";
    }

    // Material cbuffer.
    if (!scalar_params_.empty() || !vector_params_.empty()) {
        ps << "cbuffer MaterialParams : register(b1) {\n";
        ps << "    struct {\n";

        // Sort for deterministic output.
        std::vector<std::string> sp_names;
        for (auto& [name, _] : scalar_params_) sp_names.push_back(name);
        std::sort(sp_names.begin(), sp_names.end());
        for (auto& name : sp_names) {
            ps << "        float " << name << ";\n";
        }

        std::vector<std::string> vp_names;
        for (auto& [name, _] : vector_params_) vp_names.push_back(name);
        std::sort(vp_names.begin(), vp_names.end());
        for (auto& name : vp_names) {
            auto& info = vector_params_[name];
            const char* tn = hlsl_type_name(static_cast<ValueType>(1 << (info.count - 1)));
            ps << "        " << (tn ? tn : "float4") << " " << name << ";\n";
        }

        ps << "    } g_Material;\n";
        ps << "};\n\n";
    }

    // Global uniforms.
    ps << "cbuffer GlobalParams : register(b0) {\n";
    ps << "    float g_Time;\n";
    ps << "    float3 g_CameraPos;\n";
    ps << "};\n\n";

    // Input struct.
    ps << "struct PSInput {\n";
    ps << "    float4 position   : SV_POSITION;\n";
    ps << "    float3 worldPos   : TEXCOORD0;\n";
    ps << "    float3 worldNormal: TEXCOORD1;\n";
    ps << "    float2 uv0        : TEXCOORD2;\n";
    ps << "    float3 tangent    : TEXCOORD3;\n";
    ps << "    float3 bitangent  : TEXCOORD4;\n";
    ps << "};\n\n";

    // G-Buffer output struct.
    // RT0: Albedo.rgb + AO (RGBA8)
    // RT1: Normal.rgb (RGB10A2)
    // RT2: Metallic, Roughness, MaterialType, Unused (RGBA8)
    // RT3: Emission (R11G11B10F)
    ps << "struct PSOutput {\n";
    ps << "    float4 rt0 : SV_TARGET0; // Albedo.rgb, AO\n";
    ps << "    float4 rt1 : SV_TARGET1; // Normal.xyz, 0\n";
    ps << "    float4 rt2 : SV_TARGET2; // Metallic, Roughness, 0, 0\n";
    ps << "    float4 rt3 : SV_TARGET3; // Emission.rgb, 0\n";
    ps << "};\n\n";

    // Main function.
    ps << "PSOutput main(PSInput input) {\n";

    // Emit local variable declarations for non-inline chunks.
    for (auto& chunk : chunks_) {
        if (!chunk.is_inline && !chunk.var_name.empty()) {
            const char* tn = hlsl_type_name(chunk.type);
            ps << "    " << (tn ? tn : "float") << " " << chunk.var_name
               << " = " << chunk.expr << ";\n";
        }
    }
    if (!chunks_.empty()) ps << "\n";

    // Output assignment.
    ps << "    PSOutput output = (PSOutput)0;\n";

    // Pin names for documentation.
    static const char* pin_names[] = {
        "BaseColor", "Metallic", "Roughness", "Normal",
        "Emission", "AO", "Opacity", "Displacement"
    };

    // Defaults.
    static const char* defaults[] = {
        "float3(0.5, 0.5, 0.5)", // BaseColor: mid-gray
        "0.0",                     // Metallic
        "0.5",                     // Roughness
        "input.worldNormal",       // Normal: pass-through
        "float3(0.0, 0.0, 0.0)",  // Emission: none
        "1.0",                     // AO: fully lit
        "1.0",                     // Opacity: opaque
        "0.0",                     // Displacement: none
    };

    // Helper: get expression for a material output pin (or default).
    auto pin_expr = [&](int32_t pin_index) -> std::string {
        if (output_chunks_[pin_index] >= 0) {
            return get_code(output_chunks_[pin_index]);
        }
        return defaults[pin_index];
    };

    // RT0: Albedo.rgb + AO
    ps << "    output.rt0 = float4(" << pin_expr(0) << ", " << pin_expr(5) << "); // "
       << pin_names[0] << ", " << pin_names[5] << "\n";

    // RT1: Normal (world-space, encode to [0,1])
    ps << "    output.rt1 = float4(" << pin_expr(3) << " * 0.5 + 0.5, 0.0); // "
       << pin_names[3] << "\n";

    // RT2: Metallic, Roughness, 0, 0
    ps << "    output.rt2 = float4(" << pin_expr(1) << ", " << pin_expr(2)
       << ", 0.0, 0.0); // " << pin_names[1] << ", " << pin_names[2] << "\n";

    // RT3: Emission
    ps << "    output.rt3 = float4(" << pin_expr(4) << ", 0.0); // "
       << pin_names[4] << "\n";

    ps << "    return output;\n";
    ps << "}\n";

    return ps.str();
}

// ============================================================================
// Vertex shader assembly
// ============================================================================

std::string HlslTranslator::assemble_vertex_shader(ShaderGraph& /*graph*/) {
    std::ostringstream vs;

    vs << "// Auto-generated vertex shader from UzEngine Shader Graph\n\n";

    vs << "cbuffer TransformParams : register(b0) {\n";
    vs << "    float4x4 g_World;\n";
    vs << "    float4x4 g_ViewProj;\n";
    vs << "    float4x4 g_WorldInvTranspose;\n";
    vs << "    float g_Time;\n";
    vs << "    float3 g_CameraPos;\n";
    vs << "};\n\n";

    vs << "struct VSInput {\n";
    vs << "    float3 position : POSITION;\n";
    vs << "    float3 normal   : NORMAL;\n";
    vs << "    float2 uv0      : TEXCOORD0;\n";
    vs << "    float3 tangent  : TANGENT;\n";
    vs << "};\n\n";

    vs << "struct VSOutput {\n";
    vs << "    float4 position   : SV_POSITION;\n";
    vs << "    float3 worldPos   : TEXCOORD0;\n";
    vs << "    float3 worldNormal: TEXCOORD1;\n";
    vs << "    float2 uv0        : TEXCOORD2;\n";
    vs << "    float3 tangent    : TEXCOORD3;\n";
    vs << "    float3 bitangent  : TEXCOORD4;\n";
    vs << "};\n\n";

    vs << "VSOutput main(VSInput input) {\n";
    vs << "    VSOutput output;\n";
    vs << "    float4 worldPos = mul(g_World, float4(input.position, 1.0));\n";

    // If displacement pin is connected, add vertex displacement.
    if (output_chunks_[7] >= 0) {
        vs << "    // Vertex displacement (along normal)\n";
        vs << "    float3 wn = normalize(mul((float3x3)g_World, input.normal));\n";
        vs << "    worldPos.xyz += wn * 0.0; // TODO: pass displacement from PS to VS\n";
    }

    vs << "    output.position = mul(g_ViewProj, worldPos);\n";
    vs << "    output.worldPos = worldPos.xyz;\n";
    vs << "    output.worldNormal = normalize(mul((float3x3)g_WorldInvTranspose, input.normal));\n";
    vs << "    output.uv0 = input.uv0;\n";
    vs << "    output.tangent = normalize(mul((float3x3)g_World, input.tangent));\n";
    vs << "    output.bitangent = cross(output.worldNormal, output.tangent);\n";
    vs << "    return output;\n";
    vs << "}\n";

    return vs.str();
}

} // namespace uz::sg
