// shader_node.h -- Base class for all shader graph nodes.
//
// Implements the visitor-compiler pattern: nodes call typed methods on
// IShaderCompiler, never emit raw HLSL text directly.

#pragma once

#include "engine/renderer/shader_graph/shader_graph_types.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace uz::sg {

// Forward declarations.
class IShaderCompiler;

// Pin descriptor — static metadata for an input or output pin.
struct PinDesc {
    std::string name;
    ValueType   type         = ValueType::None;
    bool        is_required  = false;
};

// Input connection state — mutable, one per input pin.
struct NodeInput {
    uint32_t source_node_id      = 0;   // 0 = unconnected
    int32_t  source_output_index = -1;
    float    default_value[4]    = {0.f, 0.f, 0.f, 0.f};
};

// Base class for all shader graph nodes.
class ShaderNode {
public:
    uint32_t    id        = 0;
    std::string type_name;

    // Layout position (stored in .uzshader for future visual editor).
    float pos_x = 0.f;
    float pos_y = 0.f;

    virtual ~ShaderNode() = default;

    // Pin descriptors (static per node type).
    virtual std::span<const PinDesc> input_descs() const = 0;
    virtual std::span<const PinDesc> output_descs() const = 0;

    // Input connection access.
    virtual NodeInput& get_input(int32_t index) = 0;
    virtual const NodeInput& get_input(int32_t index) const = 0;
    virtual int32_t input_count() const = 0;

    // Compilation — visitor-compiler pattern.
    // Returns a code chunk index into the compiler's IR.
    virtual int32_t compile(IShaderCompiler& compiler, int32_t output_index) = 0;

    // Serialization — subclasses override to persist custom params.
    virtual void serialize(nlohmann::json& j) const;
    virtual void deserialize(const nlohmann::json& j);

    // Factory clone for snapshot/rollback support.
    virtual std::unique_ptr<ShaderNode> clone() const = 0;
};

// Convenience base that manages a fixed array of inputs.
// Most nodes have a small, known number of inputs.
template <int32_t N>
class ShaderNodeFixed : public ShaderNode {
public:
    NodeInput& get_input(int32_t index) override { return inputs_[index]; }
    const NodeInput& get_input(int32_t index) const override { return inputs_[index]; }
    int32_t input_count() const override { return N; }

protected:
    std::array<NodeInput, N> inputs_{};
};

// Specialization for zero-input nodes.
template <>
class ShaderNodeFixed<0> : public ShaderNode {
public:
    NodeInput& get_input(int32_t /*index*/) override {
        static NodeInput dummy;
        return dummy;
    }
    const NodeInput& get_input(int32_t /*index*/) const override {
        static const NodeInput dummy;
        return dummy;
    }
    int32_t input_count() const override { return 0; }
};

} // namespace uz::sg
