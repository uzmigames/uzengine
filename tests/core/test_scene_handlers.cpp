// test_scene_handlers.cpp -- Google Test coverage for scene_handlers.cpp.
//
// Targets >=95% line + branch coverage of scene_register_handlers() and all
// 8 scene.* IPC handlers: getTree, addNode, removeNode, setProperty,
// getProperty, load, save, selectNode.
//
// Scene is heap-allocated (~8 MB) via make_unique. Each test constructs a
// fresh IpcRegistry + Scene so tests are independent.

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "engine/scene/scene_handlers.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_saver.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"
#include "engine/renderer/scene/scene_params.h"
#include "common/ipc_registry.h"

using namespace uz;
using json = nlohmann::json;

// ============================================================================
// Test fixture
// ============================================================================

class SceneHandlersTest : public ::testing::Test {
protected:
    void SetUp() override {
        scene_ = std::make_unique<Scene>();
        scene_init(scene_.get(), "TestScene");
        registry_ = std::make_unique<IpcRegistry>();
        scene_register_handlers(*registry_, scene_.get());
    }

    json call(const std::string& name, const json& args = json::object()) {
        return registry_->call(name, args);
    }

    std::unique_ptr<Scene>       scene_;
    std::unique_ptr<IpcRegistry> registry_;
};

// ============================================================================
// scene.getTree
// ============================================================================

