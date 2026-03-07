// test_node_registry.cpp -- Google Test coverage for NodeRegistry.
//
// Covers:
//   - Registration, creation, lookup
//   - Category filtering
//   - Unknown type returns nullptr
//   - All 15+ starter nodes are registered

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/node_registry.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"
#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/nodes/texture_nodes.h"
#include "engine/renderer/shader_graph/nodes/utility_nodes.h"

using namespace uz::sg;

class NodeRegistryTest : public ::testing::Test {
protected:
    NodeRegistry& reg_ = NodeRegistry::instance();
};

TEST_F(NodeRegistryTest, AllStarterNodesRegistered) {
    // Verify all 20 expected node types are registered.
    const std::vector<std::string> expected = {
        "MaterialOutput",
        "Constant1", "Constant3", "Constant4",
        "ScalarParam", "VectorParam", "TextureParam",
        "Add", "Subtract", "Multiply", "Divide", "Lerp", "Dot", "Normalize", "Power",
        "TextureSample",
        "Append", "ComponentMask", "Time", "UV",
    };
    for (auto& name : expected) {
        EXPECT_NE(reg_.find(name), nullptr) << "Missing node type: " << name;
    }
}

TEST_F(NodeRegistryTest, CreateByName) {
    auto node = reg_.create("Add");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type_name, "Add");
}

TEST_F(NodeRegistryTest, CreateUnknownReturnsNull) {
    auto node = reg_.create("DoesNotExist");
    EXPECT_EQ(node, nullptr);
}

TEST_F(NodeRegistryTest, FindByName) {
    auto* info = reg_.find("Multiply");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "Multiply");
    EXPECT_EQ(info->category, NodeCategory::Math);
}

TEST_F(NodeRegistryTest, FindUnknownReturnsNull) {
    EXPECT_EQ(reg_.find("NonExistent"), nullptr);
}

TEST_F(NodeRegistryTest, AllReturnsNonEmpty) {
    EXPECT_GE(reg_.all().size(), 20u);
}

TEST_F(NodeRegistryTest, ByCategoryMath) {
    auto math_nodes = reg_.by_category(NodeCategory::Math);
    EXPECT_EQ(math_nodes.size(), 8u);  // Add, Sub, Mul, Div, Lerp, Dot, Normalize, Power
    for (auto* info : math_nodes) {
        EXPECT_EQ(info->category, NodeCategory::Math);
    }
}

TEST_F(NodeRegistryTest, ByCategoryOutput) {
    auto out_nodes = reg_.by_category(NodeCategory::Output);
    EXPECT_EQ(out_nodes.size(), 1u);
    EXPECT_EQ(out_nodes[0]->name, "MaterialOutput");
}

TEST_F(NodeRegistryTest, ByCategoryConstant) {
    auto const_nodes = reg_.by_category(NodeCategory::Constant);
    EXPECT_EQ(const_nodes.size(), 3u);
}

TEST_F(NodeRegistryTest, ByCategoryParameter) {
    auto param_nodes = reg_.by_category(NodeCategory::Parameter);
    EXPECT_EQ(param_nodes.size(), 3u);
}

TEST_F(NodeRegistryTest, ByCategoryTexture) {
    auto tex_nodes = reg_.by_category(NodeCategory::Texture);
    EXPECT_EQ(tex_nodes.size(), 1u);
}

TEST_F(NodeRegistryTest, ByCategoryUtility) {
    auto util_nodes = reg_.by_category(NodeCategory::Utility);
    EXPECT_EQ(util_nodes.size(), 4u);
}

TEST_F(NodeRegistryTest, CreatedNodeHasCorrectTypeName) {
    auto node = reg_.create("TextureSample");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->type_name, "TextureSample");
}

TEST_F(NodeRegistryTest, RegisterOverwriteExisting) {
    // Re-register an existing node type — should overwrite.
    size_t count_before = reg_.all().size();
    reg_.register_node("Add", NodeCategory::Math,
        []() -> std::unique_ptr<ShaderNode> { return std::make_unique<AddNode>(); });
    EXPECT_EQ(reg_.all().size(), count_before);  // No duplicate added.
}
