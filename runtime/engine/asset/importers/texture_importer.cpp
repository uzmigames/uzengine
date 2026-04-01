/// UzEngine Texture Importer — Implementation
///
/// Decodes images via stb_image, generates a box-filter mipmap chain,
/// optionally compresses each mip to BC1/BC3/BC4/BC5/BC7 via etcpak,
/// and writes the result into .uztexture format.

#include "texture_importer.h"
#include "log.h"
#include "profiler.h"
#include "uztexture_format.h"

#include <ProcessDxtc.hpp>
#include <bc7enc.h>
#include <spng.h>
#include <jpeglib.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace uz {

namespace {

/// Number of channels forced by stbi_load (always RGBA).
static constexpr int kDesiredChannels = 4;

/// Computes the number of mip levels for a given base resolution.
///
/// # Returns
/// `floor(log2(max(w, h))) + 1`, which produces mips down to 1x1.
[[nodiscard]] uint32_t ComputeMipCount(uint32_t width, uint32_t height) {
    const uint32_t maxDim = std::max(width, height);
    return static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim)))) + 1;
}

/// Generates the next mip level using a 2x2 box filter.
///
/// Each output pixel is the average of a 2x2 block from the source.
/// Handles odd dimensions by clamping source coordinates to the last
/// valid pixel.
///
/// # Arguments
/// * `src` - Source RGBA pixel data.
/// * `srcW` - Source width in pixels.
/// * `srcH` - Source height in pixels.
/// * `dstW` - Destination width (`max(srcW / 2, 1)`).
/// * `dstH` - Destination height (`max(srcH / 2, 1)`).
///
/// # Returns
/// RGBA pixel data for the downsampled mip level.
[[nodiscard]] std::vector<uint8_t> BoxFilterDownsample(
    const uint8_t* src, uint32_t srcW, uint32_t srcH,
    uint32_t dstW, uint32_t dstH) {

    std::vector<uint8_t> dst(static_cast<size_t>(dstW) * dstH * kDesiredChannels);

    for (uint32_t y = 0; y < dstH; ++y) {
        for (uint32_t x = 0; x < dstW; ++x) {
            // Source 2x2 block coordinates, clamped to valid range
            const uint32_t sx0 = std::min(x * 2, srcW - 1);
            const uint32_t sy0 = std::min(y * 2, srcH - 1);
            const uint32_t sx1 = std::min(x * 2 + 1, srcW - 1);
            const uint32_t sy1 = std::min(y * 2 + 1, srcH - 1);

            const size_t srcStride = static_cast<size_t>(srcW) * kDesiredChannels;
            const uint8_t* p00 = src + sy0 * srcStride + sx0 * kDesiredChannels;
            const uint8_t* p10 = src + sy0 * srcStride + sx1 * kDesiredChannels;
            const uint8_t* p01 = src + sy1 * srcStride + sx0 * kDesiredChannels;
            const uint8_t* p11 = src + sy1 * srcStride + sx1 * kDesiredChannels;

            const size_t dstIdx = (static_cast<size_t>(y) * dstW + x) * kDesiredChannels;
            for (int c = 0; c < kDesiredChannels; ++c) {
                const uint32_t sum = static_cast<uint32_t>(p00[c]) +
                                     static_cast<uint32_t>(p10[c]) +
                                     static_cast<uint32_t>(p01[c]) +
                                     static_cast<uint32_t>(p11[c]);
                dst[dstIdx + c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }

    return dst;
}

/// Returns the compressed block size in bytes for a given BC format.
/// BC1 and BC4 use 8-byte blocks; BC3, BC5, and BC7 use 16-byte blocks.
/// Returns 0 for non-block-compressed formats.
[[nodiscard]] uint32_t BlockBytesForFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::BC1: return 8;
        case TextureFormat::BC4: return 8;
        case TextureFormat::BC3: return 16;
        case TextureFormat::BC5: return 16;
        case TextureFormat::BC7: return 16;
        default: return 0;
    }
}

/// Returns true if the format is a block-compressed format.
[[nodiscard]] bool IsBlockCompressed(TextureFormat format) {
    return BlockBytesForFormat(format) > 0;
}

/// Compresses a single RGBA8 mip level to a block-compressed format.
///
/// Pads non-multiple-of-4 dimensions by edge-extending pixels to the
/// nearest 4x4 boundary. etcpak's CompressBcN functions operate on
/// the full padded pixel buffer.
///
/// # Arguments
/// * `rgba8` - Source RGBA8 pixel data (4 bytes per pixel, tightly packed).
/// * `width` - Mip width in pixels.
/// * `height` - Mip height in pixels.
/// * `format` - Target block-compressed format (BC1/BC3/BC4/BC5/BC7).
///
/// # Returns
/// Compressed block data ready for GPU upload.
[[nodiscard]] std::vector<uint8_t> CompressMipToBC(
    const uint8_t* rgba8, uint32_t width, uint32_t height,
    TextureFormat format) {

    const uint32_t blocksX = (width + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    const uint32_t totalBlocks = blocksX * blocksY;
    const uint32_t blockBytes = BlockBytesForFormat(format);

    std::vector<uint8_t> compressed(static_cast<size_t>(totalBlocks) * blockBytes);

    // etcpak requires the pixel buffer to be padded to 4x4 block boundaries.
    // If the image dimensions are not multiples of 4, we create a padded copy
    // with edge-extended pixels to avoid sampling garbage at block borders.
    const uint32_t paddedW = blocksX * 4;
    const uint32_t paddedH = blocksY * 4;
    std::vector<uint32_t> padded;

    const uint32_t* srcPixels = nullptr;
    if (paddedW != width || paddedH != height) {
        padded.resize(static_cast<size_t>(paddedW) * paddedH, 0);
        for (uint32_t y = 0; y < height; ++y) {
            // Copy the row's valid pixels
            std::memcpy(&padded[y * paddedW],
                        rgba8 + y * width * 4,
                        width * 4);
            // Edge-extend rightward to fill padding columns
            for (uint32_t x = width; x < paddedW; ++x) {
                padded[y * paddedW + x] = padded[y * paddedW + width - 1];
            }
        }
        // Edge-extend downward to fill padding rows
        for (uint32_t y = height; y < paddedH; ++y) {
            std::memcpy(&padded[y * paddedW],
                        &padded[(height - 1) * paddedW],
                        paddedW * 4);
        }
        srcPixels = padded.data();
    } else {
        srcPixels = reinterpret_cast<const uint32_t*>(rgba8);
    }

    auto* dst = reinterpret_cast<uint64_t*>(compressed.data());

    switch (format) {
        case TextureFormat::BC1:
            CompressBc1(srcPixels, dst, totalBlocks, paddedW);
            break;
        case TextureFormat::BC3:
            CompressBc3(srcPixels, dst, totalBlocks, paddedW);
            break;
        case TextureFormat::BC4:
            CompressBc4(srcPixels, dst, totalBlocks, paddedW);
            break;
        case TextureFormat::BC5:
            CompressBc5(srcPixels, dst, totalBlocks, paddedW);
            break;
        case TextureFormat::BC7: {
            bc7enc_compress_block_params params;
            bc7enc_compress_block_params_init(&params);
            CompressBc7(srcPixels, dst, totalBlocks, paddedW, &params);
            break;
        }
        default:
            break;
    }

    return compressed;
}

/// Returns a human-readable name for a TextureFormat enum value.
[[nodiscard]] const char* TextureFormatName(TextureFormat format) {
    switch (format) {
        case TextureFormat::Uncompressed_RGBA8: return "RGBA8";
        case TextureFormat::BC1: return "BC1";
        case TextureFormat::BC3: return "BC3";
        case TextureFormat::BC4: return "BC4";
        case TextureFormat::BC5: return "BC5";
        case TextureFormat::BC7: return "BC7";
        default: return "Unknown";
    }
}

/// One-time initialization flag for the BC7 encoder lookup tables.
/// bc7enc_compress_block_init() must be called before any BC7 compression.
static bool s_bc7Initialized = false;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TextureImporter
// ─────────────────────────────────────────────────────────────────────────────

TextureImporter::TextureImporter()
    : m_extensions({".png", ".jpg", ".jpeg", ".tga"}) {}

const char* TextureImporter::Name() const {
    return "texture";
}

uint32_t TextureImporter::Version() const {
    return 2;
}

const std::vector<std::string>& TextureImporter::Extensions() const {
    return m_extensions;
}

AssetImportResult TextureImporter::Import(
    const std::vector<uint8_t>& sourceData,
    const std::string& sourceName,
    const AssetImportOptions& options) {
    UZ_PROFILE_FUNCTION();

    AssetImportResult result;

    if (sourceData.empty()) {
        result.error = "Empty source data";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Decode image — try spng for PNG, jpeglib for JPEG, stb_image for the rest.
    int width = 0;
    int height = 0;
    int channels = 0;

    auto freeDeleter = [](unsigned char* p) { if (p) std::free(p); };
    std::unique_ptr<unsigned char, decltype(freeDeleter)> pixels(nullptr, freeDeleter);

    // PNG: spng (SIMD-accelerated, ~3x faster than stb_image)
    static constexpr uint8_t kPngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (sourceData.size() >= 8 && std::memcmp(sourceData.data(), kPngSig, 8) == 0) {
        spng_ctx* ctx = spng_ctx_new(0);
        if (ctx) {
            spng_set_png_buffer(ctx, sourceData.data(), sourceData.size());
            struct spng_ihdr ihdr;
            if (spng_get_ihdr(ctx, &ihdr) == SPNG_OK) {
                size_t outSize = 0;
                if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &outSize) == SPNG_OK) {
                    auto* px = static_cast<unsigned char*>(std::malloc(outSize));
                    if (px && spng_decode_image(ctx, px, outSize, SPNG_FMT_RGBA8, 0) == SPNG_OK) {
                        width  = static_cast<int>(ihdr.width);
                        height = static_cast<int>(ihdr.height);
                        pixels.reset(px);
                    } else {
                        std::free(px);
                    }
                }
            }
            spng_ctx_free(ctx);
        }
    }

    // JPEG: libjpeg-turbo (SIMD-accelerated)
    if (!pixels && sourceData.size() >= 2 &&
        sourceData[0] == 0xFF && sourceData[1] == 0xD8) {
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, sourceData.data(),
                     static_cast<unsigned long>(sourceData.size()));
        if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
            cinfo.out_color_space = JCS_RGB;
            jpeg_start_decompress(&cinfo);
            width  = static_cast<int>(cinfo.output_width);
            height = static_cast<int>(cinfo.output_height);
            const size_t rgbaSize = static_cast<size_t>(width) * height * 4;
            auto* px = static_cast<unsigned char*>(std::malloc(rgbaSize));
            if (px) {
                auto* rgbRow = static_cast<unsigned char*>(
                    std::malloc(static_cast<size_t>(width) * 3));
                if (rgbRow) {
                    unsigned char* rowPtr = rgbRow;
                    while (cinfo.output_scanline < cinfo.output_height) {
                        jpeg_read_scanlines(&cinfo, &rowPtr, 1);
                        unsigned char* dst = px + static_cast<size_t>(
                            cinfo.output_scanline - 1) * width * 4;
                        for (int x = 0; x < width; ++x) {
                            dst[x * 4 + 0] = rgbRow[x * 3 + 0];
                            dst[x * 4 + 1] = rgbRow[x * 3 + 1];
                            dst[x * 4 + 2] = rgbRow[x * 3 + 2];
                            dst[x * 4 + 3] = 255;
                        }
                    }
                    std::free(rgbRow);
                    pixels.reset(px);
                } else {
                    std::free(px);
                }
            }
            jpeg_finish_decompress(&cinfo);
        }
        jpeg_destroy_decompress(&cinfo);
    }

    // Fallback: stb_image (TGA, BMP, HDR, PSD, GIF, etc.)
    if (!pixels) {
        auto* px = stbi_load_from_memory(
            sourceData.data(), static_cast<int>(sourceData.size()),
            &width, &height, &channels, kDesiredChannels);
        if (px) pixels.reset(px);
    }

    if (!pixels) {
        result.error = std::string("Image decode failed for '") + sourceName + "'";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    if (width <= 0 || height <= 0) {
        result.error = "Decoded image has zero dimensions";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const auto baseW = static_cast<uint32_t>(width);
    const auto baseH = static_cast<uint32_t>(height);
    const uint32_t mipCount = ComputeMipCount(baseW, baseH);
    const TextureFormat targetFormat = options.textureCompression;
    const bool compress = IsBlockCompressed(targetFormat);

    // Lazily initialize BC7 encoder tables on first BC7 compression request.
    // bc7enc_compress_block_init() populates internal lookup tables and is
    // safe to call multiple times, but we guard it to avoid redundant work.
    if (compress && targetFormat == TextureFormat::BC7 && !s_bc7Initialized) {
        bc7enc_compress_block_init();
        s_bc7Initialized = true;
    }

    // Build uncompressed RGBA8 mip chain (box filter downsampling)
    std::vector<std::vector<uint8_t>> rgbaMips;
    rgbaMips.reserve(mipCount);

    // Mip 0: copy decoded pixels
    const size_t baseSize = static_cast<size_t>(baseW) * baseH * kDesiredChannels;
    rgbaMips.emplace_back(pixels.get(), pixels.get() + baseSize);

    // Generate subsequent mips via box filter
    uint32_t prevW = baseW;
    uint32_t prevH = baseH;
    for (uint32_t i = 1; i < mipCount; ++i) {
        const uint32_t mipW = std::max(prevW / 2, 1u);
        const uint32_t mipH = std::max(prevH / 2, 1u);

        rgbaMips.push_back(BoxFilterDownsample(
            rgbaMips.back().data(), prevW, prevH, mipW, mipH));

        prevW = mipW;
        prevH = mipH;
    }

    // If block compression is requested, compress each mip level.
    // Otherwise, the uncompressed RGBA8 mips are written directly.
    std::vector<std::vector<uint8_t>> outputMips;
    if (compress) {
        outputMips.reserve(mipCount);
        uint32_t mipW = baseW;
        uint32_t mipH = baseH;
        for (uint32_t i = 0; i < mipCount; ++i) {
            outputMips.push_back(CompressMipToBC(
                rgbaMips[i].data(), mipW, mipH, targetFormat));
            mipW = std::max(mipW / 2, 1u);
            mipH = std::max(mipH / 2, 1u);
        }
    } else {
        outputMips = std::move(rgbaMips);
    }

    // Build .uztexture header
    UzTextureHeader header;
    header.width = baseW;
    header.height = baseH;
    header.mipCount = mipCount;
    header.format = static_cast<uint32_t>(targetFormat);
    if (options.textureSRGB) {
        header.flags |= kUzTextureFlag_sRGB;
    }

    result.data = WriteUzTexture(header, outputMips);
    result.success = true;

    UZ_LOG(Asset, Info, "Imported texture '{}': {}x{}, {} mips, format={}, sRGB={}, {} bytes",
           sourceName, baseW, baseH, mipCount,
           TextureFormatName(targetFormat),
           options.textureSRGB ? "yes" : "no",
           result.data.size());

    return result;
}

} // namespace uz
