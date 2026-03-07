// output_node.cpp -- MaterialOutputNode implementation.

#include "engine/renderer/shader_graph/nodes/output_node.h"
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/node_registry.h"

namespace uz::sg {

std::span<const PinDesc> MaterialOutputNode::input_descs() const {
    static const PinDesc descs[] = {
        {"BaseColor",    ValueType::Scalar, false},
        {"Metallic",     ValueType::Scalar, false},
        {"Roughness",    ValueType::Scalar, false},
        {"Normal",       ValueType::Scalar, false},
        {"Emission",     ValueType::Scalar, false},
        {"AO",           ValueType::Scalar, false},
        {"Opacity",      ValueType::Scalar, false},
        {"Displacement", ValueType::Scalar, false},
    };
    return descs;
}

std::span<const PinDesc> MaterialOutputNode::output_descs() const {
    return {};  // Terminal node — no outputs.
}

int32_t MaterialOutputNode::compile(IShaderCompiler& /*compiler*/, int32_t /*output_index*/) {
    // MaterialOutput is compiled by the translator directly (it reads each input).
    return -1;
}

std::unique_ptr<ShaderNode> MaterialOutputNode::clone() const {
    auto n = std::make_unique<MaterialOutputNode>();
    n->id = id; n->type_name = type_name;
    n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

UZ_REGISTER_SHADER_NODE(MaterialOutputNode, "MaterialOutput", NodeCategory::Output);

} // namespace uz::sg
