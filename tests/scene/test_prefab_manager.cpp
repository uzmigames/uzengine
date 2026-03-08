// test_prefab_manager.cpp -- Tests for PrefabInstanceData and PrefabManager.
//
// PrefabManager methods like instantiate_prefab/reload/get_overrides internally
// allocate temporary Scene objects (~32MB each due to 65536-node array).
// To avoid blowing up memory, file-based tests are kept minimal and use a
// single shared Scene via a test fixture.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "engine/scene/prefab_manager.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_registry.h"

using namespace uz;

// ============================================================================
// PrefabInstanceData — pure struct tests (no Scene allocation)
// ============================================================================

TEST(PrefabInstanceData, FindOverride_Empty_ReturnsMax) {
    PrefabInstanceData pid{};
    EXPECT_EQ(pid.find_override("position"), PrefabInstanceData::kMaxOverrides);
}

TEST(PrefabInstanceData, FindOverride_NullKey_ReturnsMax) {
    PrefabInstanceData pid{};
    EXPECT_EQ(pid.find_override(nullptr), PrefabInstanceData::kMaxOverrides);
}

TEST(PrefabInstanceData, FindOverride_Present_ReturnsIndex) {
    PrefabInstanceData pid{};
    pid.set_override("position", "1 2 3");
    pid.set_override("rotation", "0 0 0 1");
    EXPECT_EQ(pid.find_override("position"), 0u);
    EXPECT_EQ(pid.find_override("rotation"), 1u);
}

TEST(PrefabInstanceData, FindOverride_NotPresent_ReturnsMax) {
    PrefabInstanceData pid{};
    pid.set_override("rotation", "0 0 0 1");
    EXPECT_EQ(pid.find_override("scale"), PrefabInstanceData::kMaxOverrides);
}

TEST(PrefabInstanceData, SetOverride_NullKey_ReturnsFalse) {
    PrefabInstanceData pid{};
    EXPECT_FALSE(pid.set_override(nullptr, "value"));
    EXPECT_EQ(pid.override_count, 0u);
}

TEST(PrefabInstanceData, SetOverride_NullValue_ReturnsFalse) {
    PrefabInstanceData pid{};
    EXPECT_FALSE(pid.set_override("key", nullptr));
    EXPECT_EQ(pid.override_count, 0u);
}

TEST(PrefabInstanceData, SetOverride_NewKey_Adds) {
    PrefabInstanceData pid{};
    EXPECT_TRUE(pid.set_override("position", "1 2 3"));
    EXPECT_EQ(pid.override_count, 1u);
    EXPECT_STREQ(pid.override_keys[0], "position");
    EXPECT_STREQ(pid.override_values[0], "1 2 3");
}

TEST(PrefabInstanceData, SetOverride_DuplicateKey_Updates) {
    PrefabInstanceData pid{};
    EXPECT_TRUE(pid.set_override("position", "1 2 3"));
    EXPECT_TRUE(pid.set_override("position", "4 5 6"));
    EXPECT_EQ(pid.override_count, 1u);
    EXPECT_STREQ(pid.override_values[0], "4 5 6");
}

TEST(PrefabInstanceData, SetOverride_MultipleKeys) {
    PrefabInstanceData pid{};
    EXPECT_TRUE(pid.set_override("position", "1 2 3"));
    EXPECT_TRUE(pid.set_override("rotation", "0 0 0 1"));
    EXPECT_TRUE(pid.set_override("scale", "2 2 2"));
    EXPECT_EQ(pid.override_count, 3u);
}

TEST(PrefabInstanceData, SetOverride_Full_ReturnsFalse) {
    PrefabInstanceData pid{};
    for (uint32_t i = 0; i < PrefabInstanceData::kMaxOverrides; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "key%u", i);
        EXPECT_TRUE(pid.set_override(key, "val"));
    }
    EXPECT_EQ(pid.override_count, PrefabInstanceData::kMaxOverrides);
    EXPECT_FALSE(pid.set_override("overflow", "val"));
    EXPECT_EQ(pid.override_count, PrefabInstanceData::kMaxOverrides);
}

