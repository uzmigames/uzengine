// test_asset_registry.cpp -- Comprehensive tests for AssetRegistry and IPC asset handlers.
//
// Covers: AssetType classification, ImportStatus, AssetEntry, AssetRegistry::scan,
//         AssetRegistry::get, AssetRegistry::import_asset, AssetRegistry::read_import_json,
//         AssetRegistry::watch, AssetRegistry::clear, IPC handlers asset.list,
//         asset.import, asset.getInfo, asset.createMaterial, asset.createScene,
//         asset.createShader, error paths, edge cases.

#include <gtest/gtest.h>

#include "engine/editor/asset_registry.h"
#include "engine/editor/ipc_asset_handlers.h"
#include "common/ipc_registry.h"
#include "engine/core/platform.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace uz;

// ============================================================================
// Test fixture -- creates a temporary directory tree for each test
// ============================================================================

class AssetRegistryTest : public ::testing::Test {
protected:
    std::string test_root_;

    void SetUp() override {
        // Create a unique temp directory for each test.
        test_root_ = fs::temp_directory_path().string() + "/uz_asset_test_" +
                     std::to_string(::testing::UnitTest::GetInstance()
                                        ->current_test_info()->line());

        // Clean up any previous run.
        fs::remove_all(test_root_);
        fs::create_directories(test_root_);
    }

    void TearDown() override {
        fs::remove_all(test_root_);
    }

    /// Convenience: create a file with the given content.
    void create_file(const std::string& rel_path, const std::string& content = "test") {
        const std::string abs = test_root_ + "/" + rel_path;
        const auto parent = fs::path(abs).parent_path();
        fs::create_directories(parent);
        std::ofstream ofs(abs, std::ios::binary);
        ofs << content;
    }

    /// Convenience: create the template files needed by IPC handlers.
    void create_templates() {
        fs::create_directories(test_root_ + "/templates");
        create_file("templates/material.uzmat",
                    "[material]\nname = {{NAME}}\nshader = unlit\n\n"
                    "[properties]\nalbedo_color = 1.0 1.0 1.0 1.0\n");
        create_file("templates/scene.uzscene",
                    "[uzscene]\nname = {{NAME}}\nversion = 1\n\n"
                    "[node]\nname = Root\ntype = Node3D\nparent =\n"
                    "position = 0.0 0.0 0.0\nrotation = 0.0 0.0 0.0 1.0\n"
                    "scale = 1.0 1.0 1.0\nactive = true\nvisible = true\n");
        create_file("templates/shader_unlit.hlsl",
                    "// {{NAME}} -- minimal shader\n\n"
                    "float4 VSMain(float3 pos : POSITION) : SV_POSITION {\n"
                    "    return float4(pos, 1.0);\n}\n");
    }

    /// Read file contents as string.
    std::string read_file(const std::string& abs_path) {
        size_t size = 0;
        char* data = platform::file_read_all(abs_path.c_str(), &size);
        if (!data) return {};
        std::string result(data, size);
        std::free(data);
        return result;
    }
};

// ============================================================================
// AssetType classification tests
// ============================================================================

TEST_F(AssetRegistryTest, ClassifyAsset_Mesh_Glb) {
    EXPECT_EQ(classify_asset("model.glb"), AssetType::Mesh);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Mesh_Gltf) {
    EXPECT_EQ(classify_asset("model.gltf"), AssetType::Mesh);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Texture_Png) {
    EXPECT_EQ(classify_asset("sprite.png"), AssetType::Texture);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Texture_Jpg) {
    EXPECT_EQ(classify_asset("photo.jpg"), AssetType::Texture);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Texture_Ktx2) {
    EXPECT_EQ(classify_asset("compressed.ktx2"), AssetType::Texture);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Audio_Wav) {
    EXPECT_EQ(classify_asset("sound.wav"), AssetType::Audio);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Audio_Ogg) {
    EXPECT_EQ(classify_asset("music.ogg"), AssetType::Audio);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Scene) {
    EXPECT_EQ(classify_asset("level1.uzscene"), AssetType::Scene);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Material) {
    EXPECT_EQ(classify_asset("stone.uzmat"), AssetType::Material);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Shader_Hlsl) {
    EXPECT_EQ(classify_asset("unlit.hlsl"), AssetType::Shader);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Shader_Glsl) {
    EXPECT_EQ(classify_asset("frag.glsl"), AssetType::Shader);
}

TEST_F(AssetRegistryTest, ClassifyAsset_Unknown_Extension) {
    EXPECT_EQ(classify_asset("readme.txt"), AssetType::Unknown);
}

TEST_F(AssetRegistryTest, ClassifyAsset_No_Extension) {
    EXPECT_EQ(classify_asset("Makefile"), AssetType::Unknown);
}

TEST_F(AssetRegistryTest, ClassifyAsset_CaseInsensitive) {
    EXPECT_EQ(classify_asset("Model.GLB"), AssetType::Mesh);
    EXPECT_EQ(classify_asset("SPRITE.PNG"), AssetType::Texture);
    EXPECT_EQ(classify_asset("MUSIC.OGG"), AssetType::Audio);
}

