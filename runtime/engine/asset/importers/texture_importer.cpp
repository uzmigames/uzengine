/// UzEngine Texture Importer — Implementation
///
/// Decodes images via stb_image, generates a box-filter mipmap chain,
/// and writes the result into .uztexture format as Uncompressed_RGBA8.

#include "texture_importer.h"
#include "log.h"
#include "profiler.h"
#include "uztexture_format.h"

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
    return 1;
}

const std::vector<std::string>& TextureImporter::Extensions() const {
    return m_extensions;
}

AssetImportResult TextureImporter::Import(
    const std::vector<uint8_t>& sourceData,
    const std::string& sourceName,
    const AssetImportOptions& /*options*/) {
    UZ_PROFILE_FUNCTION();

    AssetImportResult result;

    if (sourceData.empty()) {
        result.error = "Empty source data";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Decode image with stb_image
    int width = 0;
    int height = 0;
    int channels = 0;

    // RAII wrapper for stbi-allocated memory
    auto stbiDeleter = [](unsigned char* p) { if (p) stbi_image_free(p); };
    std::unique_ptr<unsigned char, decltype(stbiDeleter)> pixels(
        stbi_load_from_memory(
            sourceData.data(), static_cast<int>(sourceData.size()),
            &width, &height, &channels, kDesiredChannels),
        stbiDeleter);

    if (!pixels) {
        result.error = std::string("stb_image decode failed: ") + stbi_failure_reason();
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

    // Build mip chain
    std::vector<std::vector<uint8_t>> mipData;
    mipData.reserve(mipCount);

    // Mip 0: copy decoded pixels
    const size_t baseSize = static_cast<size_t>(baseW) * baseH * kDesiredChannels;
    mipData.emplace_back(pixels.get(), pixels.get() + baseSize);

    // Generate subsequent mips via box filter
    uint32_t prevW = baseW;
    uint32_t prevH = baseH;
    for (uint32_t i = 1; i < mipCount; ++i) {
        const uint32_t mipW = std::max(prevW / 2, 1u);
        const uint32_t mipH = std::max(prevH / 2, 1u);

        mipData.push_back(BoxFilterDownsample(
            mipData.back().data(), prevW, prevH, mipW, mipH));

        prevW = mipW;
        prevH = mipH;
    }

    // Build .uztexture header
    UzTextureHeader header;
    header.width = baseW;
    header.height = baseH;
    header.mipCount = mipCount;
    header.format = static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8);

    result.data = WriteUzTexture(header, mipData);
    result.success = true;

    UZ_LOG(Asset, Info, "Imported texture '{}': {}x{}, {} mips, {} bytes",
           sourceName, baseW, baseH, mipCount, result.data.size());

    return result;
}

} // namespace uz
