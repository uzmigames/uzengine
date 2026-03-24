/// UzEngine PNG Codec — Unit Tests
///
/// Tests PngCodec encode/decode round-trips for 8-bit and 16-bit images,
/// various channel counts, forceRGBA expansion, invalid input handling,
/// and file I/O operations.

#include "png_codec.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Round-trip: encode to memory, decode from memory
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, EncodeDecodeRoundTrip_RGBA8) {
    const uint32_t w = 4, h = 4;
    std::vector<uint8_t> pixels(w * h * 4);
    for (uint32_t i = 0; i < w * h; ++i) {
        pixels[i * 4 + 0] = static_cast<uint8_t>(i * 17);
        pixels[i * 4 + 1] = static_cast<uint8_t>(255 - i * 17);
        pixels[i * 4 + 2] = static_cast<uint8_t>((i * 37) % 256);
        pixels[i * 4 + 3] = 255;
    }

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 4, 8);
    ASSERT_FALSE(encoded.empty());

    // Verify PNG signature bytes.
    EXPECT_EQ(encoded[0], 0x89);
    EXPECT_EQ(encoded[1], 'P');
    EXPECT_EQ(encoded[2], 'N');
    EXPECT_EQ(encoded[3], 'G');

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    ASSERT_EQ(decoded.channels, 4u);
    ASSERT_EQ(decoded.bitDepth, 8u);

    // Pixel-exact round-trip for lossless PNG.
    ASSERT_EQ(decoded.pixels.size(), pixels.size());
    EXPECT_EQ(decoded.pixels, pixels);
}

TEST(PngCodecTest, EncodeDecodeRoundTrip_RGB8) {
    const uint32_t w = 3, h = 2;
    std::vector<uint8_t> pixels(w * h * 3);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<uint8_t>(i * 13 % 256);
    }

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 3, 8);
    ASSERT_FALSE(encoded.empty());

    // Decode WITHOUT forceRGBA — should preserve 3 channels.
    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), false);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    ASSERT_EQ(decoded.channels, 3u);
    ASSERT_EQ(decoded.bitDepth, 8u);
    EXPECT_EQ(decoded.pixels, pixels);
}

TEST(PngCodecTest, EncodeDecodeRoundTrip_Gray8) {
    const uint32_t w = 8, h = 8;
    std::vector<uint8_t> pixels(w * h);
    std::iota(pixels.begin(), pixels.end(), static_cast<uint8_t>(0));

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 1, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), false);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    ASSERT_EQ(decoded.channels, 1u);
    EXPECT_EQ(decoded.pixels, pixels);
}

TEST(PngCodecTest, EncodeDecodeRoundTrip_GrayAlpha8) {
    const uint32_t w = 2, h = 2;
    std::vector<uint8_t> pixels = {
        100, 255,   // pixel (0,0): gray=100, alpha=255
        200,  50,   // pixel (1,0): gray=200, alpha=50
         50, 128,   // pixel (0,1): gray=50,  alpha=128
        150, 200,   // pixel (1,1): gray=150, alpha=200
    };

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 2, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), false);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    ASSERT_EQ(decoded.channels, 2u);
    EXPECT_EQ(decoded.pixels, pixels);
}