TEST_F(AssetRegistryTest, ClassifyAsset_PathWithDirectories) {
    EXPECT_EQ(classify_asset("textures/ground/grass.png"), AssetType::Texture);
    EXPECT_EQ(classify_asset("models/characters/hero.glb"), AssetType::Mesh);
}

// ============================================================================
// asset_type_to_string / asset_type_from_string
// ============================================================================

TEST_F(AssetRegistryTest, AssetTypeToString_AllTypes) {
    EXPECT_STREQ(asset_type_to_string(AssetType::Mesh),     "Mesh");
    EXPECT_STREQ(asset_type_to_string(AssetType::Texture),  "Texture");
    EXPECT_STREQ(asset_type_to_string(AssetType::Audio),    "Audio");
    EXPECT_STREQ(asset_type_to_string(AssetType::Scene),    "Scene");
    EXPECT_STREQ(asset_type_to_string(AssetType::Material), "Material");
    EXPECT_STREQ(asset_type_to_string(AssetType::Shader),   "Shader");
    EXPECT_STREQ(asset_type_to_string(AssetType::Unknown),  "Unknown");
}

TEST_F(AssetRegistryTest, AssetTypeFromString_AllTypes) {
    EXPECT_EQ(asset_type_from_string("Mesh"),     AssetType::Mesh);
    EXPECT_EQ(asset_type_from_string("Texture"),  AssetType::Texture);
    EXPECT_EQ(asset_type_from_string("Audio"),    AssetType::Audio);
    EXPECT_EQ(asset_type_from_string("Scene"),    AssetType::Scene);
    EXPECT_EQ(asset_type_from_string("Material"), AssetType::Material);
    EXPECT_EQ(asset_type_from_string("Shader"),   AssetType::Shader);
    EXPECT_EQ(asset_type_from_string("Unknown"),  AssetType::Unknown);
}

TEST_F(AssetRegistryTest, AssetTypeFromString_Invalid) {
    EXPECT_EQ(asset_type_from_string("garbage"), AssetType::Unknown);
    EXPECT_EQ(asset_type_from_string(nullptr),   AssetType::Unknown);
    EXPECT_EQ(asset_type_from_string(""),        AssetType::Unknown);
}

// ============================================================================
// import_status_to_string
// ============================================================================

TEST_F(AssetRegistryTest, ImportStatusToString_AllStatuses) {
    EXPECT_STREQ(import_status_to_string(ImportStatus::Fresh),       "Fresh");
    EXPECT_STREQ(import_status_to_string(ImportStatus::Stale),       "Stale");
    EXPECT_STREQ(import_status_to_string(ImportStatus::Error),       "Error");
    EXPECT_STREQ(import_status_to_string(ImportStatus::NotImported), "NotImported");
}

// ============================================================================
// AssetRegistry::scan -- empty directory
// ============================================================================

TEST_F(AssetRegistryTest, ScanEmptyDirectory) {
    AssetRegistry reg;
    const uint32_t count = reg.scan(test_root_);
    EXPECT_EQ(count, 0u);
    EXPECT_TRUE(reg.entries().empty());
}

// ============================================================================
// AssetRegistry::scan -- discovers assets by extension
// ============================================================================

TEST_F(AssetRegistryTest, ScanDiscoversByExtension) {
    create_file("hero.glb");
    create_file("grass.png");
    create_file("level1.uzscene");
    create_file("stone.uzmat");
    create_file("unlit.hlsl");
    create_file("click.wav");
    create_file("music.ogg");
    create_file("readme.txt"); // Should be ignored (unknown type)

    AssetRegistry reg;
    const uint32_t count = reg.scan(test_root_);
    EXPECT_EQ(count, 7u); // 7 known types, 1 unknown skipped

    // Verify each type is correctly classified.
    ASSERT_NE(reg.get("hero.glb"), nullptr);
    EXPECT_EQ(reg.get("hero.glb")->type, AssetType::Mesh);

    ASSERT_NE(reg.get("grass.png"), nullptr);
    EXPECT_EQ(reg.get("grass.png")->type, AssetType::Texture);

    ASSERT_NE(reg.get("level1.uzscene"), nullptr);
    EXPECT_EQ(reg.get("level1.uzscene")->type, AssetType::Scene);

    ASSERT_NE(reg.get("stone.uzmat"), nullptr);
    EXPECT_EQ(reg.get("stone.uzmat")->type, AssetType::Material);

    ASSERT_NE(reg.get("unlit.hlsl"), nullptr);
    EXPECT_EQ(reg.get("unlit.hlsl")->type, AssetType::Shader);

    ASSERT_NE(reg.get("click.wav"), nullptr);
    EXPECT_EQ(reg.get("click.wav")->type, AssetType::Audio);

    ASSERT_NE(reg.get("music.ogg"), nullptr);
    EXPECT_EQ(reg.get("music.ogg")->type, AssetType::Audio);

    // Unknown file should not be in the registry.
    EXPECT_EQ(reg.get("readme.txt"), nullptr);
}

