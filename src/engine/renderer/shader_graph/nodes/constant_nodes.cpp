// constant_nodes.cpp -- Constant1/3/4 node implementations.

#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/node_registry.h"
#include <nlohmann/json.hpp>

namespace uz::sg {

// ============================================================================
// Constant1Node
// ============================================================================

std::span<const PinDesc> Constant1Node::input_descs() const { return {}; }
std::span<const PinDesc> Constant1Node::output_descs() const {
    static const PinDesc descs[] = {{"Value", ValueType::Float1, false}};
    return descs;
}
int32_t Constant1Node::compile(IShaderCompiler& c, int32_t) { return c.constant(value); }
void Constant1Node::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["value"] = value;
}
void Constant1Node::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params") && j["params"].contains("value"))
        value = j["params"]["value"].get<float>();
}
std::unique_ptr<ShaderNode> Constant1Node::clone() const {
    auto n = std::make_unique<Constant1Node>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->value = value;
    return n;
}

UZ_REGISTER_SHADER_NODE(Constant1Node, "Constant1", NodeCategory::Constant);

// ============================================================================
// Constant3Node
// ============================================================================

std::span<const PinDesc> Constant3Node::input_descs() const { return {}; }
std::span<const PinDesc> Constant3Node::output_descs() const {
    static const PinDesc descs[] = {{"Value", ValueType::Float3, false}};
    return descs;
}
int32_t Constant3Node::compile(IShaderCompiler& c, int32_t) {
    return c.constant_vec(value, 3);
}
void Constant3Node::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["value"] = {value[0], value[1], value[2]};
}
void Constant3Node::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params") && j["params"].contains("value")) {
        auto& v = j["params"]["value"];
        for (int i = 0; i < 3 && i < static_cast<int>(v.size()); ++i)
            value[i] = v[i].get<float>();
    }
}
std::unique_ptr<ShaderNode> Constant3Node::clone() const {
    auto n = std::make_unique<Constant3Node>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    std::copy(std::begin(value), std::end(value), std::begin(n->value));
    return n;
}

UZ_REGISTER_SHADER_NODE(Constant3Node, "Constant3", NodeCategory::Constant);

// ============================================================================
// Constant4Node
// ============================================================================

std::span<const PinDesc> Constant4Node::input_descs() const { return {}; }
std::span<const PinDesc> Constant4Node::output_descs() const {
    static const PinDesc descs[] = {{"Value", ValueType::Float4, false}};
    return descs;
}
int32_t Constant4Node::compile(IShaderCompiler& c, int32_t) {
    return c.constant_vec(value, 4);
}
void Constant4Node::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["value"] = {value[0], value[1], value[2], value[3]};
}
void Constant4Node::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params") && j["params"].contains("value")) {
        auto& v = j["params"]["value"];
        for (int i = 0; i < 4 && i < static_cast<int>(v.size()); ++i)
            value[i] = v[i].get<float>();
    }
}
std::unique_ptr<ShaderNode> Constant4Node::clone() const {
    auto n = std::make_unique<Constant4Node>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    std::copy(std::begin(value), std::end(value), std::begin(n->value));
    return n;
}

UZ_REGISTER_SHADER_NODE(Constant4Node, "Constant4", NodeCategory::Constant);

} // namespace uz::sg
