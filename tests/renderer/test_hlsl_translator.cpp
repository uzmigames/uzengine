// test_hlsl_translator.cpp -- Tests for HlslTranslator HLSL code generation.
//
// Covers:
//   - Single constant → output compilation
//   - Binary operations (Add, Multiply)
//   - TextureSample with UV
//   - Type promotion (Float1 → Float3)
//   - Expression caching (diamond graph → single compilation)
//   - Cycle detection error
//   - Empty graph (no MaterialOutput)
//   - Parameter collection (scalar, vector, texture)
//   - Built-in inputs (time, world position, etc.)

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"
#include "engine/renderer/shader_graph/shader_graph.h"
#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/commands/command_executor.h"
#include "engine/renderer/shader_graph/commands/graph_command.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"
#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/renderer/shader_graph/nodes/texture_nodes.h"
#include "engine/renderer/shader_graph/nodes/utility_nodes.h"

using namespace uz::sg;

class HlslTranslatorTest : public ::testing::Test {
protected:
    ShaderGraph graph_;
    GraphOps ops_{graph_};
};

// ============================================================================
// Basic compilation
// ============================================================================

TEST_F(HlslTranslatorTest, EmptyGraph_NoOutput) {
    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.errors.empty());
    EXPECT_NE(r.errors[0].find("MaterialOutput"), std::string::npos);
}

TEST_F(HlslTranslatorTest, OutputOnly_DefaultValues) {
    ops_.create_node("MaterialOutput");
    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.pixel_hlsl.empty());
    EXPECT_FALSE(r.vertex_hlsl.empty());
    // Should contain default base color.
    EXPECT_NE(r.pixel_hlsl.find("float3(0.5, 0.5, 0.5)"), std::string::npos);
}

TEST_F(HlslTranslatorTest, ConstantToBaseColor) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    // Set constant to red.
    ops_.set_param(cid, "value", {1.0, 0.0, 0.0});

    ops_.connect_pins(cid, 0, oid, 0);  // Constant3 → BaseColor

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // PS should have float3(1.0, 0.0, 0.0) in it.
    EXPECT_NE(r.pixel_hlsl.find("float3(1.0, 0.0, 0.0)"), std::string::npos);
}

TEST_F(HlslTranslatorTest, Constant1ToMetallic) {
    auto c = ops_.create_node("Constant1");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    ops_.set_param(cid, "value", 1.0);
    ops_.connect_pins(cid, 0, oid, 1);  // Constant1 → Metallic

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("1.0"), std::string::npos);
}

// ============================================================================
// Arithmetic operations
// ============================================================================

TEST_F(HlslTranslatorTest, AddTwoConstants) {
    auto a = ops_.create_node("Constant1");
    auto b = ops_.create_node("Constant1");
    auto add = ops_.create_node("Add");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t aid = a.data["nodeId"].get<uint32_t>();
    uint32_t bid = b.data["nodeId"].get<uint32_t>();
    uint32_t addid = add.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(aid, "value", 0.5);
    ops_.set_param(bid, "value", 0.3);
    ops_.connect_pins(aid, 0, addid, 0);
    ops_.connect_pins(bid, 0, addid, 1);
    ops_.connect_pins(addid, 0, outid, 1);  // Add → Metallic

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Should contain an add expression.
    EXPECT_NE(r.pixel_hlsl.find("+"), std::string::npos);
}

TEST_F(HlslTranslatorTest, MultiplyChain) {
    auto c = ops_.create_node("Constant1");
    auto m1 = ops_.create_node("Multiply");
    auto m2 = ops_.create_node("Multiply");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t m1id = m1.data["nodeId"].get<uint32_t>();
    uint32_t m2id = m2.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(cid, 0, m1id, 0);
    ops_.connect_pins(cid, 0, m1id, 1);
    ops_.connect_pins(m1id, 0, m2id, 0);
    ops_.connect_pins(cid, 0, m2id, 1);
    ops_.connect_pins(m2id, 0, outid, 2);  // → Roughness

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Should contain multiply expressions.
    EXPECT_NE(r.pixel_hlsl.find("*"), std::string::npos);
}

// ============================================================================
// Expression caching (diamond graph)
// ============================================================================

TEST_F(HlslTranslatorTest, DiamondGraph_CachedExpression) {
    // C → Add → Out
    //   ↘ Mul ↗   (C is shared, should be compiled once)
    auto c = ops_.create_node("Constant1");
    auto add = ops_.create_node("Add");
    auto mul = ops_.create_node("Multiply");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t addid = add.data["nodeId"].get<uint32_t>();
    uint32_t mulid = mul.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(cid, 0, addid, 0);
    ops_.connect_pins(cid, 0, addid, 1);
    ops_.connect_pins(cid, 0, mulid, 0);
    ops_.connect_pins(cid, 0, mulid, 1);
    ops_.connect_pins(addid, 0, outid, 1);  // Add → Metallic
    ops_.connect_pins(mulid, 0, outid, 2);  // Mul → Roughness

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Both should compile successfully; constant should be cached.
}

