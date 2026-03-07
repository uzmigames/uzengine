// utility_nodes.h -- Utility nodes (Append, ComponentMask, Time, UV).

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"
#include <nlohmann/json_fwd.hpp>

namespace uz::sg {

// Append: concatenates two vectors (Float1+Float2->Float3, etc.).
class AppendNode : public ShaderNodeFixed<2> {
public:
    AppendNode() { type_name = "Append"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// ComponentMask: extracts channels by mask string (e.g., "xy", "z", "xw").
class ComponentMaskNode : public ShaderNodeFixed<1> {
public:
    std::string mask = "xyz";
    ComponentMaskNode() { type_name = "ComponentMask"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// Time: outputs the current frame time.
class TimeNode : public ShaderNodeFixed<0> {
public:
    TimeNode() { type_name = "Time"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// UV: outputs texture coordinates for a given UV channel.
class UVNode : public ShaderNodeFixed<0> {
public:
    int32_t uv_channel = 0;
    UVNode() { type_name = "UV"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    void serialize(nlohmann::json& j) const override;
    void deserialize(const nlohmann::json& j) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

} // namespace uz::sg
