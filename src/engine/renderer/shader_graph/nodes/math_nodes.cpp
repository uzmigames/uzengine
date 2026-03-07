// math_nodes.cpp -- Math node implementations.

#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/node_registry.h"

namespace uz::sg {

// ============================================================================
// BinaryMathNode base
// ============================================================================

std::span<const PinDesc> BinaryMathNode::input_descs() const {
    static const PinDesc descs[] = {
        {"A", ValueType::Scalar, false},
        {"B", ValueType::Scalar, false},
    };
    return descs;
}

std::span<const PinDesc> BinaryMathNode::output_descs() const {
    static const PinDesc descs[] = {{"Result", ValueType::Scalar, false}};
    return descs;
}

// ============================================================================
// Concrete math nodes
// ============================================================================

// Helper: compile both inputs, handling unconnected (default to 0).
static int32_t compile_binary_inputs(IShaderCompiler& c, ShaderNode& node,
                                      int32_t& a, int32_t& b) {
    auto& inp_a = node.get_input(0);
    auto& inp_b = node.get_input(1);
    a = (inp_a.source_node_id != 0)
        ? c.compile_input(inp_a.source_node_id, inp_a.source_output_index)
        : c.constant(inp_a.default_value[0]);
    b = (inp_b.source_node_id != 0)
        ? c.compile_input(inp_b.source_node_id, inp_b.source_output_index)
        : c.constant(inp_b.default_value[0]);
    return 0;
}

int32_t AddNode::compile(IShaderCompiler& c, int32_t) {
    int32_t a, b; compile_binary_inputs(c, *this, a, b);
    return c.add(a, b);
}
std::unique_ptr<ShaderNode> AddNode::clone() const {
    auto n = std::make_unique<AddNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

int32_t SubtractNode::compile(IShaderCompiler& c, int32_t) {
    int32_t a, b; compile_binary_inputs(c, *this, a, b);
    return c.subtract(a, b);
}
std::unique_ptr<ShaderNode> SubtractNode::clone() const {
    auto n = std::make_unique<SubtractNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

int32_t MultiplyNode::compile(IShaderCompiler& c, int32_t) {
    int32_t a, b; compile_binary_inputs(c, *this, a, b);
    return c.multiply(a, b);
}
std::unique_ptr<ShaderNode> MultiplyNode::clone() const {
    auto n = std::make_unique<MultiplyNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

int32_t DivideNode::compile(IShaderCompiler& c, int32_t) {
    int32_t a, b; compile_binary_inputs(c, *this, a, b);
    return c.divide(a, b);
}
std::unique_ptr<ShaderNode> DivideNode::clone() const {
    auto n = std::make_unique<DivideNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

int32_t PowerNode::compile(IShaderCompiler& c, int32_t) {
    int32_t a, b; compile_binary_inputs(c, *this, a, b);
    return c.power(a, b);
}
std::unique_ptr<ShaderNode> PowerNode::clone() const {
    auto n = std::make_unique<PowerNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

// ============================================================================
// LerpNode
// ============================================================================

std::span<const PinDesc> LerpNode::input_descs() const {
    static const PinDesc descs[] = {
        {"A",     ValueType::Scalar, false},
        {"B",     ValueType::Scalar, false},
        {"Alpha", ValueType::Float1, false},
    };
    return descs;
}

std::span<const PinDesc> LerpNode::output_descs() const {
    static const PinDesc descs[] = {{"Result", ValueType::Scalar, false}};
    return descs;
}

int32_t LerpNode::compile(IShaderCompiler& c, int32_t) {
    auto& ia = get_input(0);
    auto& ib = get_input(1);
    auto& ialpha = get_input(2);
    int32_t a = (ia.source_node_id != 0)
        ? c.compile_input(ia.source_node_id, ia.source_output_index)
        : c.constant(ia.default_value[0]);
    int32_t b = (ib.source_node_id != 0)
        ? c.compile_input(ib.source_node_id, ib.source_output_index)
        : c.constant(ib.default_value[0]);
    int32_t alpha = (ialpha.source_node_id != 0)
        ? c.compile_input(ialpha.source_node_id, ialpha.source_output_index)
        : c.constant(ialpha.default_value[0]);
    return c.lerp(a, b, alpha);
}

std::unique_ptr<ShaderNode> LerpNode::clone() const {
    auto n = std::make_unique<LerpNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

// ============================================================================
// DotNode (output is always Float1)
// ============================================================================

std::span<const PinDesc> DotNode::output_descs() const {
    static const PinDesc descs[] = {{"Result", ValueType::Float1, false}};
    return descs;
}

int32_t DotNode::compile(IShaderCompiler& c, int32_t) {
    int32_t a, b; compile_binary_inputs(c, *this, a, b);
    return c.dot(a, b);
}

std::unique_ptr<ShaderNode> DotNode::clone() const {
    auto n = std::make_unique<DotNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

// ============================================================================
// NormalizeNode
// ============================================================================

std::span<const PinDesc> NormalizeNode::input_descs() const {
    static const PinDesc descs[] = {{"V", ValueType::Scalar, true}};
    return descs;
}

std::span<const PinDesc> NormalizeNode::output_descs() const {
    static const PinDesc descs[] = {{"Result", ValueType::Scalar, false}};
    return descs;
}

int32_t NormalizeNode::compile(IShaderCompiler& c, int32_t) {
    auto& inp = get_input(0);
    int32_t v = (inp.source_node_id != 0)
        ? c.compile_input(inp.source_node_id, inp.source_output_index)
        : c.constant(0.f);
    return c.normalize(v);
}

std::unique_ptr<ShaderNode> NormalizeNode::clone() const {
    auto n = std::make_unique<NormalizeNode>();
    n->id = id; n->type_name = type_name; n->pos_x = pos_x; n->pos_y = pos_y;
    n->inputs_ = inputs_;
    return n;
}

// ============================================================================
// Registration
// ============================================================================

UZ_REGISTER_SHADER_NODE(AddNode,       "Add",       NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(SubtractNode,  "Subtract",  NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(MultiplyNode,  "Multiply",  NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(DivideNode,    "Divide",    NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(LerpNode,      "Lerp",      NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(DotNode,       "Dot",       NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(NormalizeNode, "Normalize", NodeCategory::Math);
UZ_REGISTER_SHADER_NODE(PowerNode,     "Power",     NodeCategory::Math);

} // namespace uz::sg
