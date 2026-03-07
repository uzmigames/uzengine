// param_nodes.cpp -- ScalarParam, VectorParam, TextureParam implementations.

#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/node_registry.h"
#include <nlohmann/json.hpp>

namespace uz::sg {

// ============================================================================
// ScalarParamNode
// ============================================================================

std::span<const PinDesc> ScalarParamNode::input_descs() const { return {}; }
std::span<const PinDesc> ScalarParamNode::output_descs() const {
    static const PinDesc descs[] = {{"Value", ValueType::Float1, false}};
    return descs;
}
int32_t ScalarParamNode::compile(IShaderCompiler& c, int32_t) {
    return c.scalar_param(param_name.c_str(), default_val);
}
void ScalarParamNode::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["param_name"] = param_name;
    j["params"]["default"] = default_val;
}
void ScalarParamNode::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params")) {
        auto& p = j["params"];
        if (p.contains("param_name")) param_name = p["param_name"].get<std::string>();
        if (p.contains("default")) default_val = p["default"].get<float>();
    }
}
std::unique_ptr<ShaderNode> ScalarParamNode::clone() const {
    auto n = std::make_unique<ScalarParamNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->param_name = param_name; n->default_val = default_val;
    return n;
}

UZ_REGISTER_SHADER_NODE(ScalarParamNode, "ScalarParam", NodeCategory::Parameter);

// ============================================================================
// VectorParamNode
// ============================================================================

std::span<const PinDesc> VectorParamNode::input_descs() const { return {}; }
std::span<const PinDesc> VectorParamNode::output_descs() const {
    static const PinDesc descs[] = {{"Value", ValueType::Float4, false}};
    return descs;
}
int32_t VectorParamNode::compile(IShaderCompiler& c, int32_t) {
    return c.vector_param(param_name.c_str(), default_val, 4);
}
void VectorParamNode::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["param_name"] = param_name;
    j["params"]["default"] = {default_val[0], default_val[1], default_val[2], default_val[3]};
}
void VectorParamNode::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params")) {
        auto& p = j["params"];
        if (p.contains("param_name")) param_name = p["param_name"].get<std::string>();
        if (p.contains("default")) {
            auto& v = p["default"];
            for (int i = 0; i < 4 && i < static_cast<int>(v.size()); ++i)
                default_val[i] = v[i].get<float>();
        }
    }
}
std::unique_ptr<ShaderNode> VectorParamNode::clone() const {
    auto n = std::make_unique<VectorParamNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->param_name = param_name;
    std::copy(std::begin(default_val), std::end(default_val), std::begin(n->default_val));
    return n;
}

UZ_REGISTER_SHADER_NODE(VectorParamNode, "VectorParam", NodeCategory::Parameter);

// ============================================================================
// TextureParamNode
// ============================================================================

std::span<const PinDesc> TextureParamNode::input_descs() const { return {}; }
std::span<const PinDesc> TextureParamNode::output_descs() const {
    static const PinDesc descs[] = {{"Tex", ValueType::Texture2D, false}};
    return descs;
}
int32_t TextureParamNode::compile(IShaderCompiler& c, int32_t) {
    return c.texture_param(param_name.c_str());
}
void TextureParamNode::serialize(nlohmann::json& j) const {
    ShaderNode::serialize(j);
    j["params"]["param_name"] = param_name;
}
void TextureParamNode::deserialize(const nlohmann::json& j) {
    ShaderNode::deserialize(j);
    if (j.contains("params") && j["params"].contains("param_name"))
        param_name = j["params"]["param_name"].get<std::string>();
}
std::unique_ptr<ShaderNode> TextureParamNode::clone() const {
    auto n = std::make_unique<TextureParamNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->param_name = param_name;
    return n;
}

UZ_REGISTER_SHADER_NODE(TextureParamNode, "TextureParam", NodeCategory::Parameter);

} // namespace uz::sg
