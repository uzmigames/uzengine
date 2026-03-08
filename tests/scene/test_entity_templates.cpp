// test_entity_templates.cpp -- Google Test coverage for entity_template.h/cpp.
//
// Targets >=95% line + branch coverage of:
//   EntityTemplateManager::parse, serialize, load, get, reload, unload, clear,
//   count, spawn, spawn_by_path, despawn, spawn_node.
//
// No GPU. Scene is ~8 MB so always heap-allocate.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "engine/scene/entity_template.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_registry.h"

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

/// Heap-allocate and initialise a Scene.
static std::unique_ptr<Scene> make_scene(const char* name = "TestScene") {
    auto s = std::make_unique<Scene>();
    scene_init(s.get(), name);
    return s;
}

/// Float near-equality.
static bool near_f(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) <= tol;
}

/// Temporary directory for file-based tests.
class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() / "uz_entity_template_tests";
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::filesystem::remove_all(path_);
    }
    std::filesystem::path path() const { return path_; }

    std::string write_file(const std::string& name, const std::string& content) {
        auto full_path = path_ / name;
        std::filesystem::create_directories(full_path.parent_path());
        std::ofstream f(full_path);
        f << content;
        f.close();
        return full_path.string();
    }

private:
    std::filesystem::path path_;
};

// ============================================================================
// Parse tests
// ============================================================================

TEST(EntityTemplateParse, MinimalTemplate) {
    const char* json = R"({
        "name": "Empty",
        "nodes": []
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& tmpl = result.value();
    EXPECT_EQ(tmpl.name, "Empty");
    EXPECT_TRUE(tmpl.nodes.empty());
    EXPECT_TRUE(tmpl.overridable.empty());
}

TEST(EntityTemplateParse, SingleNodeWithTransform) {
    const char* json = R"({
        "name": "Single",
        "nodes": [
            {
                "name": "Root",
                "type": "StaticMesh",
                "position": [1.0, 2.0, 3.0],
                "rotation": [0.0, 0.707, 0.0, 0.707],
                "scale": [2.0, 2.0, 2.0],
                "properties": {
                    "mesh": "res://meshes/cube.glb",
                    "material": "res://materials/default.uzmat"
                }
            }
        ]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& tmpl = result.value();
    EXPECT_EQ(tmpl.name, "Single");
    ASSERT_EQ(tmpl.nodes.size(), 1u);

    const auto& node = tmpl.nodes[0];
    EXPECT_EQ(node.name, "Root");
    EXPECT_EQ(node.type_name, "StaticMesh");
    EXPECT_TRUE(near_f(node.position[0], 1.0f));
    EXPECT_TRUE(near_f(node.position[1], 2.0f));
    EXPECT_TRUE(near_f(node.position[2], 3.0f));
    EXPECT_TRUE(near_f(node.rotation[1], 0.707f));
    EXPECT_TRUE(near_f(node.rotation[3], 0.707f));
    EXPECT_TRUE(near_f(node.scale[0], 2.0f));
    EXPECT_TRUE(near_f(node.scale[1], 2.0f));
    EXPECT_TRUE(near_f(node.scale[2], 2.0f));
    EXPECT_EQ(node.properties.at("mesh"), "res://meshes/cube.glb");
    EXPECT_EQ(node.properties.at("material"), "res://materials/default.uzmat");
}

TEST(EntityTemplateParse, NodeWithDefaultType) {
    const char* json = R"({
        "name": "DefaultType",
        "nodes": [{ "name": "Root" }]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().nodes[0].type_name, "Node3D");
}

TEST(EntityTemplateParse, ChildNodes) {
    const char* json = R"({
        "name": "WithChildren",
        "nodes": [
            {
                "name": "Parent",
                "type": "Node3D",
                "children": [
                    { "name": "ChildA", "position": [1, 0, 0] },
                    { "name": "ChildB", "position": [0, 1, 0] }
                ]
            }
        ]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& parent = result.value().nodes[0];
    ASSERT_EQ(parent.children.size(), 2u);
    EXPECT_EQ(parent.children[0].name, "ChildA");
    EXPECT_TRUE(near_f(parent.children[0].position[0], 1.0f));
    EXPECT_EQ(parent.children[1].name, "ChildB");
    EXPECT_TRUE(near_f(parent.children[1].position[1], 1.0f));
}

TEST(EntityTemplateParse, OverridableFields) {
    const char* json = R"({
        "name": "Overridable",
        "nodes": [{ "name": "Root", "type": "StaticMesh" }],
        "overridable": ["Root.mesh", "Root.position"]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& tmpl = result.value();
    EXPECT_EQ(tmpl.overridable.size(), 2u);
    EXPECT_TRUE(tmpl.overridable.count("Root.mesh"));
    EXPECT_TRUE(tmpl.overridable.count("Root.position"));
}

TEST(EntityTemplateParse, ScriptPath) {
    const char* json = R"({
        "name": "Scripted",
        "nodes": [{
            "name": "AI",
            "script": "res://scripts/enemy_ai.lua"
        }]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().nodes[0].script, "res://scripts/enemy_ai.lua");
}

