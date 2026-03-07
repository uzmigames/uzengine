// test_graph_material_loader.cpp -- Tests for the shader graph → material bridge.
//
// Verifies: graph: prefix detection, HLSL compilation, parameter extraction,
// shader name generation, caching, and recompilation.

#include <gtest/gtest.h>
#include "engine/renderer/resources/graph_material_loader.h"
#include "engine/renderer/shader_graph/shader_graph_manager.h"
#include "engine/renderer/shader_graph/graph_ops.h"

#include <filesystem>
#include <fstream>

using namespace uz;
using namespace uz::sg;

// ===========================================================================
// Prefix detection tests (no GPU, no files)
// ===========================================================================

TEST(GraphMaterialPrefix, DetectsGraphPrefix) {
    EXPECT_TRUE(is_graph_material_path("graph:assets/foo.uzshader"));
    EXPECT_TRUE(is_graph_material_path("graph:C:/path/to/file.uzshader"));
}

TEST(GraphMaterialPrefix, RejectsNonGraphPaths) {
    EXPECT_FALSE(is_graph_material_path("assets/materials/gold.uzmat"));
    EXPECT_FALSE(is_graph_material_path(""));
    EXPECT_FALSE(is_graph_material_path("graph"));     // Too short
    EXPECT_FALSE(is_graph_material_path("graph:"));    // Empty after prefix
    EXPECT_FALSE(is_graph_material_path("GRAPH:foo")); // Case-sensitive
}

TEST(GraphMaterialPrefix, StripsPrefix) {
    EXPECT_EQ(strip_graph_prefix("graph:assets/foo.uzshader"), "assets/foo.uzshader");
    EXPECT_EQ(strip_graph_prefix("graph:C:/path/file.uzshader"), "C:/path/file.uzshader");
}

// ===========================================================================
// GraphMaterialLoader integration tests
// ===========================================================================

class GraphMaterialLoaderTest : public ::testing::Test {
protected:
    std::string temp_path_;

    void SetUp() override {
        // Build a minimal PBR-like graph:
        //   MaterialOutput, Constant3 albedo, ScalarParam Metallic, ScalarParam Roughness
        ShaderGraph graph;
        GraphOps ops(graph);

        auto out = ops.create_node("MaterialOutput", 600.f, 200.f);
        auto albedo = ops.create_node("Constant3", 100.f, 100.f);
        auto met = ops.create_node("ScalarParam", 100.f, 250.f);
        auto rgh = ops.create_node("ScalarParam", 100.f, 350.f);

        uint32_t out_id = out.data["nodeId"].get<uint32_t>();
        uint32_t alb_id = albedo.data["nodeId"].get<uint32_t>();
        uint32_t met_id = met.data["nodeId"].get<uint32_t>();
        uint32_t rgh_id = rgh.data["nodeId"].get<uint32_t>();

        ops.set_param(alb_id, "value", {0.8, 0.2, 0.1});
        ops.set_param(met_id, "param_name", "Metallic");
        ops.set_param(met_id, "default", 0.9);
        ops.set_param(rgh_id, "param_name", "Roughness");
        ops.set_param(rgh_id, "default", 0.3);

        ops.connect_pins(alb_id, 0, out_id, 0);  // albedo → BaseColor
        ops.connect_pins(met_id, 0, out_id, 1);   // Metallic → Metallic
        ops.connect_pins(rgh_id, 0, out_id, 2);   // Roughness → Roughness

        auto tmp = std::filesystem::temp_directory_path() / "test_graph_loader.uzshader";
        temp_path_ = tmp.string();
        std::ofstream ofs(tmp, std::ios::binary);
        ofs << graph.serialize().dump(2);
        ofs.close();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(temp_path_, ec);  // Ignore errors (file may be locked).
    }
};

TEST_F(GraphMaterialLoaderTest, LoadGraphMaterial_CompileAndExtractParams) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    Material* mat = loader.load_graph_material(temp_path_, shaderLib, matMgr);
    ASSERT_NE(mat, nullptr);

    // Shader name should be "sg_test_graph_loader".
    EXPECT_EQ(mat->shader_name, "sg_test_graph_loader");
    EXPECT_EQ(mat->name, mat->shader_name);

    // Scalar params extracted from ScalarParam nodes.
    ASSERT_TRUE(mat->parameters.count("Metallic"));
    EXPECT_FLOAT_EQ(mat->parameters["Metallic"], 0.9f);

    ASSERT_TRUE(mat->parameters.count("Roughness"));
    EXPECT_FLOAT_EQ(mat->parameters["Roughness"], 0.3f);

    // Shader should be registered in ShaderLibrary.
    const ShaderProgram* prog = shaderLib.get("sg_test_graph_loader");
    ASSERT_NE(prog, nullptr);
    EXPECT_NE(prog->vs_source, nullptr);
    EXPECT_NE(prog->ps_source, nullptr);
    EXPECT_GT(prog->vs_size, 0u);
    EXPECT_GT(prog->ps_size, 0u);
}

TEST_F(GraphMaterialLoaderTest, LoadGraphMaterial_CachesOnSecondCall) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    Material* mat1 = loader.load_graph_material(temp_path_, shaderLib, matMgr);
    Material* mat2 = loader.load_graph_material(temp_path_, shaderLib, matMgr);

    ASSERT_NE(mat1, nullptr);
    EXPECT_EQ(mat1, mat2) << "Should return cached material pointer";
}

