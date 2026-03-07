// param_nodes.h -- Parameter nodes (exposed as material parameters).

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"
#include <nlohmann/json_fwd.hpp>

namespace uz::sg {

class ScalarParamNode : public ShaderNodeFixed<0> {
public:
    std::string param_name = "Scalar";
    float default_val = 0.f;
    ScalarParamNode() { type_name = "ScalarParam"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class VectorParamNode : public ShaderNodeFixed<0> {
public:
    std::string param_name = "Vector";
    float default_val[4] = {0.f, 0.f, 0.f, 1.f};
    VectorParamNode() { type_name = "VectorParam"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class TextureParamNode : public ShaderNodeFixed<0> {
public:
    std::string param_name = "Texture";
    TextureParamNode() { type_name = "TextureParam"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

} // namespace uz::sg
