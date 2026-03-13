/// UzEngine .uzshader Format — Tests
///
/// Tests serialization round-trip, header validation, corruption detection,
/// and edge cases (empty bytecode, large reflection, version mismatch).

#include "uzshader_format.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Header constants
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzShaderFormat, HeaderSizeIs32Bytes) {
    EXPECT_EQ(sizeof(UzShaderHeader), 32u);
}

TEST(UzShaderFormat, MagicConstantMatchesUZSH) {
    EXPECT_EQ(kUzShaderMagic, 0x48535A55u);
}

TEST(UzShaderFormat, VersionIs1) {
    EXPECT_EQ(kUzShaderVersion, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader model packing
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzShaderFormat, PackShaderModelSM66) {
    EXPECT_EQ(PackShaderModel(6, 6), 0x0606u);
}

TEST(UzShaderFormat, PackShaderModelSM51) {
    EXPECT_EQ(PackShaderModel(5, 1), 0x0501u);
}

TEST(UzShaderFormat, UnpackShaderModelMajor) {
    EXPECT_EQ(UnpackShaderModelMajor(0x0606), 6u);
    EXPECT_EQ(UnpackShaderModelMajor(0x0501), 5u);
}

TEST(UzShaderFormat, UnpackShaderModelMinor) {
    EXPECT_EQ(UnpackShaderModelMinor(0x0606), 6u);
    EXPECT_EQ(UnpackShaderModelMinor(0x0501), 1u);
}

TEST(UzShaderFormat, PackUnpackRoundTrip) {
    for (uint32_t major = 5; major <= 6; ++major) {
        for (uint32_t minor = 0; minor <= 7; ++minor) {
            uint32_t packed = PackShaderModel(major, minor);
            EXPECT_EQ(UnpackShaderModelMajor(packed), major);
            EXPECT_EQ(UnpackShaderModelMinor(packed), minor);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Write + Read round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzShaderFormat, RoundTripBytecodeOnly) {
    std::vector<uint8_t> bytecode = {0x44, 0x58, 0x42, 0x43, 0x01, 0x02, 0x03, 0x04};

    auto blob = WriteUzShader(
        UzShaderStage::Vertex,
        PackShaderModel(6, 6),
        UzShaderFormat::DXIL,
        bytecode);

    EXPECT_EQ(blob.size(), sizeof(UzShaderHeader) + bytecode.size());

    auto result = ReadUzShader(blob);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.file.header.magic, kUzShaderMagic);
    EXPECT_EQ(result.file.header.version, kUzShaderVersion);
    EXPECT_EQ(result.file.header.stage, static_cast<uint32_t>(UzShaderStage::Vertex));
    EXPECT_EQ(result.file.header.model, PackShaderModel(6, 6));
    EXPECT_EQ(result.file.header.format, static_cast<uint32_t>(UzShaderFormat::DXIL));
    EXPECT_EQ(result.file.header.bytecodeSize, bytecode.size());
    EXPECT_EQ(result.file.header.reflectSize, 0u);
    EXPECT_EQ(result.file.bytecode, bytecode);
    EXPECT_TRUE(result.file.reflection.empty());
}

TEST(UzShaderFormat, RoundTripWithReflection) {
    std::vector<uint8_t> bytecode = {0xAA, 0xBB};
    std::vector<uint8_t> reflection = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto blob = WriteUzShader(
        UzShaderStage::Pixel,
        PackShaderModel(6, 0),
        UzShaderFormat::SPIRV,
        bytecode,
        reflection);

    EXPECT_EQ(blob.size(), sizeof(UzShaderHeader) + bytecode.size() + reflection.size());

    auto result = ReadUzShader(blob);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.file.header.stage, static_cast<uint32_t>(UzShaderStage::Pixel));
    EXPECT_EQ(result.file.header.format, static_cast<uint32_t>(UzShaderFormat::SPIRV));
    EXPECT_EQ(result.file.bytecode, bytecode);
    EXPECT_EQ(result.file.reflection, reflection);
}

TEST(UzShaderFormat, RoundTripAllStages) {
    std::vector<uint8_t> bytecode = {0xFF};

    const UzShaderStage stages[] = {
        UzShaderStage::Vertex, UzShaderStage::Pixel, UzShaderStage::Compute,
        UzShaderStage::Geometry, UzShaderStage::Hull, UzShaderStage::Domain,
        UzShaderStage::Amplification, UzShaderStage::Mesh,
    };

    for (auto stage : stages) {
        auto blob = WriteUzShader(stage, PackShaderModel(6, 6), UzShaderFormat::DXIL, bytecode);
        auto result = ReadUzShader(blob);
        ASSERT_TRUE(result.success) << result.error;
        EXPECT_EQ(result.file.header.stage, static_cast<uint32_t>(stage));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Error cases
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzShaderFormat, ReadEmptyDataFails) {
    auto result = ReadUzShader({});
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST(UzShaderFormat, ReadTooSmallDataFails) {
    std::vector<uint8_t> data(16, 0);
    auto result = ReadUzShader(data);
    EXPECT_FALSE(result.success);
}

TEST(UzShaderFormat, ReadBadMagicFails) {
    std::vector<uint8_t> bytecode = {0x01};
    auto blob = WriteUzShader(UzShaderStage::Vertex, 0x0606, UzShaderFormat::DXIL, bytecode);

    // Corrupt the magic
    blob[0] = 0x00;
    auto result = ReadUzShader(blob);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("magic"), std::string::npos);
}

TEST(UzShaderFormat, ReadBadVersionFails) {
    std::vector<uint8_t> bytecode = {0x01};
    auto blob = WriteUzShader(UzShaderStage::Vertex, 0x0606, UzShaderFormat::DXIL, bytecode);

    // Corrupt the version (bytes 4-7)
    blob[4] = 0xFF;
    auto result = ReadUzShader(blob);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("version"), std::string::npos);
}

TEST(UzShaderFormat, ReadTruncatedBytecodeFails) {
    std::vector<uint8_t> bytecode = {0x01, 0x02, 0x03, 0x04};
    auto blob = WriteUzShader(UzShaderStage::Vertex, 0x0606, UzShaderFormat::DXIL, bytecode);

    // Truncate the blob — remove last 2 bytes
    blob.resize(blob.size() - 2);
    auto result = ReadUzShader(blob);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("truncated"), std::string::npos);
}

