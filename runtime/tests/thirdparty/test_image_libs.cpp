/// Integration tests for image thirdparty libraries.
///
/// Validates that stb_image, stb_image_write, stb_image_resize2, tinyexr,
/// basis_universal transcoder, and astcenc are correctly installed, headers
/// compile, and core functionality works end-to-end.

#define STB_IMAGE_IMPLEMENTATION
// Use STATIC variant to avoid duplicate extern symbols when linked alongside editor.lib,
// which also defines STB_IMAGE_WRITE_IMPLEMENTATION. Each TU gets its own static copies.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION

// stb_image_resize2 requires 8-byte aligned allocations; override malloc to
// guarantee alignment on all platforms (zig's debug allocator may not align).
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#define STBIR_MALLOC(size, user_data) ((void)(user_data), _aligned_malloc((size), 16))
#define STBIR_FREE(ptr, user_data)    ((void)(user_data), _aligned_free(ptr))
#else
#define STBIR_MALLOC(size, user_data) ((void)(user_data), aligned_alloc(16, (((size) + 15) & ~15)))
#define STBIR_FREE(ptr, user_data)    ((void)(user_data), free(ptr))
#endif
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include <gtest/gtest.h>

#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <basisu_transcoder.h>

#include <astcenc.h>

#include <cstdint>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Helper: generate a solid-color test image in memory
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<uint8_t> MakeSolidRGBA(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
    return pixels;
}

// ─────────────────────────────────────────────────────────────────────────────
// stb_image — decode from memory
// ─────────────────────────────────────────────────────────────────────────────

TEST(StbImage, DecodePNGFromMemory) {
    // Encode a 4x4 red image to PNG in memory, then decode it back
    const int w = 4, h = 4, channels = 4;
    auto pixels = MakeSolidRGBA(w, h, 255, 0, 0, 255);

    // Encode to PNG buffer
    int pngLen = 0;
    unsigned char* pngData = stbi_write_png_to_mem(
        pixels.data(), w * channels, w, h, channels, &pngLen);
    ASSERT_NE(pngData, nullptr);
    ASSERT_GT(pngLen, 0);

    // Decode from memory
    int decW = 0, decH = 0, decComp = 0;
    uint8_t* decoded = stbi_load_from_memory(
        pngData, pngLen, &decW, &decH, &decComp, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decW, w);
    EXPECT_EQ(decH, h);
    EXPECT_EQ(decComp, 4);

    // Verify pixel content (solid red)
    for (int i = 0; i < w * h * 4; i += 4) {
        EXPECT_EQ(decoded[i + 0], 255) << "Red channel at pixel " << i / 4;
        EXPECT_EQ(decoded[i + 1], 0)   << "Green channel at pixel " << i / 4;
        EXPECT_EQ(decoded[i + 2], 0)   << "Blue channel at pixel " << i / 4;
        EXPECT_EQ(decoded[i + 3], 255) << "Alpha channel at pixel " << i / 4;
    }

    stbi_image_free(decoded);
    STBIW_FREE(pngData);
}

TEST(StbImage, InfoFromMemory) {
    const int w = 8, h = 4, channels = 3;
    std::vector<uint8_t> pixels(w * h * channels, 128);

    int pngLen = 0;
    unsigned char* pngData = stbi_write_png_to_mem(
        pixels.data(), w * channels, w, h, channels, &pngLen);
    ASSERT_NE(pngData, nullptr);

    int infoW = 0, infoH = 0, infoComp = 0;
    int ok = stbi_info_from_memory(pngData, pngLen, &infoW, &infoH, &infoComp);
    EXPECT_EQ(ok, 1);
    EXPECT_EQ(infoW, 8);
    EXPECT_EQ(infoH, 4);
    EXPECT_EQ(infoComp, 3);

    STBIW_FREE(pngData);
}

TEST(StbImage, RejectsInvalidData) {
    uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};
    int w = 0, h = 0, comp = 0;
    uint8_t* result = stbi_load_from_memory(garbage, sizeof(garbage), &w, &h, &comp, 4);
    EXPECT_EQ(result, nullptr);
}

