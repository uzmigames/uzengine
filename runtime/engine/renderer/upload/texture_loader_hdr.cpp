/// UzEngine Texture Loader — HDR/EXR loading path.
///
/// Loads high dynamic range textures from .exr (tinyexr) and .hdr (stb_image)
/// files. Decoded float32 RGBA data is converted to float16 (RGBA16Float)
/// on the CPU to halve VRAM usage — the visual difference is imperceptible
/// for environment maps, and GPU sampling of RGBA16F is faster than RGBA32F.

#include "texture_loader.h"
#include "upload_context.h"
#include "log.h"
#include "profiler.h"

#include <stb_image.h>
#include <tinyexr.h>

#include <cstring>
#include <memory>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Float32-to-Float16 conversion (IEEE 754 half-precision)
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a single-precision float to a half-precision float stored as uint16.
///
/// Handles the full range of IEEE 754 values: normals, denormals, infinities,
/// NaN, and signed zero. Values that overflow half range saturate to +/-Inf.
/// This is the standard float-to-half conversion used by GPU drivers.
///
/// The implementation follows the bit manipulation approach from
/// "Fast Half Float Conversions" by Jeroen van der Zijp (2010).
static uint16_t FloatToHalf(float value) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(float));

    const uint32_t sign     = (bits >> 16) & 0x8000u;
    const int32_t  exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127;
    const uint32_t mantissa = bits & 0x007FFFFFu;

    // Zero or denormal (too small for half-precision denormals)
    if (exponent < -24) {
        return static_cast<uint16_t>(sign);
    }

    // Denormal half (exponent in [-24, -15])
    if (exponent < -14) {
        // Shift mantissa right to create denormalized half
        const uint32_t shift = static_cast<uint32_t>(-14 - exponent);
        const uint32_t denormMantissa = (mantissa | 0x00800000u) >> (shift + 13);
        return static_cast<uint16_t>(sign | denormMantissa);
    }

    // Normal range
    if (exponent <= 15) {
        return static_cast<uint16_t>(sign
                                     | (static_cast<uint32_t>(exponent + 15) << 10)
                                     | (mantissa >> 13));
    }

    // Overflow — saturate to Inf (or NaN if input was NaN)
    if (exponent == 128 && mantissa != 0) {
        // NaN — preserve a non-zero mantissa bit
        return static_cast<uint16_t>(sign | 0x7C00u | (mantissa >> 13) | 1u);
    }
    // Inf or values too large for half
    return static_cast<uint16_t>(sign | 0x7C00u);
}