TEST(PrefabInstanceData, SetOverride_FullThenUpdate_Succeeds) {
    PrefabInstanceData pid{};
    for (uint32_t i = 0; i < PrefabInstanceData::kMaxOverrides; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "key%u", i);
        pid.set_override(key, "old");
    }
    // Update existing key should still work even when full.
    EXPECT_TRUE(pid.set_override("key0", "new"));
    EXPECT_STREQ(pid.override_values[0], "new");
}

TEST(PrefabInstanceData, RemoveOverride_NullKey_ReturnsFalse) {
    PrefabInstanceData pid{};
    EXPECT_FALSE(pid.remove_override(nullptr));
}

TEST(PrefabInstanceData, RemoveOverride_NotPresent_ReturnsFalse) {
    PrefabInstanceData pid{};
    pid.set_override("position", "1 2 3");
    EXPECT_FALSE(pid.remove_override("scale"));
    EXPECT_EQ(pid.override_count, 1u);
}

TEST(PrefabInstanceData, RemoveOverride_OnlyEntry) {
    PrefabInstanceData pid{};
    pid.set_override("position", "1 2 3");
    EXPECT_TRUE(pid.remove_override("position"));
    EXPECT_EQ(pid.override_count, 0u);
}

TEST(PrefabInstanceData, RemoveOverride_FirstOfTwo_ShiftsSecond) {
    PrefabInstanceData pid{};
    pid.set_override("position", "1 2 3");
    pid.set_override("rotation", "0 0 0 1");
    EXPECT_TRUE(pid.remove_override("position"));
    EXPECT_EQ(pid.override_count, 1u);
    EXPECT_STREQ(pid.override_keys[0], "rotation");
    EXPECT_STREQ(pid.override_values[0], "0 0 0 1");
}

TEST(PrefabInstanceData, RemoveOverride_LastOfTwo) {
    PrefabInstanceData pid{};
    pid.set_override("position", "1 2 3");
    pid.set_override("rotation", "0 0 0 1");
    EXPECT_TRUE(pid.remove_override("rotation"));
    EXPECT_EQ(pid.override_count, 1u);
    EXPECT_STREQ(pid.override_keys[0], "position");
}

TEST(PrefabInstanceData, RemoveOverride_MiddleOfThree_ShiftsCorrectly) {
    PrefabInstanceData pid{};
    pid.set_override("a", "1");
    pid.set_override("b", "2");
    pid.set_override("c", "3");
    EXPECT_TRUE(pid.remove_override("b"));
    EXPECT_EQ(pid.override_count, 2u);
    EXPECT_STREQ(pid.override_keys[0], "a");
    EXPECT_STREQ(pid.override_keys[1], "c");
}

TEST(PrefabInstanceData, ClearOverrides_Empty_NoOp) {
    PrefabInstanceData pid{};
    pid.clear_overrides(); // Should not crash.
    EXPECT_EQ(pid.override_count, 0u);
}

TEST(PrefabInstanceData, ClearOverrides_WithEntries) {
    PrefabInstanceData pid{};
    pid.set_override("a", "1");
    pid.set_override("b", "2");
    pid.set_override("c", "3");
    pid.clear_overrides();
    EXPECT_EQ(pid.override_count, 0u);
    EXPECT_EQ(pid.find_override("a"), PrefabInstanceData::kMaxOverrides);
}

// ============================================================================
// PrefabManager — no-scene error paths (no allocation)
// ============================================================================

TEST(PrefabManager, DefaultState) {
    PrefabManager pm;
    EXPECT_EQ(pm.editor_mode(), EditorMode::SCENE_EDIT);
    EXPECT_TRUE(pm.active_prefab_path().empty());
}

TEST(PrefabManager, SetNullScene_DoesNotCrash) {
    PrefabManager pm;
    pm.set_scene(nullptr);
    EXPECT_EQ(pm.editor_mode(), EditorMode::SCENE_EDIT);
}

TEST(PrefabManager, IsPrefabInstance_NullScene_ReturnsFalse) {
    PrefabManager pm;
    EXPECT_FALSE(pm.is_prefab_instance(1u));
}

TEST(PrefabManager, GetPrefabSource_NullScene_ReturnsEmpty) {
    PrefabManager pm;
    EXPECT_EQ(pm.get_prefab_source(1u), "");
}

TEST(PrefabManager, CountInstances_NullScene_ReturnsZero) {
    PrefabManager pm;
    EXPECT_EQ(pm.count_instances("any.uzscene"), 0u);
}