// ============================================================================
// AssetRegistry::scan -- recursive directory walking
// ============================================================================

TEST_F(AssetRegistryTest, ScanRecursive) {
    create_file("textures/ground/dirt.png");
    create_file("models/hero.glb");
    create_file("scenes/level1.uzscene");

    AssetRegistry reg;
    const uint32_t count = reg.scan(test_root_);
    EXPECT_EQ(count, 3u);

    ASSERT_NE(reg.get("textures/ground/dirt.png"), nullptr);
    EXPECT_EQ(reg.get("textures/ground/dirt.png")->type, AssetType::Texture);

    ASSERT_NE(reg.get("models/hero.glb"), nullptr);
    EXPECT_EQ(reg.get("models/hero.glb")->type, AssetType::Mesh);

    ASSERT_NE(reg.get("scenes/level1.uzscene"), nullptr);
    EXPECT_EQ(reg.get("scenes/level1.uzscene")->type, AssetType::Scene);
}

// ============================================================================
// AssetRegistry::scan -- skips hidden files and .import.json
// ============================================================================

TEST_F(AssetRegistryTest, ScanSkipsHiddenFiles) {
    create_file(".hidden.png");
    create_file("visible.png");

    AssetRegistry reg;
    reg.scan(test_root_);

    EXPECT_EQ(reg.get(".hidden.png"), nullptr);
    ASSERT_NE(reg.get("visible.png"), nullptr);
}

TEST_F(AssetRegistryTest, ScanSkipsImportJsonSidecars) {
    create_file("grass.png");
    create_file("grass.png.import.json", R"({"source":"grass.png","type":"Texture","hash":42})");

    AssetRegistry reg;
    reg.scan(test_root_);

    // Should have only the .png, not the .import.json.
    EXPECT_EQ(reg.entries().size(), 1u);
    ASSERT_NE(reg.get("grass.png"), nullptr);
}

// ============================================================================
// AssetRegistry::scan -- initial status from existing .import.json
// ============================================================================

TEST_F(AssetRegistryTest, ScanDetectsFreshStatus) {
    create_file("grass.png", "pixel data");

    // First scan to get the hash.
    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    const uint64_t hash = entry->hash;

    // Write a matching .import.json sidecar.
    nlohmann::json sidecar = {
        {"source", "grass.png"},
        {"type", "Texture"},
        {"hash", hash},
        {"imported_at", "2026-03-03T00:00:00Z"}
    };
    create_file("grass.png.import.json", sidecar.dump());

    // Re-scan: status should be Fresh.
    reg.scan(test_root_);
    entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::Fresh);
}

TEST_F(AssetRegistryTest, ScanDetectsStaleStatus) {
    create_file("grass.png", "pixel data");

    // Write a .import.json with a mismatched hash.
    nlohmann::json sidecar = {
        {"source", "grass.png"},
        {"type", "Texture"},
        {"hash", 99999},
        {"imported_at", "2026-03-03T00:00:00Z"}
    };
    create_file("grass.png.import.json", sidecar.dump());

    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::Stale);
}

TEST_F(AssetRegistryTest, ScanDetectsErrorStatus) {
    create_file("grass.png", "pixel data");

    nlohmann::json sidecar = {
        {"source", "grass.png"},
        {"type", "Texture"},
        {"error", "corrupted file"}
    };
    create_file("grass.png.import.json", sidecar.dump());

    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::Error);
    EXPECT_EQ(entry->error_msg, "corrupted file");
}

TEST_F(AssetRegistryTest, ScanDetectsNotImportedStatus) {
    create_file("grass.png", "pixel data");

    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::NotImported);
}

// ============================================================================
// AssetRegistry::scan -- rescans clear previous entries
// ============================================================================

TEST_F(AssetRegistryTest, ScanClearsPreviousEntries) {
    create_file("a.png");
    create_file("b.glb");

    AssetRegistry reg;
    reg.scan(test_root_);
    EXPECT_EQ(reg.entries().size(), 2u);

    // Remove one file and re-scan.
    fs::remove(test_root_ + "/a.png");
    reg.scan(test_root_);
    EXPECT_EQ(reg.entries().size(), 1u);
    EXPECT_EQ(reg.get("a.png"), nullptr);
    ASSERT_NE(reg.get("b.glb"), nullptr);
}

// ============================================================================
// AssetRegistry::scan -- nonexistent directory
// ============================================================================

TEST_F(AssetRegistryTest, ScanNonexistentDirectory) {
    AssetRegistry reg;
    const uint32_t count = reg.scan("/nonexistent_dir_99999");
    EXPECT_EQ(count, 0u);
}

// ============================================================================
// AssetRegistry::get -- const overload
// ============================================================================

TEST_F(AssetRegistryTest, GetConstOverload) {
    create_file("hero.glb");

    AssetRegistry reg;
    reg.scan(test_root_);

    const AssetRegistry& cref = reg;
    const AssetEntry* entry = cref.get("hero.glb");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type, AssetType::Mesh);

    EXPECT_EQ(cref.get("nonexistent.png"), nullptr);
}

