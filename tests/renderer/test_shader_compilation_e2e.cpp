// test_shader_compilation_e2e.cpp -- End-to-end shader graph compilation tests.
//
// Builds realistic PBR graphs and verifies the generated HLSL contains
// expected structures: cbuffer fields, texture bindings, local variables,
// output assignments matching G-Buffer layout.

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"
#include "engine/renderer/shader_graph/shader_graph.h"
#include "engine/renderer/shader_graph/graph_ops.h"

using namespace uz::sg;

class ShaderCompilationE2E : public ::testing::Test {
protected:
    ShaderGraph graph_;
    GraphOps ops_{graph_};
};

// ============================================================================
// Full PBR graph: TextureSample → Multiply → MaterialOutput
// ============================================================================

TEST_F(ShaderCompilationE2E, FullPBRGraph) {
    // Create nodes.
    auto albedo_tex = ops_.create_node("TextureParam");
    auto albedo_sampler = ops_.create_node("TextureSample");
    auto normal_tex = ops_.create_node("TextureParam");
    auto normal_sampler = ops_.create_node("TextureSample");
    auto roughness = ops_.create_node("ScalarParam");
    auto metallic = ops_.create_node("ScalarParam");
    auto out = ops_.create_node("MaterialOutput");

    uint32_t at_id = albedo_tex.data["nodeId"].get<uint32_t>();
    uint32_t as_id = albedo_sampler.data["nodeId"].get<uint32_t>();
    uint32_t nt_id = normal_tex.data["nodeId"].get<uint32_t>();
    uint32_t ns_id = normal_sampler.data["nodeId"].get<uint32_t>();
    uint32_t r_id = roughness.data["nodeId"].get<uint32_t>();
    uint32_t m_id = metallic.data["nodeId"].get<uint32_t>();
    uint32_t o_id = out.data["nodeId"].get<uint32_t>();

    // Set param names.
    ops_.set_param(at_id, "param_name", "AlbedoTex");
    ops_.set_param(nt_id, "param_name", "NormalTex");
    ops_.set_param(r_id, "param_name", "Roughness");
    ops_.set_param(r_id, "default", 0.5);
    ops_.set_param(m_id, "param_name", "Metallic");
    ops_.set_param(m_id, "default", 0.0);

    // Wire texture params to samplers.
    ops_.connect_pins(at_id, 0, as_id, 0);  // AlbedoTex → TextureSample.Tex
    ops_.connect_pins(nt_id, 0, ns_id, 0);  // NormalTex → TextureSample.Tex

    // Wire outputs to MaterialOutput.
    ops_.connect_pins(as_id, 0, o_id, 0);   // Albedo.RGB → BaseColor
    ops_.connect_pins(m_id, 0, o_id, 1);    // Metallic → Metallic
    ops_.connect_pins(r_id, 0, o_id, 2);    // Roughness → Roughness
    ops_.connect_pins(ns_id, 0, o_id, 3);   // Normal.RGB → Normal

    // Compile.
    HlslTranslator t;
    auto result = t.compile(graph_);

    EXPECT_TRUE(result.ok) << "Errors: "
        << (result.errors.empty() ? "none" : result.errors[0]);

    // Verify pixel shader contents.
    auto& ps = result.pixel_hlsl;

    // Texture declarations.
    EXPECT_NE(ps.find("Texture2D g_Tex_AlbedoTex"), std::string::npos);
    EXPECT_NE(ps.find("Texture2D g_Tex_NormalTex"), std::string::npos);

    // Material cbuffer.
    EXPECT_NE(ps.find("cbuffer MaterialParams"), std::string::npos);
    EXPECT_NE(ps.find("float Metallic"), std::string::npos);
    EXPECT_NE(ps.find("float Roughness"), std::string::npos);

    // G-Buffer output assignments.
    EXPECT_NE(ps.find("output.rt0"), std::string::npos);  // Albedo + AO
    EXPECT_NE(ps.find("output.rt1"), std::string::npos);  // Normal
    EXPECT_NE(ps.find("output.rt2"), std::string::npos);  // Metallic + Roughness
    EXPECT_NE(ps.find("output.rt3"), std::string::npos);  // Emission

    // Sample calls.
    EXPECT_NE(ps.find(".Sample"), std::string::npos);
    EXPECT_NE(ps.find("g_Sampler"), std::string::npos);

    // Vertex shader.
    EXPECT_NE(result.vertex_hlsl.find("g_World"), std::string::npos);
    EXPECT_NE(result.vertex_hlsl.find("g_ViewProj"), std::string::npos);
}

