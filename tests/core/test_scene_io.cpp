// test_scene_io.cpp — Google Test coverage for scene_loader.cpp and scene_saver.cpp.
//
// Targets ≥95% line + branch coverage of:
//   scene_load_from_string, scene_load_file,
//   scene_save_to_string, scene_save_file.
//
// Test suites: SceneLoaderTest, SceneSaverTest.
//
// No GPU, no audio hardware — all tests are self-contained.
// Scene is ~8 MB on stack, so always heap-allocate via make_unique.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "engine/scene/scene.h"
#include "engine/scene/scene_loader.h"
#include "engine/scene/scene_saver.h"

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

/// Heap-allocate and initialise a Scene.
static std::unique_ptr<Scene> make_scene(const char* name = "Test") {
    auto s = std::make_unique<Scene>();
    scene_init(s.get(), name);
    return s;
}

/// Float near-equality with 1e-4 tolerance.
static bool near_f(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) <= tol;
}

/// The default.uzscene content, inlined so the test is self-contained even if
/// the assets directory is unavailable on a CI agent.
static constexpr const char* kDefaultUzscene =
    "[uzscene]\n"
    "name = Default\n"
    "version = 1\n"
    "\n"
    "[node]\n"
    "name = World\n"
    "type = Node3D\n"
    "parent =\n"
    "position = 0.000000 0.000000 0.000000\n"
    "rotation = 0.000000 0.000000 0.000000 1.000000\n"
    "scale = 1.000000 1.000000 1.000000\n"
    "active = true\n"
    "visible = true\n"
    "\n"
    "[node]\n"
    "name = Sun\n"
    "type = DirectionalLight\n"
    "parent = World\n"
    "position = 0.000000 10.000000 0.000000\n"
    "rotation = 0.000000 0.000000 0.000000 1.000000\n"
    "scale = 1.000000 1.000000 1.000000\n"
    "active = true\n"
    "visible = true\n"
    "\n"
    "[node]\n"
    "name = MainCamera\n"
    "type = Camera\n"
    "parent = World\n"
    "position = 0.000000 5.000000 -10.000000\n"
    "rotation = 0.000000 0.000000 0.000000 1.000000\n"
    "scale = 1.000000 1.000000 1.000000\n"
    "active = true\n"
    "visible = true\n";

// ============================================================================
// SceneLoaderTest — null / empty guards
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_NullScene_ReturnsErr) {
    auto result = scene_load_from_string(nullptr, "[uzscene]\nname = X\n");
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty())
        << "Error message should be non-empty for null scene";
}

TEST(SceneLoaderTest, LoadFromString_NullText_ReturnsErr) {
    auto scene = make_scene();
    auto result = scene_load_from_string(scene.get(), nullptr);
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty())
        << "Error message should be non-empty for null text";
}

TEST(SceneLoaderTest, LoadFile_NullScene_ReturnsErr) {
    auto result = scene_load_file(nullptr, "/tmp/does_not_matter.uzscene");
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

TEST(SceneLoaderTest, LoadFile_NullPath_ReturnsErr) {
    auto scene = make_scene();
    auto result = scene_load_file(scene.get(), nullptr);
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

TEST(SceneLoaderTest, LoadFile_NonExistentFile_ReturnsErr) {
    auto scene = make_scene();
    auto result = scene_load_file(scene.get(), "/tmp/uz_nonexistent_42xyz.uzscene");
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// SceneLoaderTest — minimal valid input
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_EmptyString_OkEmptyScene) {
    auto scene = make_scene();
    auto result = scene_load_from_string(scene.get(), "");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(scene->node_count, 0u);
}

TEST(SceneLoaderTest, LoadFromString_OnlyUzsceneHeader_OkEmptyScene) {
    auto scene = make_scene();
    const char* text = "[uzscene]\nname = Test\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(scene->node_count, 0u);
}

TEST(SceneLoaderTest, LoadFromString_SingleRootNode_CreatesOneNode) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\n"
        "name = SingleNode\n"
        "[node]\n"
        "name = Root\n"
        "type = Node3D\n"
        "parent =\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(scene->node_count, 1u);
}

TEST(SceneLoaderTest, LoadFromString_SetsSceneName) {
    auto scene = make_scene();
    const char* text = "[uzscene]\nname = MyLevel\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok());
    EXPECT_STREQ(scene->name, "MyLevel");
}

TEST(SceneLoaderTest, LoadFromString_SetsSceneVersion) {
    auto scene = make_scene();
    const char* text = "[uzscene]\nname = X\nversion = 7\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(scene->version, 7u);
}

// ============================================================================
// SceneLoaderTest — node type parsing
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_NodeTypeStaticMesh_Correct) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Mesh\ntype = StaticMesh\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "Mesh");
    ASSERT_NE(id, kNodeIdInvalid);
    EXPECT_EQ(scene_get_node(scene.get(), id)->type, NodeType::StaticMesh);
}

TEST(SceneLoaderTest, LoadFromString_NodeTypeCamera_Correct) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Cam\ntype = Camera\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "Cam");
    ASSERT_NE(id, kNodeIdInvalid);
    EXPECT_EQ(scene_get_node(scene.get(), id)->type, NodeType::Camera);
}

TEST(SceneLoaderTest, LoadFromString_NodeTypeDirectionalLight_Correct) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Sun\ntype = DirectionalLight\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "Sun");
    ASSERT_NE(id, kNodeIdInvalid);
    EXPECT_EQ(scene_get_node(scene.get(), id)->type, NodeType::DirectionalLight);
}

TEST(SceneLoaderTest, LoadFromString_NodeType_UnknownType_FallsBackToEmpty) {
    // node_type_from_string returns NodeType::Empty for unknown strings.
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = FooBarBaz\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    EXPECT_EQ(scene_get_node(scene.get(), id)->type, NodeType::Empty);
}