TEST_F(SceneHandlersTest, GetTree_EmptyScene_ReturnsEmptyArray) {
    auto result = call("scene.getTree");
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(SceneHandlersTest, GetTree_WithNodes_ReturnsAllAlive) {
    scene_create_node(scene_.get(), "Root", "Node3D");
    scene_create_node(scene_.get(), "Child", "Camera", 1);

    auto result = call("scene.getTree");
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 2u);

    // First node
    EXPECT_EQ(result[0]["id"], 1u);
    EXPECT_EQ(result[0]["name"], "Root");
    EXPECT_EQ(result[0]["type"], "Node3D");
    EXPECT_EQ(result[0]["parent_id"], 0u);
    EXPECT_EQ(result[0]["active"], true);
    EXPECT_EQ(result[0]["visible"], true);
    EXPECT_EQ(result[0]["position"], json({0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(result[0]["rotation"], json({0.0f, 0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(result[0]["scale"], json({1.0f, 1.0f, 1.0f}));

    // Second node
    EXPECT_EQ(result[1]["id"], 2u);
    EXPECT_EQ(result[1]["name"], "Child");
    EXPECT_EQ(result[1]["type"], "Camera");
    EXPECT_EQ(result[1]["parent_id"], 1u);
}

TEST_F(SceneHandlersTest, GetTree_SkipsDeadNodes) {
    NodeId n1 = scene_create_node(scene_.get(), "A", "Node3D");
    scene_create_node(scene_.get(), "B", "Node3D");
    scene_destroy_node(scene_.get(), n1);

    auto result = call("scene.getTree");
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "B");
}

TEST_F(SceneHandlersTest, GetTree_ShowsInactiveInvisible) {
    NodeId n1 = scene_create_node(scene_.get(), "Node", "Node3D");
    SceneNode* node = scene_get_node(scene_.get(), n1);
    node->flags = NODE_FLAG_NONE;

    auto result = call("scene.getTree");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["active"], false);
    EXPECT_EQ(result[0]["visible"], false);
}

TEST_F(SceneHandlersTest, GetTree_NullScene_ReturnsError) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.getTree", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

TEST_F(SceneHandlersTest, GetTree_ShowsCustomTransform) {
    NodeId n1 = scene_create_node(scene_.get(), "Moved", "Node3D");
    SceneNode* node = scene_get_node(scene_.get(), n1);
    node->local_transform.position = {1.0f, 2.0f, 3.0f, 0.0f};
    node->local_transform.rotation = {0.1f, 0.2f, 0.3f, 0.9f};
    node->local_transform.scale = {2.0f, 3.0f, 4.0f, 0.0f};

    auto result = call("scene.getTree");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_FLOAT_EQ(result[0]["position"][0].get<float>(), 1.0f);
    EXPECT_FLOAT_EQ(result[0]["position"][1].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(result[0]["position"][2].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(result[0]["rotation"][0].get<float>(), 0.1f);
    EXPECT_FLOAT_EQ(result[0]["rotation"][1].get<float>(), 0.2f);
    EXPECT_FLOAT_EQ(result[0]["rotation"][2].get<float>(), 0.3f);
    EXPECT_FLOAT_EQ(result[0]["rotation"][3].get<float>(), 0.9f);
    EXPECT_FLOAT_EQ(result[0]["scale"][0].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(result[0]["scale"][1].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(result[0]["scale"][2].get<float>(), 4.0f);
}

// ============================================================================
// scene.addNode
// ============================================================================

TEST_F(SceneHandlersTest, AddNode_Basic) {
    auto result = call("scene.addNode", {{"name", "MyNode"}, {"type", "Node3D"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["id"], 1u);

    // Verify the node actually exists in the scene.
    const SceneNode* node = scene_get_node(scene_.get(), 1);
    ASSERT_NE(node, nullptr);
    EXPECT_STREQ(node->name, "MyNode");
    EXPECT_STREQ(node->type_name, "Node3D");
}

TEST_F(SceneHandlersTest, AddNode_WithParent) {
    scene_create_node(scene_.get(), "Parent", "Node3D");
    auto result = call("scene.addNode", {{"name", "Child"}, {"type", "Camera"}, {"parent_id", 1}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["id"], 2u);

    const SceneNode* node = scene_get_node(scene_.get(), 2);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->parent_id, 1u);
    EXPECT_STREQ(node->type_name, "Camera");
}

TEST_F(SceneHandlersTest, AddNode_DefaultTypeIsNode3D) {
    auto result = call("scene.addNode", {{"name", "NoType"}});
    EXPECT_EQ(result["ok"], true);

    const SceneNode* node = scene_get_node(scene_.get(), result["id"].get<uint32_t>());
    ASSERT_NE(node, nullptr);
    EXPECT_STREQ(node->type_name, "Node3D");
}

TEST_F(SceneHandlersTest, AddNode_DefaultParentIsRoot) {
    auto result = call("scene.addNode", {{"name", "RootChild"}});
    EXPECT_EQ(result["ok"], true);

    const SceneNode* node = scene_get_node(scene_.get(), result["id"].get<uint32_t>());
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->parent_id, kNodeIdInvalid);
}

TEST_F(SceneHandlersTest, AddNode_MissingName_Error) {
    auto result = call("scene.addNode", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("name") != std::string::npos);
}

TEST_F(SceneHandlersTest, AddNode_InvalidNameType_Error) {
    auto result = call("scene.addNode", {{"name", 123}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("name") != std::string::npos);
}

TEST_F(SceneHandlersTest, AddNode_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.addNode", {{"name", "X"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

TEST_F(SceneHandlersTest, AddNode_AllNodeTypes) {
    const char* types[] = {
        "Empty", "Node3D", "StaticMesh", "DirectionalLight",
        "PointLight", "SpotLight", "Camera", "AudioSource", "AudioListener"
    };
    for (const auto* t : types) {
        auto result = call("scene.addNode", {{"name", t}, {"type", t}});
        EXPECT_EQ(result["ok"], true) << "Failed for type: " << t;
    }
}

// ============================================================================
// scene.removeNode
// ============================================================================

TEST_F(SceneHandlersTest, RemoveNode_Basic) {
    scene_create_node(scene_.get(), "ToDelete", "Node3D");
    auto result = call("scene.removeNode", {{"id", 1}});
    EXPECT_EQ(result["ok"], true);

    // Verify removed.
    EXPECT_EQ(scene_get_node(scene_.get(), 1), nullptr);
}

TEST_F(SceneHandlersTest, RemoveNode_InvalidId) {
    auto result = call("scene.removeNode", {{"id", 999}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "invalid id");
}

TEST_F(SceneHandlersTest, RemoveNode_IdZero) {
    auto result = call("scene.removeNode", {{"id", 0}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "invalid id");
}

TEST_F(SceneHandlersTest, RemoveNode_MissingId_Error) {
    auto result = call("scene.removeNode", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("id") != std::string::npos);
}

TEST_F(SceneHandlersTest, RemoveNode_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.removeNode", {{"id", 1}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

// ============================================================================
// scene.setProperty
// ============================================================================

TEST_F(SceneHandlersTest, SetProperty_Name) {
    scene_create_node(scene_.get(), "Old", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "name"}, {"value", "New"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_STREQ(scene_get_node(scene_.get(), 1)->name, "New");
}

TEST_F(SceneHandlersTest, SetProperty_Position) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "position"}, {"value", {1.0, 2.0, 3.0}}});
    EXPECT_EQ(result["ok"], true);

    const auto& pos = scene_get_node(scene_.get(), 1)->local_transform.position;
    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
    EXPECT_FLOAT_EQ(pos._pad, 0.0f);
}

TEST_F(SceneHandlersTest, SetProperty_Rotation) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "rotation"}, {"value", {0.1, 0.2, 0.3, 0.9}}});
    EXPECT_EQ(result["ok"], true);

    const auto& rot = scene_get_node(scene_.get(), 1)->local_transform.rotation;
    EXPECT_FLOAT_EQ(rot.x, 0.1f);
    EXPECT_FLOAT_EQ(rot.y, 0.2f);
    EXPECT_FLOAT_EQ(rot.z, 0.3f);
    EXPECT_FLOAT_EQ(rot.w, 0.9f);
}

TEST_F(SceneHandlersTest, SetProperty_Scale) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "scale"}, {"value", {2.0, 3.0, 4.0}}});
    EXPECT_EQ(result["ok"], true);

    const auto& scl = scene_get_node(scene_.get(), 1)->local_transform.scale;
    EXPECT_FLOAT_EQ(scl.x, 2.0f);
    EXPECT_FLOAT_EQ(scl.y, 3.0f);
    EXPECT_FLOAT_EQ(scl.z, 4.0f);
    EXPECT_FLOAT_EQ(scl._pad, 0.0f);
}

TEST_F(SceneHandlersTest, SetProperty_Active_True) {
    scene_create_node(scene_.get(), "N", "Node3D");
    SceneNode* node = scene_get_node(scene_.get(), 1);
    node->flags = NODE_FLAG_NONE;

    auto result = call("scene.setProperty", {{"id", 1}, {"key", "active"}, {"value", true}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(node->flags & NODE_FLAG_ACTIVE);
}

TEST_F(SceneHandlersTest, SetProperty_Active_False) {
    scene_create_node(scene_.get(), "N", "Node3D");
    SceneNode* node = scene_get_node(scene_.get(), 1);
    ASSERT_TRUE(node->flags & NODE_FLAG_ACTIVE);

    auto result = call("scene.setProperty", {{"id", 1}, {"key", "active"}, {"value", false}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_FALSE(node->flags & NODE_FLAG_ACTIVE);
}

TEST_F(SceneHandlersTest, SetProperty_Visible_True) {
    scene_create_node(scene_.get(), "N", "Node3D");
    SceneNode* node = scene_get_node(scene_.get(), 1);
    node->flags = NODE_FLAG_NONE;

    auto result = call("scene.setProperty", {{"id", 1}, {"key", "visible"}, {"value", true}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(node->flags & NODE_FLAG_VISIBLE);
}

TEST_F(SceneHandlersTest, SetProperty_Visible_False) {
    scene_create_node(scene_.get(), "N", "Node3D");
    SceneNode* node = scene_get_node(scene_.get(), 1);
    ASSERT_TRUE(node->flags & NODE_FLAG_VISIBLE);

    auto result = call("scene.setProperty", {{"id", 1}, {"key", "visible"}, {"value", false}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_FALSE(node->flags & NODE_FLAG_VISIBLE);
}

TEST_F(SceneHandlersTest, SetProperty_NodeNotFound) {
    auto result = call("scene.setProperty", {{"id", 999}, {"key", "name"}, {"value", "X"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "node not found");
}

TEST_F(SceneHandlersTest, SetProperty_UnknownKey) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "color"}, {"value", "red"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("unknown key") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_NameWrongType) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "name"}, {"value", 42}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("string") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_PositionWrongSize) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "position"}, {"value", {1.0, 2.0}}});
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, SetProperty_PositionNotArray) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "position"}, {"value", "nope"}});
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, SetProperty_RotationWrongSize) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "rotation"}, {"value", {1.0, 2.0, 3.0}}});
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, SetProperty_ScaleWrongSize) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "scale"}, {"value", {1.0}}});
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, SetProperty_ActiveWrongType) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "active"}, {"value", 1}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("boolean") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_VisibleWrongType) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "visible"}, {"value", "yes"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("boolean") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_MissingId_Error) {
    auto result = call("scene.setProperty", {{"key", "name"}, {"value", "X"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("id") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_MissingKey_Error) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"value", "X"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("key") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_MissingValue_Error) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.setProperty", {{"id", 1}, {"key", "name"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("value") != std::string::npos);
}

TEST_F(SceneHandlersTest, SetProperty_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.setProperty", {{"id", 1}, {"key", "name"}, {"value", "X"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

// ============================================================================
// scene.getProperty
// ============================================================================

TEST_F(SceneHandlersTest, GetProperty_Name) {
    scene_create_node(scene_.get(), "Hello", "Node3D");
    auto result = call("scene.getProperty", {{"id", 1}, {"key", "name"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["value"], "Hello");
}

TEST_F(SceneHandlersTest, GetProperty_Position) {
    scene_create_node(scene_.get(), "N", "Node3D");
    scene_get_node(scene_.get(), 1)->local_transform.position = {5.0f, 6.0f, 7.0f, 0.0f};

    auto result = call("scene.getProperty", {{"id", 1}, {"key", "position"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_FLOAT_EQ(result["value"][0].get<float>(), 5.0f);
    EXPECT_FLOAT_EQ(result["value"][1].get<float>(), 6.0f);
    EXPECT_FLOAT_EQ(result["value"][2].get<float>(), 7.0f);
}

TEST_F(SceneHandlersTest, GetProperty_Rotation) {
    scene_create_node(scene_.get(), "N", "Node3D");
    scene_get_node(scene_.get(), 1)->local_transform.rotation = {0.5f, 0.5f, 0.5f, 0.5f};

    auto result = call("scene.getProperty", {{"id", 1}, {"key", "rotation"}});
    EXPECT_EQ(result["ok"], true);
    ASSERT_EQ(result["value"].size(), 4u);
    EXPECT_FLOAT_EQ(result["value"][0].get<float>(), 0.5f);
    EXPECT_FLOAT_EQ(result["value"][1].get<float>(), 0.5f);
    EXPECT_FLOAT_EQ(result["value"][2].get<float>(), 0.5f);
    EXPECT_FLOAT_EQ(result["value"][3].get<float>(), 0.5f);
}

TEST_F(SceneHandlersTest, GetProperty_Scale) {
    scene_create_node(scene_.get(), "N", "Node3D");
    scene_get_node(scene_.get(), 1)->local_transform.scale = {2.0f, 3.0f, 4.0f, 0.0f};

    auto result = call("scene.getProperty", {{"id", 1}, {"key", "scale"}});
    EXPECT_EQ(result["ok"], true);
    ASSERT_EQ(result["value"].size(), 3u);
    EXPECT_FLOAT_EQ(result["value"][0].get<float>(), 2.0f);
    EXPECT_FLOAT_EQ(result["value"][1].get<float>(), 3.0f);
    EXPECT_FLOAT_EQ(result["value"][2].get<float>(), 4.0f);
}

TEST_F(SceneHandlersTest, GetProperty_Active) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.getProperty", {{"id", 1}, {"key", "active"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["value"], true);

    // Disable active and re-read.
    scene_get_node(scene_.get(), 1)->flags &= static_cast<uint16_t>(~NODE_FLAG_ACTIVE);
    result = call("scene.getProperty", {{"id", 1}, {"key", "active"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["value"], false);
}

TEST_F(SceneHandlersTest, GetProperty_Visible) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.getProperty", {{"id", 1}, {"key", "visible"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["value"], true);

    // Disable visible and re-read.
    scene_get_node(scene_.get(), 1)->flags &= static_cast<uint16_t>(~NODE_FLAG_VISIBLE);
    result = call("scene.getProperty", {{"id", 1}, {"key", "visible"}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["value"], false);
}

TEST_F(SceneHandlersTest, GetProperty_NodeNotFound) {
    auto result = call("scene.getProperty", {{"id", 999}, {"key", "name"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "node not found");
}

TEST_F(SceneHandlersTest, GetProperty_UnknownKey) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.getProperty", {{"id", 1}, {"key", "color"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("unknown key") != std::string::npos);
}

TEST_F(SceneHandlersTest, GetProperty_MissingId_Error) {
    auto result = call("scene.getProperty", {{"key", "name"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("id") != std::string::npos);
}

TEST_F(SceneHandlersTest, GetProperty_MissingKey_Error) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.getProperty", {{"id", 1}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("key") != std::string::npos);
}

TEST_F(SceneHandlersTest, GetProperty_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.getProperty", {{"id", 1}, {"key", "name"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

// ============================================================================
// scene.load
// ============================================================================

TEST_F(SceneHandlersTest, Load_NonexistentFile_Error) {
    auto result = call("scene.load", {{"path", "/nonexistent/path/test.uzscene"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result.contains("error"));
    EXPECT_FALSE(result["error"].get<std::string>().empty());
}

TEST_F(SceneHandlersTest, Load_MissingPath_Error) {
    auto result = call("scene.load", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("path") != std::string::npos);
}

TEST_F(SceneHandlersTest, Load_InvalidPathType_Error) {
    auto result = call("scene.load", {{"path", 42}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("path") != std::string::npos);
}

TEST_F(SceneHandlersTest, Load_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.load", {{"path", "test.uzscene"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

// ============================================================================
// scene.save
// ============================================================================

TEST_F(SceneHandlersTest, Save_ValidFile) {
    scene_create_node(scene_.get(), "Root", "Node3D");

    // Save to a temp file.
    const std::string path = "/tmp/test_scene_handlers_save.uzscene";
    auto result = call("scene.save", {{"path", path}});
    EXPECT_EQ(result["ok"], true);

    // Verify the file was actually written by loading it back.
    auto scene2 = std::make_unique<Scene>();
    scene_init(scene2.get(), "Verify");
    auto load_result = scene_load_file(scene2.get(), path.c_str());
    EXPECT_TRUE(load_result.is_ok());
    EXPECT_EQ(scene2->node_count, 1u);
}

TEST_F(SceneHandlersTest, Save_InvalidPath_Error) {
    auto result = call("scene.save", {{"path", "/nonexistent_dir_xyz/nofile.uzscene"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result.contains("error"));
}

TEST_F(SceneHandlersTest, Save_MissingPath_Error) {
    auto result = call("scene.save", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("path") != std::string::npos);
}

TEST_F(SceneHandlersTest, Save_InvalidPathType_Error) {
    auto result = call("scene.save", {{"path", 42}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("path") != std::string::npos);
}

TEST_F(SceneHandlersTest, Save_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.save", {{"path", "/tmp/x.uzscene"}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

// ============================================================================
// scene.selectNode
// ============================================================================

TEST_F(SceneHandlersTest, SelectNode_ValidNode) {
    scene_create_node(scene_.get(), "Target", "Node3D");
    auto result = call("scene.selectNode", {{"id", 1}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["id"], 1u);
    EXPECT_EQ(result["name"], "Target");
}

TEST_F(SceneHandlersTest, SelectNode_Deselect) {
    auto result = call("scene.selectNode", {{"id", 0}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["id"], 0u);
    // No "name" field expected for deselect.
    EXPECT_FALSE(result.contains("name"));
}

TEST_F(SceneHandlersTest, SelectNode_NotFound) {
    auto result = call("scene.selectNode", {{"id", 999}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "not found");
}

TEST_F(SceneHandlersTest, SelectNode_MissingId_Error) {
    auto result = call("scene.selectNode", json::object());
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("id") != std::string::npos);
}

TEST_F(SceneHandlersTest, SelectNode_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.selectNode", {{"id", 1}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

// ============================================================================
// Handler registration
// ============================================================================

TEST_F(SceneHandlersTest, AllHandlersRegistered) {
    EXPECT_TRUE(registry_->has("scene.getTree"));
    EXPECT_TRUE(registry_->has("scene.addNode"));
    EXPECT_TRUE(registry_->has("scene.removeNode"));
    EXPECT_TRUE(registry_->has("scene.setProperty"));
    EXPECT_TRUE(registry_->has("scene.getProperty"));
    EXPECT_TRUE(registry_->has("scene.load"));
    EXPECT_TRUE(registry_->has("scene.save"));
    EXPECT_TRUE(registry_->has("scene.selectNode"));
    EXPECT_TRUE(registry_->has("scene.reparentNode"));
}

TEST_F(SceneHandlersTest, UnknownHandler_Error) {
    auto result = call("scene.doesNotExist");
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("not found") != std::string::npos);
}

// ============================================================================
// Round-trip: addNode -> getTree -> setProperty -> getProperty
// ============================================================================

TEST_F(SceneHandlersTest, RoundTrip_AddSetGet) {
    // Add a node.
    auto add_result = call("scene.addNode", {{"name", "Player"}, {"type", "Node3D"}});
    ASSERT_EQ(add_result["ok"], true);
    uint32_t id = add_result["id"].get<uint32_t>();

    // Set properties.
    call("scene.setProperty", {{"id", id}, {"key", "position"}, {"value", {10.0, 20.0, 30.0}}});
    call("scene.setProperty", {{"id", id}, {"key", "rotation"}, {"value", {0.0, 0.707, 0.0, 0.707}}});
    call("scene.setProperty", {{"id", id}, {"key", "scale"}, {"value", {2.0, 2.0, 2.0}}});
    call("scene.setProperty", {{"id", id}, {"key", "name"}, {"value", "Renamed"}});
    call("scene.setProperty", {{"id", id}, {"key", "active"}, {"value", false}});
    call("scene.setProperty", {{"id", id}, {"key", "visible"}, {"value", false}});

    // Get properties and verify.
    auto name_r = call("scene.getProperty", {{"id", id}, {"key", "name"}});
    EXPECT_EQ(name_r["value"], "Renamed");

    auto pos_r = call("scene.getProperty", {{"id", id}, {"key", "position"}});
    EXPECT_FLOAT_EQ(pos_r["value"][0].get<float>(), 10.0f);

    auto rot_r = call("scene.getProperty", {{"id", id}, {"key", "rotation"}});
    EXPECT_FLOAT_EQ(rot_r["value"][1].get<float>(), 0.707f);

    auto scl_r = call("scene.getProperty", {{"id", id}, {"key", "scale"}});
    EXPECT_FLOAT_EQ(scl_r["value"][0].get<float>(), 2.0f);

    auto act_r = call("scene.getProperty", {{"id", id}, {"key", "active"}});
    EXPECT_EQ(act_r["value"], false);

    auto vis_r = call("scene.getProperty", {{"id", id}, {"key", "visible"}});
    EXPECT_EQ(vis_r["value"], false);

    // Verify getTree reflects changes.
    auto tree = call("scene.getTree");
    ASSERT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree[0]["name"], "Renamed");
    EXPECT_EQ(tree[0]["active"], false);
    EXPECT_EQ(tree[0]["visible"], false);
}

// ============================================================================
// Round-trip: save -> load -> getTree
// ============================================================================

TEST_F(SceneHandlersTest, RoundTrip_SaveLoad) {
    // Set up a small scene.
    scene_create_node(scene_.get(), "Root", "Node3D");
    NodeId child = scene_create_node(scene_.get(), "Child", "Camera", 1);
    scene_get_node(scene_.get(), child)->local_transform.position = {1.0f, 2.0f, 3.0f, 0.0f};

    const std::string path = "/tmp/test_scene_handlers_roundtrip.uzscene";

    // Save.
    auto save_r = call("scene.save", {{"path", path}});
    ASSERT_EQ(save_r["ok"], true);

    // Clear the scene.
    scene_clear(scene_.get());
    EXPECT_EQ(scene_->node_count, 0u);

    // Load.
    auto load_r = call("scene.load", {{"path", path}});
    ASSERT_EQ(load_r["ok"], true);

    // Verify via getTree.
    auto tree = call("scene.getTree");
    ASSERT_EQ(tree.size(), 2u);
    EXPECT_EQ(tree[0]["name"], "Root");
    EXPECT_EQ(tree[1]["name"], "Child");
    EXPECT_EQ(tree[1]["type"], "Camera");
}

// ============================================================================
// scene.reparentNode
// ============================================================================

TEST_F(SceneHandlersTest, ReparentNode_Basic) {
    // Create: Root (1) and Child (2) at root level.
    scene_create_node(scene_.get(), "Root", "Node3D");
    scene_create_node(scene_.get(), "Child", "Node3D");

    // Verify Child is at root.
    ASSERT_EQ(scene_get_node(scene_.get(), 2)->parent_id, kNodeIdInvalid);

    // Reparent Child under Root.
    auto result = call("scene.reparentNode", {{"id", 2}, {"newParentId", 1}});
    EXPECT_EQ(result["ok"], true);

    // Verify parent changed.
    EXPECT_EQ(scene_get_node(scene_.get(), 2)->parent_id, 1u);
}

TEST_F(SceneHandlersTest, ReparentNode_ToRoot) {
    // Root (1) with Child (2).
    scene_create_node(scene_.get(), "Root", "Node3D");
    scene_create_node(scene_.get(), "Child", "Node3D", 1);
    ASSERT_EQ(scene_get_node(scene_.get(), 2)->parent_id, 1u);

    // Move Child to root level.
    auto result = call("scene.reparentNode", {{"id", 2}, {"newParentId", 0}});
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(scene_get_node(scene_.get(), 2)->parent_id, kNodeIdInvalid);
}

TEST_F(SceneHandlersTest, ReparentNode_CircularReference_Fails) {
    // A (1) -> B (2) -> C (3). Try to reparent A under C → should fail.
    scene_create_node(scene_.get(), "A", "Node3D");
    scene_create_node(scene_.get(), "B", "Node3D", 1);
    scene_create_node(scene_.get(), "C", "Node3D", 2);

    auto result = call("scene.reparentNode", {{"id", 1}, {"newParentId", 3}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("circular") != std::string::npos ||
                result["error"].get<std::string>().find("reparent failed") != std::string::npos);
}

TEST_F(SceneHandlersTest, ReparentNode_InvalidNodeId_Fails) {
    auto result = call("scene.reparentNode", {{"id", 999}, {"newParentId", 0}});
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, ReparentNode_InvalidParentId_Fails) {
    scene_create_node(scene_.get(), "A", "Node3D");
    auto result = call("scene.reparentNode", {{"id", 1}, {"newParentId", 999}});
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, ReparentNode_MissingId_Error) {
    auto result = call("scene.reparentNode", {{"newParentId", 0}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("id") != std::string::npos);
}

TEST_F(SceneHandlersTest, ReparentNode_MissingNewParentId_Error) {
    scene_create_node(scene_.get(), "A", "Node3D");
    auto result = call("scene.reparentNode", {{"id", 1}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("newParentId") != std::string::npos);
}

TEST_F(SceneHandlersTest, ReparentNode_NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("scene.reparentNode", {{"id", 1}, {"newParentId", 0}});
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

TEST_F(SceneHandlersTest, ReparentNode_IsRegistered) {
    EXPECT_TRUE(registry_->has("scene.reparentNode"));
}

// ============================================================================
// scene.assignAsset
// ============================================================================

TEST_F(SceneHandlersTest, AssignAsset_Mesh_CreatesStaticMeshComponent) {
    scene_create_node(scene_.get(), "Box", "Node3D");
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://meshes/cube.glb"}, {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["componentAdded"], "StaticMeshData");

    // Verify the node type changed.
    auto* node = scene_get_node(scene_.get(), 1);
    ASSERT_NE(node, nullptr);
    EXPECT_STREQ(node->type_name, "StaticMesh");

    // Verify mesh_path is set.
    auto* mesh = component_get<StaticMeshData>(&scene_->components, 1);
    ASSERT_NE(mesh, nullptr);
    EXPECT_STREQ(mesh->mesh_path, "res://meshes/cube.glb");
}

TEST_F(SceneHandlersTest, AssignAsset_Mesh_ExistingStaticMesh_UpdatesPath) {
    scene_create_node(scene_.get(), "Mesh", "Node3D");

    // First call creates the component.
    auto first = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "old.glb"}, {"assetType", "Mesh"}
    });
    ASSERT_EQ(first["ok"], true);
    ASSERT_EQ(first["componentAdded"], "StaticMeshData");

    // Second call updates the existing component — no new component added.
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://meshes/new.glb"}, {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(result["componentAdded"].is_null());
}

TEST_F(SceneHandlersTest, AssignAsset_Material_SetsPath) {
    NodeId id = scene_create_node(scene_.get(), "Box", "Node3D");

    // First assign a mesh so the component exists.
    auto setup = call("scene.assignAsset", {
        {"nodeId", id}, {"assetPath", "res://meshes/box.glb"}, {"assetType", "Mesh"}
    });
    ASSERT_EQ(setup["ok"], true);

    // Now assign a material — should update the existing component.
    auto result = call("scene.assignAsset", {
        {"nodeId", id}, {"assetPath", "res://materials/stone.uzmat"}, {"assetType", "Material"}
    });
    EXPECT_EQ(result["ok"], true);
}

TEST_F(SceneHandlersTest, AssignAsset_Material_CreatesComponent) {
    scene_create_node(scene_.get(), "Empty", "Node3D");
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://mat.uzmat"}, {"assetType", "Material"}
    });
    EXPECT_EQ(result["ok"], true);
    EXPECT_EQ(result["componentAdded"], "StaticMeshData");

    auto* node = scene_get_node(scene_.get(), 1);
    EXPECT_STREQ(node->type_name, "StaticMesh");
}

TEST_F(SceneHandlersTest, AssignAsset_Texture_Acknowledged) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://tex.png"}, {"assetType", "Texture"}
    });
    EXPECT_EQ(result["ok"], true);
}

TEST_F(SceneHandlersTest, AssignAsset_UnsupportedType_Error) {
    scene_create_node(scene_.get(), "N", "Node3D");
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://x"}, {"assetType", "AudioClip"}
    });
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result["error"].get<std::string>().find("unsupported") != std::string::npos);
}

TEST_F(SceneHandlersTest, AssignAsset_InvalidNodeId_Error) {
    auto result = call("scene.assignAsset", {
        {"nodeId", 999}, {"assetPath", "res://x"}, {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "node not found");
}

TEST_F(SceneHandlersTest, AssignAsset_MissingNodeId_Error) {
    auto result = call("scene.assignAsset", {
        {"assetPath", "res://x"}, {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, AssignAsset_MissingAssetPath_Error) {
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, AssignAsset_MissingAssetType_Error) {
    auto result = call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://x"}
    });
    EXPECT_EQ(result["ok"], false);
}

TEST_F(SceneHandlersTest, AssignAsset_NullScene_Error) {
    IpcRegistry nullReg;
    scene_register_handlers(nullReg, nullptr);
    auto result = nullReg.call("scene.assignAsset", {
        {"nodeId", 1}, {"assetPath", "res://x"}, {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

TEST_F(SceneHandlersTest, AssignAsset_IsRegistered) {
    EXPECT_TRUE(registry_->has("scene.assignAsset"));
}

// ============================================================================
// editor.placeAsset
// ============================================================================

class PlaceAssetTest : public ::testing::Test {
protected:
    void SetUp() override {
        scene_ = std::make_unique<Scene>();
        scene_init(scene_.get(), "TestScene");
        params_ = std::make_unique<SceneParams>();
        params_->camPosX = 0.0f;
        params_->camPosY = 5.0f;
        params_->camPosZ = 0.0f;
        params_->camYaw  = 0.0f;
        params_->camPitch = 0.0f;
        registry_ = std::make_unique<IpcRegistry>();
        scene_register_handlers(*registry_, scene_.get(), params_.get());
    }

    json call(const std::string& name, const json& args = json::object()) {
        return registry_->call(name, args);
    }

    std::unique_ptr<Scene>       scene_;
    std::unique_ptr<SceneParams> params_;
    std::unique_ptr<IpcRegistry> registry_;
};

TEST_F(PlaceAssetTest, IsRegistered) {
    EXPECT_TRUE(registry_->has("editor.placeAsset"));
}

TEST_F(PlaceAssetTest, PlaceMesh_CreatesNodeWithMeshComponent) {
    auto result = call("editor.placeAsset", {
        {"resPath", "res://meshes/cube.glb"},
        {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], true);
    EXPECT_TRUE(result.contains("nodeId"));
    EXPECT_TRUE(result.contains("worldPos"));

    NodeId id = result["nodeId"].get<uint32_t>();
    EXPECT_NE(id, kNodeIdInvalid);

    // Verify node was created.
    const SceneNode* node = scene_get_node(scene_.get(), id);
    ASSERT_NE(node, nullptr);
    EXPECT_STREQ(node->type_name, "StaticMesh");

    // Verify mesh_path was set.
    const auto* mesh = component_get<StaticMeshData>(&scene_->components, id);
    ASSERT_NE(mesh, nullptr);
    EXPECT_STREQ(mesh->mesh_path, "res://meshes/cube.glb");
}

TEST_F(PlaceAssetTest, PlaceMaterial_SetsMaterialPath) {
    auto result = call("editor.placeAsset", {
        {"resPath", "res://materials/metal.uzmat"},
        {"assetType", "Material"}
    });
    EXPECT_EQ(result["ok"], true);

    NodeId id = result["nodeId"].get<uint32_t>();
    const auto* mesh = component_get<StaticMeshData>(&scene_->components, id);
    ASSERT_NE(mesh, nullptr);
    EXPECT_STREQ(mesh->material_path, "res://materials/metal.uzmat");
}

TEST_F(PlaceAssetTest, PlacementPosition_CameraForwardTimesFive) {
    // Camera at (0,5,0), yaw=0, pitch=0 → forward is (0,0,1) → placement at (0,5,5).
    auto result = call("editor.placeAsset", {
        {"resPath", "res://meshes/box.glb"},
        {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], true);

    auto wp = result["worldPos"];
    ASSERT_TRUE(wp.is_array());
    EXPECT_NEAR(wp[0].get<float>(), 0.0f, 0.01f);
    EXPECT_NEAR(wp[1].get<float>(), 5.0f, 0.01f);
    EXPECT_NEAR(wp[2].get<float>(), 5.0f, 0.01f);

    // Also verify the node's position matches.
    NodeId id = result["nodeId"].get<uint32_t>();
    const SceneNode* node = scene_get_node(scene_.get(), id);
    ASSERT_NE(node, nullptr);
    EXPECT_NEAR(node->local_transform.position.x, 0.0f, 0.01f);
    EXPECT_NEAR(node->local_transform.position.y, 5.0f, 0.01f);
    EXPECT_NEAR(node->local_transform.position.z, 5.0f, 0.01f);
}

TEST_F(PlaceAssetTest, DisplayName_ExtractedFromResPath) {
    auto result = call("editor.placeAsset", {
        {"resPath", "res://meshes/my_house.glb"},
        {"assetType", "Mesh"}
    });
    NodeId id = result["nodeId"].get<uint32_t>();
    const SceneNode* node = scene_get_node(scene_.get(), id);
    ASSERT_NE(node, nullptr);
    EXPECT_STREQ(node->name, "my_house");
}

TEST_F(PlaceAssetTest, MissingResPath_Error) {
    auto result = call("editor.placeAsset", {
        {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
    EXPECT_TRUE(result.contains("error"));
}

TEST_F(PlaceAssetTest, MissingAssetType_Error) {
    auto result = call("editor.placeAsset", {
        {"resPath", "res://meshes/x.glb"}
    });
    EXPECT_EQ(result["ok"], false);
}

TEST_F(PlaceAssetTest, EmptyResPath_Error) {
    auto result = call("editor.placeAsset", {
        {"resPath", ""},
        {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
}

TEST_F(PlaceAssetTest, NullScene_Error) {
    IpcRegistry reg;
    scene_register_handlers(reg, nullptr);
    auto result = reg.call("editor.placeAsset", {
        {"resPath", "res://x.glb"},
        {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], false);
    EXPECT_EQ(result["error"], "no scene");
}

TEST_F(PlaceAssetTest, NoParams_PlacesAtOrigin) {
    // Register without params → position should be (0,0,0).
    IpcRegistry reg;
    auto scene2 = std::make_unique<Scene>();
    scene_init(scene2.get(), "TestScene2");
    scene_register_handlers(reg, scene2.get(), nullptr);

    auto result = reg.call("editor.placeAsset", {
        {"resPath", "res://meshes/cube.glb"},
        {"assetType", "Mesh"}
    });
    EXPECT_EQ(result["ok"], true);

    auto wp = result["worldPos"];
    EXPECT_NEAR(wp[0].get<float>(), 0.0f, 0.01f);
    EXPECT_NEAR(wp[1].get<float>(), 0.0f, 0.01f);
    EXPECT_NEAR(wp[2].get<float>(), 0.0f, 0.01f);
}

TEST_F(PlaceAssetTest, UnsupportedType_CreatesNodeWithoutComponent) {
    auto result = call("editor.placeAsset", {
        {"resPath", "res://audio/hit.wav"},
        {"assetType", "Audio"}
    });
    EXPECT_EQ(result["ok"], true);

    NodeId id = result["nodeId"].get<uint32_t>();
    const SceneNode* node = scene_get_node(scene_.get(), id);
    ASSERT_NE(node, nullptr);
    // No StaticMeshData for Audio type.
    const auto* mesh = component_get<StaticMeshData>(&scene_->components, id);
    EXPECT_EQ(mesh, nullptr);
}