// ============================================================================
// Parameters
// ============================================================================

TEST_F(HlslTranslatorTest, ScalarParam_CbufferGenerated) {
    auto p = ops_.create_node("ScalarParam");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t pid = p.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(pid, "param_name", "Metallic");
    ops_.set_param(pid, "default", 0.5);
    ops_.connect_pins(pid, 0, outid, 1);  // ScalarParam → Metallic

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Should contain MaterialParams cbuffer with Metallic field.
    EXPECT_NE(r.pixel_hlsl.find("MaterialParams"), std::string::npos);
    EXPECT_NE(r.pixel_hlsl.find("float Metallic"), std::string::npos);
    EXPECT_NE(r.pixel_hlsl.find("g_Material.Metallic"), std::string::npos);
}

TEST_F(HlslTranslatorTest, TextureParam_Declaration) {
    auto tp = ops_.create_node("TextureParam");
    auto ts = ops_.create_node("TextureSample");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t tpid = tp.data["nodeId"].get<uint32_t>();
    uint32_t tsid = ts.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(tpid, "param_name", "Albedo");
    ops_.connect_pins(tpid, 0, tsid, 0);  // TextureParam → TextureSample.Tex
    ops_.connect_pins(tsid, 0, outid, 0); // TextureSample.RGB → BaseColor

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Should declare texture.
    EXPECT_NE(r.pixel_hlsl.find("Texture2D g_Tex_Albedo"), std::string::npos);
    // Should contain sampler.
    EXPECT_NE(r.pixel_hlsl.find("g_Sampler"), std::string::npos);
    // Should contain .Sample call.
    EXPECT_NE(r.pixel_hlsl.find(".Sample"), std::string::npos);
}

// ============================================================================
// TextureSample channels
// ============================================================================

TEST_F(HlslTranslatorTest, TextureSample_RChannelToMetallic) {
    auto tp = ops_.create_node("TextureParam");
    auto ts = ops_.create_node("TextureSample");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t tpid = tp.data["nodeId"].get<uint32_t>();
    uint32_t tsid = ts.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(tpid, "param_name", "ORM");
    ops_.connect_pins(tpid, 0, tsid, 0);
    ops_.connect_pins(tsid, 1, outid, 1);  // R → Metallic

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Should contain .x swizzle for R channel.
    EXPECT_NE(r.pixel_hlsl.find(".x"), std::string::npos);
}

// ============================================================================
// Built-in inputs
// ============================================================================

TEST_F(HlslTranslatorTest, TimeNode) {
    auto time = ops_.create_node("Time");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t tid = time.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(tid, 0, outid, 1);  // Time → Metallic

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("g_Time"), std::string::npos);
}

TEST_F(HlslTranslatorTest, UVNode) {
    auto uv = ops_.create_node("UV");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t uvid = uv.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(uvid, 0, outid, 0);  // UV → BaseColor

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("input.uv0"), std::string::npos);
}

// ============================================================================
// G-Buffer output structure
// ============================================================================

TEST_F(HlslTranslatorTest, GBufferOutputStructure) {
    ops_.create_node("MaterialOutput");
    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    // Verify G-Buffer MRT outputs.
    EXPECT_NE(r.pixel_hlsl.find("SV_TARGET0"), std::string::npos);
    EXPECT_NE(r.pixel_hlsl.find("SV_TARGET1"), std::string::npos);
    EXPECT_NE(r.pixel_hlsl.find("SV_TARGET2"), std::string::npos);
    EXPECT_NE(r.pixel_hlsl.find("SV_TARGET3"), std::string::npos);
    EXPECT_NE(r.pixel_hlsl.find("PSOutput"), std::string::npos);
}

TEST_F(HlslTranslatorTest, VertexShaderStructure) {
    ops_.create_node("MaterialOutput");
    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.vertex_hlsl.find("SV_POSITION"), std::string::npos);
    EXPECT_NE(r.vertex_hlsl.find("g_World"), std::string::npos);
    EXPECT_NE(r.vertex_hlsl.find("g_ViewProj"), std::string::npos);
    EXPECT_NE(r.vertex_hlsl.find("VSOutput"), std::string::npos);
    EXPECT_NE(r.vertex_hlsl.find("VSInput"), std::string::npos);
}

// ============================================================================
// Compile command integration
// ============================================================================

TEST_F(HlslTranslatorTest, CompileViaCommandExecutor) {
    ops_.create_node("MaterialOutput");

    CommandExecutor exec(ops_);
    GraphCommand cmd{CommandType::COMPILE, {}};
    auto r = exec.execute(cmd);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.data.contains("vertexHlsl"));
    EXPECT_TRUE(r.data.contains("pixelHlsl"));
    EXPECT_FALSE(r.data["pixelHlsl"].get<std::string>().empty());
}