// ============================================================================
// SceneLoaderTest — transform parsing
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_Position_ParsedCorrectly) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "position = 1.0 2.0 3.0\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.position.x, 1.0f));
    EXPECT_TRUE(near_f(n->local_transform.position.y, 2.0f));
    EXPECT_TRUE(near_f(n->local_transform.position.z, 3.0f));
}

TEST(SceneLoaderTest, LoadFromString_Scale_ParsedCorrectly) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "scale = 2.0 3.0 4.0\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.scale.x, 2.0f));
    EXPECT_TRUE(near_f(n->local_transform.scale.y, 3.0f));
    EXPECT_TRUE(near_f(n->local_transform.scale.z, 4.0f));
}

TEST(SceneLoaderTest, LoadFromString_Rotation_ParsedCorrectly) {
    auto scene = make_scene();
    // 90° rotation around Y: quat (0, 0.707, 0, 0.707)
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "rotation = 0.0 0.707 0.0 0.707\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.rotation.x, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.y, 0.707f, 1e-3f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.z, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.w, 0.707f, 1e-3f));
}

TEST(SceneLoaderTest, LoadFromString_ActiveFalse_FlagNotSet) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "active = false\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_FALSE(n->flags & NODE_FLAG_ACTIVE)
        << "NODE_FLAG_ACTIVE should NOT be set when active = false";
}

TEST(SceneLoaderTest, LoadFromString_VisibleFalse_FlagNotSet) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "visible = false\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_FALSE(n->flags & NODE_FLAG_VISIBLE)
        << "NODE_FLAG_VISIBLE should NOT be set when visible = false";
}

TEST(SceneLoaderTest, LoadFromString_ActiveTrue_FlagSet) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "active = true\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(n->flags & NODE_FLAG_ACTIVE)
        << "NODE_FLAG_ACTIVE should be set when active = true";
}

TEST(SceneLoaderTest, LoadFromString_ActiveOne_FlagSet) {
    // parse_bool also accepts "1" as truthy.
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "active = 1\n"
        "visible = 1\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(n->flags & NODE_FLAG_ACTIVE);
    EXPECT_TRUE(n->flags & NODE_FLAG_VISIBLE);
}

TEST(SceneLoaderTest, LoadFromString_MissingPosition_DefaultsToZero) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    // scene_create_node initialises with identity transform: position = (0,0,0).
    EXPECT_TRUE(near_f(n->local_transform.position.x, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.position.y, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.position.z, 0.0f));
}

TEST(SceneLoaderTest, LoadFromString_MissingRotation_DefaultsToIdentity) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    // Identity quaternion: (0, 0, 0, 1).
    EXPECT_TRUE(near_f(n->local_transform.rotation.x, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.y, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.z, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.w, 1.0f));
}

TEST(SceneLoaderTest, LoadFromString_MissingScale_DefaultsToOne) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.scale.x, 1.0f));
    EXPECT_TRUE(near_f(n->local_transform.scale.y, 1.0f));
    EXPECT_TRUE(near_f(n->local_transform.scale.z, 1.0f));
}

TEST(SceneLoaderTest, LoadFromString_MissingActive_DefaultsToActiveAndVisible) {
    // When "active" and "visible" keys are absent, the loader should default
    // both flags to ON (see flush_node: default is active + visible).
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    NodeId id = scene_find_by_name(scene.get(), "N");
    ASSERT_NE(id, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(n->flags & NODE_FLAG_ACTIVE)
        << "Default active flag should be ON when key is absent";
    EXPECT_TRUE(n->flags & NODE_FLAG_VISIBLE)
        << "Default visible flag should be ON when key is absent";
}

// ============================================================================
// SceneLoaderTest — hierarchy
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_ParentChild_CorrectParentId) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Root\ntype = Node3D\nparent =\n"
        "[node]\nname = Child\ntype = Node3D\nparent = Root\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());

    NodeId root_id  = scene_find_by_name(scene.get(), "Root");
    NodeId child_id = scene_find_by_name(scene.get(), "Child");
    ASSERT_NE(root_id,  kNodeIdInvalid);
    ASSERT_NE(child_id, kNodeIdInvalid);

    const SceneNode* child = scene_get_node(scene.get(), child_id);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->parent_id, root_id)
        << "Child's parent_id must match root's NodeId";
}

TEST(SceneLoaderTest, LoadFromString_TwoChildren_BothParented) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Parent\ntype = Node3D\nparent =\n"
        "[node]\nname = ChildA\ntype = Node3D\nparent = Parent\n"
        "[node]\nname = ChildB\ntype = Node3D\nparent = Parent\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_EQ(scene->node_count, 3u);

    NodeId parent_id = scene_find_by_name(scene.get(), "Parent");
    NodeId ca_id     = scene_find_by_name(scene.get(), "ChildA");
    NodeId cb_id     = scene_find_by_name(scene.get(), "ChildB");
    ASSERT_NE(parent_id, kNodeIdInvalid);
    ASSERT_NE(ca_id,     kNodeIdInvalid);
    ASSERT_NE(cb_id,     kNodeIdInvalid);

    EXPECT_EQ(scene_get_node(scene.get(), ca_id)->parent_id, parent_id);
    EXPECT_EQ(scene_get_node(scene.get(), cb_id)->parent_id, parent_id);

    // Parent's child list must contain both.
    const SceneNode* p = scene_get_node(scene.get(), parent_id);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->child_count, 2u);
}

TEST(SceneLoaderTest, LoadFromString_EmptyParent_IsRootNode) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Root\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());

    NodeId id = scene_find_by_name(scene.get(), "Root");
    ASSERT_NE(id, kNodeIdInvalid);
    EXPECT_EQ(scene_get_node(scene.get(), id)->parent_id, kNodeIdInvalid)
        << "Empty parent string must produce a root node (parent_id == kNodeIdInvalid)";
}