TEST(StbImage, DecodeGrayscaleAs4Channel) {
    // 2x2 grayscale PNG: encode as 1-channel, decode as 4-channel
    const int w = 2, h = 2;
    uint8_t gray[4] = {50, 100, 150, 200};

    int pngLen = 0;
    unsigned char* pngData = stbi_write_png_to_mem(gray, w * 1, w, h, 1, &pngLen);
    ASSERT_NE(pngData, nullptr);

    int decW = 0, decH = 0, decComp = 0;
    uint8_t* decoded = stbi_load_from_memory(pngData, pngLen, &decW, &decH, &decComp, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decW, 2);
    EXPECT_EQ(decH, 2);
    EXPECT_EQ(decComp, 1); // Original was 1 channel

    // When forcing 4 channels, grayscale expands to (V, V, V, 255)
    EXPECT_EQ(decoded[0], 50);  // R
    EXPECT_EQ(decoded[1], 50);  // G
    EXPECT_EQ(decoded[2], 50);  // B
    EXPECT_EQ(decoded[3], 255); // A

    stbi_image_free(decoded);
    STBIW_FREE(pngData);
}

// ─────────────────────────────────────────────────────────────────────────────
// stb_image_write — encode to PNG buffer
// ─────────────────────────────────────────────────────────────────────────────

TEST(StbImageWrite, EncodePNGToMemory) {
    const int w = 16, h = 16, channels = 4;
    auto pixels = MakeSolidRGBA(w, h, 0, 255, 0, 255);

    int pngLen = 0;
    unsigned char* pngData = stbi_write_png_to_mem(
        pixels.data(), w * channels, w, h, channels, &pngLen);
    ASSERT_NE(pngData, nullptr);
    EXPECT_GT(pngLen, 8); // Minimum PNG header size

    // Verify PNG signature (first 8 bytes)
    EXPECT_EQ(pngData[0], 0x89);
    EXPECT_EQ(pngData[1], 'P');
    EXPECT_EQ(pngData[2], 'N');
    EXPECT_EQ(pngData[3], 'G');

    STBIW_FREE(pngData);
}

TEST(StbImageWrite, EncodeViaCallback) {
    const int w = 4, h = 4, channels = 4;
    auto pixels = MakeSolidRGBA(w, h, 0, 0, 255, 255);

    struct CallbackCtx {
        std::vector<uint8_t> data;
    } ctx;

    auto writeFn = [](void* context, void* data, int size) {
        auto* c = static_cast<CallbackCtx*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        c->data.insert(c->data.end(), bytes, bytes + size);
    };

    int ok = stbi_write_png_to_func(writeFn, &ctx, w, h, channels, pixels.data(), w * channels);
    EXPECT_EQ(ok, 1);
    EXPECT_GT(ctx.data.size(), 8u);

    // Verify the callback output is a valid PNG (decode it back)
    int decW = 0, decH = 0, decComp = 0;
    uint8_t* decoded = stbi_load_from_memory(
        ctx.data.data(), static_cast<int>(ctx.data.size()),
        &decW, &decH, &decComp, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decW, 4);
    EXPECT_EQ(decH, 4);
    // Blue pixel
    EXPECT_EQ(decoded[0], 0);
    EXPECT_EQ(decoded[1], 0);
    EXPECT_EQ(decoded[2], 255);
    stbi_image_free(decoded);
}

// ─────────────────────────────────────────────────────────────────────────────
// stb_image_resize2 — downscale and upscale
// ─────────────────────────────────────────────────────────────────────────────

TEST(StbImageResize, DownscaleSolidColor) {
    const int srcW = 8, srcH = 8, channels = 4;
    auto src = MakeSolidRGBA(srcW, srcH, 128, 64, 32, 255);

    const int dstW = 4, dstH = 4;
    std::vector<uint8_t> dst(dstW * dstH * channels, 0);

    unsigned char* result = stbir_resize_uint8_linear(
        src.data(), srcW, srcH, srcW * channels,
        dst.data(), dstW, dstH, dstW * channels,
        static_cast<stbir_pixel_layout>(channels));
    ASSERT_NE(result, nullptr);

    // Solid color should survive downscale exactly
    for (int i = 0; i < dstW * dstH * channels; i += channels) {
        EXPECT_EQ(dst[i + 0], 128) << "R at pixel " << i / channels;
        EXPECT_EQ(dst[i + 1], 64)  << "G at pixel " << i / channels;
        EXPECT_EQ(dst[i + 2], 32)  << "B at pixel " << i / channels;
        EXPECT_EQ(dst[i + 3], 255) << "A at pixel " << i / channels;
    }
}

