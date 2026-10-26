/// UzEngine stb_image decoder allow-list — Unit Tests
///
/// `engine/asset/importers/stb_impl.cpp` compiles only the PNG, JPEG, HDR and
/// TGA decoders. stb_image chooses a decoder from file content, not from the
/// extension, so these tests feed well-formed files of every excluded format
/// (each decodes with an unrestricted stb_image v2.30) and require both the
/// raw `stbi_*` entry points and the engine's `TextureImporter` to reject
/// them, while the formats the engine ships keep decoding.

#include "texture_importer.h"
#include "uztexture_format.h"

#include <stb_image.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Fixtures — minimal well-formed files
// ─────────────────────────────────────────────────────────────────────────────

/// 1x1 GIF89a: 2-entry global palette, graphic control extension, one LZW
/// image block (min code size 2) selecting palette index 0.
const std::vector<uint8_t> kGif1x1 = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61,             // "GIF89a"
    0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,       // screen 1x1, global palette (2 entries)
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,             // palette: white, black
    0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, // graphic control extension
    0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, // image descriptor 1x1
    0x02, 0x02, 0x44, 0x01, 0x00,                   // LZW min size 2, one 2-byte sub-block
    0x3B,                                           // trailer
};

/// 1x1 8-bit RGB Photoshop document, raw (uncompressed) planar data, red.
const std::vector<uint8_t> kPsd1x1 = {
    '8', 'B', 'P', 'S', 0x00, 0x01,                 // signature, version 1
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // reserved
    0x00, 0x03,                                     // 3 channels
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, // height 1, width 1
    0x00, 0x08, 0x00, 0x03,                         // depth 8, RGB colour mode
    0x00, 0x00, 0x00, 0x00,                         // colour mode data length
    0x00, 0x00, 0x00, 0x00,                         // image resources length
    0x00, 0x00, 0x00, 0x00,                         // layer and mask length
    0x00, 0x00,                                     // compression: raw
    0xFF, 0x00, 0x00,                               // R, G, B planes
};

/// 1x1 binary PPM (P6), red.
const std::vector<uint8_t> kPnm1x1 = {
    'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 0xFF, 0x00, 0x00,
};

/// 1x1 24-bit BMP (BITMAPINFOHEADER), red, row padded to 4 bytes.
const std::vector<uint8_t> kBmp1x1 = {
    'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, // file header, pixels at 54
    40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0,            // info header size, width 1, height 1
    1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0,            // planes 1, 24 bpp, BI_RGB, image size 4
    0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0,             // 72 DPI
    0, 0, 0, 0, 0, 0, 0, 0,                         // palette counts
    0x00, 0x00, 0xFF, 0x00,                         // BGR pixel + padding
};

/// 2x1 uncompressed true-colour TGA, 32 bpp, top-left origin: red, green.
const std::vector<uint8_t> kTga2x1 = {
    0x00, 0x00, 0x02,                               // no id, no colour map, type 2
    0x00, 0x00, 0x00, 0x00, 0x00,                   // colour map spec (unused)
    0x00, 0x00, 0x00, 0x00,                         // x/y origin
    0x02, 0x00, 0x01, 0x00,                         // width 2, height 1
    0x20, 0x28,                                     // 32 bpp, 8 alpha bits, top-left origin
    0x00, 0x00, 0xFF, 0xFF,                         // BGRA red
    0x00, 0xFF, 0x00, 0xFF,                         // BGRA green
};

/// 1x1 Radiance RGBE image whose single pixel (128,128,128, e=129) is 1.0.
std::vector<uint8_t> MakeHdr1x1() {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    std::vector<uint8_t> bytes(header.begin(), header.end());
    bytes.insert(bytes.end(), {0x80, 0x80, 0x80, 0x81});
    return bytes;
}

struct ExcludedFormat {
    const char* name;
    const std::vector<uint8_t>* bytes;
};