TEST(SceneLoaderTest, LoadFromString_NodeWithNoNameKey_UsesUnnamedFallback) {
    // If the "name" key is absent, the loader uses "Unnamed" as the fallback.
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_EQ(scene->node_count, 1u);
    NodeId id = scene_find_by_name(scene.get(), "Unnamed");
    EXPECT_NE(id, kNodeIdInvalid)
        << "Node with no name key must default to name 'Unnamed'";
}

// ============================================================================
// SceneLoaderTest — comments and formatting
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_HashComments_Ignored) {
    auto scene = make_scene();
    const char* text =
        "# This is a comment\n"
        "[uzscene]\n"
        "# Another comment\n"
        "name = HasComments\n"
        "[node]\n"
        "name = N\ntype = Node3D\nparent =\n"
        "# position comment\n"
        "position = 1.0 2.0 3.0\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_STREQ(scene->name, "HasComments");
    EXPECT_EQ(scene->node_count, 1u);
}

TEST(SceneLoaderTest, LoadFromString_SemicolonComments_Ignored) {
    auto scene = make_scene();
    const char* text =
        "; semicolon comment\n"
        "[uzscene]\n"
        "; another\n"
        "name = SemicolonScene\n"
        "[node]\n"
        "name = N\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_STREQ(scene->name, "SemicolonScene");
    EXPECT_EQ(scene->node_count, 1u);
}

TEST(SceneLoaderTest, LoadFromString_BlankLines_Ignored) {
    auto scene = make_scene();
    const char* text =
        "\n"
        "\n"
        "[uzscene]\n"
        "\n"
        "name = Blank\n"
        "\n"
        "[node]\n"
        "\n"
        "name = N\ntype = Node3D\nparent =\n"
        "\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_STREQ(scene->name, "Blank");
    EXPECT_EQ(scene->node_count, 1u);
}

TEST(SceneLoaderTest, LoadFromString_WhitespaceAroundEquals_Trimmed) {
    auto scene = make_scene();
    // Extra spaces around '=' and trailing spaces on the value.
    const char* text =
        "[uzscene]\n"
        "name  =  SpaceScene  \n"
        "[node]\n"
        "name  =  SpaceNode  \n"
        "type  =  Node3D  \n"
        "parent  =  \n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_STREQ(scene->name, "SpaceScene");
    NodeId id = scene_find_by_name(scene.get(), "SpaceNode");
    EXPECT_NE(id, kNodeIdInvalid)
        << "Node name with surrounding whitespace must be trimmed to 'SpaceNode'";
}

TEST(SceneLoaderTest, LoadFromString_UnknownKey_Ignored) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\n"
        "name = X\n"
        "totally_unknown_key = some_value\n"
        "[node]\n"
        "name = N\ntype = Node3D\nparent =\n"
        "unknown_node_key = 99\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok()) << "Unknown keys must be silently ignored";
    EXPECT_EQ(scene->node_count, 1u);
}

TEST(SceneLoaderTest, LoadFromString_UnknownSection_Ignored) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\n"
        "name = X\n"
        "[future_section]\n"
        "some_key = some_value\n"
        "[node]\n"
        "name = N\ntype = Node3D\nparent =\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok()) << "Unknown sections must be silently ignored";
    EXPECT_EQ(scene->node_count, 1u);
}

TEST(SceneLoaderTest, LoadFromString_LineWithoutEquals_Ignored) {
    // A line that has no '=' should be silently skipped.
    auto scene = make_scene();
    const char* text =
        "[uzscene]\n"
        "name = X\n"
        "thisisnotakeyvalue\n"
        "[node]\n"
        "name = N\ntype = Node3D\nparent =\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(scene->node_count, 1u);
}

// ============================================================================
// SceneLoaderTest — error cases
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_PositionTooFewFloats_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "position = 1.0 2.0\n";   // only 2 floats, need 3
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("position"), std::string::npos)
        << "Error message must mention 'position'";
}

TEST(SceneLoaderTest, LoadFromString_PositionTooManyFloats_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "position = 1.0 2.0 3.0 4.0\n";   // 4 floats, need exactly 3
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("position"), std::string::npos);
}

TEST(SceneLoaderTest, LoadFromString_RotationTooFewFloats_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "rotation = 0 0 0\n";   // 3 floats, need 4
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("rotation"), std::string::npos);
}

TEST(SceneLoaderTest, LoadFromString_RotationTooManyFloats_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "rotation = 0 0 0 1 0\n";   // 5 floats, need exactly 4
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("rotation"), std::string::npos);
}

TEST(SceneLoaderTest, LoadFromString_ScaleTooFewFloats_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "scale = 1.0 1.0\n";   // only 2 floats, need 3
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("scale"), std::string::npos);
}

TEST(SceneLoaderTest, LoadFromString_ScaleTooManyFloats_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "scale = 1.0 1.0 1.0 1.0\n";   // 4 floats, need exactly 3
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("scale"), std::string::npos);
}

TEST(SceneLoaderTest, LoadFromString_MalformedFloat_ReturnsErr) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = N\ntype = Node3D\nparent =\n"
        "position = abc 0 0\n";   // "abc" is not a float
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
}

TEST(SceneLoaderTest, LoadFromString_UnresolvedParent_ReturnsErr) {
    auto scene = make_scene();
    // "Child" references "DoesNotExist" which was never defined.
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Child\ntype = Node3D\nparent = DoesNotExist\n";
    auto result = scene_load_from_string(scene.get(), text);
    EXPECT_TRUE(result.is_err());
    EXPECT_NE(result.error().find("DoesNotExist"), std::string::npos)
        << "Error message must contain the missing parent name";
}

