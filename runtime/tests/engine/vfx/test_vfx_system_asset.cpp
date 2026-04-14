/// Unit tests for the VfxSystem asset type (multi-emitter compound `.uzvfxsys`).
///
/// Covers: LoadFromString with a fully-specified valid JSON (all fields
/// parsed correctly), optional fields with default values, missing required
/// fields (format, version, emitters), empty emitters array, malformed JSON,
/// per-emitter validation (missing path, empty path, wrong types), id
/// auto-generation when omitted, and SaveToFile round-trip.
///
/// These tests exercise the asset layer only — no GPU hardware required.
/// Runtime instantiation (VfxSystemInstance) is covered separately in
/// `test_vfx_system_instance.cpp`.

#include <gtest/gtest.h>

#include "engine/vfx/system/vfx_system.h"

#include <filesystem>
#include <string>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Valid JSON fixtures
// ─────────────────────────────────────────────────────────────────────────────

/// A fully-specified fireball-like system — 3 emitters, all fields populated.
constexpr const char* kFireballJson = R"({
    "format": "uzvfxsys",
    "version": 1,
    "name": "Fireball",
    "emitters": [
        {"name": "head",   "path": "preset:FireballHead",   "id": "e1", "enabled": true,  "localSpace": true},
        {"name": "sparks", "path": "preset:FireballSparks", "id": "e2", "enabled": true,  "localSpace": true},
        {"name": "smoke",  "path": "preset:FireballSmoke",  "id": "e3", "enabled": false, "localSpace": false}
    ]
})";

/// Minimal valid JSON — only required fields, optional fields take defaults.
constexpr const char* kMinimalJson = R"({
    "format": "uzvfxsys",
    "version": 1,
    "emitters": [
        {"path": "preset:Fire"}
    ]
})";

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Valid parse paths
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemAsset, LoadFromString_ParsesFullySpecifiedJson) {
    auto asset = VfxSystem::LoadFromString(kFireballJson);
    ASSERT_NE(asset, nullptr);

    EXPECT_EQ(asset->name, "Fireball");
    ASSERT_EQ(asset->emitters.size(), 3u);

    EXPECT_EQ(asset->emitters[0].name,        "head");
    EXPECT_EQ(asset->emitters[0].emitterPath, "preset:FireballHead");
    EXPECT_EQ(asset->emitters[0].id,          "e1");
    EXPECT_TRUE(asset->emitters[0].enabled);
    EXPECT_TRUE(asset->emitters[0].localSpace);

    EXPECT_EQ(asset->emitters[1].name,        "sparks");
    EXPECT_EQ(asset->emitters[1].emitterPath, "preset:FireballSparks");
    EXPECT_EQ(asset->emitters[1].id,          "e2");

    EXPECT_EQ(asset->emitters[2].name,        "smoke");
    EXPECT_EQ(asset->emitters[2].emitterPath, "preset:FireballSmoke");
    EXPECT_EQ(asset->emitters[2].id,          "e3");
    EXPECT_FALSE(asset->emitters[2].enabled);     // explicitly disabled
    EXPECT_FALSE(asset->emitters[2].localSpace);  // explicitly world-space

    // sourcePath only set by Load(path, vfs) — LoadFromString leaves it empty.
    EXPECT_TRUE(asset->sourcePath.empty());
}

TEST(VfxSystemAsset, LoadFromString_AppliesDefaultsForOptionalFields) {
    auto asset = VfxSystem::LoadFromString(kMinimalJson);
    ASSERT_NE(asset, nullptr);

    // name absent -> empty.
    EXPECT_TRUE(asset->name.empty());

    ASSERT_EQ(asset->emitters.size(), 1u);
    const auto& e = asset->emitters[0];
    EXPECT_TRUE(e.name.empty());                    // not provided
    EXPECT_EQ(e.emitterPath, "preset:Fire");
    EXPECT_EQ(e.id, "e0");                          // auto-generated from index
    EXPECT_TRUE(e.enabled);                         // default true
    EXPECT_TRUE(e.localSpace);                      // default true
}