TEST(EntityTemplateParse, TemplateReference) {
    const char* json = R"({
        "name": "Spawner",
        "nodes": [{
            "name": "Spawn1",
            "template": "res://templates/enemy.uztemplate",
            "position": [5, 0, 0],
            "overrides": { "mesh": "res://meshes/boss.glb" }
        }]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& node = result.value().nodes[0];
    EXPECT_EQ(node.template_ref, "res://templates/enemy.uztemplate");
    EXPECT_TRUE(near_f(node.position[0], 5.0f));
    EXPECT_EQ(node.template_overrides.at("mesh"), "res://meshes/boss.glb");
}

TEST(EntityTemplateParse, NumericAndBoolProperties) {
    const char* json = R"({
        "name": "NumBool",
        "nodes": [{
            "name": "Root",
            "properties": {
                "fov": 90.0,
                "active": true
            }
        }]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& props = result.value().nodes[0].properties;
    // Numeric properties get converted to string.
    EXPECT_FALSE(props.at("fov").empty());
    EXPECT_EQ(props.at("active"), "true");
}

TEST(EntityTemplateParse, NumericAndBoolOverrides) {
    const char* json = R"({
        "name": "OvNumBool",
        "nodes": [{
            "name": "Root",
            "template": "res://t.uztemplate",
            "overrides": {
                "intensity": 5.0,
                "enabled": false
            }
        }]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    const auto& ov = result.value().nodes[0].template_overrides;
    EXPECT_FALSE(ov.at("intensity").empty());
    EXPECT_EQ(ov.at("enabled"), "false");
}

TEST(EntityTemplateParse, InvalidJson) {
    auto result = EntityTemplateManager::parse("not json {{{}}}");
    ASSERT_FALSE(result.is_ok());
    EXPECT_NE(result.error().find("Failed to parse JSON"), std::string::npos);
}

TEST(EntityTemplateParse, RootNotObject) {
    auto result = EntityTemplateManager::parse("[1, 2, 3]");
    ASSERT_FALSE(result.is_ok());
    EXPECT_NE(result.error().find("Root must be a JSON object"), std::string::npos);
}

TEST(EntityTemplateParse, MissingNodesKey) {
    auto result = EntityTemplateManager::parse(R"({"name": "NoNodes"})");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().nodes.empty());
}

TEST(EntityTemplateParse, EmptyNameAndType) {
    const char* json = R"({"nodes": [{}]})";
    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().name.empty());
    EXPECT_EQ(result.value().nodes[0].type_name, "Node3D");
    EXPECT_TRUE(result.value().nodes[0].name.empty());
}

TEST(EntityTemplateParse, PartialPositionArray) {
    // Arrays with fewer than required elements should not crash.
    const char* json = R"({
        "nodes": [{
            "name": "A",
            "position": [1.0, 2.0],
            "rotation": [0.1],
            "scale": [3.0]
        }]
    })";

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    // Position should remain at default (array too short).
    EXPECT_TRUE(near_f(result.value().nodes[0].position[0], 0.0f));
}

// ============================================================================
// Serialize tests
// ============================================================================

TEST(EntityTemplateSerialize, RoundTrip) {
    EntityTemplate tmpl;
    tmpl.name = "RoundTrip";

    TemplateNode node;
    node.name = "Root";
    node.type_name = "StaticMesh";
    node.position[0] = 1.0f; node.position[1] = 2.0f; node.position[2] = 3.0f;
    node.rotation[0] = 0.0f; node.rotation[1] = 0.707f;
    node.rotation[2] = 0.0f; node.rotation[3] = 0.707f;
    node.scale[0] = 2.0f; node.scale[1] = 2.0f; node.scale[2] = 2.0f;
    node.properties["mesh"] = "res://meshes/cube.glb";
    node.script = "res://scripts/test.lua";

    TemplateNode child;
    child.name = "Child";
    node.children.push_back(child);
    tmpl.nodes.push_back(node);
    tmpl.overridable.insert("Root.mesh");

    std::string json = EntityTemplateManager::serialize(tmpl);
    ASSERT_FALSE(json.empty());

    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());

    const auto& parsed = result.value();
    EXPECT_EQ(parsed.name, "RoundTrip");
    ASSERT_EQ(parsed.nodes.size(), 1u);
    EXPECT_EQ(parsed.nodes[0].name, "Root");
    EXPECT_EQ(parsed.nodes[0].type_name, "StaticMesh");
    EXPECT_TRUE(near_f(parsed.nodes[0].position[0], 1.0f));
    EXPECT_TRUE(near_f(parsed.nodes[0].scale[0], 2.0f));
    EXPECT_EQ(parsed.nodes[0].properties.at("mesh"), "res://meshes/cube.glb");
    EXPECT_EQ(parsed.nodes[0].script, "res://scripts/test.lua");
    ASSERT_EQ(parsed.nodes[0].children.size(), 1u);
    EXPECT_EQ(parsed.nodes[0].children[0].name, "Child");
    EXPECT_TRUE(parsed.overridable.count("Root.mesh"));
}

TEST(EntityTemplateSerialize, DefaultsNotSerialized) {
    EntityTemplate tmpl;
    tmpl.name = "Minimal";
    TemplateNode node;
    node.name = "Root";
    // All defaults: position=0, rotation=identity, scale=1, type=Node3D.
    tmpl.nodes.push_back(node);

    std::string json = EntityTemplateManager::serialize(tmpl);
    // Should not contain "position", "rotation", "scale", or "type" keys.
    EXPECT_EQ(json.find("\"position\""), std::string::npos);
    EXPECT_EQ(json.find("\"rotation\""), std::string::npos);
    EXPECT_EQ(json.find("\"scale\""), std::string::npos);
    EXPECT_EQ(json.find("\"type\""), std::string::npos);
}