TEST(SceneLoaderTest, LoadFromString_ClearsSceneBeforeParsing) {
    // Pre-populate with 3 nodes, then load a 1-node file.
    // After loading, only 1 node must exist.
    auto scene = make_scene();
    scene_create_node(scene.get(), "Old1", NodeType::Node3D);
    scene_create_node(scene.get(), "Old2", NodeType::Node3D);
    scene_create_node(scene.get(), "Old3", NodeType::Node3D);
    ASSERT_EQ(scene->node_count, 3u);

    const char* text =
        "[uzscene]\nname = Fresh\n"
        "[node]\nname = NewRoot\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());

    EXPECT_EQ(scene->node_count, 1u)
        << "scene_load_from_string must clear the scene before parsing";
    EXPECT_NE(scene_find_by_name(scene.get(), "NewRoot"), kNodeIdInvalid);
    EXPECT_EQ(scene_find_by_name(scene.get(), "Old1"), kNodeIdInvalid);
}

// ============================================================================
// SceneLoaderTest — multiple nodes
// ============================================================================

TEST(SceneLoaderTest, LoadFromString_ThreeNodes_AllCreated) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = A\ntype = Node3D\nparent =\n"
        "[node]\nname = B\ntype = Node3D\nparent =\n"
        "[node]\nname = C\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_EQ(scene->node_count, 3u);
    EXPECT_NE(scene_find_by_name(scene.get(), "A"), kNodeIdInvalid);
    EXPECT_NE(scene_find_by_name(scene.get(), "B"), kNodeIdInvalid);
    EXPECT_NE(scene_find_by_name(scene.get(), "C"), kNodeIdInvalid);
}

TEST(SceneLoaderTest, LoadFromString_DefaultUzscene_LoadsThreeNodes) {
    // Verifies the inlined default.uzscene content produces the expected
    // scene graph: World (Node3D), Sun (DirectionalLight), MainCamera (Camera).
    auto scene = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene.get(), kDefaultUzscene).is_ok());

    EXPECT_EQ(scene->node_count, 3u);
    EXPECT_STREQ(scene->name, "Default");

    NodeId world_id  = scene_find_by_name(scene.get(), "World");
    NodeId sun_id    = scene_find_by_name(scene.get(), "Sun");
    NodeId cam_id    = scene_find_by_name(scene.get(), "MainCamera");

    ASSERT_NE(world_id, kNodeIdInvalid);
    ASSERT_NE(sun_id,   kNodeIdInvalid);
    ASSERT_NE(cam_id,   kNodeIdInvalid);

    EXPECT_EQ(scene_get_node(scene.get(), world_id)->type, NodeType::Node3D);
    EXPECT_EQ(scene_get_node(scene.get(), sun_id)->type,   NodeType::DirectionalLight);
    EXPECT_EQ(scene_get_node(scene.get(), cam_id)->type,   NodeType::Camera);

    // Sun and MainCamera must be parented to World.
    EXPECT_EQ(scene_get_node(scene.get(), sun_id)->parent_id,  world_id);
    EXPECT_EQ(scene_get_node(scene.get(), cam_id)->parent_id,  world_id);
    // World is a root node.
    EXPECT_EQ(scene_get_node(scene.get(), world_id)->parent_id, kNodeIdInvalid);
}

TEST(SceneLoaderTest, LoadFromString_DefaultUzscene_SunPosition) {
    auto scene = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene.get(), kDefaultUzscene).is_ok());
    NodeId sun_id = scene_find_by_name(scene.get(), "Sun");
    ASSERT_NE(sun_id, kNodeIdInvalid);
    const SceneNode* sun = scene_get_node(scene.get(), sun_id);
    ASSERT_NE(sun, nullptr);
    EXPECT_TRUE(near_f(sun->local_transform.position.x, 0.0f));
    EXPECT_TRUE(near_f(sun->local_transform.position.y, 10.0f));
    EXPECT_TRUE(near_f(sun->local_transform.position.z, 0.0f));
}

TEST(SceneLoaderTest, LoadFromString_DefaultUzscene_CameraPosition) {
    auto scene = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene.get(), kDefaultUzscene).is_ok());
    NodeId cam_id = scene_find_by_name(scene.get(), "MainCamera");
    ASSERT_NE(cam_id, kNodeIdInvalid);
    const SceneNode* cam = scene_get_node(scene.get(), cam_id);
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(near_f(cam->local_transform.position.x,  0.0f));
    EXPECT_TRUE(near_f(cam->local_transform.position.y,  5.0f));
    EXPECT_TRUE(near_f(cam->local_transform.position.z, -10.0f));
}

// ============================================================================
// SceneLoaderTest — LoadFile round-trip
// ============================================================================

TEST(SceneLoaderTest, LoadFile_ValidTempFile_LoadsCorrectly) {
    // Write a minimal .uzscene to /tmp and then load it from disk.
    const std::string path = "/tmp/uz_test_load_valid.uzscene";
    {
        std::ofstream f(path);
        ASSERT_TRUE(f.is_open()) << "Could not open temp file for writing";
        f << "[uzscene]\nname = FromFile\nversion = 3\n"
          << "[node]\nname = FileNode\ntype = Camera\nparent =\n"
          << "position = 10.0 20.0 30.0\n";
    }

    auto scene = make_scene();
    auto result = scene_load_file(scene.get(), path.c_str());
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_STREQ(scene->name, "FromFile");
    EXPECT_EQ(scene->version, 3u);
    EXPECT_EQ(scene->node_count, 1u);

    NodeId id = scene_find_by_name(scene.get(), "FileNode");
    ASSERT_NE(id, kNodeIdInvalid);
    EXPECT_EQ(scene_get_node(scene.get(), id)->type, NodeType::Camera);
    EXPECT_TRUE(near_f(scene_get_node(scene.get(), id)->local_transform.position.x, 10.0f));
    EXPECT_TRUE(near_f(scene_get_node(scene.get(), id)->local_transform.position.y, 20.0f));
    EXPECT_TRUE(near_f(scene_get_node(scene.get(), id)->local_transform.position.z, 30.0f));

    std::filesystem::remove(path);
}

// ============================================================================
// SceneSaverTest — null / empty guards
// ============================================================================