// ============================================================================
// AssetRegistry::get -- not found
// ============================================================================

TEST_F(AssetRegistryTest, GetNotFound) {
    AssetRegistry reg;
    EXPECT_EQ(reg.get("nonexistent.png"), nullptr);
}

// ============================================================================
// AssetRegistry::project_root
// ============================================================================

TEST_F(AssetRegistryTest, ProjectRootReturnsScannedPath) {
    AssetRegistry reg;
    EXPECT_TRUE(reg.project_root().empty());

    reg.scan(test_root_);
    // project_root should have a trailing slash.
    EXPECT_FALSE(reg.project_root().empty());
    EXPECT_EQ(reg.project_root().back(), '/');
}

// ============================================================================
// AssetRegistry::clear
// ============================================================================

TEST_F(AssetRegistryTest, ClearRemovesAllEntries) {
    create_file("hero.glb");
    create_file("grass.png");

    AssetRegistry reg;
    reg.scan(test_root_);
    EXPECT_EQ(reg.entries().size(), 2u);

    reg.clear();
    EXPECT_TRUE(reg.entries().empty());
    EXPECT_TRUE(reg.project_root().empty());
}

// ============================================================================
// AssetRegistry::import_asset -- basic import
// ============================================================================

TEST_F(AssetRegistryTest, ImportAssetCreatesImportJson) {
    create_file("grass.png", "pixel data here");

    AssetRegistry reg;
    reg.scan(test_root_);
    ASSERT_NE(reg.get("grass.png"), nullptr);
    EXPECT_EQ(reg.get("grass.png")->status, ImportStatus::NotImported);

    // Import.
    EXPECT_TRUE(reg.import_asset("grass.png"));

    // Verify status changed.
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::Fresh);
    EXPECT_TRUE(entry->error_msg.empty());
    EXPECT_NE(entry->hash, 0u);

    // Verify .import.json was created.
    const std::string sidecar_path = test_root_ + "/grass.png.import.json";
    EXPECT_TRUE(platform::path_exists(sidecar_path.c_str()));

    // Parse and verify the JSON content.
    const std::string json_str = read_file(sidecar_path);
    EXPECT_FALSE(json_str.empty());

    auto j = nlohmann::json::parse(json_str);
    EXPECT_EQ(j["source"].get<std::string>(), "grass.png");
    EXPECT_EQ(j["type"].get<std::string>(), "Texture");
    EXPECT_TRUE(j["hash"].is_number_unsigned());
    EXPECT_TRUE(j["imported_at"].is_string());
}

// ============================================================================
// AssetRegistry::import_asset -- already fresh (hash unchanged)
// ============================================================================

TEST_F(AssetRegistryTest, ImportAlreadyFreshSkipped) {
    create_file("grass.png", "pixel data");

    AssetRegistry reg;
    reg.scan(test_root_);

    // First import.
    EXPECT_TRUE(reg.import_asset("grass.png"));
    EXPECT_EQ(reg.get("grass.png")->status, ImportStatus::Fresh);

    // Second import -- should be a no-op (returns true, no error).
    EXPECT_TRUE(reg.import_asset("grass.png"));
    EXPECT_EQ(reg.get("grass.png")->status, ImportStatus::Fresh);
}

// ============================================================================
// AssetRegistry::import_asset -- nonexistent path
// ============================================================================

TEST_F(AssetRegistryTest, ImportNonexistentPath) {
    AssetRegistry reg;
    reg.scan(test_root_);

    // Trying to import a path not in the registry should return false.
    EXPECT_FALSE(reg.import_asset("nonexistent.png"));
}

// ============================================================================
// AssetRegistry::import_asset -- source file deleted after scan
// ============================================================================

TEST_F(AssetRegistryTest, ImportSourceFileDeletedAfterScan) {
    create_file("grass.png", "pixel data");

    AssetRegistry reg;
    reg.scan(test_root_);
    ASSERT_NE(reg.get("grass.png"), nullptr);

    // Delete the source file.
    fs::remove(test_root_ + "/grass.png");

    // Import should fail and mark as Error.
    EXPECT_FALSE(reg.import_asset("grass.png"));
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::Error);
    EXPECT_FALSE(entry->error_msg.empty());
}

// ============================================================================
// AssetRegistry::read_import_json
// ============================================================================

TEST_F(AssetRegistryTest, ReadImportJsonReturnsContent) {
    create_file("grass.png", "pixel data");

    AssetRegistry reg;
    reg.scan(test_root_);
    EXPECT_TRUE(reg.import_asset("grass.png"));

    const std::string json_str = reg.read_import_json("grass.png");
    EXPECT_FALSE(json_str.empty());

    auto j = nlohmann::json::parse(json_str);
    EXPECT_EQ(j["source"].get<std::string>(), "grass.png");
}

