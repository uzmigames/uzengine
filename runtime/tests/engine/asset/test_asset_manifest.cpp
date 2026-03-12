/// UzEngine Asset Manifest — Tests
///
/// Tests the full manifest lifecycle: writer serialization, reader loading,
/// binary search queries, dependency resolution, file I/O, error handling
/// for corrupted/truncated data, and edge cases (empty manifest, large
/// entry counts, missing entries).

#include "asset_manifest.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifestWriter — serialization
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetManifestWriter, EmptyWriterProducesValidHeader) {
    AssetManifestWriter writer;
    auto data = writer.Serialize();

    ASSERT_GE(data.size(), sizeof(ManifestHeader));

    ManifestHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    EXPECT_EQ(header.magic, kManifestMagic);
    EXPECT_EQ(header.version, kManifestVersion);
    EXPECT_EQ(header.entryCount, 0u);
    EXPECT_EQ(header.flags, 0u);
}

TEST(AssetManifestWriter, SingleEntrySerializes) {
    AssetManifestWriter writer;
    writer.AddEntry({"shaders/test.uzshader", 1024, AssetType::Shader, {}});

    EXPECT_EQ(writer.EntryCount(), 1u);

    auto data = writer.Serialize();
    size_t expectedSize = sizeof(ManifestHeader) + sizeof(ManifestEntry);
    EXPECT_EQ(data.size(), expectedSize);
}

TEST(AssetManifestWriter, MultipleEntriesSortedByHash) {
    AssetManifestWriter writer;
    writer.AddEntry({"z_last.bin", 100, AssetType::Generic, {}});
    writer.AddEntry({"a_first.bin", 200, AssetType::Generic, {}});
    writer.AddEntry({"m_middle.bin", 300, AssetType::Generic, {}});

    auto data = writer.Serialize();

    ManifestHeader header;
    std::memcpy(&header, data.data(), sizeof(header));
    ASSERT_EQ(header.entryCount, 3u);

    // Verify entries are sorted by pathHash (ascending)
    const auto* entries = reinterpret_cast<const ManifestEntry*>(
        data.data() + sizeof(ManifestHeader));

    EXPECT_LE(entries[0].pathHash, entries[1].pathHash);
    EXPECT_LE(entries[1].pathHash, entries[2].pathHash);
}

TEST(AssetManifestWriter, EntryWithDependenciesHasDepTable) {
    AssetManifestWriter writer;
    writer.AddEntry({
        "shaders/pbr.uzshader",
        2048,
        AssetType::Shader,
        {"common.hlsli", "brdf.hlsli"},
    });

    auto data = writer.Serialize();

    // Should have header + 1 entry + 2 deps (2 * 8 bytes)
    size_t expectedSize = sizeof(ManifestHeader)
                        + sizeof(ManifestEntry)
                        + 2 * sizeof(uint64_t);
    EXPECT_EQ(data.size(), expectedSize);

    const auto* entry = reinterpret_cast<const ManifestEntry*>(
        data.data() + sizeof(ManifestHeader));
    EXPECT_EQ(entry->depCount, 2u);
    EXPECT_EQ(entry->depOffset, 0u);
}

TEST(AssetManifestWriter, EntryCountTracksAdditions) {
    AssetManifestWriter writer;
    EXPECT_EQ(writer.EntryCount(), 0u);

    writer.AddEntry({"a.bin", 1, AssetType::Generic, {}});
    EXPECT_EQ(writer.EntryCount(), 1u);

    writer.AddEntry({"b.bin", 2, AssetType::Generic, {}});
    EXPECT_EQ(writer.EntryCount(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifest — round-trip (write then read)
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetManifest, RoundTripSingleEntry) {
    AssetManifestWriter writer;
    writer.AddEntry({"textures/brick.uztexture", 65536, AssetType::Texture, {}});

    auto data = writer.Serialize();

    AssetManifest manifest;
    ASSERT_TRUE(manifest.Load(data));
    EXPECT_TRUE(manifest.IsLoaded());
    EXPECT_EQ(manifest.EntryCount(), 1u);

    auto* result = manifest.Find("textures/brick.uztexture");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->size, 65536u);
    EXPECT_EQ(result->type, AssetType::Texture);
    EXPECT_TRUE(result->dependencies.empty());
}

TEST(AssetManifest, RoundTripMultipleEntries) {
    AssetManifestWriter writer;
    writer.AddEntry({"a.bin", 100, AssetType::Generic, {}});
    writer.AddEntry({"b.uzshader", 200, AssetType::Shader, {}});
    writer.AddEntry({"c.uztexture", 300, AssetType::Texture, {}});

    auto data = writer.Serialize();

    AssetManifest manifest;
    ASSERT_TRUE(manifest.Load(data));
    EXPECT_EQ(manifest.EntryCount(), 3u);

    EXPECT_NE(manifest.Find("a.bin"), nullptr);
    EXPECT_NE(manifest.Find("b.uzshader"), nullptr);
    EXPECT_NE(manifest.Find("c.uztexture"), nullptr);

    EXPECT_EQ(manifest.Find("b.uzshader")->size, 200u);
    EXPECT_EQ(manifest.Find("c.uztexture")->type, AssetType::Texture);
}

TEST(AssetManifest, RoundTripWithDependencies) {
    AssetManifestWriter writer;
    writer.AddEntry({
        "shaders/pbr.uzshader",
        4096,
        AssetType::Shader,
        {"common.hlsli", "brdf.hlsli", "lighting.hlsli"},
    });

    auto data = writer.Serialize();

    AssetManifest manifest;
    ASSERT_TRUE(manifest.Load(data));

    auto* result = manifest.Find("shaders/pbr.uzshader");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->dependencies.size(), 3u);

    // Verify each dep hash is non-zero (valid xxHash64)
    for (uint64_t depHash : result->dependencies) {
        EXPECT_NE(depHash, 0u);
    }
}