TEST(SceneSaverTest, SaveToString_NullScene_ReturnsEmptyString) {
    // The implementation returns {} for null — must not crash.
    std::string result = scene_save_to_string(nullptr);
    EXPECT_TRUE(result.empty())
        << "scene_save_to_string(nullptr) must return an empty string";
}

TEST(SceneSaverTest, SaveFile_NullScene_ReturnsErr) {
    auto result = scene_save_file(nullptr, "/tmp/uz_should_never_write.uzscene");
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

TEST(SceneSaverTest, SaveFile_NullPath_ReturnsErr) {
    auto scene = make_scene();
    auto result = scene_save_file(scene.get(), nullptr);
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// SceneSaverTest — basic output structure
// ============================================================================

TEST(SceneSaverTest, SaveToString_EmptyScene_ContainsUzsceneHeader) {
    auto scene = make_scene();
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("[uzscene]"), std::string::npos)
        << "Output must contain [uzscene] header";
}

TEST(SceneSaverTest, SaveToString_EmptyScene_NoNodeSections) {
    auto scene = make_scene();
    std::string out = scene_save_to_string(scene.get());
    EXPECT_EQ(out.find("[node]"), std::string::npos)
        << "Empty scene must not produce any [node] sections";
}

TEST(SceneSaverTest, SaveToString_SingleNode_ContainsNodeSection) {
    auto scene = make_scene();
    scene_create_node(scene.get(), "Root", NodeType::Node3D);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("[node]"), std::string::npos)
        << "Output for a scene with one node must contain [node]";
}

TEST(SceneSaverTest, SaveToString_SceneName_WrittenCorrectly) {
    auto scene = make_scene("MyTestLevel");
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("name = MyTestLevel"), std::string::npos)
        << "Scene name must appear in [uzscene] header as 'name = MyTestLevel'";
}

TEST(SceneSaverTest, SaveToString_NodeName_WrittenCorrectly) {
    auto scene = make_scene();
    scene_create_node(scene.get(), "SpecialNode", NodeType::Node3D);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("name = SpecialNode"), std::string::npos)
        << "Node name must appear in the [node] section";
}

TEST(SceneSaverTest, SaveToString_NodeType_WrittenCorrectly) {
    auto scene = make_scene();
    scene_create_node(scene.get(), "Cam", NodeType::Camera);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("type = Camera"), std::string::npos)
        << "Node type must appear as 'type = Camera'";
}

TEST(SceneSaverTest, SaveToString_RootNode_ParentIsEmpty) {
    auto scene = make_scene();
    scene_create_node(scene.get(), "Root", NodeType::Node3D);
    std::string out = scene_save_to_string(scene.get());
    // Root nodes have parent = (empty).
    EXPECT_NE(out.find("parent =\n"), std::string::npos)
        << "Root node must be saved as 'parent =' (empty)";
}

TEST(SceneSaverTest, SaveToString_ChildNode_ParentNameWritten) {
    auto scene = make_scene();
    NodeId parent_id = scene_create_node(scene.get(), "TheParent", NodeType::Node3D);
    scene_create_node(scene.get(), "TheChild", NodeType::Node3D, parent_id);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("parent = TheParent"), std::string::npos)
        << "Child node must be saved with 'parent = TheParent'";
}

TEST(SceneSaverTest, SaveToString_ActiveFalse_WrittenCorrectly) {
    auto scene = make_scene();
    NodeId id = scene_create_node(scene.get(), "Hidden", NodeType::Node3D);
    scene_get_node(scene.get(), id)->flags &= ~static_cast<uint16_t>(NODE_FLAG_ACTIVE);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("active = false"), std::string::npos)
        << "Inactive node must be saved as 'active = false'";
}

TEST(SceneSaverTest, SaveToString_VisibleFalse_WrittenCorrectly) {
    auto scene = make_scene();
    NodeId id = scene_create_node(scene.get(), "Invis", NodeType::Node3D);
    scene_get_node(scene.get(), id)->flags &= ~static_cast<uint16_t>(NODE_FLAG_VISIBLE);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("visible = false"), std::string::npos)
        << "Invisible node must be saved as 'visible = false'";
}

TEST(SceneSaverTest, SaveToString_ActiveTrue_WrittenCorrectly) {
    auto scene = make_scene();
    scene_create_node(scene.get(), "Active", NodeType::Node3D);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("active = true"), std::string::npos);
}

TEST(SceneSaverTest, SaveToString_VisibleTrue_WrittenCorrectly) {
    auto scene = make_scene();
    scene_create_node(scene.get(), "Vis", NodeType::Node3D);
    std::string out = scene_save_to_string(scene.get());
    EXPECT_NE(out.find("visible = true"), std::string::npos);
}

TEST(SceneSaverTest, SaveToString_ParentWithOrphanedParentId_WritesEmptyParent) {
    // When the stored parent_id does not resolve to a live node, the saver
    // must fall back to writing "parent =" (empty), not crash.
    auto scene = make_scene();
    NodeId parent_id = scene_create_node(scene.get(), "Doomed", NodeType::Node3D);
    NodeId child_id  = scene_create_node(scene.get(), "Orphan", NodeType::Node3D, parent_id);
    // Kill the parent slot directly without going through scene_destroy_node
    // so the child's parent_id still points to a dead slot.
    scene_get_node(scene.get(), parent_id)->alive = false;
    scene->node_count -= 1;

    // The saver iterates next_id slots (1..next_id), skipping dead ones.
    // The child's parent is dead, so it must fall back to "parent =".
    std::string out = scene_save_to_string(scene.get());
    // "Orphan" section must exist (child is still alive).
    EXPECT_NE(out.find("name = Orphan"), std::string::npos);
    // Its parent line should fall back to empty since the parent is dead.
    EXPECT_NE(out.find("parent =\n"), std::string::npos)
        << "Orphaned child (parent is dead) should write 'parent =' (empty)";
}

// ============================================================================
// SceneSaverTest — float precision round-trip
// ============================================================================

