/// UzEngine Texture Importer — Unit Tests
///
/// Tests TextureImporter interface contract (Name/Version/Extensions) and
/// the Import() pipeline for valid PNG, empty data, and invalid data.
/// The minimal PNG is a hardcoded 2x2 RGBA8 image — no file I/O required.

#include "texture_importer.h"
#include "uztexture_format.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal valid PNG binary (2x2 RGBA pixels)
//
// Pixels (row-major, RGBA):
//   (0,0) = 255,  0,  0, 255  (red)
//   (1,0) =   0,255,  0, 255  (green)
//   (0,1) =   0,  0,255, 255  (blue)
//   (1,1) = 255,255,  0, 255  (yellow)
// ─────────────────────────────────────────────────────────────────────────────

static const uint8_t kMinimal2x2PngBytes[] = {
0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49,
0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06,
0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x14, 0x49, 0x44,
0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x0C, 0x81, 0x34,
0x10, 0x30, 0xFC, 0x07, 0x00, 0x47, 0xCA, 0x08, 0xF8, 0x5B, 0x9A, 0xA4, 0xBE,
0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

static std::vector<uint8_t> MakeMinimalPngData() {
    return std::vector<uint8_t>(kMinimal2x2PngBytes,
                                kMinimal2x2PngBytes + sizeof(kMinimal2x2PngBytes));
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface contract
// ─────────────────────────────────────────────────────────────────────────────

TEST(TextureImporter, Name_ReturnsExactString) {
    TextureImporter imp;
    EXPECT_STREQ(imp.Name(), "texture");
}

TEST(TextureImporter, Version_ReturnsTwo) {
    TextureImporter imp;
    EXPECT_EQ(imp.Version(), 2u);
}

TEST(TextureImporter, Extensions_ContainsPng) {
    TextureImporter imp;
    const auto& exts = imp.Extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), ".png"), exts.end())
        << ".png must be in Extensions()";
}

TEST(TextureImporter, Extensions_ContainsJpg) {
    TextureImporter imp;
    const auto& exts = imp.Extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), ".jpg"), exts.end())
        << ".jpg must be in Extensions()";
}

TEST(TextureImporter, Extensions_ContainsJpeg) {
    TextureImporter imp;
    const auto& exts = imp.Extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), ".jpeg"), exts.end())
        << ".jpeg must be in Extensions()";
}

TEST(TextureImporter, Extensions_ContainsTga) {
    TextureImporter imp;
    const auto& exts = imp.Extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), ".tga"), exts.end())
        << ".tga must be in Extensions()";
}

TEST(TextureImporter, Extensions_HasAtLeastFourEntries) {
    TextureImporter imp;
    EXPECT_GE(imp.Extensions().size(), 4u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error paths
// ─────────────────────────────────────────────────────────────────────────────

TEST(TextureImporter, Import_EmptySourceData_ReturnsFalseWithError) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import({}, "empty.png", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

TEST(TextureImporter, Import_InvalidData_NotImage_ReturnsFalseWithError) {
    TextureImporter imp;
    AssetImportOptions opts;

    // Random non-image bytes
    std::vector<uint8_t> junk = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03,
                                  0xFF, 0xFE, 0xFD, 0xFC, 0xAB, 0xCD, 0xEF, 0x00};
    auto r = imp.Import(junk, "bad.png", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

TEST(TextureImporter, Import_CorruptPngSignature_ReturnsFalseWithError) {
    TextureImporter imp;
    AssetImportOptions opts;

    // Corrupt the PNG signature byte
    auto data = MakeMinimalPngData();
    data[0] = 0x00; // Break PNG signature
    auto r = imp.Import(data, "corrupt.png", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

TEST(TextureImporter, Import_TruncatedPng_ReturnsFalseWithError) {
    TextureImporter imp;
    AssetImportOptions opts;

    // Keep only the 8-byte signature — no chunks follow
    std::vector<uint8_t> truncated(kMinimal2x2PngBytes, kMinimal2x2PngBytes + 8);
    auto r = imp.Import(truncated, "truncated.png", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Valid PNG import
// ─────────────────────────────────────────────────────────────────────────────

TEST(TextureImporter, Import_ValidPng_ReturnsSuccess) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    EXPECT_TRUE(r.success) << "Import failed: " << r.error;
    EXPECT_TRUE(r.error.empty());
    EXPECT_FALSE(r.data.empty());
}

TEST(TextureImporter, Import_ValidPng_OutputIsValidUzTextureFile) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    EXPECT_TRUE(parsed.success) << "ReadUzTexture failed: " << parsed.error;
}

TEST(TextureImporter, Import_ValidPng_OutputHasCorrectMagic) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_GE(r.data.size(), sizeof(UzTextureHeader));

    UzTextureHeader h;
    std::memcpy(&h, r.data.data(), sizeof(h));
    EXPECT_EQ(h.magic, kUzTextureMagic);
    EXPECT_EQ(h.version, kUzTextureVersion);
}

TEST(TextureImporter, Import_ValidPng_Dimensions2x2) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.width, 2u);
    EXPECT_EQ(parsed.file.header.height, 2u);
}

TEST(TextureImporter, Import_ValidPng_FormatIsRGBA8) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.format,
              static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8));
}