TEST(PngCodecTest, EncodeDecodeRoundTrip_RGBA16) {
    const uint32_t w = 2, h = 2;
    const uint32_t channels = 4;
    const uint32_t samplesPerRow = w * channels;

    // Build 16-bit pixel data in native (LE) byte order.
    std::vector<uint16_t> samples(w * h * channels);
    samples = {
        1000, 2000, 3000, 65535,  // pixel (0,0)
        4000, 5000, 6000, 65535,  // pixel (1,0)
        7000, 8000, 9000, 32768,  // pixel (0,1)
       10000, 11000, 12000, 16384, // pixel (1,1)
    };

    // Reinterpret as uint8_t for the codec API.
    const auto* pixelBytes = reinterpret_cast<const uint8_t*>(samples.data());
    const size_t byteCount = samples.size() * sizeof(uint16_t);

    auto encoded = PngCodec::EncodeMemory(pixelBytes, w, h, channels, 16);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    ASSERT_EQ(decoded.channels, channels);
    ASSERT_EQ(decoded.bitDepth, 16u);
    ASSERT_EQ(decoded.pixels.size(), byteCount);

    // Verify pixel-exact round-trip for 16-bit data.
    const auto* decodedSamples = reinterpret_cast<const uint16_t*>(decoded.pixels.data());
    for (size_t i = 0; i < samples.size(); ++i) {
        EXPECT_EQ(decodedSamples[i], samples[i])
            << "Mismatch at sample index " << i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ForceRGBA expansion
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, ForceRGBA_ExpandsGray) {
    const uint32_t w = 2, h = 2;
    std::vector<uint8_t> gray = {10, 20, 30, 40};

    auto encoded = PngCodec::EncodeMemory(gray.data(), w, h, 1, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.channels, 4u);
    ASSERT_EQ(decoded.pixels.size(), w * h * 4);

    // Gray value should be replicated to R, G, B with A=255.
    for (uint32_t i = 0; i < w * h; ++i) {
        const uint8_t g = gray[i];
        EXPECT_EQ(decoded.pixels[i * 4 + 0], g) << "R at pixel " << i;
        EXPECT_EQ(decoded.pixels[i * 4 + 1], g) << "G at pixel " << i;
        EXPECT_EQ(decoded.pixels[i * 4 + 2], g) << "B at pixel " << i;
        EXPECT_EQ(decoded.pixels[i * 4 + 3], 255) << "A at pixel " << i;
    }
}

TEST(PngCodecTest, ForceRGBA_ExpandsRGB) {
    const uint32_t w = 1, h = 1;
    std::vector<uint8_t> rgb = {100, 150, 200};

    auto encoded = PngCodec::EncodeMemory(rgb.data(), w, h, 3, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.channels, 4u);
    EXPECT_EQ(decoded.pixels[0], 100);
    EXPECT_EQ(decoded.pixels[1], 150);
    EXPECT_EQ(decoded.pixels[2], 200);
    EXPECT_EQ(decoded.pixels[3], 255);
}

// ─────────────────────────────────────────────────────────────────────────────
// Invalid input handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, DecodeMemory_NullData_ReturnsEmpty) {
    auto result = PngCodec::DecodeMemory(nullptr, 100);
    EXPECT_EQ(result.width, 0u);
    EXPECT_TRUE(result.pixels.empty());
}

TEST(PngCodecTest, DecodeMemory_TooSmall_ReturnsEmpty) {
    uint8_t tiny[] = {0x89, 'P', 'N', 'G'};
    auto result = PngCodec::DecodeMemory(tiny, sizeof(tiny));
    EXPECT_EQ(result.width, 0u);
}

TEST(PngCodecTest, DecodeMemory_InvalidSignature_ReturnsEmpty) {
    uint8_t garbage[] = {0, 1, 2, 3, 4, 5, 6, 7};
    auto result = PngCodec::DecodeMemory(garbage, sizeof(garbage));
    EXPECT_EQ(result.width, 0u);
}

TEST(PngCodecTest, EncodeMemory_ZeroWidth_ReturnsEmpty) {
    uint8_t pixel = 255;
    auto result = PngCodec::EncodeMemory(&pixel, 0, 1, 1, 8);
    EXPECT_TRUE(result.empty());
}

TEST(PngCodecTest, EncodeMemory_ZeroHeight_ReturnsEmpty) {
    uint8_t pixel = 255;
    auto result = PngCodec::EncodeMemory(&pixel, 1, 0, 1, 8);
    EXPECT_TRUE(result.empty());
}

TEST(PngCodecTest, EncodeMemory_NullPixels_ReturnsEmpty) {
    auto result = PngCodec::EncodeMemory(nullptr, 4, 4, 4, 8);
    EXPECT_TRUE(result.empty());
}

TEST(PngCodecTest, EncodeMemory_InvalidBitDepth_ReturnsEmpty) {
    std::vector<uint8_t> px(16, 128);
    auto result = PngCodec::EncodeMemory(px.data(), 2, 2, 1, 12);
    EXPECT_TRUE(result.empty());
}