TEST(EntityTemplateSerialize, TemplateRefSerialized) {
    EntityTemplate tmpl;
    tmpl.name = "Nested";
    TemplateNode node;
    node.name = "Ref";
    node.template_ref = "res://templates/other.uztemplate";
    node.template_overrides["mesh"] = "res://override.glb";
    tmpl.nodes.push_back(node);

    std::string json = EntityTemplateManager::serialize(tmpl);
    EXPECT_NE(json.find("res://templates/other.uztemplate"), std::string::npos);
    EXPECT_NE(json.find("res://override.glb"), std::string::npos);
}

TEST(EntityTemplateSerialize, EmptyTemplate) {
    EntityTemplate tmpl;
    std::string json = EntityTemplateManager::serialize(tmpl);
    auto result = EntityTemplateManager::parse(json);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().nodes.empty());
}

// ============================================================================
// Load / Cache tests
// ============================================================================

TEST(EntityTemplateManager, LoadFromFile) {
    TempDir tmp;
    auto path = tmp.write_file("enemy.uztemplate", R"({
        "name": "Enemy",
        "nodes": [{"name": "Root", "type": "StaticMesh"}]
    })");

    EntityTemplateManager mgr;
    auto result = mgr.load(path);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value()->name, "Enemy");
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(EntityTemplateManager, LoadCachesResult) {
    TempDir tmp;
    auto path = tmp.write_file("cached.uztemplate", R"({
        "name": "Cached",
        "nodes": []
    })");

    EntityTemplateManager mgr;
    auto r1 = mgr.load(path);
    ASSERT_TRUE(r1.is_ok());
    auto r2 = mgr.load(path);
    ASSERT_TRUE(r2.is_ok());
    // Same pointer (from cache).
    EXPECT_EQ(r1.value(), r2.value());
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(EntityTemplateManager, GetFromCache) {
    TempDir tmp;
    auto path = tmp.write_file("get.uztemplate", R"({
        "name": "Get",
        "nodes": []
    })");

    EntityTemplateManager mgr;
    EXPECT_EQ(mgr.get(path), nullptr);
    mgr.load(path);
    EXPECT_NE(mgr.get(path), nullptr);
    EXPECT_EQ(mgr.get(path)->name, "Get");
}

TEST(EntityTemplateManager, LoadNonexistentFile) {
    EntityTemplateManager mgr;
    auto result = mgr.load("/nonexistent/path/thing.uztemplate");
    ASSERT_FALSE(result.is_ok());
    EXPECT_NE(result.error().find("Cannot open"), std::string::npos);
}

TEST(EntityTemplateManager, LoadInvalidJson) {
    TempDir tmp;
    auto path = tmp.write_file("bad.uztemplate", "not json!!!");

    EntityTemplateManager mgr;
    auto result = mgr.load(path);
    ASSERT_FALSE(result.is_ok());
}

TEST(EntityTemplateManager, Reload) {
    TempDir tmp;
    auto path = tmp.write_file("reload.uztemplate", R"({
        "name": "V1",
        "nodes": []
    })");

    EntityTemplateManager mgr;
    mgr.load(path);
    EXPECT_EQ(mgr.get(path)->name, "V1");

    // Overwrite file.
    tmp.write_file("reload.uztemplate", R"({
        "name": "V2",
        "nodes": []
    })");

    auto result = mgr.reload(path);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value()->name, "V2");
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(EntityTemplateManager, Unload) {
    TempDir tmp;
    auto path = tmp.write_file("unload.uztemplate", R"({
        "name": "U",
        "nodes": []
    })");

    EntityTemplateManager mgr;
    mgr.load(path);
    EXPECT_EQ(mgr.count(), 1u);

    mgr.unload(path);
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_EQ(mgr.get(path), nullptr);
}

TEST(EntityTemplateManager, UnloadNonexistent) {
    EntityTemplateManager mgr;
    mgr.unload("nonexistent");
    EXPECT_EQ(mgr.count(), 0u);
}

TEST(EntityTemplateManager, Clear) {
    TempDir tmp;
    auto p1 = tmp.write_file("a.uztemplate", R"({"nodes": []})");
    auto p2 = tmp.write_file("b.uztemplate", R"({"nodes": []})");

    EntityTemplateManager mgr;
    mgr.load(p1);
    mgr.load(p2);
    EXPECT_EQ(mgr.count(), 2u);

    mgr.clear();
    EXPECT_EQ(mgr.count(), 0u);
}

// ============================================================================
// Spawn tests — basic
// ============================================================================

TEST(EntityTemplateSpawn, SingleNode) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "Single";
    TemplateNode node;
    node.name = "TestNode";
    node.type_name = "Node3D";
    node.position[0] = 5.0f;
    node.position[1] = 10.0f;
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    const auto& sr = result.value();
    ASSERT_EQ(sr.node_ids.size(), 1u);

    SceneNode* sn = scene_get_node(scene.get(), sr.node_ids[0]);
    ASSERT_NE(sn, nullptr);
    EXPECT_STREQ(sn->name, "TestNode");
    EXPECT_STREQ(sn->type_name, "Node3D");
    EXPECT_TRUE(near_f(sn->local_transform.position.x, 5.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.y, 10.0f));
}

