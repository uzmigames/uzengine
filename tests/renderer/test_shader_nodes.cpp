// test_shader_nodes.cpp -- Google Test coverage for all shader graph node types.
//
// Covers:
//   - Pin descriptors (count, names, types) for each node
//   - input_count()
//   - compile() with mock compiler
//   - Serialization round-trip for nodes with custom params
//   - Default values when unconnected
//   - Clone produces equivalent node

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"
#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/nodes/texture_nodes.h"
#include "engine/renderer/shader_graph/nodes/utility_nodes.h"
#include <nlohmann/json.hpp>

using namespace uz::sg;

// ============================================================================
// Mock compiler that records calls and returns sequential chunk IDs.
// ============================================================================

class MockCompiler : public IShaderCompiler {
public:
    int32_t next_chunk = 100;
    std::vector<std::string> calls;

    int32_t alloc() { return next_chunk++; }

    int32_t constant(float v) override { calls.push_back("constant"); return alloc(); }
    int32_t constant_vec(const float* v, int32_t count) override { calls.push_back("constant_vec"); return alloc(); }
    int32_t add(int32_t a, int32_t b) override { calls.push_back("add"); return alloc(); }
    int32_t subtract(int32_t a, int32_t b) override { calls.push_back("subtract"); return alloc(); }
    int32_t multiply(int32_t a, int32_t b) override { calls.push_back("multiply"); return alloc(); }
    int32_t divide(int32_t a, int32_t b) override { calls.push_back("divide"); return alloc(); }
    int32_t lerp(int32_t a, int32_t b, int32_t alpha) override { calls.push_back("lerp"); return alloc(); }
    int32_t dot(int32_t a, int32_t b) override { calls.push_back("dot"); return alloc(); }
    int32_t normalize(int32_t v) override { calls.push_back("normalize"); return alloc(); }
    int32_t power(int32_t base, int32_t exp) override { calls.push_back("power"); return alloc(); }
    int32_t texture_sample(int32_t tex, int32_t uv) override { calls.push_back("texture_sample"); return alloc(); }
    int32_t component_mask(int32_t v, const char* mask) override { calls.push_back(std::string("component_mask:") + mask); return alloc(); }
    int32_t append(int32_t a, int32_t b) override { calls.push_back("append"); return alloc(); }
    int32_t texcoord(int32_t index) override { calls.push_back("texcoord"); return alloc(); }
    int32_t time() override { calls.push_back("time"); return alloc(); }
    int32_t world_position() override { calls.push_back("world_position"); return alloc(); }
    int32_t world_normal() override { calls.push_back("world_normal"); return alloc(); }
    int32_t camera_position() override { calls.push_back("camera_position"); return alloc(); }
    int32_t promote(int32_t chunk, int32_t target) override { calls.push_back("promote"); return alloc(); }
    int32_t scalar_param(const char* name, float def) override { calls.push_back(std::string("scalar_param:") + name); return alloc(); }
    int32_t vector_param(const char* name, const float* def, int32_t count) override { calls.push_back(std::string("vector_param:") + name); return alloc(); }
    int32_t texture_param(const char* name) override { calls.push_back(std::string("texture_param:") + name); return alloc(); }
    int32_t compile_input(uint32_t node_id, int32_t output_index) override { calls.push_back("compile_input"); return alloc(); }
};

// ============================================================================
// MaterialOutputNode
// ============================================================================

TEST(ShaderNodesTest, MaterialOutput_Pins) {
    MaterialOutputNode node;
    EXPECT_EQ(node.input_descs().size(), 8u);
    EXPECT_EQ(node.output_descs().size(), 0u);
    EXPECT_EQ(node.input_count(), 8);

    auto inputs = node.input_descs();
    EXPECT_EQ(inputs[0].name, "BaseColor");
    EXPECT_EQ(inputs[0].type, ValueType::Scalar);
    EXPECT_EQ(inputs[1].name, "Metallic");
    EXPECT_EQ(inputs[1].type, ValueType::Scalar);
    EXPECT_EQ(inputs[7].name, "Displacement");
}

TEST(ShaderNodesTest, MaterialOutput_Compile) {
    MaterialOutputNode node;
    MockCompiler mc;
    EXPECT_EQ(node.compile(mc, 0), -1);  // Terminal node.
}

// ============================================================================
// Constant nodes
// ============================================================================

TEST(ShaderNodesTest, Constant1_PinsAndCompile) {
    Constant1Node node;
    node.value = 3.14f;
    EXPECT_EQ(node.input_count(), 0);
    EXPECT_EQ(node.output_descs().size(), 1u);
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float1);

    MockCompiler mc;
    int32_t chunk = node.compile(mc, 0);
    EXPECT_GE(chunk, 100);
    EXPECT_EQ(mc.calls.back(), "constant");
}

