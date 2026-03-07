// texture_nodes.cpp -- TextureSampleNode implementation.

#include "engine/renderer/shader_graph/nodes/texture_nodes.h"
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/node_registry.h"

namespace uz::sg {

std::span<const PinDesc> TextureSampleNode::input_descs() const {
    static const PinDesc descs[] = {
        {"Tex", ValueType::Texture2D, true},
        {"UV",  ValueType::Scalar,    false},
    };
    return descs;
}

std::span<const PinDesc> TextureSampleNode::output_descs() const {
    static const PinDesc descs[] = {
        {"RGB", ValueType::Float3, false},
        {"R",   ValueType::Float1, false},
        {"G",   ValueType::Float1, false},
        {"B",   ValueType::Float1, false},
        {"A",   ValueType::Float1, false},
    };
    return descs;
}

int32_t TextureSampleNode::compile(IShaderCompiler& c, int32_t output_index) {
    auto& tex_inp = get_input(0);
    auto& uv_inp = get_input(1);

    int32_t tex = (tex_inp.source_node_id != 0)
        ? c.compile_input(tex_inp.source_node_id, tex_inp.source_output_index)
        : -1;

    int32_t uv = (uv_inp.source_node_id != 0)
        ? c.compile_input(uv_inp.source_node_id, uv_inp.source_output_index)
        : c.texcoord(0);  // Default to UV0.

    if (tex < 0) return c.constant(0.f);  // No texture connected.

    int32_t sample = c.texture_sample(tex, uv);

    // Route to the correct output channel.
    switch (output_index) {
        case 0: return c.component_mask(sample, "xyz");  // RGB
        case 1: return c.component_mask(sample, "x");    // R
        case 2: return c.component_mask(sample, "y");    // G
        case 3: return c.component_mask(sample, "z");    // B
        case 4: return c.component_mask(sample, "w");    // A
        default: return c.component_mask(sample, "xyz");
    }
}

std::unique_ptr<ShaderNode> TextureSampleNode::clone() const {
    auto n = std::make_unique<TextureSampleNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

UZ_REGISTER_SHADER_NODE(TextureSampleNode, "TextureSample", NodeCategory::Texture);

} // namespace uz::sg
