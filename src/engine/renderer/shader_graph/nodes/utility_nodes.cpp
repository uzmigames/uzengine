// utility_nodes.cpp -- Append, ComponentMask, Time, UV implementations.

#include "engine/renderer/shader_graph/nodes/utility_nodes.h"
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/node_registry.h"
#include <nlohmann/json.hpp>

namespace uz::sg {

// ============================================================================
// AppendNode
// ============================================================================

std::span<const PinDesc> AppendNode::input_descs() const {
    static const PinDesc descs[] = {
        {"A", ValueType::Scalar, false},
        {"B", ValueType::Scalar, false},
    };
    return descs;
}

std::span<const PinDesc> AppendNode::output_descs() const {
    static const PinDesc descs[] = {{"Result", ValueType::Scalar, false}};
    return descs;
}

int32_t AppendNode::compile(IShaderCompiler& c, int32_t) {
    auto& ia = get_input(0);
    auto& ib = get_input(1);
    int32_t a = (ia.source_node_id != 0)
        ? c.compile_input(ia.source_node_id, ia.source_output_index)
        : c.constant(ia.default_value[0]);
    int32_t b = (ib.source_node_id != 0)
        ? c.compile_input(ib.source_node_id, ib.source_output_index)
        : c.constant(ib.default_value[0]);
    return c.append(a, b);
}

std::unique_ptr<ShaderNode> AppendNode::clone() const {
    auto n = std::make_unique<AppendNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

// ============================================================================
// ComponentMaskNode
// ============================================================================

std::span<const PinDesc> ComponentMaskNode::input_descs() const {
    static const PinDesc descs[] = {{"V", ValueType::Scalar, true}};
    return descs;
}

std::span<const PinDesc> ComponentMaskNode::output_descs() const {
    static const PinDesc descs[] = {{"Result", ValueType::Scalar, false}};
    return descs;
}

int32_t ComponentMaskNode::compile(IShaderCompiler& c, int32_t) {
    auto& inp = get_input(0);
    int32_t v = (inp.source_node_id != 0)
        ? c.compile_input(inp.source_node_id, inp.source_output_index)
        : c.constant(0.f);
    return c.component_mask(v, mask.c_str());
}

void ComponentMaskNode::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["mask"] = mask;
}

void ComponentMaskNode::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params") && j["params"].contains("mask"))
        mask = j["params"]["mask"].get<std::string>();
}

std::unique_ptr<ShaderNode> ComponentMaskNode::clone() const {
    auto n = std::make_unique<ComponentMaskNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_; n->mask = mask;
    return n;
}

// ============================================================================
// TimeNode
// ============================================================================

std::span<const PinDesc> TimeNode::input_descs() const { return {}; }
std::span<const PinDesc> TimeNode::output_descs() const {
    static const PinDesc descs[] = {{"Time", ValueType::Float1, false}};
    return descs;
}
int32_t TimeNode::compile(IShaderCompiler& c, int32_t) { return c.time(); }
std::unique_ptr<ShaderNode> TimeNode::clone() const {
    auto n = std::make_unique<TimeNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    return n;
}

// ============================================================================
// UVNode
// ============================================================================

std::span<const PinDesc> UVNode::input_descs() const { return {}; }
std::span<const PinDesc> UVNode::output_descs() const {
    static const PinDesc descs[] = {{"UV", ValueType::Float2, false}};
    return descs;
}
int32_t UVNode::compile(IShaderCompiler& c, int32_t) { return c.texcoord(uv_channel); }
void UVNode::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["uv_channel"] = uv_channel;
}
void UVNode::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params") && j["params"].contains("uv_channel"))
        uv_channel = j["params"]["uv_channel"].get<int32_t>();
}
std::unique_ptr<ShaderNode> UVNode::clone() const {
    auto n = std::make_unique<UVNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->uv_channel = uv_channel;
    return n;
}

// ============================================================================
// Registration
// ============================================================================

UZ_REGISTER_SHADER_NODE(AppendNode,        "Append",        NodeCategory::Utility);
UZ_REGISTER_SHADER_NODE(ComponentMaskNode, "ComponentMask", NodeCategory::Utility);
UZ_REGISTER_SHADER_NODE(TimeNode,          "Time",          NodeCategory::Utility);
UZ_REGISTER_SHADER_NODE(UVNode,            "UV",            NodeCategory::Utility);

} // namespace uz::sg