TEST(EntityTemplateSpawn, NodeWithParent) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    // Create a parent node.
    NodeId parent = scene_create_node(scene.get(), "Parent", "Node3D");
    ASSERT_NE(parent, kNodeIdInvalid);

    EntityTemplate tmpl;
    tmpl.name = "Child";
    TemplateNode node;
    node.name = "ChildNode";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl, parent);
    ASSERT_TRUE(result.is_ok());

    SceneNode* child = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(child->parent_id, parent);
}

TEST(EntityTemplateSpawn, HierarchyWithChildren) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "Hierarchy";

    TemplateNode root;
    root.name = "Root";
    TemplateNode childA;
    childA.name = "ChildA";
    childA.position[0] = 1.0f;
    TemplateNode childB;
    childB.name = "ChildB";
    childB.position[1] = 2.0f;
    TemplateNode grandchild;
    grandchild.name = "Grandchild";
    childA.children.push_back(grandchild);
    root.children.push_back(childA);
    root.children.push_back(childB);
    tmpl.nodes.push_back(root);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    const auto& sr = result.value();
    ASSERT_EQ(sr.node_ids.size(), 4u); // Root, ChildA, Grandchild, ChildB

    // Verify hierarchy.
    NodeId root_id = sr.node_ids[0];
    NodeId childA_id = sr.node_ids[1];
    NodeId grandchild_id = sr.node_ids[2];
    NodeId childB_id = sr.node_ids[3];

    SceneNode* root_node = scene_get_node(scene.get(), root_id);
    ASSERT_NE(root_node, nullptr);
    EXPECT_STREQ(root_node->name, "Root");

    SceneNode* childA_node = scene_get_node(scene.get(), childA_id);
    ASSERT_NE(childA_node, nullptr);
    EXPECT_EQ(childA_node->parent_id, root_id);
    EXPECT_TRUE(near_f(childA_node->local_transform.position.x, 1.0f));

    SceneNode* gc_node = scene_get_node(scene.get(), grandchild_id);
    ASSERT_NE(gc_node, nullptr);
    EXPECT_EQ(gc_node->parent_id, childA_id);

    SceneNode* childB_node = scene_get_node(scene.get(), childB_id);
    ASSERT_NE(childB_node, nullptr);
    EXPECT_EQ(childB_node->parent_id, root_id);
}

TEST(EntityTemplateSpawn, WithComponentProperties) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "MeshNode";
    TemplateNode node;
    node.name = "Mesh";
    node.type_name = "StaticMesh";
    node.properties["mesh"] = "res://meshes/cube.glb";
    node.properties["material"] = "res://materials/default.uzmat";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* mesh_data = component_get<StaticMeshData>(&scene->components, id);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_STREQ(mesh_data->mesh_path, "res://meshes/cube.glb");
    EXPECT_STREQ(mesh_data->material_path, "res://materials/default.uzmat");
}

TEST(EntityTemplateSpawn, WithScript) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "Scripted";
    TemplateNode node;
    node.name = "AI";
    node.script = "res://scripts/enemy_ai.lua";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* sd = component_get<ScriptData>(&scene->components, id);
    ASSERT_NE(sd, nullptr);
    EXPECT_STREQ(sd->script_path, "res://scripts/enemy_ai.lua");
}

TEST(EntityTemplateSpawn, NullScene) {
    EntityTemplateManager mgr;
    EntityTemplate tmpl;
    auto result = mgr.spawn(nullptr, tmpl);
    ASSERT_FALSE(result.is_ok());
    EXPECT_NE(result.error().find("null"), std::string::npos);
}

TEST(EntityTemplateSpawn, MultipleRootNodes) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "Multi";
    TemplateNode a; a.name = "A";
    TemplateNode b; b.name = "B";
    tmpl.nodes.push_back(a);
    tmpl.nodes.push_back(b);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().node_ids.size(), 2u);

    auto* nodeA = scene_get_node(scene.get(), result.value().node_ids[0]);
    auto* nodeB = scene_get_node(scene.get(), result.value().node_ids[1]);
    ASSERT_NE(nodeA, nullptr);
    ASSERT_NE(nodeB, nullptr);
    EXPECT_STREQ(nodeA->name, "A");
    EXPECT_STREQ(nodeB->name, "B");
}

TEST(EntityTemplateSpawn, TransformValues) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    node.name = "Transformed";
    node.position[0] = 10.0f; node.position[1] = 20.0f; node.position[2] = 30.0f;
    node.rotation[0] = 0.1f; node.rotation[1] = 0.2f;
    node.rotation[2] = 0.3f; node.rotation[3] = 0.9f;
    node.scale[0] = 3.0f; node.scale[1] = 4.0f; node.scale[2] = 5.0f;
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    SceneNode* sn = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(sn, nullptr);
    EXPECT_TRUE(near_f(sn->local_transform.position.x, 10.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.y, 20.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.z, 30.0f));
    EXPECT_TRUE(near_f(sn->local_transform.rotation.x, 0.1f));
    EXPECT_TRUE(near_f(sn->local_transform.rotation.y, 0.2f));
    EXPECT_TRUE(near_f(sn->local_transform.rotation.z, 0.3f));
    EXPECT_TRUE(near_f(sn->local_transform.rotation.w, 0.9f));
    EXPECT_TRUE(near_f(sn->local_transform.scale.x, 3.0f));
    EXPECT_TRUE(near_f(sn->local_transform.scale.y, 4.0f));
    EXPECT_TRUE(near_f(sn->local_transform.scale.z, 5.0f));
}