TEST(PngCodecTest, EncodeMemory_InvalidChannelCount_ReturnsEmpty) {
    std::vector<uint8_t> px(40, 128);
    auto result = PngCodec::EncodeMemory(px.data(), 2, 2, 5, 8);
    EXPECT_TRUE(result.empty());
}

TEST(PngCodecTest, EncodeFile_NullPixels_ReturnsFalse) {
    EXPECT_FALSE(PngCodec::EncodeFile("dummy.png", nullptr, 4, 4, 4));
}

TEST(PngCodecTest, EncodeFile_ZeroDimensions_ReturnsFalse) {
    uint8_t px = 0;
    EXPECT_FALSE(PngCodec::EncodeFile("dummy.png", &px, 0, 0, 4));
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, EncodeDecodeFile_RoundTrip) {
    const uint32_t w = 4, h = 4;
    std::vector<uint8_t> pixels(w * h * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<uint8_t>(i % 256);
    }

    // Use a temporary file path.
    const std::string tmpPath = (std::filesystem::temp_directory_path() / "uz_test_png_codec.png").string();

    ASSERT_TRUE(PngCodec::EncodeFile(tmpPath, pixels.data(), w, h, 4));

    auto decoded = PngCodec::DecodeFile(tmpPath, true);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    ASSERT_EQ(decoded.channels, 4u);
    ASSERT_EQ(decoded.bitDepth, 8u);
    EXPECT_EQ(decoded.pixels, pixels);

    // Clean up.
    std::remove(tmpPath.c_str());
}

TEST(PngCodecTest, DecodeFile_NonexistentPath_ReturnsEmpty) {
    auto result = PngCodec::DecodeFile("this_file_does_not_exist_12345.png");
    EXPECT_EQ(result.width, 0u);
}

TEST(PngCodecTest, EncodeFile_InvalidPath_ReturnsFalse) {
    std::vector<uint8_t> px(16, 128);
    // Path with invalid characters / nonexistent deep directory.
    EXPECT_FALSE(PngCodec::EncodeFile("/nonexistent_dir_xyz/abc/test.png", px.data(), 2, 2, 4));
}

// ─────────────────────────────────────────────────────────────────────────────
// Large image stress test
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, LargeImage_256x256_RoundTrip) {
    const uint32_t w = 256, h = 256;
    std::vector<uint8_t> pixels(w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
            pixels[idx + 0] = static_cast<uint8_t>(x);
            pixels[idx + 1] = static_cast<uint8_t>(y);
            pixels[idx + 2] = static_cast<uint8_t>((x + y) % 256);
            pixels[idx + 3] = 255;
        }
    }

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 4, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    EXPECT_EQ(decoded.pixels, pixels);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-pixel edge case
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, SinglePixel_1x1_RGBA8) {
    std::vector<uint8_t> pixel = {42, 99, 200, 128};

    auto encoded = PngCodec::EncodeMemory(pixel.data(), 1, 1, 4, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.width, 1u);
    ASSERT_EQ(decoded.height, 1u);
    EXPECT_EQ(decoded.pixels, pixel);
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-square image
// ─────────────────────────────────────────────────────────────────────────────

TEST(PngCodecTest, NonSquare_WideImage) {
    const uint32_t w = 16, h = 1;
    std::vector<uint8_t> pixels(w * h * 4, 200);

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 4, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    EXPECT_EQ(decoded.pixels, pixels);
}

TEST(PngCodecTest, NonSquare_TallImage) {
    const uint32_t w = 1, h = 16;
    std::vector<uint8_t> pixels(w * h * 4, 100);

    auto encoded = PngCodec::EncodeMemory(pixels.data(), w, h, 4, 8);
    ASSERT_FALSE(encoded.empty());

    auto decoded = PngCodec::DecodeMemory(encoded.data(), encoded.size(), true);
    ASSERT_EQ(decoded.width, w);
    ASSERT_EQ(decoded.height, h);
    EXPECT_EQ(decoded.pixels, pixels);
}