TEST(PrefabManager, OpenPrefab_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.open_prefab("anything.uzscene");
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("no scene"), std::string::npos);
}

TEST(PrefabManager, SavePrefab_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.save_prefab("out.uzscene");
    ASSERT_TRUE(r.is_err());
}

TEST(PrefabManager, ClosePrefab_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.close_prefab();
    ASSERT_TRUE(r.is_err());
}

TEST(PrefabManager, InstantiatePrefab_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.instantiate_prefab("path.uzscene", "name");
    ASSERT_TRUE(r.is_err());
}

TEST(PrefabManager, ReloadPrefab_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.reload_prefab_instances("path.uzscene");
    ASSERT_TRUE(r.is_err());
}

TEST(PrefabManager, GetOverrides_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.get_prefab_overrides(1u);
    ASSERT_TRUE(r.is_err());
}

TEST(PrefabManager, RevertOverride_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.revert_prefab_override(1u, "position");
    ASSERT_TRUE(r.is_err());
}

TEST(PrefabManager, RevertAllOverrides_NullScene_ReturnsErr) {
    PrefabManager pm;
    auto r = pm.revert_all_prefab_overrides(1u);
    ASSERT_TRUE(r.is_err());
}

// ============================================================================
// PrefabManager — with Scene (shared fixture to limit memory)
// ============================================================================

class PrefabManagerFixture : public ::testing::Test {
protected:
    // Single shared Scene for all tests in this fixture (~32MB).
    static std::unique_ptr<Scene> scene_;
    PrefabManager pm;

    static void SetUpTestSuite() {
        scene_ = std::make_unique<Scene>();
        scene_init(scene_.get(), "PrefabTestScene");
        component_store_register<PrefabInstanceData>(&scene_->components);
    }
    static void TearDownTestSuite() {
        scene_.reset();
    }
    void SetUp() override {
        // Reinit scene to clean state for each test.
        scene_init(scene_.get(), "PrefabTestScene");
        component_store_register<PrefabInstanceData>(&scene_->components);
        pm = PrefabManager{};
        pm.set_scene(scene_.get());
    }
};

std::unique_ptr<Scene> PrefabManagerFixture::scene_;

TEST_F(PrefabManagerFixture, SetScene_ModeIsSCENE_EDIT) {
    EXPECT_EQ(pm.editor_mode(), EditorMode::SCENE_EDIT);
}

TEST_F(PrefabManagerFixture, CountInstances_NullPath_ReturnsZero) {
    EXPECT_EQ(pm.count_instances(nullptr), 0u);
}

TEST_F(PrefabManagerFixture, CountInstances_NoInstances_ReturnsZero) {
    EXPECT_EQ(pm.count_instances("none.uzscene"), 0u);
}

TEST_F(PrefabManagerFixture, IsPrefabInstance_InvalidNode_ReturnsFalse) {
    EXPECT_FALSE(pm.is_prefab_instance(9999u));
}

TEST_F(PrefabManagerFixture, OpenPrefab_EmptyPath_ReturnsErr) {
    auto r = pm.open_prefab("");
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("empty prefab path"), std::string::npos);
}

TEST_F(PrefabManagerFixture, OpenPrefab_NullPath_ReturnsErr) {
    auto r = pm.open_prefab(nullptr);
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, OpenPrefab_FileNotFound_ReturnsErr) {
    auto r = pm.open_prefab("/nonexistent/path.uzscene");
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(pm.editor_mode(), EditorMode::SCENE_EDIT);
}

TEST_F(PrefabManagerFixture, SavePrefab_EmptyPath_ReturnsErr) {
    auto r = pm.save_prefab("");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, SavePrefab_NotInPrefabMode_ReturnsErr) {
    auto r = pm.save_prefab("out.uzscene");
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("not in prefab edit mode"), std::string::npos);
}

TEST_F(PrefabManagerFixture, ClosePrefab_NotInPrefabMode_ReturnsErr) {
    auto r = pm.close_prefab();
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("not in prefab edit mode"), std::string::npos);
}