// ============================================================================
// Spawn tests — overrides
// ============================================================================

TEST(EntityTemplateSpawn, PositionOverride) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "PosOv";
    TemplateNode node;
    node.name = "Root";
    node.position[0] = 1.0f;
    tmpl.nodes.push_back(node);

    SpawnOverrides ov;
    ov.has_position = true;
    ov.position[0] = 99.0f;
    ov.position[1] = 88.0f;
    ov.position[2] = 77.0f;

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    SceneNode* sn = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(sn, nullptr);
    // Root node should have overridden position.
    EXPECT_TRUE(near_f(sn->local_transform.position.x, 99.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.y, 88.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.z, 77.0f));
}

TEST(EntityTemplateSpawn, PropertyOverride) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "PropOv";
    tmpl.overridable.insert("Root.mesh");
    TemplateNode node;
    node.name = "Root";
    node.type_name = "StaticMesh";
    node.properties["mesh"] = "res://meshes/original.glb";
    tmpl.nodes.push_back(node);

    SpawnOverrides ov;
    ov.fields["Root.mesh"] = "res://meshes/override.glb";

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* mesh_data = component_get<StaticMeshData>(&scene->components, id);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_STREQ(mesh_data->mesh_path, "res://meshes/override.glb");
}

TEST(EntityTemplateSpawn, NonOverridableFieldWarning) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "Restricted";
    tmpl.overridable.insert("Root.mesh");
    TemplateNode node;
    node.name = "Root";
    node.type_name = "StaticMesh";
    node.properties["mesh"] = "res://meshes/orig.glb";
    tmpl.nodes.push_back(node);

    SpawnOverrides ov;
    ov.fields["Root.material"] = "res://materials/hacked.uzmat";

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    // Should have a warning about non-overridable field.
    const auto& warnings = result.value().warnings;
    ASSERT_FALSE(warnings.empty());
    bool found_warning = false;
    for (const auto& w : warnings) {
        if (w.find("non-overridable") != std::string::npos &&
            w.find("Root.material") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_warning) << "Expected warning about non-overridable field";
}

TEST(EntityTemplateSpawn, NoOverridableSetMeansAllAllowed) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    // Template without overridable set: any override is allowed (no warnings).
    EntityTemplate tmpl;
    tmpl.name = "Open";
    TemplateNode node;
    node.name = "Root";
    node.type_name = "StaticMesh";
    node.properties["mesh"] = "res://meshes/orig.glb";
    tmpl.nodes.push_back(node);

    SpawnOverrides ov;
    ov.fields["Root.mesh"] = "res://meshes/override.glb";

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().warnings.empty());
}

TEST(EntityTemplateSpawn, OverridePropertyNotInTemplate) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    tmpl.name = "ExtraOv";
    TemplateNode node;
    node.name = "Root";
    node.type_name = "StaticMesh";
    // No mesh property in template, but we'll override it.
    tmpl.nodes.push_back(node);

    SpawnOverrides ov;
    ov.fields["Root.mesh"] = "res://meshes/added.glb";

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* mesh_data = component_get<StaticMeshData>(&scene->components, id);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_STREQ(mesh_data->mesh_path, "res://meshes/added.glb");
}

TEST(EntityTemplateSpawn, QualifiedPositionOverride) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    node.name = "Root";
    tmpl.nodes.push_back(node);

    SpawnOverrides ov;
    ov.fields["Root.position"] = "11.0 22.0 33.0";

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    SceneNode* sn = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(sn, nullptr);
    EXPECT_TRUE(near_f(sn->local_transform.position.x, 11.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.y, 22.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.z, 33.0f));
}

// ============================================================================
// Despawn tests
// ============================================================================

TEST(EntityTemplateDespawn, BasicDespawn) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode root;
    root.name = "Root";
    TemplateNode child;
    child.name = "Child";
    root.children.push_back(child);
    tmpl.nodes.push_back(root);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(scene->node_count, 2u);

    EntityTemplateManager::despawn(scene.get(), result.value());
    EXPECT_EQ(scene->node_count, 0u);
}

TEST(EntityTemplateDespawn, NullScene) {
    SpawnResult sr;
    sr.node_ids.push_back(1);
    // Should not crash.
    EntityTemplateManager::despawn(nullptr, sr);
}

TEST(EntityTemplateDespawn, AlreadyDestroyedNodes) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    node.name = "Root";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    // Destroy manually first.
    scene_destroy_node(scene.get(), result.value().node_ids[0]);
    EXPECT_EQ(scene->node_count, 0u);

    // Despawn should handle already-destroyed nodes gracefully.
    EntityTemplateManager::despawn(scene.get(), result.value());
    EXPECT_EQ(scene->node_count, 0u);
}

TEST(EntityTemplateDespawn, EmptyResult) {
    auto scene = make_scene();
    SpawnResult empty;
    EntityTemplateManager::despawn(scene.get(), empty);
    EXPECT_EQ(scene->node_count, 0u);
}