TEST(SceneSaverTest, SaveToString_Position_RoundTripAccurate) {
    auto scene_a = make_scene();
    NodeId id = scene_create_node(scene_a.get(), "N", NodeType::Node3D);
    scene_get_node(scene_a.get(), id)->local_transform.position = {1.5f, 2.5f, 3.5f, 0.0f};

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());
    NodeId id2 = scene_find_by_name(scene_b.get(), "N");
    ASSERT_NE(id2, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene_b.get(), id2);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.position.x, 1.5f))
        << "position.x round-trip failed";
    EXPECT_TRUE(near_f(n->local_transform.position.y, 2.5f))
        << "position.y round-trip failed";
    EXPECT_TRUE(near_f(n->local_transform.position.z, 3.5f))
        << "position.z round-trip failed";
}

TEST(SceneSaverTest, SaveToString_Rotation_RoundTripAccurate) {
    auto scene_a = make_scene();
    NodeId id = scene_create_node(scene_a.get(), "N", NodeType::Node3D);
    // 45° rotation around Z: (0, 0, 0.3827, 0.9239)
    scene_get_node(scene_a.get(), id)->local_transform.rotation = {0.0f, 0.0f, 0.3827f, 0.9239f};

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());
    NodeId id2 = scene_find_by_name(scene_b.get(), "N");
    ASSERT_NE(id2, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene_b.get(), id2);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.rotation.x, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.y, 0.0f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.z, 0.3827f, 1e-3f));
    EXPECT_TRUE(near_f(n->local_transform.rotation.w, 0.9239f, 1e-3f));
}

TEST(SceneSaverTest, SaveToString_Scale_RoundTripAccurate) {
    auto scene_a = make_scene();
    NodeId id = scene_create_node(scene_a.get(), "N", NodeType::Node3D);
    scene_get_node(scene_a.get(), id)->local_transform.scale = {0.5f, 2.0f, 3.14159f, 0.0f};

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());
    NodeId id2 = scene_find_by_name(scene_b.get(), "N");
    ASSERT_NE(id2, kNodeIdInvalid);
    const SceneNode* n = scene_get_node(scene_b.get(), id2);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(near_f(n->local_transform.scale.x, 0.5f));
    EXPECT_TRUE(near_f(n->local_transform.scale.y, 2.0f));
    EXPECT_TRUE(near_f(n->local_transform.scale.z, 3.14159f, 1e-4f));
}

// ============================================================================
// SceneSaverTest — round-trip tests
// ============================================================================

TEST(SceneSaverTest, RoundTrip_SingleNode_IdenticalScene) {
    auto scene_a = make_scene("RoundTrip");
    NodeId id_a = scene_create_node(scene_a.get(), "Solo", NodeType::StaticMesh);
    scene_get_node(scene_a.get(), id_a)->local_transform.position = {7.0f, -3.0f, 2.0f, 0.0f};

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());

    EXPECT_EQ(scene_b->node_count, 1u);
    EXPECT_STREQ(scene_b->name, "RoundTrip");

    NodeId id_b = scene_find_by_name(scene_b.get(), "Solo");
    ASSERT_NE(id_b, kNodeIdInvalid);
    const SceneNode* nb = scene_get_node(scene_b.get(), id_b);
    ASSERT_NE(nb, nullptr);
    EXPECT_EQ(nb->type, NodeType::StaticMesh);
    EXPECT_TRUE(near_f(nb->local_transform.position.x,  7.0f));
    EXPECT_TRUE(near_f(nb->local_transform.position.y, -3.0f));
    EXPECT_TRUE(near_f(nb->local_transform.position.z,  2.0f));
}

TEST(SceneSaverTest, RoundTrip_TwoNodesWithParent_IdenticalHierarchy) {
    auto scene_a = make_scene("Hierarchy");
    NodeId parent_a = scene_create_node(scene_a.get(), "TopNode", NodeType::Node3D);
    NodeId child_a  = scene_create_node(scene_a.get(), "BotNode", NodeType::Node3D, parent_a);
    (void)child_a;

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());

    EXPECT_EQ(scene_b->node_count, 2u);
    NodeId parent_b = scene_find_by_name(scene_b.get(), "TopNode");
    NodeId child_b  = scene_find_by_name(scene_b.get(), "BotNode");
    ASSERT_NE(parent_b, kNodeIdInvalid);
    ASSERT_NE(child_b,  kNodeIdInvalid);

    EXPECT_EQ(scene_get_node(scene_b.get(), child_b)->parent_id, parent_b)
        << "Child must be re-parented to 'TopNode' after round-trip";
    EXPECT_EQ(scene_get_node(scene_b.get(), parent_b)->parent_id, kNodeIdInvalid)
        << "Root node must have no parent after round-trip";
}

TEST(SceneSaverTest, RoundTrip_AllNodeTypes_Preserved) {
    auto scene_a = make_scene("Types");
    // One of each type.
    const struct { const char* name; NodeType type; } types[] = {
        { "E",  NodeType::Empty          },
        { "N",  NodeType::Node3D         },
        { "SM", NodeType::StaticMesh     },
        { "DL", NodeType::DirectionalLight},
        { "PL", NodeType::PointLight     },
        { "SL", NodeType::SpotLight      },
        { "CA", NodeType::Camera         },
        { "AS", NodeType::AudioSource    },
        { "AL", NodeType::AudioListener  },
    };
    for (const auto& t : types) {
        scene_create_node(scene_a.get(), t.name, t.type);
    }

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());
    EXPECT_EQ(scene_b->node_count, static_cast<uint32_t>(std::size(types)));

    for (const auto& t : types) {
        NodeId id = scene_find_by_name(scene_b.get(), t.name);
        ASSERT_NE(id, kNodeIdInvalid) << "Missing node: " << t.name;
        EXPECT_EQ(scene_get_node(scene_b.get(), id)->type, t.type)
            << "Type mismatch for node: " << t.name;
    }
}