TEST_F(PrefabManagerFixture, InstantiatePrefab_EmptyPath_ReturnsErr) {
    auto r = pm.instantiate_prefab("", "name");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, InstantiatePrefab_NullPath_ReturnsErr) {
    auto r = pm.instantiate_prefab(nullptr, "name");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, InstantiatePrefab_FileNotFound_ReturnsErr) {
    auto r = pm.instantiate_prefab("/no/file.uzscene", "inst");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, ReloadPrefab_EmptyPath_ReturnsErr) {
    auto r = pm.reload_prefab_instances("");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, ReloadPrefab_NullPath_ReturnsErr) {
    auto r = pm.reload_prefab_instances(nullptr);
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, GetOverrides_InvalidNode_ReturnsErr) {
    auto r = pm.get_prefab_overrides(9999u);
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, GetOverrides_NotPrefabInstance_ReturnsErr) {
    NodeId nid = scene_create_node(scene_.get(), "Normal", "Node3D", kNodeIdInvalid);
    ASSERT_NE(nid, kNodeIdInvalid);
    auto r = pm.get_prefab_overrides(nid);
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("not a prefab instance"), std::string::npos);
}

TEST_F(PrefabManagerFixture, RevertOverride_EmptyKey_ReturnsErr) {
    auto r = pm.revert_prefab_override(1u, "");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, RevertOverride_NullKey_ReturnsErr) {
    auto r = pm.revert_prefab_override(1u, nullptr);
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, RevertOverride_InvalidNode_ReturnsErr) {
    auto r = pm.revert_prefab_override(9999u, "position");
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, RevertAllOverrides_InvalidNode_ReturnsErr) {
    auto r = pm.revert_all_prefab_overrides(9999u);
    ASSERT_TRUE(r.is_err());
}

TEST_F(PrefabManagerFixture, RevertAllOverrides_NotPrefabInstance_ReturnsErr) {
    NodeId nid = scene_create_node(scene_.get(), "Normal2", "Node3D", kNodeIdInvalid);
    ASSERT_NE(nid, kNodeIdInvalid);
    auto r = pm.revert_all_prefab_overrides(nid);
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("not a prefab instance"), std::string::npos);
}

// ============================================================================
// PrefabManager — file-based tests (single TempDir, single shared Scene)
// ============================================================================

namespace {
std::string make_prefab_text(const char* node_name = "PrefabRoot",
                             float px = 0.f, float py = 0.f, float pz = 0.f) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "[uzscene]\n"
        "name = Prefab\n"
        "version = 1\n"
        "\n"
        "[node]\n"
        "name = %s\n"
        "type = Node3D\n"
        "parent =\n"
        "position = %.6f %.6f %.6f\n"
        "rotation = 0.000000 0.000000 0.000000 1.000000\n"
        "scale = 1.000000 1.000000 1.000000\n"
        "active = true\n"
        "visible = true\n",
        node_name,
        static_cast<double>(px), static_cast<double>(py), static_cast<double>(pz));
    return buf;
}

class TempDir {
public:
    TempDir() {
        path_ = std::filesystem::temp_directory_path() / "uz_prefab_tests";
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    std::filesystem::path path() const { return path_; }

    std::string write_file(const std::string& name, const std::string& content) {
        auto fp = path_ / name;
        std::filesystem::create_directories(fp.parent_path());
        std::ofstream f(fp);
        f << content;
        f.close();
        return fp.string();
    }
private:
    std::filesystem::path path_;
};
} // anonymous namespace

// File-based fixture: shares one Scene AND one TempDir across a small set of tests.
class PrefabFileFixture : public ::testing::Test {
protected:
    static std::unique_ptr<Scene> scene_;
    static TempDir* tmp_;
    PrefabManager pm;

    static void SetUpTestSuite() {
        scene_ = std::make_unique<Scene>();
        scene_init(scene_.get(), "FileTestScene");
        component_store_register<PrefabInstanceData>(&scene_->components);
        tmp_ = new TempDir();
    }
    static void TearDownTestSuite() {
        scene_.reset();
        delete tmp_;
        tmp_ = nullptr;
    }
    void SetUp() override {
        scene_init(scene_.get(), "FileTestScene");
        component_store_register<PrefabInstanceData>(&scene_->components);
        pm = PrefabManager{};
        pm.set_scene(scene_.get());
    }
};

std::unique_ptr<Scene> PrefabFileFixture::scene_;
TempDir* PrefabFileFixture::tmp_ = nullptr;

