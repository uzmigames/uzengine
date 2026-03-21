/// UzEngine Texture Loader — Implementation.
///
/// Decodes images via stb_image, generates a full mip chain on the CPU via
/// box filtering, and uploads all mip levels to the GPU via UploadContext.
///
/// # Mipmap Generation
/// CPU-side box filter (2x2 average) produces each mip level from the previous.
/// This runs during asset load, not per-frame, so the CPU cost is acceptable.
/// The full mip chain enables proper trilinear and anisotropic filtering at
/// distance, eliminating texture shimmer and aliasing.

#include "texture_loader.h"
#include "upload_context.h"
#include "uztexture_format.h"
#include "log.h"
#include "profiler.h"

#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Box filter mip generation — CPU side
// ─────────────────────────────────────────────────────────────────────────────

/// Generates the next mip level from the source using a 2x2 box filter.
///
/// Each output texel is the average of a 2x2 block of source texels.
/// Handles non-power-of-two source dimensions correctly by clamping
/// sample coordinates to source bounds.
///
/// # Arguments
/// * `src` — Source RGBA8 pixel data (srcW * srcH * 4 bytes).
/// * `srcW` — Source width in texels.
/// * `srcH` — Source height in texels.
/// * `dst` — Destination buffer (dstW * dstH * 4 bytes, must be pre-allocated).
/// * `dstW` — Destination width (max(srcW >> 1, 1)).
/// * `dstH` — Destination height (max(srcH >> 1, 1)).
static void BoxFilterDownsample(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                                uint8_t* dst, uint32_t dstW, uint32_t dstH) noexcept
{
    constexpr uint32_t kBpp = 4; // RGBA8

    for (uint32_t y = 0; y < dstH; ++y) {
        for (uint32_t x = 0; x < dstW; ++x) {
            // Source coordinates for the 2x2 block
            const uint32_t sx0 = x * 2;
            const uint32_t sy0 = y * 2;
            const uint32_t sx1 = std::min(sx0 + 1, srcW - 1);
            const uint32_t sy1 = std::min(sy0 + 1, srcH - 1);

            // Fetch the four source texels
            const uint8_t* p00 = src + (sy0 * srcW + sx0) * kBpp;
            const uint8_t* p10 = src + (sy0 * srcW + sx1) * kBpp;
            const uint8_t* p01 = src + (sy1 * srcW + sx0) * kBpp;
            const uint8_t* p11 = src + (sy1 * srcW + sx1) * kBpp;

            // Average each channel with rounding (add 2 before dividing by 4
            // to get round-half-up behavior, reducing cumulative rounding bias
            // across the mip chain)
            uint8_t* out = dst + (y * dstW + x) * kBpp;
            for (uint32_t c = 0; c < kBpp; ++c) {
                const uint32_t sum = static_cast<uint32_t>(p00[c])
                                   + static_cast<uint32_t>(p10[c])
                                   + static_cast<uint32_t>(p01[c])
                                   + static_cast<uint32_t>(p11[c]);
                out[c] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::Load
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::Load(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const std::string& path,
    bool isSRGB)
{
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "TextureLoader::Load: null device or cmdList");
        return {};
    }

    int w = 0;
    int h = 0;
    int channels = 0;

    // Force 4-channel RGBA output regardless of source format
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        UZ_LOG(Renderer, Error, "TextureLoader::Load: failed to decode image: path='{}', reason='{}'",
               path, stbi_failure_reason());
        return {};
    }

    if (w <= 0 || h <= 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::Load: invalid dimensions: path='{}', {}x{}",
               path, w, h);
        stbi_image_free(pixels);
        return {};
    }

    auto result = UploadPixels(device, cmdList, pixels,
                               static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h),
                               path.c_str(),
                               isSRGB);

    stbi_image_free(pixels);

    if (result.handle.IsValid()) {
        UZ_LOG(Renderer, Info, "Loaded texture: path='{}', {}x{}, {} mips, SRV={}, sRGB={}",
               path, result.width, result.height, result.mipLevels, result.srvIndex, isSRGB);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadFromMemory
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::LoadFromMemory(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const uint8_t* data,
    size_t size,
    const char* debugName,
    bool isSRGB)
{
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: null device or cmdList");
        return {};
    }

    if (!data || size == 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: null or empty data buffer, name='{}'",
               debugName ? debugName : "unknown");
        return {};
    }

    int w = 0;
    int h = 0;
    int channels = 0;

    // stbi expects int length; clamp to INT_MAX for safety
    const int len = (size > static_cast<size_t>(INT32_MAX))
                        ? INT32_MAX
                        : static_cast<int>(size);

    unsigned char* pixels = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
    if (!pixels) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: failed to decode image: name='{}', reason='{}'",
               debugName ? debugName : "unknown", stbi_failure_reason());
        return {};
    }

    if (w <= 0 || h <= 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: invalid dimensions: name='{}', {}x{}",
               debugName ? debugName : "unknown", w, h);
        stbi_image_free(pixels);
        return {};
    }

    const char* name = debugName ? debugName : "memory_texture";
    auto result = UploadPixels(device, cmdList, pixels,
                               static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h),
                               name,
                               isSRGB);

    stbi_image_free(pixels);

    if (result.handle.IsValid()) {
        UZ_LOG(Renderer, Info, "Loaded texture: path='{}', {}x{}, {} mips, SRV={}, sRGB={}",
               name, result.width, result.height, result.mipLevels, result.srvIndex, isSRGB);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadPixelsCore — shared mip-gen + upload logic used by both variants
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a GPU texture, generates the full mip chain via CPU box filter,
/// uploads all mip levels through the provided UploadContext, and records
/// a CopyDst -> ShaderResource barrier. Does NOT flush the command list.
///
/// Returns a TextureLoadResult with the SRV index populated on success.
static TextureLoadResult UploadPixelsCore(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    unsigned char* pixels,
    uint32_t width,
    uint32_t height,
    const char* debugName,
    UploadContext& upload,
    bool isSRGB)
{
    UZ_PROFILE_FUNCTION();

    constexpr uint32_t kBpp = 4; // RGBA8 = 4 bytes per texel

    const uint16_t mipCount = ComputeTextureMipCount(width, height);

    // Create GPU texture with full mip chain.
    // sRGB format enables automatic gamma-to-linear conversion on GPU sample,
    // which is correct for color textures (albedo, emissive). Linear data
    // (normal maps, roughness, metallic, AO, ORM) must use Unorm to avoid
    // double-gamma artifacts.
    RHITextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.mipLevels = mipCount;
    desc.format = isSRGB ? RHIFormat::RGBA8Srgb : RHIFormat::RGBA8Unorm;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error, "TextureLoader: failed to create GPU texture: name='{}', {}x{}, {} mips",
               debugName ? debugName : "unknown", width, height, mipCount);
        return {};
    }

    // Upload mip 0 (original pixels)
    const uint64_t mip0DataSize = static_cast<uint64_t>(width) * height * kBpp;

    if (!upload.UploadTexture(cmdList, texture, pixels, mip0DataSize, 0)) {
        UZ_LOG(Renderer, Error, "TextureLoader: mip 0 upload failed: name='{}', {}x{}",
               debugName ? debugName : "unknown", width, height);
        device->DestroyTexture(texture);
        return {};
    }

    // Generate and upload remaining mip levels via CPU box filter.
    // We keep two buffers: the "previous mip" source and the "current mip"
    // destination. After generating each mip, the current buffer becomes the
    // source for the next iteration.
    if (mipCount > 1) {
        UZ_PROFILE_SCOPE("TextureLoader::MipGenCPU");

        // Allocate working buffer large enough for the largest mip we need to
        // generate (mip 1 = half resolution). Subsequent mips reuse this buffer
        // since each is smaller than the last.
        const uint32_t mip1W = std::max(width >> 1, 1u);
        const uint32_t mip1H = std::max(height >> 1, 1u);
        const uint64_t maxMipBytes = static_cast<uint64_t>(mip1W) * mip1H * kBpp;

        // Two ping-pong buffers: src is the previous mip's data, dst is where
        // we generate the current mip. After each iteration, swap them.
        auto bufA = std::make_unique<uint8_t[]>(maxMipBytes);
        auto bufB = std::make_unique<uint8_t[]>(maxMipBytes);

        // First iteration reads from the original pixel data (mip 0)
        const uint8_t* srcData = pixels;
        uint32_t srcW = width;
        uint32_t srcH = height;
        uint8_t* dstData = bufA.get();

        for (uint16_t mip = 1; mip < mipCount; ++mip) {
            const uint32_t dstW = std::max(width >> mip, 1u);
            const uint32_t dstH = std::max(height >> mip, 1u);

            BoxFilterDownsample(srcData, srcW, srcH, dstData, dstW, dstH);

            const uint64_t mipDataSize = static_cast<uint64_t>(dstW) * dstH * kBpp;
            if (!upload.UploadTexture(cmdList, texture, dstData, mipDataSize, mip)) {
                UZ_LOG(Renderer, Warning,
                       "TextureLoader: mip {} upload failed: name='{}', {}x{} — "
                       "texture will have incomplete mip chain",
                       mip, debugName ? debugName : "unknown", dstW, dstH);
                break;
            }

            // Prepare for next iteration: current output becomes next input
            srcData = dstData;
            srcW = dstW;
            srcH = dstH;

            // Swap destination buffer so we don't overwrite the source we just set
            dstData = (dstData == bufA.get()) ? bufB.get() : bufA.get();
        }
    }

    // Transition from CopyDst to ShaderResource
    RHIBarrierDesc barrier{};
    barrier.texture = texture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // Build result
    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = width;
    result.height = height;
    result.mipLevels = mipCount;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadPixelsWithMipsCore — upload pre-computed mip chain (no mip generation)
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a GPU texture and uploads a pre-computed mip chain directly,
/// bypassing CPU box filter mip generation entirely. The caller provides
/// mip 0 pixels and an array of pre-generated mip level data.
///
/// This is the performance-critical path for async-decoded textures where
/// worker threads have already computed the full mip chain. Using this
/// avoids redundant main-thread mip computation.
///
/// Records upload commands and a CopyDst -> ShaderResource barrier on the
/// provided command list. Does NOT flush the command list.
static TextureLoadResult UploadPixelsWithMipsCore(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    unsigned char* pixels,
    uint32_t width,
    uint32_t height,
    const char* debugName,
    UploadContext& upload,
    uint8_t* const* mipData,
    const uint32_t* mipWidths,
    const uint32_t* mipHeights,
    uint16_t mipCount,
    bool isSRGB)
{
    UZ_PROFILE_FUNCTION();

    constexpr uint32_t kBpp = 4; // RGBA8 = 4 bytes per texel

    // Create GPU texture with the caller-specified mip count.
    // See UploadPixelsCore for sRGB vs Unorm rationale.
    RHITextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.mipLevels = mipCount;
    desc.format = isSRGB ? RHIFormat::RGBA8Srgb : RHIFormat::RGBA8Unorm;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error,
               "TextureLoader: failed to create GPU texture: name='{}', {}x{}, {} mips",
               debugName ? debugName : "unknown", width, height, mipCount);
        return {};
    }

    // Upload mip 0 (original pixels)
    const uint64_t mip0DataSize = static_cast<uint64_t>(width) * height * kBpp;
    if (!upload.UploadTexture(cmdList, texture, pixels, mip0DataSize, 0)) {
        UZ_LOG(Renderer, Error, "TextureLoader: mip 0 upload failed: name='{}', {}x{}",
               debugName ? debugName : "unknown", width, height);
        device->DestroyTexture(texture);
        return {};
    }

    // Upload pre-computed mip levels directly — no CPU box filter needed.
    // mipData[i] corresponds to mip level (i + 1).
    for (uint16_t mip = 1; mip < mipCount; ++mip) {
        const uint16_t idx = mip - 1;
        const uint32_t mipW = mipWidths[idx];
        const uint32_t mipH = mipHeights[idx];
        const uint64_t mipDataSize = static_cast<uint64_t>(mipW) * mipH * kBpp;

        if (!upload.UploadTexture(cmdList, texture, mipData[idx], mipDataSize, mip)) {
            UZ_LOG(Renderer, Warning,
                   "TextureLoader: mip {} upload failed: name='{}', {}x{} — "
                   "texture will have incomplete mip chain",
                   mip, debugName ? debugName : "unknown", mipW, mipH);
            break;
        }
    }

    // Transition from CopyDst to ShaderResource
    RHIBarrierDesc barrier{};
    barrier.texture = texture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // Build result
    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = width;
    result.height = height;
    result.mipLevels = mipCount;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::UploadPixels — synchronous upload (flushes + waits)
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::UploadPixels(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    unsigned char* pixels,
    uint32_t width,
    uint32_t height,
    const char* debugName,
    bool isSRGB)
{
    UploadContext upload(device);
    TextureLoadResult result = UploadPixelsCore(device, cmdList, pixels,
                                                 width, height, debugName, upload, isSRGB);

    if (!result.handle.IsValid()) {
        return result;
    }

    // Flush the command list and wait for GPU completion
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();

    // Safe to release staging buffers now that GPU is idle
    upload.ReleaseStaging();

    // Reset command list to recording state for caller
    cmdList->Begin();

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::UploadPixelsDeferred — batched upload (no flush, no wait)
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::UploadPixelsDeferred(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    unsigned char* pixels,
    uint32_t width,
    uint32_t height,
    const char* debugName,
    UploadContext* outUpload,
    bool isSRGB)
{
    if (!device || !cmdList || !pixels || !outUpload) {
        UZ_LOG(Renderer, Error, "TextureLoader::UploadPixelsDeferred: null argument");
        return {};
    }

    return UploadPixelsCore(device, cmdList, pixels, width, height,
                            debugName, *outUpload, isSRGB);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::UploadPixelsWithMipsDeferred — batched upload with pre-computed mips
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::UploadPixelsWithMipsDeferred(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    unsigned char* pixels,
    uint32_t width,
    uint32_t height,
    const char* debugName,
    UploadContext* outUpload,
    uint8_t* const* mipData,
    const uint32_t* mipWidths,
    const uint32_t* mipHeights,
    uint16_t mipCount,
    bool isSRGB)
{
    if (!device || !cmdList || !pixels || !outUpload) {
        UZ_LOG(Renderer, Error, "TextureLoader::UploadPixelsWithMipsDeferred: null argument");
        return {};
    }

    if (mipCount < 1) {
        UZ_LOG(Renderer, Error, "TextureLoader::UploadPixelsWithMipsDeferred: mipCount must be >= 1");
        return {};
    }

    // Validate mip data pointers when mipCount > 1 (mip 0 is in `pixels`)
    if (mipCount > 1 && (!mipData || !mipWidths || !mipHeights)) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::UploadPixelsWithMipsDeferred: null mip arrays with mipCount={}",
               mipCount);
        return {};
    }

    return UploadPixelsWithMipsCore(device, cmdList, pixels, width, height,
                                     debugName, *outUpload,
                                     mipData, mipWidths, mipHeights, mipCount,
                                     isSRGB);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadUzTex — pre-cooked .uztexture direct GPU upload
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::LoadUzTex(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const uint8_t* data,
    size_t size,
    const char* debugName,
    UploadContext* outUpload)
{
    UZ_PROFILE_FUNCTION();

    if (!device || !cmdList || !data || size == 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadUzTex: null argument");
        return {};
    }

    // Parse the .uztexture binary format — validates magic, version, dimensions,
    // mip descriptors, and extracts per-mip data blobs.
    std::vector<uint8_t> fileData(data, data + size);
    auto parsed = ReadUzTexture(fileData);
    if (!parsed.success) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadUzTex: parse failed for '{}': {}",
               debugName ? debugName : "unknown", parsed.error);
        return {};
    }

    const auto& header = parsed.file.header;
    const bool isSRGB = (header.flags & kUzTextureFlag_sRGB) != 0;
    const auto texFmt = static_cast<TextureFormat>(header.format);
    const RHIFormat rhiFormat = TextureFormatToRHI(texFmt, isSRGB);

    if (rhiFormat == RHIFormat::Unknown) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadUzTex: unsupported format {} in '{}'",
               header.format, debugName ? debugName : "unknown");
        return {};
    }

    // Create GPU texture with the exact format and mip count from the file.
    // No format conversion — the .uztexture data is already in the target format.
    RHITextureDesc desc{};
    desc.width = header.width;
    desc.height = header.height;
    desc.mipLevels = static_cast<uint16_t>(header.mipCount);
    desc.format = rhiFormat;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadUzTex: failed to create GPU texture '{}' {}x{} fmt={}",
               debugName ? debugName : "unknown", header.width, header.height, static_cast<uint32_t>(rhiFormat));
        return {};
    }

    // Upload each mip level — data is already GPU-ready (BC7/BC1/BC5/BC4/RGBA8).
    // Use either deferred (batched) or sync (immediate) path based on outUpload.
    UploadContext localUpload(device);
    UploadContext& upload = outUpload ? *outUpload : localUpload;

    for (uint32_t mip = 0; mip < header.mipCount; ++mip) {
        const auto& mipBlob = parsed.file.mipData[mip];
        if (!upload.UploadTexture(cmdList, texture, mipBlob.data(),
                                   static_cast<uint64_t>(mipBlob.size()), mip)) {
            UZ_LOG(Renderer, Warning,
                   "TextureLoader::LoadUzTex: mip {} upload failed for '{}'",
                   mip, debugName ? debugName : "unknown");
            break;
        }
    }

    // Transition to ShaderResource state for sampling.
    RHIBarrierDesc barrier{};
    barrier.texture = texture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // Sync path: flush the command list now and wait for GPU completion.
    // Deferred path: caller flushes later in a single batch submission.
    if (!outUpload) {
        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        localUpload.ReleaseStaging();
        cmdList->Begin();
    }

    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = header.width;
    result.height = header.height;
    result.mipLevels = static_cast<uint16_t>(header.mipCount);

    UZ_LOG(Renderer, Info, "Loaded .uztexture: name='{}', {}x{}, {} mips, fmt={}, sRGB={}, SRV={}",
           debugName ? debugName : "unknown", result.width, result.height,
           result.mipLevels, header.format, isSRGB, result.srvIndex);

    return result;
}

} // namespace uz