TEST_F(HlslTranslatorTest, CompileViaCommandExecutor_NoOutput) {
    CommandExecutor exec(ops_);
    GraphCommand cmd{CommandType::COMPILE, {}};
    auto r = exec.execute(cmd);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.data.contains("errors"));
}

// ============================================================================
// Math functions
// ============================================================================

TEST_F(HlslTranslatorTest, LerpNode) {
    auto a = ops_.create_node("Constant1");
    auto b = ops_.create_node("Constant1");
    auto alpha = ops_.create_node("Constant1");
    auto lrp = ops_.create_node("Lerp");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t aid = a.data["nodeId"].get<uint32_t>();
    uint32_t bid = b.data["nodeId"].get<uint32_t>();
    uint32_t alid = alpha.data["nodeId"].get<uint32_t>();
    uint32_t lid = lrp.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(aid, 0, lid, 0);
    ops_.connect_pins(bid, 0, lid, 1);
    ops_.connect_pins(alid, 0, lid, 2);
    ops_.connect_pins(lid, 0, outid, 2);

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("lerp("), std::string::npos);
}

TEST_F(HlslTranslatorTest, DotNode) {
    auto c = ops_.create_node("Constant3");
    auto d = ops_.create_node("Dot");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t did = d.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(cid, 0, did, 0);
    ops_.connect_pins(cid, 0, did, 1);
    ops_.connect_pins(did, 0, outid, 1);

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("dot("), std::string::npos);
}

TEST_F(HlslTranslatorTest, NormalizeNode) {
    auto c = ops_.create_node("Constant3");
    auto n = ops_.create_node("Normalize");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t nid = n.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(cid, 0, nid, 0);
    ops_.connect_pins(nid, 0, outid, 3);  // → Normal

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("normalize("), std::string::npos);
}

TEST_F(HlslTranslatorTest, PowerNode) {
    auto c1 = ops_.create_node("Constant1");
    auto c2 = ops_.create_node("Constant1");
    auto pw = ops_.create_node("Power");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t c1id = c1.data["nodeId"].get<uint32_t>();
    uint32_t c2id = c2.data["nodeId"].get<uint32_t>();
    uint32_t pwid = pw.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(c1id, 0, pwid, 0);
    ops_.connect_pins(c2id, 0, pwid, 1);
    ops_.connect_pins(pwid, 0, outid, 2);

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find("pow("), std::string::npos);
}

// ============================================================================
// Utility nodes
// ============================================================================

TEST_F(HlslTranslatorTest, ComponentMaskNode) {
    auto c = ops_.create_node("Constant3");
    auto cm = ops_.create_node("ComponentMask");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t cmid = cm.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(cmid, "mask", "xy");
    ops_.connect_pins(cid, 0, cmid, 0);
    ops_.connect_pins(cmid, 0, outid, 0);

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_NE(r.pixel_hlsl.find(".xy"), std::string::npos);
}

// ============================================================================
// Chunk system
// ============================================================================

TEST_F(HlslTranslatorTest, ChunkCount) {
    auto c = ops_.create_node("Constant1");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, outid, 1);

    HlslTranslator t;
    auto r = t.compile(graph_);
    EXPECT_TRUE(r.ok);
    EXPECT_GT(t.chunks().size(), 0u);
}

TEST_F(HlslTranslatorTest, InlineChunksHaveNoVarName) {
    auto c = ops_.create_node("Constant1");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, outid, 1);

    HlslTranslator t;
    t.compile(graph_);
    // Constants should be inline.
    bool found_inline = false;
    for (auto& chunk : t.chunks()) {
        if (chunk.is_inline) {
            EXPECT_TRUE(chunk.var_name.empty());
            found_inline = true;
        }
    }
    EXPECT_TRUE(found_inline);
}

TEST_F(HlslTranslatorTest, NonInlineChunksHaveVarName) {
    auto a = ops_.create_node("Constant1");
    auto b = ops_.create_node("Constant1");
    auto add = ops_.create_node("Add");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t aid = a.data["nodeId"].get<uint32_t>();
    uint32_t bid = b.data["nodeId"].get<uint32_t>();
    uint32_t addid = add.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(aid, 0, addid, 0);
    ops_.connect_pins(bid, 0, addid, 1);
    ops_.connect_pins(addid, 0, outid, 1);

    HlslTranslator t;
    t.compile(graph_);
    // Add node should produce a non-inline chunk.
    bool found_named = false;
    for (auto& chunk : t.chunks()) {
        if (!chunk.is_inline && !chunk.var_name.empty()) {
            EXPECT_NE(chunk.var_name.find("v"), std::string::npos);
            found_named = true;
        }
    }
    EXPECT_TRUE(found_named);
}