TEST(TextureImporter, Import_ValidPng_2x2Image_HasTwoMipLevels) {
    // 2x2 image: mip 0 = 2x2, mip 1 = 1x1 (floor(log2(2)) + 1 = 2)
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.mipCount, 2u)
        << "2x2 image should produce exactly 2 mip levels (2x2 + 1x1)";
    EXPECT_EQ(parsed.file.mips.size(), 2u);
    EXPECT_EQ(parsed.file.mipData.size(), 2u);
}

TEST(TextureImporter, Import_ValidPng_Mip0Size_Is2x2x4Bytes) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mipData.size(), 1u);

    // 2x2 RGBA8 = 16 bytes
    EXPECT_EQ(parsed.file.mipData[0].size(), 2u * 2u * 4u);
}

TEST(TextureImporter, Import_ValidPng_Mip1Size_Is1x1x4Bytes) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mipData.size(), 2u);

    // 1x1 RGBA8 = 4 bytes
    EXPECT_EQ(parsed.file.mipData[1].size(), 1u * 1u * 4u);
}

TEST(TextureImporter, Import_ValidPng_Mip0Descriptors_CorrectDimensions) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mips.size(), 1u);
    EXPECT_EQ(parsed.file.mips[0].width, 2u);
    EXPECT_EQ(parsed.file.mips[0].height, 2u);
}

TEST(TextureImporter, Import_ValidPng_Mip1Descriptors_CorrectDimensions) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mips.size(), 2u);
    EXPECT_EQ(parsed.file.mips[1].width, 1u);
    EXPECT_EQ(parsed.file.mips[1].height, 1u);
}

TEST(TextureImporter, Import_ValidPng_Mip0_ContainsNonZeroPixels) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mipData[0].size(), 16u);

    // At least one of the 4 pixels must have a non-zero alpha (255)
    bool foundOpaquePixel = false;
    for (size_t i = 0; i + 3 < parsed.file.mipData[0].size(); i += 4) {
        if (parsed.file.mipData[0][i + 3] != 0) {
            foundOpaquePixel = true;
            break;
        }
    }
    EXPECT_TRUE(foundOpaquePixel) << "Mip 0 should contain at least one non-transparent pixel";
}

TEST(TextureImporter, Import_ValidPng_Mip1_IsBoxFilterAverageOf4Pixels) {
    // The 2x2 PNG has pixels: R=(255,0,0,255), G=(0,255,0,255),
    //                          B=(0,0,255,255), Y=(255,255,0,255)
    // Box filter average: ((255+0+0+255)/4, (0+255+0+255)/4,
    //                       (0+0+255+0)/4, (255+255+255+255)/4)
    // = (127 or 128, 127 or 128, 63 or 64, 255)
    // (integer rounding: (sum+2)/4)
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mipData.size(), 2u);
    ASSERT_EQ(parsed.file.mipData[1].size(), 4u);

    // Alpha must be 255 (all source pixels have alpha=255, avg=255)
    EXPECT_EQ(parsed.file.mipData[1][3], 255u)
        << "Mip 1 alpha should be 255 (average of four 255 values)";

    // R channel: (255 + 0 + 0 + 255 + 2) / 4 = 128
    // Allow ±1 for rounding
    const int r_val = static_cast<int>(parsed.file.mipData[1][0]);
    EXPECT_GE(r_val, 127) << "Mip 1 R channel out of expected range";
    EXPECT_LE(r_val, 128) << "Mip 1 R channel out of expected range";
}

TEST(TextureImporter, Import_ValidPng_MipDescriptorOffsets_Ascending) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalPngData(), "test.png", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    ASSERT_GE(parsed.file.mips.size(), 2u);
    EXPECT_LT(parsed.file.mips[0].offset, parsed.file.mips[1].offset)
        << "Mip descriptors must have ascending offsets (largest mip first)";
}

TEST(TextureImporter, Import_IsDeterministic_SameInputSameOutput) {
    TextureImporter imp;
    AssetImportOptions opts;
    auto pngData = MakeMinimalPngData();

    auto r1 = imp.Import(pngData, "test.png", opts);
    auto r2 = imp.Import(pngData, "test.png", opts);

    ASSERT_TRUE(r1.success) << r1.error;
    ASSERT_TRUE(r2.success) << r2.error;
    ASSERT_EQ(r1.data.size(), r2.data.size());
    EXPECT_EQ(std::memcmp(r1.data.data(), r2.data.data(), r1.data.size()), 0)
        << "Import is not deterministic — identical inputs produced different outputs";
}
