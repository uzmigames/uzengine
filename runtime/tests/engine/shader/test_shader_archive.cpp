/// Tests for ShaderArchive — binary archive for shipping shader bytecode.

#include "shader_archive.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class ShaderArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_testDir = fs::temp_directory_path() / "uzengine_test" / "shader_archive";
        // Clean up stale leftovers from a previous aborted run
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
        fs::create_directories(m_testDir);
        m_archivePath = m_testDir / "shaders.uzpak";
    }

    void TearDown() override {
        fs::remove_all(m_testDir);
    }

    /// Helper to create bytecode from a string.
    static std::vector<uint8_t> MakeBytecode(const std::string& content) {
        return std::vector<uint8_t>(content.begin(), content.end());
    }

    /// Helper to create bytecode of a given size with a fill pattern.
    static std::vector<uint8_t> MakeBytecodeOfSize(size_t size, uint8_t fill = 0xAB) {
        return std::vector<uint8_t>(size, fill);
    }

    /// Helper to create a single test entry.
    static uz::ShaderArchiveEntry MakeEntry(const std::string& hash, const std::string& data) {
        return {hash, MakeBytecode(data)};
    }

    fs::path m_testDir;
    fs::path m_archivePath;
};

// ─────────────────────────────────────────────────────────────────────────────
// Build tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, BuildEmptyArchive) {
    std::vector<uz::ShaderArchiveEntry> entries;
    EXPECT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));
    EXPECT_TRUE(fs::exists(m_archivePath));

    // Verify file has correct header size (16 bytes, no entries)
    EXPECT_EQ(fs::file_size(m_archivePath), 16u);
}

TEST_F(ShaderArchiveTest, BuildSingleEntry) {
    std::vector<uz::ShaderArchiveEntry> entries = {
        MakeEntry("abc123", "DXIL_DATA_HERE"),
    };
    EXPECT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));
    EXPECT_TRUE(fs::exists(m_archivePath));

    // Header(16) + 1 entry(64+8+8=80) + data(14)
    EXPECT_EQ(fs::file_size(m_archivePath), 16u + 80u + 14u);
}

TEST_F(ShaderArchiveTest, BuildMultipleEntries) {
    std::vector<uz::ShaderArchiveEntry> entries = {
        MakeEntry("hash_a", "bytecode_alpha"),
        MakeEntry("hash_b", "bytecode_beta"),
        MakeEntry("hash_c", "bytecode_gamma"),
    };
    EXPECT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));
    EXPECT_TRUE(fs::exists(m_archivePath));

    // Header(16) + 3 entries(3*80=240) + data(14+13+14=41)
    EXPECT_EQ(fs::file_size(m_archivePath), 16u + 240u + 41u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Load validation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, LoadAndVerifyMagicVersion) {
    std::vector<uz::ShaderArchiveEntry> entries = {MakeEntry("h1", "data")};
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    EXPECT_TRUE(archive.Load(m_archivePath));
    EXPECT_TRUE(archive.IsLoaded());
    EXPECT_EQ(archive.EntryCount(), 1u);
}

TEST_F(ShaderArchiveTest, LoadInvalidMagicFails) {
    // Write a file with wrong magic
    std::ofstream f(m_archivePath, std::ios::binary);
    uint32_t badMagic = 0xDEADBEEF;
    uint32_t version = 1;
    uint32_t count = 0;
    uint32_t reserved = 0;
    f.write(reinterpret_cast<const char*>(&badMagic), 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&count), 4);
    f.write(reinterpret_cast<const char*>(&reserved), 4);
    f.close();

    uz::ShaderArchive archive;
    EXPECT_FALSE(archive.Load(m_archivePath));
    EXPECT_FALSE(archive.IsLoaded());
}

TEST_F(ShaderArchiveTest, LoadInvalidVersionFails) {
    std::ofstream f(m_archivePath, std::ios::binary);
    uint32_t magic = uz::kShaderArchiveMagic;
    uint32_t badVersion = 99;
    uint32_t count = 0;
    uint32_t reserved = 0;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&badVersion), 4);
    f.write(reinterpret_cast<const char*>(&count), 4);
    f.write(reinterpret_cast<const char*>(&reserved), 4);
    f.close();

    uz::ShaderArchive archive;
    EXPECT_FALSE(archive.Load(m_archivePath));
    EXPECT_FALSE(archive.IsLoaded());
}

TEST_F(ShaderArchiveTest, LoadNonExistentFileFails) {
    uz::ShaderArchive archive;
    EXPECT_FALSE(archive.Load(m_testDir / "does_not_exist.uzpak"));
    EXPECT_FALSE(archive.IsLoaded());
}

// ─────────────────────────────────────────────────────────────────────────────
// Contains
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, ContainsTrueForExistingHash) {
    std::vector<uz::ShaderArchiveEntry> entries = {MakeEntry("found_me", "data")};
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_TRUE(archive.Contains("found_me"));
}