TEST(SceneSaverTest, RoundTrip_ActiveFalse_Preserved) {
    auto scene_a = make_scene();
    NodeId id_a = scene_create_node(scene_a.get(), "Inactive", NodeType::Node3D);
    // Turn off active.
    scene_get_node(scene_a.get(), id_a)->flags &= ~static_cast<uint16_t>(NODE_FLAG_ACTIVE);
    // Keep visible on so we can verify only active was stored.
    scene_get_node(scene_a.get(), id_a)->flags |= NODE_FLAG_VISIBLE;

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());
    NodeId id_b = scene_find_by_name(scene_b.get(), "Inactive");
    ASSERT_NE(id_b, kNodeIdInvalid);
    const SceneNode* nb = scene_get_node(scene_b.get(), id_b);
    ASSERT_NE(nb, nullptr);
    EXPECT_FALSE(nb->flags & NODE_FLAG_ACTIVE)
        << "active = false must survive the round-trip";
    EXPECT_TRUE(nb->flags & NODE_FLAG_VISIBLE)
        << "visible = true must survive the round-trip";
}

TEST(SceneSaverTest, RoundTrip_VisibleFalse_Preserved) {
    auto scene_a = make_scene();
    NodeId id_a = scene_create_node(scene_a.get(), "Invisible", NodeType::Node3D);
    scene_get_node(scene_a.get(), id_a)->flags |=  NODE_FLAG_ACTIVE;
    scene_get_node(scene_a.get(), id_a)->flags &= ~static_cast<uint16_t>(NODE_FLAG_VISIBLE);

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());
    NodeId id_b = scene_find_by_name(scene_b.get(), "Invisible");
    ASSERT_NE(id_b, kNodeIdInvalid);
    const SceneNode* nb = scene_get_node(scene_b.get(), id_b);
    ASSERT_NE(nb, nullptr);
    EXPECT_TRUE(nb->flags  & NODE_FLAG_ACTIVE);
    EXPECT_FALSE(nb->flags & NODE_FLAG_VISIBLE)
        << "visible = false must survive the round-trip";
}

TEST(SceneSaverTest, RoundTrip_DefaultUzscene_ThreeNodesPreserved) {
    // Load default.uzscene → save → reload → compare node count and types.
    auto scene_a = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_a.get(), kDefaultUzscene).is_ok());
    ASSERT_EQ(scene_a->node_count, 3u);

    std::string text = scene_save_to_string(scene_a.get());

    auto scene_b = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene_b.get(), text.c_str()).is_ok());

    EXPECT_EQ(scene_b->node_count, 3u);
    EXPECT_STREQ(scene_b->name, "Default");

    NodeId world_b = scene_find_by_name(scene_b.get(), "World");
    NodeId sun_b   = scene_find_by_name(scene_b.get(), "Sun");
    NodeId cam_b   = scene_find_by_name(scene_b.get(), "MainCamera");

    ASSERT_NE(world_b, kNodeIdInvalid);
    ASSERT_NE(sun_b,   kNodeIdInvalid);
    ASSERT_NE(cam_b,   kNodeIdInvalid);

    EXPECT_EQ(scene_get_node(scene_b.get(), world_b)->type, NodeType::Node3D);
    EXPECT_EQ(scene_get_node(scene_b.get(), sun_b)->type,   NodeType::DirectionalLight);
    EXPECT_EQ(scene_get_node(scene_b.get(), cam_b)->type,   NodeType::Camera);

    // Hierarchy must be preserved.
    EXPECT_EQ(scene_get_node(scene_b.get(), sun_b)->parent_id,  world_b);
    EXPECT_EQ(scene_get_node(scene_b.get(), cam_b)->parent_id,  world_b);
    EXPECT_EQ(scene_get_node(scene_b.get(), world_b)->parent_id, kNodeIdInvalid);
}

// ============================================================================
// SceneSaverTest — file I/O
// ============================================================================

TEST(SceneSaverTest, SaveFile_WritesToDisk_CanBeReadBack) {
    const std::string path = "/tmp/uz_test_save_disk.uzscene";

    auto scene_a = make_scene("DiskSave");
    NodeId id_a = scene_create_node(scene_a.get(), "DiskNode", NodeType::PointLight);
    scene_get_node(scene_a.get(), id_a)->local_transform.position = {11.0f, 22.0f, 33.0f, 0.0f};

    auto save_result = scene_save_file(scene_a.get(), path.c_str());
    ASSERT_TRUE(save_result.is_ok()) << save_result.error();

    // Verify the file actually exists on disk.
    EXPECT_TRUE(std::filesystem::exists(path)) << "Saved file must exist on disk";

    // Read it back.
    auto scene_b = make_scene();
    auto load_result = scene_load_file(scene_b.get(), path.c_str());
    ASSERT_TRUE(load_result.is_ok()) << load_result.error();

    EXPECT_STREQ(scene_b->name, "DiskSave");
    EXPECT_EQ(scene_b->node_count, 1u);

    NodeId id_b = scene_find_by_name(scene_b.get(), "DiskNode");
    ASSERT_NE(id_b, kNodeIdInvalid);
    const SceneNode* nb = scene_get_node(scene_b.get(), id_b);
    ASSERT_NE(nb, nullptr);
    EXPECT_EQ(nb->type, NodeType::PointLight);
    EXPECT_TRUE(near_f(nb->local_transform.position.x, 11.0f));
    EXPECT_TRUE(near_f(nb->local_transform.position.y, 22.0f));
    EXPECT_TRUE(near_f(nb->local_transform.position.z, 33.0f));

    std::filesystem::remove(path);
}