TEST(UzShaderFormat, ReadTruncatedReflectionFails) {
    std::vector<uint8_t> bytecode = {0x01};
    std::vector<uint8_t> reflection = {0x02, 0x03, 0x04};
    auto blob = WriteUzShader(UzShaderStage::Vertex, 0x0606, UzShaderFormat::DXIL, bytecode, reflection);

    // Truncate reflection
    blob.resize(blob.size() - 2);
    auto result = ReadUzShader(blob);
    EXPECT_FALSE(result.success);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzShaderFormat, EmptyBytecodeRoundTrips) {
    std::vector<uint8_t> bytecode;
    auto blob = WriteUzShader(UzShaderStage::Compute, 0x0606, UzShaderFormat::DXIL, bytecode);

    EXPECT_EQ(blob.size(), sizeof(UzShaderHeader));

    auto result = ReadUzShader(blob);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.file.bytecode.empty());
    EXPECT_EQ(result.file.header.bytecodeSize, 0u);
}

TEST(UzShaderFormat, LargeReflectionRoundTrips) {
    std::vector<uint8_t> bytecode = {0x01};
    std::vector<uint8_t> reflection(4096, 0xCC);

    auto blob = WriteUzShader(UzShaderStage::Pixel, 0x0600, UzShaderFormat::SPIRV, bytecode, reflection);
    auto result = ReadUzShader(blob);
    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.file.reflection.size(), 4096u);
    EXPECT_EQ(result.file.reflection, reflection);
}

} // namespace uz::test