TEST(ShaderNodesTest, Constant1_Serialization) {
    Constant1Node node;
    node.value = 2.71f;
    nlohmann::json j;
    node.serialize(j);
    EXPECT_FLOAT_EQ(j["params"]["value"].get<float>(), 2.71f);

    Constant1Node restored;
    restored.deserialize(j);
    EXPECT_FLOAT_EQ(restored.value, 2.71f);
}

TEST(ShaderNodesTest, Constant3_PinsAndCompile) {
    Constant3Node node;
    node.value[0] = 1.f; node.value[1] = 2.f; node.value[2] = 3.f;
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float3);

    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "constant_vec");
}

TEST(ShaderNodesTest, Constant3_Serialization) {
    Constant3Node node;
    node.value[0] = 1.f; node.value[1] = 0.5f; node.value[2] = 0.f;
    nlohmann::json j;
    node.serialize(j);

    Constant3Node restored;
    restored.deserialize(j);
    EXPECT_FLOAT_EQ(restored.value[0], 1.f);
    EXPECT_FLOAT_EQ(restored.value[1], 0.5f);
    EXPECT_FLOAT_EQ(restored.value[2], 0.f);
}

TEST(ShaderNodesTest, Constant4_PinsAndCompile) {
    Constant4Node node;
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float4);
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "constant_vec");
}

TEST(ShaderNodesTest, Constant4_Serialization) {
    Constant4Node node;
    node.value[0] = 1.f; node.value[3] = 0.8f;
    nlohmann::json j;
    node.serialize(j);
    Constant4Node restored;
    restored.deserialize(j);
    EXPECT_FLOAT_EQ(restored.value[0], 1.f);
    EXPECT_FLOAT_EQ(restored.value[3], 0.8f);
}

// ============================================================================
// Parameter nodes
// ============================================================================

TEST(ShaderNodesTest, ScalarParam_PinsAndCompile) {
    ScalarParamNode node;
    node.param_name = "Roughness";
    node.default_val = 0.5f;
    EXPECT_EQ(node.input_count(), 0);
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float1);

    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "scalar_param:Roughness");
}

TEST(ShaderNodesTest, ScalarParam_Serialization) {
    ScalarParamNode node;
    node.param_name = "Metallic";
    node.default_val = 0.3f;
    nlohmann::json j;
    node.serialize(j);

    ScalarParamNode restored;
    restored.deserialize(j);
    EXPECT_EQ(restored.param_name, "Metallic");
    EXPECT_FLOAT_EQ(restored.default_val, 0.3f);
}

TEST(ShaderNodesTest, VectorParam_PinsAndCompile) {
    VectorParamNode node;
    node.param_name = "BaseColor";
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float4);

    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "vector_param:BaseColor");
}

TEST(ShaderNodesTest, VectorParam_Serialization) {
    VectorParamNode node;
    node.param_name = "Tint";
    node.default_val[0] = 1.f; node.default_val[1] = 0.5f;
    nlohmann::json j;
    node.serialize(j);

    VectorParamNode restored;
    restored.deserialize(j);
    EXPECT_EQ(restored.param_name, "Tint");
    EXPECT_FLOAT_EQ(restored.default_val[0], 1.f);
    EXPECT_FLOAT_EQ(restored.default_val[1], 0.5f);
}

TEST(ShaderNodesTest, TextureParam_PinsAndCompile) {
    TextureParamNode node;
    node.param_name = "AlbedoTex";
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Texture2D);

    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "texture_param:AlbedoTex");
}

TEST(ShaderNodesTest, TextureParam_Serialization) {
    TextureParamNode node;
    node.param_name = "NormalMap";
    nlohmann::json j;
    node.serialize(j);

    TextureParamNode restored;
    restored.deserialize(j);
    EXPECT_EQ(restored.param_name, "NormalMap");
}

// ============================================================================
// Math nodes
// ============================================================================

TEST(ShaderNodesTest, AddNode_Pins) {
    AddNode node;
    EXPECT_EQ(node.input_descs().size(), 2u);
    EXPECT_EQ(node.output_descs().size(), 1u);
    EXPECT_EQ(node.input_descs()[0].type, ValueType::Scalar);
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Scalar);
}

TEST(ShaderNodesTest, AddNode_CompileUnconnected) {
    AddNode node;
    MockCompiler mc;
    int32_t result = node.compile(mc, 0);
    EXPECT_GE(result, 100);
    // Should compile default constants for both inputs, then add.
    EXPECT_EQ(mc.calls.size(), 3u);  // constant, constant, add
    EXPECT_EQ(mc.calls[2], "add");
}