std::string ExcludedFormatName(const ::testing::TestParamInfo<ExcludedFormat>& info) {
    return info.param.name;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Excluded formats are rejected
// ─────────────────────────────────────────────────────────────────────────────

class StbExcludedFormat : public ::testing::TestWithParam<ExcludedFormat> {};

TEST_P(StbExcludedFormat, LoadFromMemory_ReturnsNullWithUnknownType) {
    const auto& bytes = *GetParam().bytes;
    int w = -1;
    int h = -1;
    int comp = -1;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                        &w, &h, &comp, 4);
    EXPECT_EQ(px, nullptr) << GetParam().name << " decoded although its decoder is compiled out";
    stbi_image_free(px);
    ASSERT_NE(stbi_failure_reason(), nullptr);
    EXPECT_STREQ(stbi_failure_reason(), "unknown image type");
}

TEST_P(StbExcludedFormat, InfoFromMemory_ReportsFailure) {
    const auto& bytes = *GetParam().bytes;
    int w = 0;
    int h = 0;
    int comp = 0;
    EXPECT_EQ(stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp), 0)
        << GetParam().name << " header parsed although its decoder is compiled out";
}

TEST_P(StbExcludedFormat, TextureImporter_RejectsWithError) {
    TextureImporter imp;
    AssetImportOptions opts;
    const auto r = imp.Import(*GetParam().bytes, std::string("texture.") + GetParam().name, opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

INSTANTIATE_TEST_SUITE_P(DecoderAllowList, StbExcludedFormat,
                         ::testing::Values(ExcludedFormat{"gif", &kGif1x1},
                                           ExcludedFormat{"psd", &kPsd1x1},
                                           ExcludedFormat{"pnm", &kPnm1x1},
                                           ExcludedFormat{"bmp", &kBmp1x1}),
                         ExcludedFormatName);

TEST(StbImageFormats, LoadfFromMemory_GifRejected) {
    int w = -1;
    int h = -1;
    int comp = -1;
    float* px = stbi_loadf_from_memory(kGif1x1.data(), static_cast<int>(kGif1x1.size()),
                                       &w, &h, &comp, 4);
    EXPECT_EQ(px, nullptr);
    stbi_image_free(px);
}

// ─────────────────────────────────────────────────────────────────────────────
// Allowed formats keep decoding
// ─────────────────────────────────────────────────────────────────────────────

TEST(StbImageFormats, Tga_DecodesPixels) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* px = stbi_load_from_memory(kTga2x1.data(), static_cast<int>(kTga2x1.size()),
                                        &w, &h, &comp, 4);
    ASSERT_NE(px, nullptr) << stbi_failure_reason();
    EXPECT_EQ(w, 2);
    EXPECT_EQ(h, 1);
    EXPECT_EQ(comp, 4);
    const uint8_t expected[8] = {255, 0, 0, 255, 0, 255, 0, 255};
    EXPECT_EQ(std::memcmp(px, expected, sizeof(expected)), 0);
    stbi_image_free(px);
}

TEST(StbImageFormats, TextureImporter_TgaImportsAsRgba8) {
    TextureImporter imp;
    AssetImportOptions opts;
    const auto r = imp.Import(kTga2x1, "flipbook.tga", opts);
    ASSERT_TRUE(r.success) << r.error;

    const auto parsed = ReadUzTexture(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.width, 2u);
    EXPECT_EQ(parsed.file.header.height, 1u);
    EXPECT_EQ(parsed.file.header.format, static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8));
    ASSERT_FALSE(parsed.file.mipData.empty());
    ASSERT_GE(parsed.file.mipData[0].size(), 8u);
    const uint8_t expected[8] = {255, 0, 0, 255, 0, 255, 0, 255};
    EXPECT_EQ(std::memcmp(parsed.file.mipData[0].data(), expected, sizeof(expected)), 0);
}

TEST(StbImageFormats, Hdr_DecodesFloatPixels) {
    const auto bytes = MakeHdr1x1();
    int w = 0;
    int h = 0;
    int comp = 0;
    float* px = stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                       &w, &h, &comp, 4);
    ASSERT_NE(px, nullptr) << stbi_failure_reason();
    EXPECT_EQ(w, 1);
    EXPECT_EQ(h, 1);
    EXPECT_EQ(comp, 3);
    EXPECT_FLOAT_EQ(px[0], 1.0f);
    EXPECT_FLOAT_EQ(px[1], 1.0f);
    EXPECT_FLOAT_EQ(px[2], 1.0f);
    EXPECT_FLOAT_EQ(px[3], 1.0f);
    stbi_image_free(px);
}