TEST_F(PrefabFileFixture, OpenPrefab_ValidFile_EntersPrefabEditMode) {
    std::string path = tmp_->write_file("open.uzscene", make_prefab_text("OpenRoot"));

    auto r = pm.open_prefab(path.c_str());
    ASSERT_TRUE(r.is_ok()) << r.error();
    EXPECT_EQ(pm.editor_mode(), EditorMode::PREFAB_EDIT);
    EXPECT_EQ(pm.active_prefab_path(), path);
}

TEST_F(PrefabFileFixture, OpenPrefab_AlreadyInPrefabMode_ReturnsErr) {
    std::string path = tmp_->write_file("double_open.uzscene", make_prefab_text("DRoot"));
    ASSERT_TRUE(pm.open_prefab(path.c_str()).is_ok());

    auto r = pm.open_prefab(path.c_str());
    ASSERT_TRUE(r.is_err());
    EXPECT_NE(r.error().find("already in prefab edit mode"), std::string::npos);
}

TEST_F(PrefabFileFixture, ClosePrefab_RestoresMainScene) {
    // Create a node in the main scene first.
    scene_create_node(scene_.get(), "MainNode", "Node3D", kNodeIdInvalid);

    std::string path = tmp_->write_file("close.uzscene", make_prefab_text("CloseRoot"));
    ASSERT_TRUE(pm.open_prefab(path.c_str()).is_ok());
    EXPECT_EQ(pm.editor_mode(), EditorMode::PREFAB_EDIT);

    auto r = pm.close_prefab();
    ASSERT_TRUE(r.is_ok()) << r.error();
    EXPECT_EQ(pm.editor_mode(), EditorMode::SCENE_EDIT);
    EXPECT_TRUE(pm.active_prefab_path().empty());
}

TEST_F(PrefabFileFixture, SavePrefab_InPrefabMode_Succeeds) {
    std::string src = tmp_->write_file("save_src.uzscene", make_prefab_text("SaveRoot"));
    ASSERT_TRUE(pm.open_prefab(src.c_str()).is_ok());

    std::string out = (tmp_->path() / "save_out.uzscene").string();
    auto r = pm.save_prefab(out.c_str());
    ASSERT_TRUE(r.is_ok()) << r.error();

    // Verify file was written.
    EXPECT_TRUE(std::filesystem::exists(out));
}

TEST_F(PrefabFileFixture, InstantiatePrefab_ValidFile_CreatesNode) {
    std::string path = tmp_->write_file("inst.uzscene",
                                        make_prefab_text("InstRoot", 1.f, 2.f, 3.f));

    auto r = pm.instantiate_prefab(path.c_str(), "MyInstance");
    ASSERT_TRUE(r.is_ok()) << r.error();

    NodeId iid = r.value();
    EXPECT_NE(iid, kNodeIdInvalid);
    EXPECT_TRUE(pm.is_prefab_instance(iid));
    EXPECT_EQ(pm.get_prefab_source(iid), path);
    EXPECT_EQ(pm.count_instances(path.c_str()), 1u);
}

TEST_F(PrefabFileFixture, InstantiatePrefab_CopiesTransform) {
    std::string path = tmp_->write_file("xform.uzscene",
                                        make_prefab_text("XRoot", 10.f, 20.f, 30.f));

    auto r = pm.instantiate_prefab(path.c_str(), "XInst");
    ASSERT_TRUE(r.is_ok()) << r.error();

    SceneNode* node = scene_get_node(scene_.get(), r.value());
    ASSERT_NE(node, nullptr);
    EXPECT_NEAR(node->local_transform.position.x, 10.f, 1e-4f);
    EXPECT_NEAR(node->local_transform.position.y, 20.f, 1e-4f);
    EXPECT_NEAR(node->local_transform.position.z, 30.f, 1e-4f);
}

TEST_F(PrefabFileFixture, CountInstances_MultipleInstances) {
    std::string path = tmp_->write_file("multi.uzscene", make_prefab_text("MRoot"));
    ASSERT_TRUE(pm.instantiate_prefab(path.c_str(), "I1").is_ok());
    ASSERT_TRUE(pm.instantiate_prefab(path.c_str(), "I2").is_ok());
    ASSERT_TRUE(pm.instantiate_prefab(path.c_str(), "I3").is_ok());
    EXPECT_EQ(pm.count_instances(path.c_str()), 3u);
}
