// math_nodes.h -- Math operation nodes (Add, Subtract, Multiply, Divide, Lerp, Dot, Normalize, Power).

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"

namespace uz::sg {

// Binary math op base: 2 Scalar inputs, 1 Scalar output.
class BinaryMathNode : public ShaderNodeFixed<2> {
public:
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
};

class AddNode : public BinaryMathNode {
public:
    AddNode() { type_name = "Add"; }
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class SubtractNode : public BinaryMathNode {
public:
    SubtractNode() { type_name = "Subtract"; }
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class MultiplyNode : public BinaryMathNode {
public:
    MultiplyNode() { type_name = "Multiply"; }
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

class DivideNode : public BinaryMathNode {
public:
    DivideNode() { type_name = "Divide"; }
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// Lerp: 3 inputs (A, B, Alpha), 1 output.
class LerpNode : public ShaderNodeFixed<3> {
public:
    LerpNode() { type_name = "Lerp"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// Dot: 2 Scalar inputs, 1 Float1 output.
class DotNode : public BinaryMathNode {
public:
    DotNode() { type_name = "Dot"; }
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// Normalize: 1 Scalar input, 1 Scalar output.
class NormalizeNode : public ShaderNodeFixed<1> {
public:
    NormalizeNode() { type_name = "Normalize"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

// Power: 2 inputs (Base, Exp), 1 output.
class PowerNode : public BinaryMathNode {
public:
    PowerNode() { type_name = "Power"; }
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

} // namespace uz::sg