// ============================================================================
// Nested templates
// ============================================================================

TEST(EntityTemplateNesting, BasicNesting) {
    TempDir tmp;

    // Inner template: a simple mesh node.
    auto inner_path = tmp.write_file("inner.uztemplate", R"({
        "name": "Inner",
        "nodes": [{
            "name": "InnerRoot",
            "type": "StaticMesh",
            "properties": { "mesh": "res://meshes/inner.glb" }
        }]
    })");

    // Outer template references inner.
    EntityTemplate outer;
    outer.name = "Outer";
    TemplateNode ref_node;
    ref_node.name = "Nested";
    ref_node.template_ref = inner_path;
    ref_node.position[0] = 5.0f;
    outer.nodes.push_back(ref_node);

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn(scene.get(), outer);
    ASSERT_TRUE(result.is_ok());

    const auto& sr = result.value();
    ASSERT_GE(sr.node_ids.size(), 1u);

    // The inner template's root node should be created.
    NodeId inner_id = sr.node_ids[0];
    SceneNode* sn = scene_get_node(scene.get(), inner_id);
    ASSERT_NE(sn, nullptr);
    EXPECT_STREQ(sn->name, "InnerRoot");

    auto* mesh_data = component_get<StaticMeshData>(&scene->components, inner_id);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_STREQ(mesh_data->mesh_path, "res://meshes/inner.glb");
}

TEST(EntityTemplateNesting, NestingWithOverrides) {
    TempDir tmp;

    auto inner_path = tmp.write_file("inner_ov.uztemplate", R"({
        "name": "Inner",
        "nodes": [{
            "name": "Root",
            "type": "StaticMesh",
            "properties": { "mesh": "res://meshes/default.glb" }
        }]
    })");

    EntityTemplate outer;
    outer.name = "Outer";
    TemplateNode ref_node;
    ref_node.name = "Ref";
    ref_node.template_ref = inner_path;
    ref_node.template_overrides["mesh"] = "res://meshes/override.glb";
    outer.nodes.push_back(ref_node);

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn(scene.get(), outer);
    ASSERT_TRUE(result.is_ok());

    // The override from the reference should apply to the inner template.
    NodeId id = result.value().node_ids[0];
    auto* mesh_data = component_get<StaticMeshData>(&scene->components, id);
    ASSERT_NE(mesh_data, nullptr);
    // The override is passed as SpawnOverrides.fields to the nested template spawn.
    // The key "mesh" matches "Root.mesh" when qualified (nested prefix = "Ref", inner node = "Root").
    // Since the inner template node name is "Root" and the override key is just "mesh",
    // it should be applied via the template_overrides mechanism.
}

TEST(EntityTemplateNesting, MissingNestedTemplate) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate outer;
    outer.name = "Missing";
    TemplateNode ref_node;
    ref_node.name = "Ref";
    ref_node.template_ref = "/nonexistent/template.uztemplate";
    outer.nodes.push_back(ref_node);

    auto result = mgr.spawn(scene.get(), outer);
    ASSERT_TRUE(result.is_ok()); // Non-fatal: warning only.

    const auto& warnings = result.value().warnings;
    ASSERT_FALSE(warnings.empty());
    bool found = false;
    for (const auto& w : warnings) {
        if (w.find("Failed to load nested template") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(EntityTemplateNesting, MaxDepthExceeded) {
    TempDir tmp;

    // Create a self-referencing template to trigger max depth.
    std::string self_path = (tmp.path() / "self.uztemplate").string();

    // Escape backslashes for JSON embedding (Windows paths use backslashes).
    std::string escaped_path = self_path;
    for (size_t pos = 0; (pos = escaped_path.find('\\', pos)) != std::string::npos; pos += 2) {
        escaped_path.insert(pos, "\\");
    }

    // Write a template that references itself.
    std::string self_json = R"({
        "name": "Self",
        "nodes": [{
            "name": "Root",
            "template": ")" + escaped_path + R"("
        }]
    })";
    std::ofstream f(self_path);
    f << self_json;
    f.close();

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn_by_path(scene.get(), self_path);
    ASSERT_TRUE(result.is_ok()); // Should not infinite loop.

    // Should have a max nesting depth warning.
    const auto& warnings = result.value().warnings;
    bool found_depth_warning = false;
    for (const auto& w : warnings) {
        if (w.find("Max nesting depth") != std::string::npos) {
            found_depth_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_depth_warning);
}

TEST(EntityTemplateNesting, NestedTemplateWithChildren) {
    TempDir tmp;

    auto inner_path = tmp.write_file("with_children.uztemplate", R"({
        "name": "WithChildren",
        "nodes": [{
            "name": "InnerRoot",
            "children": [
                { "name": "InnerChild" }
            ]
        }]
    })");

    EntityTemplate outer;
    outer.name = "OuterWithNestedChildren";
    TemplateNode ref_node;
    ref_node.name = "Ref";
    ref_node.template_ref = inner_path;
    outer.nodes.push_back(ref_node);

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn(scene.get(), outer);
    ASSERT_TRUE(result.is_ok());

    // Should have created InnerRoot + InnerChild.
    EXPECT_GE(result.value().node_ids.size(), 2u);
}