TEST(StbImageResize, UpscaleSolidColor) {
    const int srcW = 2, srcH = 2, channels = 4;
    auto src = MakeSolidRGBA(srcW, srcH, 200, 100, 50, 255);

    const int dstW = 8, dstH = 8;
    std::vector<uint8_t> dst(dstW * dstH * channels, 0);

    unsigned char* result = stbir_resize_uint8_linear(
        src.data(), srcW, srcH, srcW * channels,
        dst.data(), dstW, dstH, dstW * channels,
        static_cast<stbir_pixel_layout>(channels));
    ASSERT_NE(result, nullptr);

    // Solid color upscale should be exact
    for (int i = 0; i < dstW * dstH * channels; i += channels) {
        EXPECT_EQ(dst[i + 0], 200);
        EXPECT_EQ(dst[i + 1], 100);
        EXPECT_EQ(dst[i + 2], 50);
        EXPECT_EQ(dst[i + 3], 255);
    }
}

TEST(StbImageResize, RoundTrip8x8To4x4To8x8) {
    // Create a checkerboard pattern, downscale, upscale, verify dimensions
    const int w = 8, h = 8, ch = 4;
    auto src = MakeSolidRGBA(w, h, 255, 255, 255, 255);
    // Make top-left quadrant black
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            std::memset(&src[(y * w + x) * ch], 0, 3);

    std::vector<uint8_t> mid(4 * 4 * ch);
    stbir_resize_uint8_linear(src.data(), w, h, w * ch,
        mid.data(), 4, 4, 4 * ch, static_cast<stbir_pixel_layout>(ch));

    std::vector<uint8_t> out(w * h * ch);
    unsigned char* result = stbir_resize_uint8_linear(mid.data(), 4, 4, 4 * ch,
        out.data(), w, h, w * ch, static_cast<stbir_pixel_layout>(ch));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(out.size(), static_cast<size_t>(w * h * ch));
}

// ─────────────────────────────────────────────────────────────────────────────
// tinyexr — EXR encode/decode round-trip in memory
// ─────────────────────────────────────────────────────────────────────────────

TEST(TinyEXR, VersionStructLayout) {
    EXRVersion version;
    std::memset(&version, 0, sizeof(version));
    EXPECT_EQ(version.version, 0);
    EXPECT_EQ(version.tiled, 0);
    EXPECT_EQ(version.non_image, 0);
}

TEST(TinyEXR, SaveAndLoadRoundTrip) {
    const int w = 4, h = 4;
    // Create a simple float RGBA image
    std::vector<float> pixels(w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        pixels[i * 4 + 0] = 1.0f; // R
        pixels[i * 4 + 1] = 0.5f; // G
        pixels[i * 4 + 2] = 0.0f; // B
        pixels[i * 4 + 3] = 1.0f; // A
    }

    // Save to memory
    unsigned char* exrBuf = nullptr;
    const char* err = nullptr;
    int exrSize = SaveEXRToMemory(pixels.data(), w, h, 4, 0, &exrBuf, &err);
    if (err) {
        FAIL() << "SaveEXRToMemory failed: " << err;
        FreeEXRErrorMessage(err);
    }
    ASSERT_GT(exrSize, 0);
    ASSERT_NE(exrBuf, nullptr);

    // Parse version from memory
    EXRVersion version;
    int ret = ParseEXRVersionFromMemory(&version, exrBuf, exrSize);
    EXPECT_EQ(ret, TINYEXR_SUCCESS);
    EXPECT_EQ(version.version, 2); // EXR version 2

    // Parse header from memory
    EXRHeader header;
    InitEXRHeader(&header);
    ret = ParseEXRHeaderFromMemory(&header, &version, exrBuf, exrSize, &err);
    if (err) {
        FAIL() << "ParseEXRHeaderFromMemory failed: " << err;
        FreeEXRErrorMessage(err);
    }
    EXPECT_EQ(ret, TINYEXR_SUCCESS);
    EXPECT_EQ(header.num_channels, 4);

    // Load image from memory
    EXRImage image;
    InitEXRImage(&image);
    ret = LoadEXRImageFromMemory(&image, &header, exrBuf, exrSize, &err);
    if (err) {
        FAIL() << "LoadEXRImageFromMemory failed: " << err;
        FreeEXRErrorMessage(err);
    }
    EXPECT_EQ(ret, TINYEXR_SUCCESS);
    EXPECT_EQ(image.width, w);
    EXPECT_EQ(image.height, h);

    FreeEXRImage(&image);
    FreeEXRHeader(&header);
    free(exrBuf);
}