TEST_F(AssetRegistryTest, ReadImportJsonReturnsEmptyForUnimported) {
    create_file("grass.png", "pixel data");

    AssetRegistry reg;
    reg.scan(test_root_);

    const std::string json_str = reg.read_import_json("grass.png");
    EXPECT_TRUE(json_str.empty());
}

TEST_F(AssetRegistryTest, ReadImportJsonReturnsEmptyForNonexistent) {
    AssetRegistry reg;
    reg.scan(test_root_);

    const std::string json_str = reg.read_import_json("nonexistent.png");
    EXPECT_TRUE(json_str.empty());
}

// ============================================================================
// AssetRegistry::scan -- malformed .import.json
// ============================================================================

TEST_F(AssetRegistryTest, ScanHandlesMalformedImportJson) {
    create_file("grass.png", "pixel data");
    create_file("grass.png.import.json", "not valid json {{{");

    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    // Malformed JSON should be treated as NotImported.
    EXPECT_EQ(entry->status, ImportStatus::NotImported);
}

// ============================================================================
// AssetRegistry::scan -- .import.json missing hash field
// ============================================================================

TEST_F(AssetRegistryTest, ScanImportJsonMissingHash) {
    create_file("grass.png", "pixel data");
    create_file("grass.png.import.json", R"({"source":"grass.png","type":"Texture"})");

    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);
    // Missing hash should be treated as Stale.
    EXPECT_EQ(entry->status, ImportStatus::Stale);
}

// ============================================================================
// AssetRegistry hash -- nonzero for valid files
// ============================================================================

TEST_F(AssetRegistryTest, HashIsNonzeroForValidFile) {
    create_file("hero.glb", "mesh binary data");

    AssetRegistry reg;
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("hero.glb");
    ASSERT_NE(entry, nullptr);
    EXPECT_NE(entry->hash, 0u);
}

// ============================================================================
// AssetRegistry -- multiple imports update hash
// ============================================================================

TEST_F(AssetRegistryTest, ImportUpdatesHashOnStale) {
    create_file("grass.png", "version1");

    AssetRegistry reg;
    reg.scan(test_root_);
    EXPECT_TRUE(reg.import_asset("grass.png"));

    // Modify the file to change its hash -- different size guarantees different hash.
    {
        std::ofstream ofs(test_root_ + "/grass.png", std::ios::binary | std::ios::trunc);
        ofs << "version2 with different size to guarantee different hash";
    }

    // Re-scan picks up the new hash.
    reg.scan(test_root_);
    const AssetEntry* entry = reg.get("grass.png");
    ASSERT_NE(entry, nullptr);

    // Re-import should succeed.
    EXPECT_TRUE(reg.import_asset("grass.png"));
    EXPECT_EQ(entry->status, ImportStatus::Fresh);
}

// ============================================================================
// AssetRegistry::scan -- deeply nested directories
// ============================================================================

TEST_F(AssetRegistryTest, ScanDeeplyNested) {
    create_file("a/b/c/d/deep.png");

    AssetRegistry reg;
    reg.scan(test_root_);

    ASSERT_NE(reg.get("a/b/c/d/deep.png"), nullptr);
    EXPECT_EQ(reg.get("a/b/c/d/deep.png")->type, AssetType::Texture);
}

// ============================================================================
// AssetRegistry::import_asset -- import for subdirectory asset
// ============================================================================

TEST_F(AssetRegistryTest, ImportSubdirectoryAsset) {
    create_file("textures/grass.png", "pixel data");

    AssetRegistry reg;
    reg.scan(test_root_);
    EXPECT_TRUE(reg.import_asset("textures/grass.png"));

    // Verify the .import.json is created next to the asset.
    const std::string sidecar = test_root_ + "/textures/grass.png.import.json";
    EXPECT_TRUE(platform::path_exists(sidecar.c_str()));
}

// ============================================================================
// AssetRegistry::scan -- project root with trailing slash
// ============================================================================

TEST_F(AssetRegistryTest, ScanProjectRootWithTrailingSlash) {
    create_file("hero.glb");

    AssetRegistry reg;
    reg.scan(test_root_ + "/");

    ASSERT_NE(reg.get("hero.glb"), nullptr);
    // Verify no double-slash in project_root.
    const std::string& root = reg.project_root();
    EXPECT_EQ(root.back(), '/');
    // Check there's no "//" at end.
    if (root.size() >= 2) {
        EXPECT_FALSE(root[root.size()-1] == '/' && root[root.size()-2] == '/');
    }
}

// ============================================================================
// AssetType enum values are distinct
// ============================================================================

TEST_F(AssetRegistryTest, AssetTypeEnumValuesDistinct) {
    EXPECT_NE(static_cast<uint8_t>(AssetType::Mesh),     static_cast<uint8_t>(AssetType::Texture));
    EXPECT_NE(static_cast<uint8_t>(AssetType::Texture),  static_cast<uint8_t>(AssetType::Audio));
    EXPECT_NE(static_cast<uint8_t>(AssetType::Audio),    static_cast<uint8_t>(AssetType::Scene));
    EXPECT_NE(static_cast<uint8_t>(AssetType::Scene),    static_cast<uint8_t>(AssetType::Material));
    EXPECT_NE(static_cast<uint8_t>(AssetType::Material), static_cast<uint8_t>(AssetType::Shader));
    EXPECT_NE(static_cast<uint8_t>(AssetType::Shader),   static_cast<uint8_t>(AssetType::Unknown));
}