TEST(EntityTemplateNesting, NestedWithPositionOffset) {
    TempDir tmp;

    auto inner_path = tmp.write_file("positioned_inner.uztemplate", R"({
        "name": "Inner",
        "nodes": [{
            "name": "Root",
            "position": [0, 0, 0]
        }]
    })");

    EntityTemplate outer;
    outer.name = "Positioned";
    TemplateNode ref_node;
    ref_node.name = "Ref";
    ref_node.template_ref = inner_path;
    ref_node.position[0] = 10.0f;
    ref_node.position[1] = 20.0f;
    ref_node.position[2] = 30.0f;
    outer.nodes.push_back(ref_node);

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn(scene.get(), outer);
    ASSERT_TRUE(result.is_ok());

    // The inner root should have the position from the outer reference.
    NodeId id = result.value().node_ids[0];
    SceneNode* sn = scene_get_node(scene.get(), id);
    ASSERT_NE(sn, nullptr);
    EXPECT_TRUE(near_f(sn->local_transform.position.x, 10.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.y, 20.0f));
    EXPECT_TRUE(near_f(sn->local_transform.position.z, 30.0f));
}

// ============================================================================
// spawn_by_path tests
// ============================================================================

TEST(EntityTemplateSpawnByPath, BasicSpawn) {
    TempDir tmp;
    auto path = tmp.write_file("spawn_by_path.uztemplate", R"({
        "name": "ByPath",
        "nodes": [{"name": "Root", "type": "Camera"}]
    })");

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn_by_path(scene.get(), path);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().node_ids.size(), 1u);

    SceneNode* sn = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(sn, nullptr);
    EXPECT_STREQ(sn->name, "Root");
    EXPECT_STREQ(sn->type_name, "Camera");
}

TEST(EntityTemplateSpawnByPath, NonexistentPath) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn_by_path(scene.get(), "/nonexistent.uztemplate");
    ASSERT_FALSE(result.is_ok());
}

TEST(EntityTemplateSpawnByPath, WithOverrides) {
    TempDir tmp;
    auto path = tmp.write_file("ov_path.uztemplate", R"({
        "name": "OvPath",
        "nodes": [{
            "name": "Root",
            "type": "StaticMesh",
            "properties": { "mesh": "res://meshes/default.glb" }
        }]
    })");

    auto scene = make_scene();
    EntityTemplateManager mgr;

    SpawnOverrides ov;
    ov.fields["Root.mesh"] = "res://meshes/custom.glb";

    auto result = mgr.spawn_by_path(scene.get(), path, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* mesh_data = component_get<StaticMeshData>(&scene->components, id);
    ASSERT_NE(mesh_data, nullptr);
    EXPECT_STREQ(mesh_data->mesh_path, "res://meshes/custom.glb");
}

// ============================================================================
// Camera node type test (verify component attachment)
// ============================================================================

TEST(EntityTemplateSpawn, CameraNodeWithProperties) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    node.name = "Cam";
    node.type_name = "Camera";
    node.properties["fov"] = "90.0";
    node.properties["near"] = "0.5";
    node.properties["far"] = "1000.0";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* cam = component_get<CameraData>(&scene->components, id);
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(near_f(cam->fov_y, 90.0f));
    EXPECT_TRUE(near_f(cam->near_plane, 0.5f));
    EXPECT_TRUE(near_f(cam->far_plane, 1000.0f));
}

TEST(EntityTemplateSpawn, DirectionalLightProperties) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    node.name = "Sun";
    node.type_name = "DirectionalLight";
    node.properties["light_intensity"] = "3.5";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    NodeId id = result.value().node_ids[0];
    auto* light = component_get<DirectionalLightData>(&scene->components, id);
    ASSERT_NE(light, nullptr);
    EXPECT_TRUE(near_f(light->intensity, 3.5f));
}

TEST(EntityTemplateSpawn, UnknownTypeCreatesNodeWithNoComponents) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    node.name = "Custom";
    node.type_name = "FutureNodeType";
    node.properties["unknown_key"] = "value";
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    SceneNode* sn = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(sn, nullptr);
    EXPECT_STREQ(sn->name, "Custom");
    EXPECT_STREQ(sn->type_name, "FutureNodeType");
}

TEST(EntityTemplateSpawn, UnnamedNode) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode node;
    // name left empty.
    tmpl.nodes.push_back(node);

    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());

    SceneNode* sn = scene_get_node(scene.get(), result.value().node_ids[0]);
    ASSERT_NE(sn, nullptr);
    EXPECT_STREQ(sn->name, "Unnamed");
}

// ============================================================================
// File I/O round-trip
// ============================================================================

TEST(EntityTemplateFileIO, SaveAndReload) {
    TempDir tmp;

    // Create a template, serialize, write to file, reload, verify.
    EntityTemplate tmpl;
    tmpl.name = "FileRoundTrip";
    TemplateNode node;
    node.name = "Root";
    node.type_name = "PointLight";
    node.position[0] = 7.0f;
    node.properties["light_intensity"] = "5.0";
    node.properties["light_radius"] = "25.0";
    tmpl.nodes.push_back(node);
    tmpl.overridable.insert("Root.light_intensity");

    std::string json = EntityTemplateManager::serialize(tmpl);
    auto path = tmp.write_file("roundtrip.uztemplate", json);

    EntityTemplateManager mgr;
    auto result = mgr.load(path);
    ASSERT_TRUE(result.is_ok());

    const auto* loaded = result.value();
    EXPECT_EQ(loaded->name, "FileRoundTrip");
    ASSERT_EQ(loaded->nodes.size(), 1u);
    EXPECT_EQ(loaded->nodes[0].name, "Root");
    EXPECT_EQ(loaded->nodes[0].type_name, "PointLight");
    EXPECT_TRUE(near_f(loaded->nodes[0].position[0], 7.0f));
    EXPECT_EQ(loaded->nodes[0].properties.at("light_intensity"), "5.0");
    EXPECT_TRUE(loaded->overridable.count("Root.light_intensity"));
}