TEST(SceneSaverTest, SaveFile_BadPath_ReturnsErr) {
    // Attempt to save to a directory that doesn't exist.
    auto scene = make_scene();
    scene_create_node(scene.get(), "N", NodeType::Node3D);
    auto result = scene_save_file(
        scene.get(),
        "/tmp/uz_nonexistent_dir_xyz/scene.uzscene");
    EXPECT_TRUE(result.is_err());
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// Editor section — saver
// ============================================================================

TEST(SceneSaverTest, EditorSection_WritesEditorCameraState) {
    auto scene = make_scene("EditorTest");

    // Set editor camera state.
    scene->editor_state.cam_pos_x = 5.5f;
    scene->editor_state.cam_pos_y = 12.3f;
    scene->editor_state.cam_pos_z = -15.7f;
    scene->editor_state.cam_yaw   = 0.45f;
    scene->editor_state.cam_pitch = -0.6f;
    scene->editor_state.cam_speed = 20.0f;
    scene->editor_state.valid     = true;

    std::string text = scene_save_to_string(scene.get());

    // The [editor] section must be present.
    EXPECT_NE(text.find("[editor]"), std::string::npos)
        << "Saved text must contain [editor] section";
    EXPECT_NE(text.find("camera_position"), std::string::npos)
        << "Saved text must contain camera_position";
    EXPECT_NE(text.find("camera_yaw"), std::string::npos)
        << "Saved text must contain camera_yaw";
    EXPECT_NE(text.find("camera_pitch"), std::string::npos)
        << "Saved text must contain camera_pitch";
    EXPECT_NE(text.find("camera_speed"), std::string::npos)
        << "Saved text must contain camera_speed";
    EXPECT_NE(text.find("5.500000"), std::string::npos)
        << "cam_pos_x value must appear in output";
}

// ============================================================================
// Editor section — loader
// ============================================================================

TEST(SceneLoaderTest, EditorSection_ParsedCorrectly) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Root\ntype = Node3D\nparent =\n"
        "[editor]\n"
        "camera_position = 5.5 12.3 -15.7\n"
        "camera_yaw = 0.45\n"
        "camera_pitch = -0.6\n"
        "camera_speed = 20.0\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_EQ(scene->node_count, 1u);

    // Check that EditorState was populated.
    EXPECT_TRUE(scene->editor_state.valid);
    EXPECT_TRUE(near_f(scene->editor_state.cam_pos_x, 5.5f));
    EXPECT_TRUE(near_f(scene->editor_state.cam_pos_y, 12.3f));
    EXPECT_TRUE(near_f(scene->editor_state.cam_pos_z, -15.7f));
    EXPECT_TRUE(near_f(scene->editor_state.cam_yaw, 0.45f));
    EXPECT_TRUE(near_f(scene->editor_state.cam_pitch, -0.6f));
    EXPECT_TRUE(near_f(scene->editor_state.cam_speed, 20.0f));
}

TEST(SceneLoaderTest, EditorSection_MissingIsOk) {
    // Old .uzscene files without [editor] should load fine with defaults.
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Root\ntype = Node3D\nparent =\n";
    ASSERT_TRUE(scene_load_from_string(scene.get(), text).is_ok());
    EXPECT_EQ(scene->node_count, 1u);
    // EditorState should NOT be marked as valid (no [editor] section).
    EXPECT_FALSE(scene->editor_state.valid);
}

TEST(SceneLoaderTest, EditorSection_UnknownKeysIgnored) {
    auto scene = make_scene();
    const char* text =
        "[uzscene]\nname = T\n"
        "[node]\nname = Root\ntype = Node3D\nparent =\n"
        "[editor]\n"
        "camera_position = 1.0 2.0 3.0\n"
        "future_key = 42\n"
        "another_new_setting = true\n";
    auto result = scene_load_from_string(scene.get(), text);
    ASSERT_TRUE(result.is_ok()) << "Unknown editor keys must be silently ignored";
    EXPECT_TRUE(scene->editor_state.valid);
    EXPECT_TRUE(near_f(scene->editor_state.cam_pos_x, 1.0f));
}

TEST(SceneLoaderTest, EditorSection_RoundTrip) {
    // Save a scene with editor state, load it back, verify camera values.
    auto scene = make_scene("RoundTrip");
    scene_create_node(scene.get(), "Root", NodeType::Node3D);
    scene->editor_state.cam_pos_x = 7.0f;
    scene->editor_state.cam_pos_y = 3.5f;
    scene->editor_state.cam_pos_z = -9.2f;
    scene->editor_state.cam_yaw   = 1.2f;
    scene->editor_state.cam_pitch = -0.3f;
    scene->editor_state.cam_speed = 15.0f;
    scene->editor_state.valid     = true;

    std::string text = scene_save_to_string(scene.get());

    // Load into a fresh scene.
    auto scene2 = make_scene();
    ASSERT_TRUE(scene_load_from_string(scene2.get(), text.c_str()).is_ok());

    EXPECT_TRUE(scene2->editor_state.valid);
    EXPECT_TRUE(near_f(scene2->editor_state.cam_pos_x, 7.0f));
    EXPECT_TRUE(near_f(scene2->editor_state.cam_pos_y, 3.5f));
    EXPECT_TRUE(near_f(scene2->editor_state.cam_pos_z, -9.2f));
    EXPECT_TRUE(near_f(scene2->editor_state.cam_yaw, 1.2f));
    EXPECT_TRUE(near_f(scene2->editor_state.cam_pitch, -0.3f));
    EXPECT_TRUE(near_f(scene2->editor_state.cam_speed, 15.0f));
}

TEST(SceneLoaderTest, SourcePath_PreservedOnLoad) {
    // Write a temp file, load it, verify source_path is set.
    const std::string path = "/tmp/uz_test_source_path.uzscene";
    {
        std::ofstream f(path);
        ASSERT_TRUE(f.is_open());
        f << "[uzscene]\nname = SP\n"
          << "[node]\nname = N\ntype = Node3D\nparent =\n";
    }

    auto scene = make_scene();
    auto result = scene_load_file(scene.get(), path.c_str());
    ASSERT_TRUE(result.is_ok()) << result.error();
    EXPECT_STREQ(scene->source_path, path.c_str());

    std::filesystem::remove(path);
}