TEST(TinyEXR, RejectsInvalidData) {
    uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};
    EXRVersion version;
    int ret = ParseEXRVersionFromMemory(&version, garbage, sizeof(garbage));
    EXPECT_NE(ret, TINYEXR_SUCCESS);
}

// ─────────────────────────────────────────────────────────────────────────────
// basis_universal — transcoder initialization and validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(BasisUniversal, TranscoderInitDoesNotCrash) {
    basist::basisu_transcoder_init();
    // Calling twice should be safe (idempotent)
    basist::basisu_transcoder_init();
}

TEST(BasisUniversal, TranscoderRejectsInvalidData) {
    basist::basisu_transcoder_init();
    basist::basisu_transcoder transcoder;

    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    bool valid = transcoder.validate_header(garbage, sizeof(garbage));
    EXPECT_FALSE(valid);
}

TEST(BasisUniversal, TranscoderRejectsEmptyData) {
    basist::basisu_transcoder_init();
    basist::basisu_transcoder transcoder;

    bool valid = transcoder.validate_header(nullptr, 0);
    EXPECT_FALSE(valid);
}

TEST(BasisUniversal, TextureFormatEnumsExist) {
    // Verify critical GPU format enums are defined
    EXPECT_EQ(static_cast<int>(basist::transcoder_texture_format::cTFBC1_RGB), 2);
    EXPECT_EQ(static_cast<int>(basist::transcoder_texture_format::cTFBC7_RGBA), 6);
    EXPECT_EQ(static_cast<int>(basist::transcoder_texture_format::cTFASTC_4x4_RGBA), 10);
    EXPECT_EQ(static_cast<int>(basist::transcoder_texture_format::cTFRGBA32), 13);
}

// ─────────────────────────────────────────────────────────────────────────────
// astcenc — config initialization and compress/decompress round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(ASTCEnc, ConfigInitLDR) {
    astcenc_config config;
    astcenc_error status = astcenc_config_init(
        ASTCENC_PRF_LDR, 4, 4, 1,
        ASTCENC_PRE_MEDIUM, 0, &config);
    EXPECT_EQ(status, ASTCENC_SUCCESS);
    EXPECT_EQ(config.block_x, 4u);
    EXPECT_EQ(config.block_y, 4u);
    EXPECT_EQ(config.block_z, 1u);
    EXPECT_EQ(config.profile, ASTCENC_PRF_LDR);
}

TEST(ASTCEnc, ConfigInitHDR) {
    astcenc_config config;
    astcenc_error status = astcenc_config_init(
        ASTCENC_PRF_HDR, 6, 6, 1,
        ASTCENC_PRE_MEDIUM, 0, &config);
    EXPECT_EQ(status, ASTCENC_SUCCESS);
    EXPECT_EQ(config.block_x, 6u);
    EXPECT_EQ(config.block_y, 6u);
}

TEST(ASTCEnc, ConfigInitRejectsInvalidBlockSize) {
    astcenc_config config;
    // 3x3 is not a valid ASTC block size
    astcenc_error status = astcenc_config_init(
        ASTCENC_PRF_LDR, 3, 3, 1,
        ASTCENC_PRE_MEDIUM, 0, &config);
    EXPECT_NE(status, ASTCENC_SUCCESS);
}

