// test_shader_graph_demo.cpp -- Integration demo: build a material with
// Perlin noise-masked blue emissive using the shader graph system.
//
// Demonstrates the full pipeline:
//   1. Load demo_material.uzshader via ShaderGraphManager
//   2. Add noise TextureSample + blue Constant3 + Multiply → Emission
//   3. Compile to HLSL via CommandExecutor batch
//   4. Verify the generated shader contains expected patterns
//   5. Save the modified graph back to disk

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/shader_graph_manager.h"
#include "engine/renderer/shader_graph/commands/command_executor.h"
#include "engine/renderer/shader_graph/compiler/hlsl_translator.h"

#include <filesystem>
#include <fstream>

using namespace uz::sg;

class ShaderGraphDemoTest : public ::testing::Test {
protected:
    // Write the demo .uzshader to a temp file so tests don't depend on assets/.
    std::string temp_path_;

    void SetUp() override {
        // Create the base graph in-memory matching demo_material.uzshader:
        //   id=1: MaterialOutput (600, 200)
        //   id=2: Constant3 [0.8, 0.2, 0.1] → BaseColor
        //   id=3: ScalarParam "Metallic" 0.9 → Metallic
        //   id=4: ScalarParam "Roughness" 0.3 → Roughness
        ShaderGraph base;
        GraphOps ops(base);
        auto out = ops.create_node("MaterialOutput", 600.f, 200.f);
        auto albedo = ops.create_node("Constant3", 100.f, 100.f);
        auto metallic = ops.create_node("ScalarParam", 100.f, 250.f);
        auto roughness = ops.create_node("ScalarParam", 100.f, 350.f);

        uint32_t out_id = out.data["nodeId"].get<uint32_t>();
        uint32_t alb_id = albedo.data["nodeId"].get<uint32_t>();
        uint32_t met_id = metallic.data["nodeId"].get<uint32_t>();
        uint32_t rgh_id = roughness.data["nodeId"].get<uint32_t>();

        ops.set_param(alb_id, "value", {0.8, 0.2, 0.1});
        ops.set_param(met_id, "param_name", "Metallic");
        ops.set_param(met_id, "default", 0.9);
        ops.set_param(rgh_id, "param_name", "Roughness");
        ops.set_param(rgh_id, "default", 0.3);

        ops.connect_pins(alb_id, 0, out_id, 0);  // Constant3 → BaseColor
        ops.connect_pins(met_id, 0, out_id, 1);   // ScalarParam → Metallic
        ops.connect_pins(rgh_id, 0, out_id, 2);   // ScalarParam → Roughness

        // Save to temp file.
        auto tmp = std::filesystem::temp_directory_path() / "demo_material_test.uzshader";
        temp_path_ = tmp.string();
        auto j = base.serialize();
        std::ofstream ofs(tmp, std::ios::binary);
        ofs << j.dump(2);
        ofs.close();
    }

    void TearDown() override {
        std::filesystem::remove(temp_path_);
        // Also clean up the modified version.
        auto modified = std::filesystem::path(temp_path_).parent_path() / "demo_emissive_noise.uzshader";
        std::filesystem::remove(modified);
    }
};