// ============================================================================
// ImportStatus enum values are distinct
// ============================================================================

TEST_F(AssetRegistryTest, ImportStatusEnumValuesDistinct) {
    EXPECT_NE(static_cast<uint8_t>(ImportStatus::Fresh),       static_cast<uint8_t>(ImportStatus::Stale));
    EXPECT_NE(static_cast<uint8_t>(ImportStatus::Stale),       static_cast<uint8_t>(ImportStatus::Error));
    EXPECT_NE(static_cast<uint8_t>(ImportStatus::Error),       static_cast<uint8_t>(ImportStatus::NotImported));
}

// ============================================================================
// AssetEntry default values
// ============================================================================

TEST_F(AssetRegistryTest, AssetEntryDefaults) {
    AssetEntry entry;
    EXPECT_TRUE(entry.path.empty());
    EXPECT_EQ(entry.type, AssetType::Unknown);
    EXPECT_EQ(entry.status, ImportStatus::NotImported);
    EXPECT_EQ(entry.hash, 0u);
    EXPECT_TRUE(entry.error_msg.empty());
}

// ============================================================================
// IPC handler tests -- using AssetRegistryIpcTest fixture
// ============================================================================

class AssetRegistryIpcTest : public AssetRegistryTest {
protected:
    IpcRegistry    ipc_;
    AssetRegistry  reg_;

    void SetUp() override {
        AssetRegistryTest::SetUp();
        asset_register_handlers(ipc_, &reg_);
    }
};

// ============================================================================
// asset.list -- empty registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListEmpty) {
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.list", {});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["assets"].is_array());
    EXPECT_EQ(result["assets"].size(), 0u);
}

// ============================================================================
// asset.list -- returns all entries
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListReturnsAllEntries) {
    create_file("hero.glb");
    create_file("grass.png");
    create_file("level.uzscene");

    reg_.scan(test_root_);

    auto result = ipc_.call("asset.list", {});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["assets"].size(), 3u);

    // Verify each entry has path, type, status.
    for (const auto& asset : result["assets"]) {
        EXPECT_TRUE(asset.contains("path"));
        EXPECT_TRUE(asset.contains("type"));
        EXPECT_TRUE(asset.contains("status"));
        EXPECT_TRUE(asset["path"].is_string());
        EXPECT_TRUE(asset["type"].is_string());
        EXPECT_TRUE(asset["status"].is_string());
    }
}

// ============================================================================
// asset.list -- filter by type
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListFilterByType) {
    create_file("hero.glb");
    create_file("grass.png");
    create_file("sky.png");
    create_file("level.uzscene");

    reg_.scan(test_root_);

    auto result = ipc_.call("asset.list", {{"type", "Texture"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["assets"].size(), 2u);

    for (const auto& asset : result["assets"]) {
        EXPECT_EQ(asset["type"].get<std::string>(), "Texture");
    }
}

// ============================================================================
// asset.list -- filter with unknown type returns all
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListFilterUnknownTypeReturnsAll) {
    create_file("hero.glb");
    create_file("grass.png");

    reg_.scan(test_root_);

    // "InvalidType" maps to Unknown which sets has_filter = false -> returns all.
    auto result = ipc_.call("asset.list", {{"type", "InvalidType"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["assets"].size(), 2u);
}

// ============================================================================
// asset.list -- null registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListNullRegistry) {
    IpcRegistry ipc2;
    asset_register_handlers(ipc2, nullptr);

    auto result = ipc2.call("asset.list", {});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result.contains("error"));
}

// ============================================================================
// asset.list -- type filter with no matching entries
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListFilterNoMatches) {
    create_file("hero.glb");
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.list", {{"type", "Texture"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["assets"].size(), 0u);
}

// ============================================================================
// asset.list -- type filter as non-string (ignored)
// ============================================================================

TEST_F(AssetRegistryIpcTest, ListTypeFilterAsNumber) {
    create_file("hero.glb");
    create_file("grass.png");
    reg_.scan(test_root_);

    // Type filter is a number, not a string -- should be ignored, returns all.
    auto result = ipc_.call("asset.list", {{"type", 42}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["assets"].size(), 2u);
}

// ============================================================================
// asset.import -- triggers import
// ============================================================================

TEST_F(AssetRegistryIpcTest, ImportTriggersImport) {
    create_file("grass.png", "pixel data");
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.import", {{"path", "grass.png"}});
    EXPECT_TRUE(result["ok"].get<bool>());

    // Verify the entry is now Fresh.
    const AssetEntry* entry = reg_.get("grass.png");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ImportStatus::Fresh);
}

// ============================================================================
// asset.import -- missing path argument
// ============================================================================