TEST(ShaderNodesTest, SubtractNode_Compile) {
    SubtractNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "subtract");
}

TEST(ShaderNodesTest, MultiplyNode_Compile) {
    MultiplyNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "multiply");
}

TEST(ShaderNodesTest, DivideNode_Compile) {
    DivideNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "divide");
}

TEST(ShaderNodesTest, PowerNode_Compile) {
    PowerNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "power");
}

TEST(ShaderNodesTest, LerpNode_Pins) {
    LerpNode node;
    EXPECT_EQ(node.input_descs().size(), 3u);
    EXPECT_EQ(node.input_descs()[2].name, "Alpha");
    EXPECT_EQ(node.input_descs()[2].type, ValueType::Float1);
}

TEST(ShaderNodesTest, LerpNode_Compile) {
    LerpNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "lerp");
}

TEST(ShaderNodesTest, DotNode_OutputIsFloat1) {
    DotNode node;
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float1);
}

TEST(ShaderNodesTest, DotNode_Compile) {
    DotNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "dot");
}

TEST(ShaderNodesTest, NormalizeNode_Pins) {
    NormalizeNode node;
    EXPECT_EQ(node.input_descs().size(), 1u);
    EXPECT_TRUE(node.input_descs()[0].is_required);
}

TEST(ShaderNodesTest, NormalizeNode_Compile) {
    NormalizeNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "normalize");
}

// ============================================================================
// TextureSampleNode
// ============================================================================

TEST(ShaderNodesTest, TextureSample_Pins) {
    TextureSampleNode node;
    EXPECT_EQ(node.input_descs().size(), 2u);
    EXPECT_EQ(node.input_descs()[0].name, "Tex");
    EXPECT_EQ(node.input_descs()[0].type, ValueType::Texture2D);
    EXPECT_EQ(node.input_descs()[1].name, "UV");
    EXPECT_EQ(node.input_descs()[1].type, ValueType::Scalar);

    EXPECT_EQ(node.output_descs().size(), 5u);
    EXPECT_EQ(node.output_descs()[0].name, "RGB");
    EXPECT_EQ(node.output_descs()[0].type, ValueType::Float3);
    EXPECT_EQ(node.output_descs()[1].name, "R");
    EXPECT_EQ(node.output_descs()[4].name, "A");
}

TEST(ShaderNodesTest, TextureSample_CompileNoTexture) {
    TextureSampleNode node;
    MockCompiler mc;
    // No texture connected — should return constant 0.
    int32_t chunk = node.compile(mc, 0);
    EXPECT_GE(chunk, 100);
}

TEST(ShaderNodesTest, TextureSample_CompileWithTexture) {
    TextureSampleNode node;
    node.get_input(0).source_node_id = 99;
    node.get_input(0).source_output_index = 0;
    MockCompiler mc;
    // Output 0 = RGB → component_mask:xyz.
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "component_mask:xyz");
}

TEST(ShaderNodesTest, TextureSample_CompileOutputChannels) {
    TextureSampleNode node;
    node.get_input(0).source_node_id = 99;
    node.get_input(0).source_output_index = 0;

    for (int i = 0; i < 5; ++i) {
        MockCompiler mc;
        node.compile(mc, i);
        std::string expected_mask;
        switch (i) {
            case 0: expected_mask = "component_mask:xyz"; break;
            case 1: expected_mask = "component_mask:x"; break;
            case 2: expected_mask = "component_mask:y"; break;
            case 3: expected_mask = "component_mask:z"; break;
            case 4: expected_mask = "component_mask:w"; break;
        }
        EXPECT_EQ(mc.calls.back(), expected_mask) << "output_index=" << i;
    }
}

// ============================================================================
// Utility nodes
// ============================================================================

TEST(ShaderNodesTest, AppendNode_Compile) {
    AppendNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "append");
}

TEST(ShaderNodesTest, ComponentMask_Compile) {
    ComponentMaskNode node;
    node.mask = "xz";
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "component_mask:xz");
}

TEST(ShaderNodesTest, ComponentMask_Serialization) {
    ComponentMaskNode node;
    node.mask = "xy";
    nlohmann::json j;
    node.serialize(j);

    ComponentMaskNode restored;
    restored.deserialize(j);
    EXPECT_EQ(restored.mask, "xy");
}

TEST(ShaderNodesTest, TimeNode_Compile) {
    TimeNode node;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "time");
}

TEST(ShaderNodesTest, UVNode_Compile) {
    UVNode node;
    node.uv_channel = 1;
    MockCompiler mc;
    node.compile(mc, 0);
    EXPECT_EQ(mc.calls.back(), "texcoord");
}