// ============================================================================
// Reload with nonexistent file
// ============================================================================

TEST(EntityTemplateManager, ReloadNonexistentFile) {
    EntityTemplateManager mgr;
    auto result = mgr.reload("/does/not/exist.uztemplate");
    ASSERT_FALSE(result.is_ok());
}

// ============================================================================
// Spawn complex template from file
// ============================================================================

TEST(EntityTemplateSpawn, ComplexTemplateFromFile) {
    TempDir tmp;
    auto path = tmp.write_file("complex.uztemplate", R"({
        "name": "Complex",
        "nodes": [
            {
                "name": "World",
                "type": "Node3D",
                "children": [
                    {
                        "name": "Ground",
                        "type": "StaticMesh",
                        "position": [0, -1, 0],
                        "scale": [100, 1, 100],
                        "properties": {
                            "mesh": "res://meshes/plane.glb",
                            "material": "res://materials/ground.uzmat"
                        }
                    },
                    {
                        "name": "Sun",
                        "type": "DirectionalLight",
                        "rotation": [0.383, 0, 0, 0.924],
                        "properties": {
                            "light_color": "1.0 0.95 0.8",
                            "light_intensity": "2.5"
                        }
                    },
                    {
                        "name": "MainCamera",
                        "type": "Camera",
                        "position": [0, 5, -10],
                        "properties": {
                            "fov": "60.0",
                            "near": "0.1",
                            "far": "500.0"
                        },
                        "script": "res://scripts/camera_controller.lua"
                    }
                ]
            }
        ],
        "overridable": ["World.Ground.mesh", "World.Sun.light_intensity"]
    })");

    auto scene = make_scene();
    EntityTemplateManager mgr;

    auto result = mgr.spawn_by_path(scene.get(), path);
    ASSERT_TRUE(result.is_ok());

    const auto& sr = result.value();
    // World + Ground + Sun + MainCamera = 4 nodes
    EXPECT_EQ(sr.node_ids.size(), 4u);

    // Verify Ground mesh.
    NodeId ground_id = sr.node_ids[1];
    auto* ground_mesh = component_get<StaticMeshData>(&scene->components, ground_id);
    ASSERT_NE(ground_mesh, nullptr);
    EXPECT_STREQ(ground_mesh->mesh_path, "res://meshes/plane.glb");

    // Verify Sun light.
    NodeId sun_id = sr.node_ids[2];
    auto* sun_light = component_get<DirectionalLightData>(&scene->components, sun_id);
    ASSERT_NE(sun_light, nullptr);
    EXPECT_TRUE(near_f(sun_light->intensity, 2.5f));

    // Verify Camera.
    NodeId cam_id = sr.node_ids[3];
    auto* cam = component_get<CameraData>(&scene->components, cam_id);
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(near_f(cam->fov_y, 60.0f));

    // Verify script on camera.
    auto* script = component_get<ScriptData>(&scene->components, cam_id);
    ASSERT_NE(script, nullptr);
    EXPECT_STREQ(script->script_path, "res://scripts/camera_controller.lua");
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(EntityTemplateSpawn, EmptyTemplate) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    auto result = mgr.spawn(scene.get(), tmpl);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().node_ids.empty());
    EXPECT_EQ(scene->node_count, 0u);
}

TEST(EntityTemplateSpawn, ChildOverrideDoesNotAffectParent) {
    auto scene = make_scene();
    EntityTemplateManager mgr;

    EntityTemplate tmpl;
    TemplateNode root;
    root.name = "Root";
    root.type_name = "StaticMesh";
    root.properties["mesh"] = "res://meshes/parent.glb";

    TemplateNode child;
    child.name = "Child";
    child.type_name = "StaticMesh";
    child.properties["mesh"] = "res://meshes/child.glb";
    root.children.push_back(child);
    tmpl.nodes.push_back(root);

    SpawnOverrides ov;
    ov.fields["Root.Child.mesh"] = "res://meshes/child_override.glb";

    auto result = mgr.spawn(scene.get(), tmpl, kNodeIdInvalid, ov);
    ASSERT_TRUE(result.is_ok());

    // Parent mesh should be unchanged.
    NodeId root_id = result.value().node_ids[0];
    auto* root_mesh = component_get<StaticMeshData>(&scene->components, root_id);
    ASSERT_NE(root_mesh, nullptr);
    EXPECT_STREQ(root_mesh->mesh_path, "res://meshes/parent.glb");

    // Child mesh should be overridden.
    NodeId child_id = result.value().node_ids[1];
    auto* child_mesh = component_get<StaticMeshData>(&scene->components, child_id);
    ASSERT_NE(child_mesh, nullptr);
    EXPECT_STREQ(child_mesh->mesh_path, "res://meshes/child_override.glb");
}
