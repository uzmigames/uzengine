/// UzEngine Import Sidecar — Tests
///
/// Tests JSON round-trip, file I/O, reimport detection, hex conversion,
/// platform variant support, and error handling.

#include "import_sidecar.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Hex conversion
// ─────────────────────────────────────────────────────────────────────────────

TEST(ImportSidecar, HashToHexFormats16Chars) {
    EXPECT_EQ(HashToHex(0), "0000000000000000");
    EXPECT_EQ(HashToHex(0xDEADBEEFCAFE1234), "deadbeefcafe1234");
    EXPECT_EQ(HashToHex(1), "0000000000000001");
}

TEST(ImportSidecar, HexToHashRoundTrip) {
    uint64_t original = 0xABCDEF0123456789;
    std::string hex = HashToHex(original);
    EXPECT_EQ(HexToHash(hex), original);
}

TEST(ImportSidecar, HexToHashReturnsZeroOnBadInput) {
    EXPECT_EQ(HexToHash(""), 0u);
    EXPECT_EQ(HexToHash("short"), 0u);
    EXPECT_EQ(HexToHash("zzzzzzzzzzzzzzzz"), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(ImportSidecar, JsonRoundTrip) {
    ImportSidecar original;
    original.importer = "shader";
    original.version = 1;
    original.sourceHash = "a1b2c3d4e5f67890";
    original.options = {{"entry_point", "main"}, {"stage", "vertex"}, {"model", "sm6_6"}};
    original.outputs = {{"sm6_6", ".cache/imported/a1b2c3d4e5f67890.bin"}};
    original.dependencies = {"common.hlsli"};

    std::string json = WriteSidecarJson(original);
    auto result = ReadSidecarJson(json);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.sidecar.importer, "shader");
    EXPECT_EQ(result.sidecar.version, 1u);
    EXPECT_EQ(result.sidecar.sourceHash, "a1b2c3d4e5f67890");
    EXPECT_EQ(result.sidecar.options.size(), 3u);
    EXPECT_EQ(result.sidecar.options.at("stage"), "vertex");
    EXPECT_EQ(result.sidecar.outputs.size(), 1u);
    EXPECT_EQ(result.sidecar.outputs.at("sm6_6"), ".cache/imported/a1b2c3d4e5f67890.bin");
    EXPECT_EQ(result.sidecar.dependencies.size(), 1u);
    EXPECT_EQ(result.sidecar.dependencies[0], "common.hlsli");
}

TEST(ImportSidecar, JsonRoundTripEmptyFields) {
    ImportSidecar original;
    original.importer = "texture";
    original.version = 2;
    original.sourceHash = "0000000000000000";

    std::string json = WriteSidecarJson(original);
    auto result = ReadSidecarJson(json);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.sidecar.importer, "texture");
    EXPECT_TRUE(result.sidecar.options.empty());
    EXPECT_TRUE(result.sidecar.outputs.empty());
    EXPECT_TRUE(result.sidecar.dependencies.empty());
}

TEST(ImportSidecar, JsonMultipleOutputVariants) {
    ImportSidecar original;
    original.importer = "shader";
    original.version = 1;
    original.sourceHash = "1111111111111111";
    original.outputs = {
        {"sm6_6", ".cache/imported/1111_sm66.bin"},
        {"sm6_0", ".cache/imported/1111_sm60.bin"},
        {"sm5_1", ".cache/imported/1111_sm51.bin"},
    };

    std::string json = WriteSidecarJson(original);
    auto result = ReadSidecarJson(json);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.sidecar.outputs.size(), 3u);
    EXPECT_EQ(result.sidecar.outputs.at("sm6_6"), ".cache/imported/1111_sm66.bin");
    EXPECT_EQ(result.sidecar.outputs.at("sm6_0"), ".cache/imported/1111_sm60.bin");
    EXPECT_EQ(result.sidecar.outputs.at("sm5_1"), ".cache/imported/1111_sm51.bin");
}