/// Converts a contiguous array of float32 RGBA texels to float16 RGBA in-place
/// or to a separate destination buffer.
///
/// # Arguments
/// * `src` — Source float32 RGBA data (width * height * 4 floats).
/// * `dst` — Destination uint16 buffer (width * height * 4 uint16s). Must be pre-allocated.
/// * `texelCount` — Number of texels (width * height). Each texel has 4 channels.
static void ConvertFloat32ToFloat16(const float* src, uint16_t* dst, size_t texelCount) noexcept {
    const size_t channelCount = texelCount * 4;
    for (size_t i = 0; i < channelCount; ++i) {
        dst[i] = FloatToHalf(src[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadHDR — load .exr and .hdr files as RGBA16Float
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::LoadHDR(
    IRHIDevice* device,
    const std::string& path)
{
    UZ_PROFILE_FUNCTION();

    if (!device) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadHDR: null device");
        return {};
    }

    if (path.empty()) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadHDR: empty path");
        return {};
    }

    // Create a temporary command list for the upload — caller doesn't need to manage GPU commands.
    auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadHDR: failed to create command list");
        return {};
    }
    cmdList->Begin();

    float* pixels = nullptr;
    int w = 0;
    int h = 0;
    bool isExr = false;

    // Determine format from file extension
    const auto dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        const std::string ext = path.substr(dotPos);
        if (ext == ".exr" || ext == ".EXR") {
            isExr = true;
        }
    }

    if (isExr) {
        // ── EXR loading via tinyexr ─────────────────────────────────────
        const char* err = nullptr;
        const int ret = LoadEXR(&pixels, &w, &h, path.c_str(), &err);
        if (ret != TINYEXR_SUCCESS || !pixels) {
            UZ_LOG(Renderer, Error,
                   "TextureLoader::LoadHDR: tinyexr failed to load '{}': {}",
                   path, err ? err : "unknown error");
            if (err) {
                FreeEXRErrorMessage(err);
            }
            cmdList->End(); device->DestroyCommandList(cmdList);
            return {};
        }
    } else {
        // ── HDR (Radiance RGBE) loading via stb_image ───────────────────
        int channels = 0;
        pixels = stbi_loadf(path.c_str(), &w, &h, &channels, 4);
        if (!pixels) {
            UZ_LOG(Renderer, Error,
                   "TextureLoader::LoadHDR: stbi_loadf failed for '{}': {}",
                   path, stbi_failure_reason());
            cmdList->End(); device->DestroyCommandList(cmdList);
            return {};
        }
    }

    if (w <= 0 || h <= 0) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadHDR: invalid dimensions for '{}': {}x{}",
               path, w, h);
        if (isExr) {
            free(pixels);
        } else {
            stbi_image_free(pixels);
        }
        cmdList->End(); device->DestroyCommandList(cmdList);
        return {};
    }

    const uint32_t width  = static_cast<uint32_t>(w);
    const uint32_t height = static_cast<uint32_t>(h);
    const size_t texelCount = static_cast<size_t>(width) * height;

    // Convert float32 RGBA to float16 RGBA for VRAM savings.
    // RGBA16F is 8 bytes/texel vs 16 bytes/texel for RGBA32F — half the memory
    // with imperceptible quality loss for environment maps.
    constexpr size_t kBytesPerTexelF16 = 8; // 4 channels * 2 bytes
    auto halfPixels = std::make_unique<uint16_t[]>(texelCount * 4);
    ConvertFloat32ToFloat16(pixels, halfPixels.get(), texelCount);

    // Free the float32 source — we only need the float16 data from here
    if (isExr) {
        free(pixels);
    } else {
        stbi_image_free(pixels);
    }
    pixels = nullptr;

    // Create GPU texture as RGBA16Float, single mip level.
    // HDR environment maps are consumed by the IBL baker which generates its
    // own mip pyramid during prefiltered specular baking. A source-level mip
    // chain would be wasted memory.
    RHITextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.mipLevels = 1;
    desc.format = RHIFormat::RGBA16Float;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = path.c_str();

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadHDR: failed to create GPU texture for '{}', {}x{}",
               path, width, height);
        cmdList->End(); device->DestroyCommandList(cmdList);
        return {};
    }

    // Upload the float16 data via staging buffer
    UploadContext upload(device);
    const uint64_t dataSize = static_cast<uint64_t>(texelCount) * kBytesPerTexelF16;

    if (!upload.UploadTexture(cmdList, texture, halfPixels.get(), dataSize, 0)) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadHDR: upload failed for '{}', {}x{}",
               path, width, height);
        device->DestroyTexture(texture);
        cmdList->End(); device->DestroyCommandList(cmdList);
        return {};
    }

    // Transition from CopyDst to ShaderResource
    RHIBarrierDesc barrier{};
    barrier.texture = texture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // Flush and wait for GPU completion
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();

    // Clean up the temporary command list
    device->DestroyCommandList(cmdList);

    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = width;
    result.height = height;
    result.mipLevels = 1;
    result.loadState = TextureLoadState::FullyLoaded;

    UZ_LOG(Renderer, Info,
           "Loaded HDR texture: path='{}', {}x{}, format={}, SRV={}",
           path, width, height, isExr ? "EXR->RGBA16F" : "HDR->RGBA16F",
           result.srvIndex);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadHDRFromMemory — load .exr/.hdr from in-memory VFS data
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::LoadHDRFromMemory(
    IRHIDevice* device,
    const void* data,
    size_t dataSize,
    const char* formatHint,
    const char* debugName)
{
    UZ_PROFILE_FUNCTION();

    if (!device || !data || dataSize == 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadHDRFromMemory: invalid arguments");
        return {};
    }

    float* pixels = nullptr;
    int w = 0;
    int h = 0;
    const bool isExr = formatHint && (std::string(formatHint) == "exr" || std::string(formatHint) == "EXR");

    if (isExr) {
        const char* err = nullptr;
        const int ret = LoadEXRFromMemory(
            &pixels, &w, &h,
            static_cast<const unsigned char*>(data),
            dataSize, &err);
        if (ret != TINYEXR_SUCCESS || !pixels) {
            UZ_LOG(Renderer, Error,
                   "TextureLoader::LoadHDRFromMemory: tinyexr decode failed: {}",
                   err ? err : "unknown error");
            if (err) FreeEXRErrorMessage(err);
            return {};
        }
    } else {
        int channels = 0;
        pixels = stbi_loadf_from_memory(
            static_cast<const unsigned char*>(data),
            static_cast<int>(dataSize), &w, &h, &channels, 4);
        if (!pixels) {
            UZ_LOG(Renderer, Error,
                   "TextureLoader::LoadHDRFromMemory: stbi_loadf decode failed: {}",
                   stbi_failure_reason());
            return {};
        }
    }

    if (w <= 0 || h <= 0) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadHDRFromMemory: invalid dimensions {}x{}", w, h);
        if (isExr) free(pixels); else stbi_image_free(pixels);
        return {};
    }

    uint32_t width  = static_cast<uint32_t>(w);
    uint32_t height = static_cast<uint32_t>(h);

    // Downscale if larger than needed — HDRI environment maps are typically
    // consumed at cubemap resolution (512-1024), so uploading 4K+ is wasteful.
    // 2:1 box filter halving preserves HDR range without clamping.
    constexpr uint32_t kMaxHDRResolution = 2048;
    std::unique_ptr<float[]> downscaled;
    while (width > kMaxHDRResolution && height > 1) {
        const uint32_t newW = width / 2;
        const uint32_t newH = height / 2;
        downscaled = std::make_unique<float[]>(static_cast<size_t>(newW) * newH * 4);
        for (uint32_t y = 0; y < newH; ++y) {
            for (uint32_t x = 0; x < newW; ++x) {
                const size_t dst = (static_cast<size_t>(y) * newW + x) * 4;
                const size_t s00 = (static_cast<size_t>(y * 2)     * width + x * 2) * 4;
                const size_t s10 = (static_cast<size_t>(y * 2)     * width + x * 2 + 1) * 4;
                const size_t s01 = (static_cast<size_t>(y * 2 + 1) * width + x * 2) * 4;
                const size_t s11 = (static_cast<size_t>(y * 2 + 1) * width + x * 2 + 1) * 4;
                for (int c = 0; c < 4; ++c) {
                    downscaled[dst + c] = (pixels[s00 + c] + pixels[s10 + c] +
                                           pixels[s01 + c] + pixels[s11 + c]) * 0.25f;
                }
            }
        }
        if (isExr) { free(pixels); } else { stbi_image_free(pixels); }
        pixels = downscaled.get();
        width = newW;
        height = newH;
        UZ_LOG(Renderer, Info, "TextureLoader: downscaled HDR to {}x{}", width, height);
    }

    const size_t texelCount = static_cast<size_t>(width) * height;
    constexpr size_t kBytesPerTexelF16 = 8;

    auto halfPixels = std::make_unique<uint16_t[]>(texelCount * 4);
    ConvertFloat32ToFloat16(pixels, halfPixels.get(), texelCount);

    if (!downscaled) {
        // Original buffer not yet freed (no downscale happened)
        if (isExr) free(pixels); else stbi_image_free(pixels);
    }
    pixels = nullptr;

    RHITextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.mipLevels = 1;
    desc.format = RHIFormat::RGBA16Float;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    auto* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadHDRFromMemory: failed to create command list");
        return {};
    }
    cmdList->Begin();

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadHDRFromMemory: GPU texture creation failed, {}x{}", width, height);
        cmdList->End(); device->DestroyCommandList(cmdList);
        return {};
    }

    UploadContext upload(device);
    const uint64_t uploadSize = static_cast<uint64_t>(texelCount) * kBytesPerTexelF16;

    if (!upload.UploadTexture(cmdList, texture, halfPixels.get(), uploadSize, 0)) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadHDRFromMemory: upload failed, {}x{}", width, height);
        device->DestroyTexture(texture);
        cmdList->End(); device->DestroyCommandList(cmdList);
        return {};
    }

    RHIBarrierDesc barrier{};
    barrier.texture = texture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();
    upload.ReleaseStaging();
    device->DestroyCommandList(cmdList);

    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = width;
    result.height = height;
    result.mipLevels = 1;
    result.loadState = TextureLoadState::FullyLoaded;

    UZ_LOG(Renderer, Info,
           "Loaded HDR texture from memory: {}x{}, format={}, SRV={}",
           width, height, isExr ? "EXR->RGBA16F" : "HDR->RGBA16F",
           result.srvIndex);

    return result;
}

} // namespace uz