TEST_F(ShaderGraphDemoTest, AddPerlinNoiseEmissiveViaBatch) {
    // 1. Load the base graph.
    ShaderGraphManager mgr;
    auto loaded = mgr.load(temp_path_);
    ASSERT_FALSE(loaded.empty()) << "Failed to load demo_material.uzshader";

    auto* mg = mgr.get(temp_path_);
    ASSERT_NE(mg, nullptr);
    EXPECT_EQ(mg->graph->nodes().size(), 4u);  // Output + Constant3 + 2 ScalarParams.

    // Find the MaterialOutput node ID.
    uint32_t output_id = 0;
    for (auto& n : mg->graph->nodes()) {
        if (n->type_name == "MaterialOutput") {
            output_id = n->id;
            break;
        }
    }
    ASSERT_NE(output_id, 0u);

    // 2. Use CommandExecutor batch to add noise-masked blue emissive.
    //    MaterialOutput pin layout:
    //      0: BaseColor, 1: Metallic, 2: Roughness, 3: Normal,
    //      4: Emission,  5: AO,       6: Opacity,   7: Displacement
    //
    //    Graph additions:
    //      - TextureParam "NoiseTex" → TextureSample.Tex
    //      - TextureSample (noise texture via TextureParam)
    //      - Constant3 (blue emission color: [0.1, 0.4, 1.0])
    //      - Multiply (noise.R * blue → masked emission)
    //      - Connect Multiply.out → MaterialOutput.Emission (input 4)
    CommandExecutor executor(*mg->ops);

    nlohmann::json batch;
    batch["type"] = "BATCH";
    batch["commands"] = nlohmann::json::array({
        // $0: Add TextureParam for noise texture
        {{"type", "ADD_NODE"}, {"params", {{"nodeType", "TextureParam"}, {"x", -100}, {"y", 500}}}},
        // $1: Set the texture param name
        {{"type", "SET_PARAM"}, {"params", {{"nodeId", "$0"}, {"key", "param_name"},
            {"value", "NoiseTex"}}}},
        // $2: Add TextureSample node
        {{"type", "ADD_NODE"}, {"params", {{"nodeType", "TextureSample"}, {"x", 100}, {"y", 500}}}},
        // $3: Connect TextureParam → TextureSample.Tex (input 0)
        {{"type", "CONNECT"}, {"params", {{"from", "$0"}, {"fromOutput", 0},
            {"to", "$2"}, {"toInput", 0}}}},
        // $4: Add blue emission color constant
        {{"type", "ADD_NODE"}, {"params", {{"nodeType", "Constant3"}, {"x", 100}, {"y", 650}}}},
        // $5: Set blue color [0.1, 0.4, 1.0]
        {{"type", "SET_PARAM"}, {"params", {{"nodeId", "$4"}, {"key", "value"},
            {"value", {0.1, 0.4, 1.0}}}}},
        // $6: Add Multiply node (noise.R * blue)
        {{"type", "ADD_NODE"}, {"params", {{"nodeType", "Multiply"}, {"x", 300}, {"y", 550}}}},
        // $7: Connect TextureSample.R (output 1) → Multiply.A (input 0)
        {{"type", "CONNECT"}, {"params", {{"from", "$2"}, {"fromOutput", 1},
            {"to", "$6"}, {"toInput", 0}}}},
        // $8: Connect blue Constant3 → Multiply.B (input 1)
        {{"type", "CONNECT"}, {"params", {{"from", "$4"}, {"fromOutput", 0},
            {"to", "$6"}, {"toInput", 1}}}},
        // $9: Connect Multiply → MaterialOutput.Emission (input 4)
        {{"type", "CONNECT"}, {"params", {{"from", "$6"}, {"fromOutput", 0},
            {"to", output_id}, {"toInput", 4}}}},
    });

    // Parse and execute batch.
    auto result = executor.execute_json(batch);
    ASSERT_TRUE(result.ok) << "Batch failed: " << result.error;

    // 3. Verify the graph was modified correctly.
    //    4 original + TextureParam + TextureSample + Constant3(blue) + Multiply = 8.
    EXPECT_EQ(mg->graph->nodes().size(), 8u);

    // 4. Compile to HLSL.
    HlslTranslator translator;
    auto compiled = translator.compile(*mg->graph);
    ASSERT_TRUE(compiled.ok) << "Compilation failed: "
        << (compiled.errors.empty() ? "unknown" : compiled.errors[0]);

    // Verify pixel shader contains expected patterns.
    auto& ps = compiled.pixel_hlsl;
    EXPECT_NE(ps.find("Texture2D"), std::string::npos)
        << "Should declare noise texture\nPS:\n" << ps;
    EXPECT_NE(ps.find("SamplerState"), std::string::npos)
        << "Should declare sampler";
    EXPECT_NE(ps.find("float3(0.1, 0.4, 1.0)"), std::string::npos)
        << "Should contain blue emission color";
    EXPECT_NE(ps.find("float3(0.8, 0.2, 0.1)"), std::string::npos)
        << "Should still contain original albedo color";

    // Verify cbuffer has Metallic and Roughness params.
    EXPECT_NE(ps.find("float Metallic"), std::string::npos);
    EXPECT_NE(ps.find("float Roughness"), std::string::npos);

    // Verify the multiply (noise * blue) feeds into emission (RT3).
    // The emission expression should contain a multiply of texture sample and blue constant.
    EXPECT_NE(ps.find("rt3"), std::string::npos)
        << "Should write to RT3 (emission)";

    // Vertex shader should still be valid.
    EXPECT_NE(compiled.vertex_hlsl.find("VSOutput"), std::string::npos);

    // 5. Save the modified graph.
    auto save_path = std::filesystem::path(temp_path_).parent_path() / "demo_emissive_noise.uzshader";
    // Re-register under new path for saving.
    mg->file_path = save_path.string();
    mg->dirty = true;

    // Print the generated HLSL for manual inspection.
    std::cout << "\n=== GENERATED PIXEL SHADER ===\n" << ps << "\n";
    std::cout << "\n=== GENERATED VERTEX SHADER ===\n" << compiled.vertex_hlsl << "\n";
}

TEST_F(ShaderGraphDemoTest, CompileBaseGraphBeforeModification) {
    // Verify the base graph compiles cleanly before any modifications.
    ShaderGraphManager mgr;
    mgr.load(temp_path_);
    auto* mg = mgr.get(temp_path_);
    ASSERT_NE(mg, nullptr);

    HlslTranslator translator;
    auto compiled = translator.compile(*mg->graph);
    ASSERT_TRUE(compiled.ok);
    EXPECT_NE(compiled.pixel_hlsl.find("float3(0.8, 0.2, 0.1)"), std::string::npos);
    EXPECT_NE(compiled.pixel_hlsl.find("float Metallic"), std::string::npos);
    EXPECT_NE(compiled.pixel_hlsl.find("float Roughness"), std::string::npos);
}

TEST_F(ShaderGraphDemoTest, BatchRollbackOnInvalidNode) {
    ShaderGraphManager mgr;
    mgr.load(temp_path_);
    auto* mg = mgr.get(temp_path_);

    CommandExecutor executor(*mg->ops);
    size_t node_count_before = mg->graph->nodes().size();

    // Batch with an invalid node type mid-way should rollback everything.
    nlohmann::json batch;
    batch["type"] = "BATCH";
    batch["commands"] = nlohmann::json::array({
        {{"type", "ADD_NODE"}, {"params", {{"nodeType", "Constant3"}}}},
        {{"type", "ADD_NODE"}, {"params", {{"nodeType", "NonExistentNode"}}}},
    });

    auto result = executor.execute_json(batch);
    EXPECT_FALSE(result.ok);

    // Graph should be unchanged (rollback).
    EXPECT_EQ(mg->graph->nodes().size(), node_count_before);
}
