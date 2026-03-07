// constant_nodes.h -- Constant value nodes (Float1, Float3, Float4).

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"
#include <nlohmann/json_fwd.hpp>

namespace uz::sg {

class Constant1Node : public ShaderNodeFixed<0> {
public:
    float value = 0.f;
    Constant1Node() { type_name = "Constant1"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class Constant3Node : public ShaderNodeFixed<0> {
public:
    float value[3] = {0.f, 0.f, 0.f};
    Constant3Node() { type_name = "Constant3"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class Constant4Node : public ShaderNodeFixed<0> {
public:
    float value[4] = {0.f, 0.f, 0.f, 0.f};
    Constant4Node() { type_name = "Constant4"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

} // namespace uz::sg