TEST_F(ShaderArchiveTest, ContainsFalseForMissingHash) {
    std::vector<uz::ShaderArchiveEntry> entries = {MakeEntry("existing", "data")};
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_FALSE(archive.Contains("nonexistent"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, LookupReturnsCorrectBytecode) {
    auto bytecodeA = MakeBytecode("alpha_shader_bytecode");
    auto bytecodeB = MakeBytecode("beta_shader_bytecode");

    std::vector<uz::ShaderArchiveEntry> entries = {
        {"hash_alpha", bytecodeA},
        {"hash_beta", bytecodeB},
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));

    EXPECT_EQ(archive.Lookup("hash_alpha"), bytecodeA);
    EXPECT_EQ(archive.Lookup("hash_beta"), bytecodeB);
}

TEST_F(ShaderArchiveTest, LookupReturnsEmptyForMissingHash) {
    std::vector<uz::ShaderArchiveEntry> entries = {MakeEntry("only_hash", "data")};
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_TRUE(archive.Lookup("missing_hash").empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// EntryCount
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, EntryCountCorrectness) {
    std::vector<uz::ShaderArchiveEntry> entries = {
        MakeEntry("a", "1"),
        MakeEntry("b", "2"),
        MakeEntry("c", "3"),
        MakeEntry("d", "4"),
        MakeEntry("e", "5"),
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_EQ(archive.EntryCount(), 5u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetHashes
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, GetHashesReturnsAllHashes) {
    std::vector<uz::ShaderArchiveEntry> entries = {
        MakeEntry("hash_x", "x"),
        MakeEntry("hash_y", "y"),
        MakeEntry("hash_z", "z"),
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));

    auto hashes = archive.GetHashes();
    ASSERT_EQ(hashes.size(), 3u);

    // Sort for deterministic comparison (unordered_map iteration order is unspecified)
    std::sort(hashes.begin(), hashes.end());
    EXPECT_EQ(hashes[0], "hash_x");
    EXPECT_EQ(hashes[1], "hash_y");
    EXPECT_EQ(hashes[2], "hash_z");
}

// ─────────────────────────────────────────────────────────────────────────────
// Unload
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, UnloadClearsState) {
    std::vector<uz::ShaderArchiveEntry> entries = {MakeEntry("h", "d")};
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_TRUE(archive.IsLoaded());
    EXPECT_EQ(archive.EntryCount(), 1u);

    archive.Unload();
    EXPECT_FALSE(archive.IsLoaded());
    EXPECT_EQ(archive.EntryCount(), 0u);
    EXPECT_FALSE(archive.Contains("h"));
    EXPECT_TRUE(archive.Lookup("h").empty());
    EXPECT_TRUE(archive.GetHashes().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// IsLoaded state tracking
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, IsLoadedStateTracking) {
    uz::ShaderArchive archive;
    EXPECT_FALSE(archive.IsLoaded());

    std::vector<uz::ShaderArchiveEntry> entries = {MakeEntry("h", "d")};
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_TRUE(archive.IsLoaded());

    archive.Unload();
    EXPECT_FALSE(archive.IsLoaded());
}

// ─────────────────────────────────────────────────────────────────────────────
// Double Load replaces previous
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, DoubleLoadReplacesPrevious) {
    // Build first archive with entry A
    auto pathA = m_testDir / "archive_a.uzpak";
    std::vector<uz::ShaderArchiveEntry> entriesA = {MakeEntry("hash_a", "data_a")};
    ASSERT_TRUE(uz::ShaderArchive::Build(pathA, entriesA));

    // Build second archive with entry B
    auto pathB = m_testDir / "archive_b.uzpak";
    std::vector<uz::ShaderArchiveEntry> entriesB = {
        MakeEntry("hash_b", "data_b"),
        MakeEntry("hash_c", "data_c"),
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(pathB, entriesB));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(pathA));
    EXPECT_EQ(archive.EntryCount(), 1u);
    EXPECT_TRUE(archive.Contains("hash_a"));

    // Load second archive — should replace first
    ASSERT_TRUE(archive.Load(pathB));
    EXPECT_EQ(archive.EntryCount(), 2u);
    EXPECT_FALSE(archive.Contains("hash_a"));
    EXPECT_TRUE(archive.Contains("hash_b"));
    EXPECT_TRUE(archive.Contains("hash_c"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Build + Load roundtrip
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, RoundtripPreservesAllData) {
    std::vector<uz::ShaderArchiveEntry> entries = {
        {"abcdef0123456789", MakeBytecode("shader_pbr_vertex")},
        {"fedcba9876543210", MakeBytecode("shader_pbr_pixel")},
        {"0000000000000000", MakeBytecode("shader_shadow_vertex")},
        {"ffffffffffffffff", MakeBytecode("shader_postprocess")},
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_EQ(archive.EntryCount(), entries.size());

    for (const auto& entry : entries) {
        EXPECT_TRUE(archive.Contains(entry.hash)) << "Missing hash: " << entry.hash;
        EXPECT_EQ(archive.Lookup(entry.hash), entry.bytecode) << "Bytecode mismatch: " << entry.hash;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Large bytecode entry (1MB+)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, LargeBytecodeEntry) {
    constexpr size_t kOneMB = 1024 * 1024;
    auto largeBytecode = MakeBytecodeOfSize(kOneMB + 512, 0xCD);

    std::vector<uz::ShaderArchiveEntry> entries = {
        {"small_shader", MakeBytecode("tiny")},
        {"large_shader", largeBytecode},
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));

    auto result = archive.Lookup("large_shader");
    ASSERT_EQ(result.size(), kOneMB + 512);
    EXPECT_EQ(result, largeBytecode);

    // Verify small entry is still correct
    EXPECT_EQ(archive.Lookup("small_shader"), MakeBytecode("tiny"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Empty bytecode entry
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderArchiveTest, EmptyBytecodeEntry) {
    std::vector<uz::ShaderArchiveEntry> entries = {
        {"empty_hash", {}},
        {"nonempty_hash", MakeBytecode("has_data")},
    };
    ASSERT_TRUE(uz::ShaderArchive::Build(m_archivePath, entries));

    uz::ShaderArchive archive;
    ASSERT_TRUE(archive.Load(m_archivePath));
    EXPECT_EQ(archive.EntryCount(), 2u);

    EXPECT_TRUE(archive.Lookup("empty_hash").empty());
    EXPECT_EQ(archive.Lookup("nonempty_hash"), MakeBytecode("has_data"));
}