TEST(VfxSystemAsset, LoadFromString_AutoGeneratesMissingIds) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1,
        "emitters": [
            {"path": "preset:Fire"},
            {"path": "preset:Smoke"},
            {"path": "preset:Sparks"}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    ASSERT_EQ(asset->emitters.size(), 3u);
    EXPECT_EQ(asset->emitters[0].id, "e0");
    EXPECT_EQ(asset->emitters[1].id, "e1");
    EXPECT_EQ(asset->emitters[2].id, "e2");
}

TEST(VfxSystemAsset, LoadFromString_AutoGeneratesWhenExplicitEmptyString) {
    // If an author writes "id": "", we treat it as "missing" and auto-generate.
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1,
        "emitters": [ {"path": "preset:Fire", "id": ""} ]
    })";
    auto asset = VfxSystem::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->emitters[0].id, "e0");
}

// ─────────────────────────────────────────────────────────────────────────────
// Failure paths — malformed or invalid JSON
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemAsset, LoadFromString_RejectsMalformedJson) {
    EXPECT_EQ(VfxSystem::LoadFromString("{not json"), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsMissingFormat) {
    constexpr const char* json = R"({
        "version": 1, "emitters": [{"path":"preset:Fire"}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsWrongFormat) {
    constexpr const char* json = R"({
        "format": "uzdecal", "version": 1, "emitters": [{"path":"preset:Fire"}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsMissingVersion) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "emitters": [{"path":"preset:Fire"}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsNonIntegerVersion) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": "1", "emitters": [{"path":"preset:Fire"}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsMissingEmitters) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsEmptyEmitters) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1, "emitters": []
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsEmittersNotArray) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1,
        "emitters": {"path":"preset:Fire"}
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsEmitterMissingPath) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1,
        "emitters": [ {"name":"x"} ]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsEmitterEmptyPath) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1,
        "emitters": [ {"path": ""} ]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsEmitterNotObject) {
    constexpr const char* json = R"({
        "format": "uzvfxsys", "version": 1,
        "emitters": [ "preset:Fire" ]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsNonBooleanEnabled) {
    constexpr const char* json = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[{"path":"preset:Fire","enabled":"yes"}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsNonBooleanLocalSpace) {
    constexpr const char* json = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[{"path":"preset:Fire","localSpace":1}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

TEST(VfxSystemAsset, LoadFromString_RejectsNonStringName) {
    constexpr const char* json = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[{"path":"preset:Fire","name":123}]
    })";
    EXPECT_EQ(VfxSystem::LoadFromString(json), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveToFile round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemAsset, SaveToFile_RoundTripsThroughDisk) {
    auto original = VfxSystem::LoadFromString(kFireballJson);
    ASSERT_NE(original, nullptr);

    // Use the OS temp dir so parallel test runs don't collide.
    const auto tmpPath = std::filesystem::temp_directory_path() /
                         "uz_vfx_system_roundtrip.uzvfxsys";
    const std::string tmp = tmpPath.string();

    ASSERT_TRUE(original->SaveToFile(tmp));

    auto reloaded = VfxSystem::Load(tmp, /*vfs=*/nullptr);
    ASSERT_NE(reloaded, nullptr);

    ASSERT_EQ(reloaded->emitters.size(), original->emitters.size());
    for (size_t i = 0; i < reloaded->emitters.size(); ++i) {
        EXPECT_EQ(reloaded->emitters[i].name,        original->emitters[i].name);
        EXPECT_EQ(reloaded->emitters[i].emitterPath, original->emitters[i].emitterPath);
        EXPECT_EQ(reloaded->emitters[i].id,          original->emitters[i].id);
        EXPECT_EQ(reloaded->emitters[i].enabled,     original->emitters[i].enabled);
        EXPECT_EQ(reloaded->emitters[i].localSpace,  original->emitters[i].localSpace);
    }
    EXPECT_EQ(reloaded->name, original->name);
    EXPECT_EQ(reloaded->sourcePath, tmp);

    std::filesystem::remove(tmpPath);
}

TEST(VfxSystemAsset, Load_ReturnsNullForMissingFile) {
    EXPECT_EQ(VfxSystem::Load("does/not/exist.uzvfxsys", nullptr), nullptr);
}