TEST(ShaderNodesTest, UVNode_Serialization) {
    UVNode node;
    node.uv_channel = 2;
    nlohmann::json j;
    node.serialize(j);

    UVNode restored;
    restored.deserialize(j);
    EXPECT_EQ(restored.uv_channel, 2);
}

// ============================================================================
// Clone tests
// ============================================================================

TEST(ShaderNodesTest, Clone_Constant1) {
    Constant1Node node;
    node.id = 5; node.value = 1.5f;
    auto cloned = node.clone();
    EXPECT_EQ(cloned->id, 5u);
    EXPECT_EQ(static_cast<Constant1Node*>(cloned.get())->value, 1.5f);
}

TEST(ShaderNodesTest, Clone_ScalarParam) {
    ScalarParamNode node;
    node.id = 7; node.param_name = "Roughness"; node.default_val = 0.5f;
    auto cloned = node.clone();
    auto* sp = static_cast<ScalarParamNode*>(cloned.get());
    EXPECT_EQ(sp->param_name, "Roughness");
    EXPECT_FLOAT_EQ(sp->default_val, 0.5f);
}

TEST(ShaderNodesTest, Clone_AddNode) {
    AddNode node;
    node.id = 3;
    node.get_input(0).default_value[0] = 2.f;
    auto cloned = node.clone();
    EXPECT_EQ(cloned->id, 3u);
    EXPECT_FLOAT_EQ(cloned->get_input(0).default_value[0], 2.f);
}

TEST(ShaderNodesTest, Clone_TextureSample) {
    TextureSampleNode node;
    node.id = 10;
    node.get_input(0).source_node_id = 5;
    auto cloned = node.clone();
    EXPECT_EQ(cloned->get_input(0).source_node_id, 5u);
}

TEST(ShaderNodesTest, Clone_ComponentMask) {
    ComponentMaskNode node;
    node.mask = "yz";
    auto cloned = node.clone();
    EXPECT_EQ(static_cast<ComponentMaskNode*>(cloned.get())->mask, "yz");
}

TEST(ShaderNodesTest, Clone_UVNode) {
    UVNode node;
    node.uv_channel = 3;
    auto cloned = node.clone();
    EXPECT_EQ(static_cast<UVNode*>(cloned.get())->uv_channel, 3);
}

TEST(ShaderNodesTest, Clone_VectorParam) {
    VectorParamNode node;
    node.param_name = "Color";
    node.default_val[0] = 1.f;
    auto cloned = node.clone();
    auto* vp = static_cast<VectorParamNode*>(cloned.get());
    EXPECT_EQ(vp->param_name, "Color");
    EXPECT_FLOAT_EQ(vp->default_val[0], 1.f);
}

TEST(ShaderNodesTest, Clone_TextureParam) {
    TextureParamNode node;
    node.param_name = "Normal";
    auto cloned = node.clone();
    EXPECT_EQ(static_cast<TextureParamNode*>(cloned.get())->param_name, "Normal");
}

TEST(ShaderNodesTest, Clone_MaterialOutput) {
    MaterialOutputNode node;
    node.id = 1;
    node.get_input(0).default_value[0] = 0.8f;
    auto cloned = node.clone();
    EXPECT_EQ(cloned->id, 1u);
    EXPECT_FLOAT_EQ(cloned->get_input(0).default_value[0], 0.8f);
}

// Math node connected input test.
TEST(ShaderNodesTest, AddNode_CompileWithConnectedInput) {
    AddNode node;
    node.get_input(0).source_node_id = 5;
    node.get_input(0).source_output_index = 0;
    MockCompiler mc;
    node.compile(mc, 0);
    // First input was connected → compile_input, second unconnected → constant, then add.
    EXPECT_EQ(mc.calls[0], "compile_input");
    EXPECT_EQ(mc.calls[1], "constant");
    EXPECT_EQ(mc.calls[2], "add");
}

// Constant1 deserialization with missing params.
TEST(ShaderNodesTest, Constant1_DeserializeMissingParams) {
    Constant1Node node;
    node.value = 5.f;
    nlohmann::json j = {{"id", 1}, {"type", "Constant1"}};
    node.deserialize(j);
    EXPECT_FLOAT_EQ(node.value, 5.f);  // Unchanged.
}

// ScalarParam deserialization with missing params.
TEST(ShaderNodesTest, ScalarParam_DeserializeMissingParams) {
    ScalarParamNode node;
    nlohmann::json j = {{"id", 1}, {"type", "ScalarParam"}};
    node.deserialize(j);
    EXPECT_EQ(node.param_name, "Scalar");  // Default.
}