TEST_F(GraphMaterialLoaderTest, HasCompiled) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    EXPECT_FALSE(loader.has_compiled(temp_path_));
    loader.load_graph_material(temp_path_, shaderLib, matMgr);
    EXPECT_TRUE(loader.has_compiled(temp_path_));
}

TEST_F(GraphMaterialLoaderTest, GetCompiled_ReturnsHLSL) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    loader.load_graph_material(temp_path_, shaderLib, matMgr);

    auto* compiled = loader.get_compiled(temp_path_);
    ASSERT_NE(compiled, nullptr);
    EXPECT_FALSE(compiled->vs_hlsl.empty());
    EXPECT_FALSE(compiled->ps_hlsl.empty());
    EXPECT_EQ(compiled->shader_name, "sg_test_graph_loader");

    // VS should contain standard passthrough (entry point is "main").
    EXPECT_NE(compiled->vs_hlsl.find("VSOutput main("), std::string::npos);
    EXPECT_NE(compiled->vs_hlsl.find("VSOutput"), std::string::npos);

    // PS should contain material params and albedo constant.
    EXPECT_NE(compiled->ps_hlsl.find("float Metallic"), std::string::npos);
    EXPECT_NE(compiled->ps_hlsl.find("float3(0.8, 0.2, 0.1)"), std::string::npos);
}

TEST_F(GraphMaterialLoaderTest, GetCompiled_NullForUnknownPath) {
    GraphMaterialLoader loader;
    EXPECT_EQ(loader.get_compiled("nonexistent.uzshader"), nullptr);
}

TEST_F(GraphMaterialLoaderTest, LoadGraphMaterial_FailsOnNonexistentFile) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    Material* mat = loader.load_graph_material("nonexistent.uzshader", shaderLib, matMgr);
    EXPECT_EQ(mat, nullptr);
}

TEST_F(GraphMaterialLoaderTest, Recompile_ForcesNewCompilation) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    Material* mat1 = loader.load_graph_material(temp_path_, shaderLib, matMgr);
    ASSERT_NE(mat1, nullptr);

    // Mutate the graph (add a VectorParam node).
    auto* managed = loader.graph_manager().get(temp_path_);
    ASSERT_NE(managed, nullptr);
    auto vec_result = managed->ops->create_node("VectorParam", 200.f, 400.f);
    uint32_t vec_id = vec_result.data["nodeId"].get<uint32_t>();
    managed->ops->set_param(vec_id, "param_name", "Color");

    // Recompile.
    Material* mat2 = loader.recompile(temp_path_, shaderLib, matMgr);
    ASSERT_NE(mat2, nullptr);

    // New material should have the VectorParam.
    ASSERT_TRUE(mat2->vec_parameters.count("Color"));
}

TEST_F(GraphMaterialLoaderTest, LoadGraphMaterial_WithTextureParam) {
    // Create a graph with a TextureParam node.
    ShaderGraph graph;
    GraphOps ops(graph);

    auto out = ops.create_node("MaterialOutput", 600.f, 200.f);
    auto tex_param = ops.create_node("TextureParam", -100.f, 500.f);
    auto tex_sample = ops.create_node("TextureSample", 100.f, 500.f);

    uint32_t out_id = out.data["nodeId"].get<uint32_t>();
    uint32_t tparam_id = tex_param.data["nodeId"].get<uint32_t>();
    uint32_t tsamp_id = tex_sample.data["nodeId"].get<uint32_t>();

    ops.set_param(tparam_id, "param_name", "AlbedoTex");
    ops.connect_pins(tparam_id, 0, tsamp_id, 0);  // TextureParam → TextureSample.Tex
    ops.connect_pins(tsamp_id, 0, out_id, 0);      // TextureSample.RGB → BaseColor

    auto tmp = std::filesystem::temp_directory_path() / "test_graph_tex.uzshader";
    std::string tex_path = tmp.string();
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs << graph.serialize().dump(2);
    }

    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    Material* mat = loader.load_graph_material(tex_path, shaderLib, matMgr);
    ASSERT_NE(mat, nullptr);

    // Should have extracted the texture param slot.
    EXPECT_TRUE(mat->textures.count("AlbedoTex"));

    std::error_code ec;
    std::filesystem::remove(tex_path, ec);
}

TEST_F(GraphMaterialLoaderTest, MakeShaderName_StripsPathAndExtension) {
    // Test via indirect observation: the shader_name in Material.
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    Material* mat = loader.load_graph_material(temp_path_, shaderLib, matMgr);
    ASSERT_NE(mat, nullptr);
    // temp_path_ ends with "test_graph_loader.uzshader"
    // -> shader name should be "sg_test_graph_loader"
    EXPECT_EQ(mat->shader_name, "sg_test_graph_loader");
}

TEST_F(GraphMaterialLoaderTest, GraphManagerAccessible) {
    GraphMaterialLoader loader;
    ShaderLibrary shaderLib;
    MaterialManager matMgr;

    loader.load_graph_material(temp_path_, shaderLib, matMgr);

    // Should be able to access the internal ShaderGraphManager.
    auto& mgr = loader.graph_manager();
    EXPECT_TRUE(mgr.has(temp_path_));
    EXPECT_FALSE(mgr.list().empty());
}