TEST(ImportSidecar, ReadInvalidJsonFails) {
    auto result = ReadSidecarJson("not valid json {{{");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST(ImportSidecar, ReadEmptyJsonSucceeds) {
    auto result = ReadSidecarJson("{}");
    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.sidecar.importer.empty());
    EXPECT_EQ(result.sidecar.version, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

class SidecarFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_sidecar").string();
        fs::create_directories(m_dir);
        m_path = m_dir + "/test.uzimport";
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string m_dir;
    std::string m_path;
};

TEST_F(SidecarFileTest, WriteAndReadFile) {
    ImportSidecar sidecar;
    sidecar.importer = "shader";
    sidecar.version = 1;
    sidecar.sourceHash = "abcdef0123456789";
    sidecar.options = {{"stage", "pixel"}};
    sidecar.outputs = {{"sm6_6", ".cache/imported/abcdef.bin"}};

    ASSERT_TRUE(WriteSidecarFile(m_path, sidecar));

    auto result = ReadSidecarFile(m_path);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.sidecar.importer, "shader");
    EXPECT_EQ(result.sidecar.sourceHash, "abcdef0123456789");
    EXPECT_EQ(result.sidecar.outputs.at("sm6_6"), ".cache/imported/abcdef.bin");
}

TEST_F(SidecarFileTest, ReadNonexistentFileFails) {
    auto result = ReadSidecarFile(m_dir + "/nonexistent.uzimport");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Reimport detection
// ─────────────────────────────────────────────────────────────────────────────

TEST(ImportSidecar, NeedsReimportReturnsFalseWhenUpToDate) {
    ImportSidecar sidecar;
    sidecar.sourceHash = "aabbccdd11223344";
    sidecar.version = 1;
    // No outputs → no missing file check

    EXPECT_FALSE(NeedsReimport(sidecar, "aabbccdd11223344", 1));
}

TEST(ImportSidecar, NeedsReimportReturnsTrueOnHashChange) {
    ImportSidecar sidecar;
    sidecar.sourceHash = "aabbccdd11223344";
    sidecar.version = 1;

    EXPECT_TRUE(NeedsReimport(sidecar, "different_hash_00", 1));
}

TEST(ImportSidecar, NeedsReimportReturnsTrueOnVersionBump) {
    ImportSidecar sidecar;
    sidecar.sourceHash = "aabbccdd11223344";
    sidecar.version = 1;

    EXPECT_TRUE(NeedsReimport(sidecar, "aabbccdd11223344", 2));
}

class SidecarReimportTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_dir = (fs::temp_directory_path() / "uz_test_reimport").string();
        fs::create_directories(m_dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    std::string m_dir;
};

TEST_F(SidecarReimportTest, NeedsReimportReturnsTrueOnMissingOutput) {
    ImportSidecar sidecar;
    sidecar.sourceHash = "1234567890abcdef";
    sidecar.version = 1;
    sidecar.outputs = {{"sm6_6", m_dir + "/nonexistent.bin"}};

    EXPECT_TRUE(NeedsReimport(sidecar, "1234567890abcdef", 1));
}

TEST_F(SidecarReimportTest, NeedsReimportReturnsFalseWhenOutputExists) {
    std::string outputPath = m_dir + "/output.bin";
    std::ofstream(outputPath) << "data";

    ImportSidecar sidecar;
    sidecar.sourceHash = "1234567890abcdef";
    sidecar.version = 1;
    sidecar.outputs = {{"sm6_6", outputPath}};

    EXPECT_FALSE(NeedsReimport(sidecar, "1234567890abcdef", 1));
}

TEST_F(SidecarReimportTest, NeedsReimportChecksAllVariants) {
    std::string existing = m_dir + "/existing.bin";
    std::ofstream(existing) << "data";

    ImportSidecar sidecar;
    sidecar.sourceHash = "1234567890abcdef";
    sidecar.version = 1;
    sidecar.outputs = {
        {"sm6_6", existing},
        {"sm6_0", m_dir + "/missing.bin"},
    };

    // One output missing → reimport needed
    EXPECT_TRUE(NeedsReimport(sidecar, "1234567890abcdef", 1));
}

} // namespace uz::test