TEST(ASTCEnc, ContextAllocAndFree) {
    astcenc_config config;
    astcenc_error status = astcenc_config_init(
        ASTCENC_PRF_LDR, 4, 4, 1,
        ASTCENC_PRE_MEDIUM, 0, &config);
    ASSERT_EQ(status, ASTCENC_SUCCESS);

    astcenc_context* ctx = nullptr;
    status = astcenc_context_alloc(&config, 1, &ctx);
    ASSERT_EQ(status, ASTCENC_SUCCESS);
    ASSERT_NE(ctx, nullptr);

    astcenc_context_free(ctx);
}

TEST(ASTCEnc, CompressAndDecompressRoundTrip) {
    // Create a 4x4 RGBA8 image (one ASTC block)
    const unsigned int blockW = 4, blockH = 4;
    std::vector<uint8_t> pixels(blockW * blockH * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 200; // R
        pixels[i + 1] = 100; // G
        pixels[i + 2] = 50;  // B
        pixels[i + 3] = 255; // A
    }

    // Config + context
    astcenc_config config;
    astcenc_error status = astcenc_config_init(
        ASTCENC_PRF_LDR, blockW, blockH, 1,
        ASTCENC_PRE_MEDIUM, 0, &config);
    ASSERT_EQ(status, ASTCENC_SUCCESS);

    astcenc_context* ctx = nullptr;
    status = astcenc_context_alloc(&config, 1, &ctx);
    ASSERT_EQ(status, ASTCENC_SUCCESS);

    // Setup input image
    uint8_t* slices = pixels.data();
    astcenc_image image;
    image.dim_x = blockW;
    image.dim_y = blockH;
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;
    image.data = reinterpret_cast<void**>(&slices);

    // Compress (one 4x4 block = 16 bytes ASTC)
    const size_t compressedSize = 16; // One ASTC block is always 16 bytes
    std::vector<uint8_t> compressed(compressedSize, 0);

    astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    status = astcenc_compress_image(ctx, &image, &swizzle,
        compressed.data(), compressedSize, 0);
    ASSERT_EQ(status, ASTCENC_SUCCESS);

    // Verify compressed data is not all zeros
    bool allZero = true;
    for (auto b : compressed) { if (b != 0) { allZero = false; break; } }
    EXPECT_FALSE(allZero) << "Compressed ASTC block should not be all zeros";

    // Decompress back
    astcenc_compress_reset(ctx);

    std::vector<uint8_t> decompressed(blockW * blockH * 4, 0);
    uint8_t* decSlices = decompressed.data();
    astcenc_image decImage;
    decImage.dim_x = blockW;
    decImage.dim_y = blockH;
    decImage.dim_z = 1;
    decImage.data_type = ASTCENC_TYPE_U8;
    decImage.data = reinterpret_cast<void**>(&decSlices);

    status = astcenc_decompress_image(ctx, compressed.data(), compressedSize,
        &decImage, &swizzle, 0);
    ASSERT_EQ(status, ASTCENC_SUCCESS);

    // Verify decompressed pixels are close to original (lossy compression)
    for (size_t i = 0; i < decompressed.size(); i += 4) {
        EXPECT_NEAR(decompressed[i + 0], 200, 15) << "R at pixel " << i / 4;
        EXPECT_NEAR(decompressed[i + 1], 100, 15) << "G at pixel " << i / 4;
        EXPECT_NEAR(decompressed[i + 2], 50, 15)  << "B at pixel " << i / 4;
        EXPECT_NEAR(decompressed[i + 3], 255, 5)  << "A at pixel " << i / 4;
    }

    astcenc_context_free(ctx);
}

TEST(ASTCEnc, ErrorStringIsValid) {
    const char* msg = astcenc_get_error_string(ASTCENC_SUCCESS);
    ASSERT_NE(msg, nullptr);
    EXPECT_STREQ(msg, "ASTCENC_SUCCESS");

    msg = astcenc_get_error_string(ASTCENC_ERR_BAD_BLOCK_SIZE);
    ASSERT_NE(msg, nullptr);
    EXPECT_NE(std::strlen(msg), 0u);
}