TEST(AssetManifest, RoundTripEmptyManifest) {
    AssetManifestWriter writer;
    auto data = writer.Serialize();

    AssetManifest manifest;
    ASSERT_TRUE(manifest.Load(data));
    EXPECT_TRUE(manifest.IsLoaded());
    EXPECT_EQ(manifest.EntryCount(), 0u);
}

TEST(AssetManifest, RoundTripMixedDepsAndNoDeps) {
    AssetManifestWriter writer;
    writer.AddEntry({"no_deps.bin", 100, AssetType::Generic, {}});
    writer.AddEntry({"with_deps.bin", 200, AssetType::Shader, {"dep1.h", "dep2.h"}});
    writer.AddEntry({"also_no_deps.bin", 300, AssetType::Texture, {}});

    auto data = writer.Serialize();

    AssetManifest manifest;
    ASSERT_TRUE(manifest.Load(data));
    EXPECT_EQ(manifest.EntryCount(), 3u);

    auto* noDeps = manifest.Find("no_deps.bin");
    ASSERT_NE(noDeps, nullptr);
    EXPECT_TRUE(noDeps->dependencies.empty());

    auto* withDeps = manifest.Find("with_deps.bin");
    ASSERT_NE(withDeps, nullptr);
    EXPECT_EQ(withDeps->dependencies.size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifest — query operations
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetManifest, FindMissingPathReturnsNull) {
    AssetManifestWriter writer;
    writer.AddEntry({"exists.bin", 100, AssetType::Generic, {}});

    auto data = writer.Serialize();
    AssetManifest manifest;
    manifest.Load(data);

    EXPECT_EQ(manifest.Find("nonexistent.bin"), nullptr);
}

TEST(AssetManifest, ExistsReturnsTrueForKnownPath) {
    AssetManifestWriter writer;
    writer.AddEntry({"known.bin", 100, AssetType::Generic, {}});

    auto data = writer.Serialize();
    AssetManifest manifest;
    manifest.Load(data);

    EXPECT_TRUE(manifest.Exists("known.bin"));
    EXPECT_FALSE(manifest.Exists("unknown.bin"));
}

TEST(AssetManifest, FindByHashMatchesFind) {
    AssetManifestWriter writer;
    writer.AddEntry({"test_path.bin", 512, AssetType::Mesh, {}});

    auto data = writer.Serialize();
    AssetManifest manifest;
    manifest.Load(data);

    auto* byPath = manifest.Find("test_path.bin");
    ASSERT_NE(byPath, nullptr);

    auto* byHash = manifest.FindByHash(byPath->pathHash);
    ASSERT_NE(byHash, nullptr);
    EXPECT_EQ(byPath, byHash);
}

TEST(AssetManifest, FindByHashMissingReturnsNull) {
    AssetManifestWriter writer;
    writer.AddEntry({"a.bin", 1, AssetType::Generic, {}});

    auto data = writer.Serialize();
    AssetManifest manifest;
    manifest.Load(data);

    EXPECT_EQ(manifest.FindByHash(0xDEADBEEF), nullptr);
}

TEST(AssetManifest, GetEntriesReturnsSortedVector) {
    AssetManifestWriter writer;
    writer.AddEntry({"z.bin", 1, AssetType::Generic, {}});
    writer.AddEntry({"a.bin", 2, AssetType::Generic, {}});
    writer.AddEntry({"m.bin", 3, AssetType::Generic, {}});

    auto data = writer.Serialize();
    AssetManifest manifest;
    manifest.Load(data);

    const auto& entries = manifest.GetEntries();
    ASSERT_EQ(entries.size(), 3u);

    // Verify ascending pathHash order
    for (size_t i = 1; i < entries.size(); ++i) {
        EXPECT_LE(entries[i - 1].pathHash, entries[i].pathHash);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifest — error handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetManifest, LoadTooSmallDataFails) {
    AssetManifest manifest;
    std::vector<uint8_t> tiny = {0x01, 0x02, 0x03};
    EXPECT_FALSE(manifest.Load(tiny));
    EXPECT_FALSE(manifest.IsLoaded());
}

TEST(AssetManifest, LoadWrongMagicFails) {
    ManifestHeader header;
    header.magic = 0xBADC0DE;

    std::vector<uint8_t> data(sizeof(ManifestHeader));
    std::memcpy(data.data(), &header, sizeof(header));

    AssetManifest manifest;
    EXPECT_FALSE(manifest.Load(data));
}

TEST(AssetManifest, LoadWrongVersionFails) {
    ManifestHeader header;
    header.version = 999;

    std::vector<uint8_t> data(sizeof(ManifestHeader));
    std::memcpy(data.data(), &header, sizeof(header));

    AssetManifest manifest;
    EXPECT_FALSE(manifest.Load(data));
}

TEST(AssetManifest, LoadTruncatedEntriesFails) {
    ManifestHeader header;
    header.entryCount = 10;

    // Only provide the header, not the 10 entries
    std::vector<uint8_t> data(sizeof(ManifestHeader));
    std::memcpy(data.data(), &header, sizeof(header));

    AssetManifest manifest;
    EXPECT_FALSE(manifest.Load(data));
}

TEST(AssetManifest, LoadEmptyDataFails) {
    AssetManifest manifest;
    std::vector<uint8_t> empty;
    EXPECT_FALSE(manifest.Load(empty));
}

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifest — file I/O
// ─────────────────────────────────────────────────────────────────────────────

class ManifestFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_manifest").string();
        fs::create_directories(m_dir);
        m_path = m_dir + "/test.uzmanifest";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string m_dir;
    std::string m_path;
};

TEST_F(ManifestFileTest, WriteAndLoadFile) {
    AssetManifestWriter writer;
    writer.AddEntry({"shaders/vs.uzshader", 2048, AssetType::Shader, {"common.hlsli"}});
    writer.AddEntry({"textures/brick.uztexture", 65536, AssetType::Texture, {}});

    ASSERT_TRUE(WriteManifestFile(m_path, writer));

    AssetManifest manifest;
    ASSERT_TRUE(manifest.LoadFile(m_path));
    EXPECT_EQ(manifest.EntryCount(), 2u);

    auto* shader = manifest.Find("shaders/vs.uzshader");
    ASSERT_NE(shader, nullptr);
    EXPECT_EQ(shader->size, 2048u);
    EXPECT_EQ(shader->type, AssetType::Shader);
    EXPECT_EQ(shader->dependencies.size(), 1u);

    auto* texture = manifest.Find("textures/brick.uztexture");
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->size, 65536u);
    EXPECT_EQ(texture->type, AssetType::Texture);
}

TEST_F(ManifestFileTest, LoadNonexistentFileFails) {
    AssetManifest manifest;
    EXPECT_FALSE(manifest.LoadFile(m_dir + "/nonexistent.uzmanifest"));
    EXPECT_FALSE(manifest.IsLoaded());
}

TEST_F(ManifestFileTest, WriteToInvalidPathFails) {
    AssetManifestWriter writer;
    EXPECT_FALSE(WriteManifestFile("/nonexistent/dir/file.uzmanifest", writer));
}

// ─────────────────────────────────────────────────────────────────────────────
// Static assertions — struct sizes
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetManifestLayout, HeaderSize) {
    EXPECT_EQ(sizeof(ManifestHeader), 16u);
}

TEST(AssetManifestLayout, EntrySize) {
    EXPECT_EQ(sizeof(ManifestEntry), 32u);
}

TEST(AssetManifestLayout, MagicValue) {
    EXPECT_EQ(kManifestMagic, 0x464D5A55u);
}

} // namespace uz::test