TEST_F(AssetRegistryIpcTest, ImportMissingPath) {
    auto result = ipc_.call("asset.import", {});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].get<std::string>().find("missing") != std::string::npos);
}

// ============================================================================
// asset.import -- nonexistent asset
// ============================================================================

TEST_F(AssetRegistryIpcTest, ImportNonexistentAsset) {
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.import", {{"path", "nonexistent.png"}});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].get<std::string>().find("not found") != std::string::npos);
}

// ============================================================================
// asset.import -- null registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, ImportNullRegistry) {
    IpcRegistry ipc2;
    asset_register_handlers(ipc2, nullptr);

    auto result = ipc2.call("asset.import", {{"path", "grass.png"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.import -- source file deleted (error path)
// ============================================================================

TEST_F(AssetRegistryIpcTest, ImportSourceDeleted) {
    create_file("grass.png", "data");
    reg_.scan(test_root_);

    // Delete the source file after scanning.
    fs::remove(test_root_ + "/grass.png");

    auto result = ipc_.call("asset.import", {{"path", "grass.png"}});
    EXPECT_FALSE(result["ok"].get<bool>());
    // The error message should include context about the failure.
    EXPECT_FALSE(result["error"].get<std::string>().empty());
}

// ============================================================================
// asset.import -- invalid path argument type
// ============================================================================

TEST_F(AssetRegistryIpcTest, ImportInvalidPathType) {
    auto result = ipc_.call("asset.import", {{"path", 42}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.getInfo -- returns import metadata
// ============================================================================

TEST_F(AssetRegistryIpcTest, GetInfoReturnsMetadata) {
    create_file("grass.png", "pixel data");
    reg_.scan(test_root_);
    reg_.import_asset("grass.png");

    auto result = ipc_.call("asset.getInfo", {{"path", "grass.png"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_TRUE(result["info"].is_object());
    EXPECT_EQ(result["info"]["source"].get<std::string>(), "grass.png");
    EXPECT_EQ(result["info"]["type"].get<std::string>(), "Texture");
    EXPECT_TRUE(result["info"]["hash"].is_number_unsigned());
    EXPECT_TRUE(result["info"]["imported_at"].is_string());
}

// ============================================================================
// asset.getInfo -- unimported asset
// ============================================================================

TEST_F(AssetRegistryIpcTest, GetInfoUnimported) {
    create_file("grass.png", "pixel data");
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.getInfo", {{"path", "grass.png"}});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].get<std::string>().find("no import info") != std::string::npos);
}

// ============================================================================
// asset.getInfo -- missing path argument
// ============================================================================

TEST_F(AssetRegistryIpcTest, GetInfoMissingPath) {
    auto result = ipc_.call("asset.getInfo", {});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].get<std::string>().find("missing") != std::string::npos);
}

// ============================================================================
// asset.getInfo -- null registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, GetInfoNullRegistry) {
    IpcRegistry ipc2;
    asset_register_handlers(ipc2, nullptr);

    auto result = ipc2.call("asset.getInfo", {{"path", "grass.png"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.getInfo -- invalid path argument type
// ============================================================================

TEST_F(AssetRegistryIpcTest, GetInfoInvalidPathType) {
    auto result = ipc_.call("asset.getInfo", {{"path", true}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.getInfo -- corrupt .import.json on disk
// ============================================================================

TEST_F(AssetRegistryIpcTest, GetInfoCorruptImportJson) {
    create_file("grass.png", "pixel data");
    reg_.scan(test_root_);

    // Manually write a corrupt .import.json.
    const std::string sidecar = test_root_ + "/grass.png.import.json";
    platform::file_write_all(sidecar.c_str(), "{{corrupt", 9);

    auto result = ipc_.call("asset.getInfo", {{"path", "grass.png"}});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].get<std::string>().find("parse") != std::string::npos);
}

// ============================================================================
// asset.createMaterial -- creates .uzmat file
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateMaterial) {
    create_templates();
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.createMaterial", {{"name", "Stone"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["path"].get<std::string>(), "Stone.uzmat");

    // Verify file was created.
    const std::string abs = test_root_ + "/Stone.uzmat";
    EXPECT_TRUE(platform::path_exists(abs.c_str()));

    // Verify template was applied -- name placeholder replaced.
    const std::string content = read_file(abs);
    EXPECT_TRUE(content.find("Stone") != std::string::npos);
    EXPECT_TRUE(content.find("{{NAME}}") == std::string::npos);

    // Verify the asset appears in the registry after re-scan.
    ASSERT_NE(reg_.get("Stone.uzmat"), nullptr);
    EXPECT_EQ(reg_.get("Stone.uzmat")->type, AssetType::Material);
}

// ============================================================================
// asset.createMaterial -- missing name
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateMaterialMissingName) {
    auto result = ipc_.call("asset.createMaterial", {});
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_TRUE(result["error"].get<std::string>().find("missing") != std::string::npos);
}

// ============================================================================
// asset.createMaterial -- null registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateMaterialNullRegistry) {
    IpcRegistry ipc2;
    asset_register_handlers(ipc2, nullptr);

    auto result = ipc2.call("asset.createMaterial", {{"name", "test"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createMaterial -- invalid name argument type
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateMaterialInvalidNameType) {
    auto result = ipc_.call("asset.createMaterial", {{"name", 123}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createMaterial -- fallback when template is missing
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateMaterialFallbackNoTemplate) {
    // Don't create templates -- should use fallback inline content.
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.createMaterial", {{"name", "Brick"}});
    EXPECT_TRUE(result["ok"].get<bool>());

    const std::string abs = test_root_ + "/Brick.uzmat";
    EXPECT_TRUE(platform::path_exists(abs.c_str()));

    const std::string content = read_file(abs);
    EXPECT_TRUE(content.find("Brick") != std::string::npos);
    EXPECT_TRUE(content.find("[material]") != std::string::npos);
}

// ============================================================================
// asset.createScene -- creates .uzscene file
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateScene) {
    create_templates();
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.createScene", {{"name", "Level1"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["path"].get<std::string>(), "Level1.uzscene");

    // Verify file was created.
    const std::string abs = test_root_ + "/Level1.uzscene";
    EXPECT_TRUE(platform::path_exists(abs.c_str()));

    // Verify template was applied.
    const std::string content = read_file(abs);
    EXPECT_TRUE(content.find("Level1") != std::string::npos);
    EXPECT_TRUE(content.find("{{NAME}}") == std::string::npos);
    EXPECT_TRUE(content.find("[uzscene]") != std::string::npos);

    // Verify the asset appears in the registry.
    ASSERT_NE(reg_.get("Level1.uzscene"), nullptr);
    EXPECT_EQ(reg_.get("Level1.uzscene")->type, AssetType::Scene);
}

// ============================================================================
// asset.createScene -- missing name
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateSceneMissingName) {
    auto result = ipc_.call("asset.createScene", {});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createScene -- null registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateSceneNullRegistry) {
    IpcRegistry ipc2;
    asset_register_handlers(ipc2, nullptr);

    auto result = ipc2.call("asset.createScene", {{"name", "test"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createScene -- invalid name argument type
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateSceneInvalidNameType) {
    auto result = ipc_.call("asset.createScene", {{"name", false}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createScene -- fallback when template is missing
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateSceneFallbackNoTemplate) {
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.createScene", {{"name", "TestLevel"}});
    EXPECT_TRUE(result["ok"].get<bool>());

    const std::string abs = test_root_ + "/TestLevel.uzscene";
    EXPECT_TRUE(platform::path_exists(abs.c_str()));

    const std::string content = read_file(abs);
    EXPECT_TRUE(content.find("TestLevel") != std::string::npos);
    EXPECT_TRUE(content.find("[uzscene]") != std::string::npos);
}

// ============================================================================
// asset.createShader -- creates .hlsl file
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateShader) {
    create_templates();
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.createShader", {{"name", "MyUnlit"}});
    EXPECT_TRUE(result["ok"].get<bool>());
    EXPECT_EQ(result["path"].get<std::string>(), "MyUnlit.hlsl");

    // Verify file was created.
    const std::string abs = test_root_ + "/MyUnlit.hlsl";
    EXPECT_TRUE(platform::path_exists(abs.c_str()));

    // Verify template was applied.
    const std::string content = read_file(abs);
    EXPECT_TRUE(content.find("MyUnlit") != std::string::npos);
    EXPECT_TRUE(content.find("{{NAME}}") == std::string::npos);
    EXPECT_TRUE(content.find("VSMain") != std::string::npos);

    // Verify the asset appears in the registry.
    ASSERT_NE(reg_.get("MyUnlit.hlsl"), nullptr);
    EXPECT_EQ(reg_.get("MyUnlit.hlsl")->type, AssetType::Shader);
}

// ============================================================================
// asset.createShader -- missing name
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateShaderMissingName) {
    auto result = ipc_.call("asset.createShader", {});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createShader -- null registry
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateShaderNullRegistry) {
    IpcRegistry ipc2;
    asset_register_handlers(ipc2, nullptr);

    auto result = ipc2.call("asset.createShader", {{"name", "test"}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createShader -- invalid name argument type
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateShaderInvalidNameType) {
    auto result = ipc_.call("asset.createShader", {{"name", nullptr}});
    EXPECT_FALSE(result["ok"].get<bool>());
}

// ============================================================================
// asset.createShader -- fallback when template is missing
// ============================================================================

TEST_F(AssetRegistryIpcTest, CreateShaderFallbackNoTemplate) {
    reg_.scan(test_root_);

    auto result = ipc_.call("asset.createShader", {{"name", "SimpleFx"}});
    EXPECT_TRUE(result["ok"].get<bool>());

    const std::string abs = test_root_ + "/SimpleFx.hlsl";
    EXPECT_TRUE(platform::path_exists(abs.c_str()));

    const std::string content = read_file(abs);
    EXPECT_TRUE(content.find("SimpleFx") != std::string::npos);
}