// ============================================================================
// Multiply texture by scalar parameter
// ============================================================================

TEST_F(ShaderCompilationE2E, TextureMultipliedByScalar) {
    auto tex = ops_.create_node("TextureParam");
    auto sampler = ops_.create_node("TextureSample");
    auto scalar = ops_.create_node("ScalarParam");
    auto mul = ops_.create_node("Multiply");
    auto out = ops_.create_node("MaterialOutput");

    uint32_t tex_id = tex.data["nodeId"].get<uint32_t>();
    uint32_t samp_id = sampler.data["nodeId"].get<uint32_t>();
    uint32_t sc_id = scalar.data["nodeId"].get<uint32_t>();
    uint32_t mul_id = mul.data["nodeId"].get<uint32_t>();
    uint32_t out_id = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(tex_id, "param_name", "Albedo");
    ops_.set_param(sc_id, "param_name", "Brightness");
    ops_.set_param(sc_id, "default", 1.0);

    ops_.connect_pins(tex_id, 0, samp_id, 0);
    ops_.connect_pins(samp_id, 0, mul_id, 0);  // TextureSample.RGB → Multiply.A
    ops_.connect_pins(sc_id, 0, mul_id, 1);     // Brightness → Multiply.B
    ops_.connect_pins(mul_id, 0, out_id, 0);    // Multiply → BaseColor

    HlslTranslator t;
    auto result = t.compile(graph_);
    EXPECT_TRUE(result.ok);
    EXPECT_NE(result.pixel_hlsl.find("*"), std::string::npos);
    EXPECT_NE(result.pixel_hlsl.find("g_Material.Brightness"), std::string::npos);
}

// ============================================================================
// Multiple outputs connected: Metallic + Roughness from same ORM texture
// ============================================================================

TEST_F(ShaderCompilationE2E, ORMTextureUnpacking) {
    auto tex = ops_.create_node("TextureParam");
    auto sampler = ops_.create_node("TextureSample");
    auto out = ops_.create_node("MaterialOutput");

    uint32_t tex_id = tex.data["nodeId"].get<uint32_t>();
    uint32_t samp_id = sampler.data["nodeId"].get<uint32_t>();
    uint32_t out_id = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(tex_id, "param_name", "ORM");

    ops_.connect_pins(tex_id, 0, samp_id, 0);
    // R → AO (output 1 = R), G → Roughness (output 2 = G), B → Metallic (output 3 = B)
    ops_.connect_pins(samp_id, 1, out_id, 5);  // R → AO
    ops_.connect_pins(samp_id, 2, out_id, 2);  // G → Roughness
    ops_.connect_pins(samp_id, 3, out_id, 1);  // B → Metallic

    HlslTranslator t;
    auto result = t.compile(graph_);
    EXPECT_TRUE(result.ok);
    // Should contain swizzle operations.
    auto& ps = result.pixel_hlsl;
    EXPECT_NE(ps.find(".x"), std::string::npos);  // R
    EXPECT_NE(ps.find(".y"), std::string::npos);  // G
    EXPECT_NE(ps.find(".z"), std::string::npos);  // B
}

// ============================================================================
// Emission: constant emissive color
// ============================================================================

TEST_F(ShaderCompilationE2E, EmissiveConstant) {
    auto c = ops_.create_node("Constant3");
    auto out = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t outid = out.data["nodeId"].get<uint32_t>();

    ops_.set_param(cid, "value", {10.0, 5.0, 0.0});  // Bright orange emission
    ops_.connect_pins(cid, 0, outid, 4);  // Constant3 → Emission

    HlslTranslator t;
    auto result = t.compile(graph_);
    EXPECT_TRUE(result.ok);
    // RT3 should reference the emission.
    EXPECT_NE(result.pixel_hlsl.find("output.rt3"), std::string::npos);
    EXPECT_NE(result.pixel_hlsl.find("float3(10.0, 5.0, 0.0)"), std::string::npos);
}
