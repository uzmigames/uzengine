/// UzEngine Texture Loader — Zero-copy ring-buffer upload path.
///
/// Loads pre-cooked .uztexture files to the GPU via UploadRing suballocation.
/// Eliminates N staging buffer allocations per texture (one per mip level) by
/// using a pre-allocated, persistently mapped ring buffer. Each mip upload is
/// just: bump-allocate -> memcpy -> record CopyBufferToTexture.

#include "texture_loader.h"
#include "upload_ring.h"
#include "uztexture_format.h"
#include "log.h"
#include "profiler.h"

#include <chrono>
#include <cstring>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadUzTexViaRing — zero-alloc .uztexture upload via ring buffer
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::LoadUzTexViaRing(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const uint8_t* data,
    size_t size,
    const char* debugName,
    UploadRing& ring,
    bool skipBarrier)
{
    UZ_PROFILE_FUNCTION();
    auto t0 = std::chrono::high_resolution_clock::now();

    if (!device || !cmdList || !data || size == 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadUzTexViaRing: null argument");
        return {};
    }

    // Zero-copy parse: read header + mip descriptors directly from the source
    // buffer without copying mip data. ReadUzTexture copies all mip blobs into
    // vectors (37-82ms for 2K BC7) — we skip that entirely by computing mip
    // data pointers as offsets into the original `data` buffer.
    // ParseUzTextureLayout applies the same validation as ReadUzTexture: every
    // mip range lies inside `data` and its size matches its format/dimensions.
    UzTextureLayout layout;
    std::string parseError;
    if (!ParseUzTextureLayout(data, size, layout, parseError)) {
        UZ_LOG(Renderer, Error, "LoadUzTexViaRing: invalid .uztexture '{}': {}",
               debugName ? debugName : "unknown", parseError);
        return {};
    }

    const UzTextureHeader& header = layout.header;
    const std::vector<UzTextureMipLevel>& mips = layout.mips;

    const bool isSRGB = (header.flags & kUzTextureFlag_sRGB) != 0;
    const auto texFmt = static_cast<TextureFormat>(header.format);
    const RHIFormat rhiFormat = TextureFormatToRHI(texFmt, isSRGB);

    if (rhiFormat == RHIFormat::Unknown) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadUzTexViaRing: unsupported format {} in '{}'",
               header.format, debugName ? debugName : "unknown");
        return {};
    }

    // Create GPU texture with the exact format and mip count from the file.
    RHITextureDesc desc{};
    desc.width = header.width;
    desc.height = header.height;
    desc.mipLevels = static_cast<uint16_t>(header.mipCount);
    desc.format = rhiFormat;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    auto t1 = std::chrono::high_resolution_clock::now();

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error,
               "TextureLoader::LoadUzTexViaRing: failed to create GPU texture '{}' {}x{} fmt={}",
               debugName ? debugName : "unknown", header.width, header.height,
               static_cast<uint32_t>(rhiFormat));
        return {};
    }

    auto t2 = std::chrono::high_resolution_clock::now();

    const bool isBC = IsBlockCompressed(rhiFormat);
    const uint32_t blockBytes = isBC ? BytesPerBlock(rhiFormat) : 0;
    const uint32_t bpp = isBC ? 0 : BytesPerTexel(rhiFormat);

    // Upload each mip level by suballocating from the ring buffer.
    // Zero CreateBuffer, zero data copy — reads directly from source buffer.
    for (uint32_t mip = 0; mip < header.mipCount; ++mip) {
        const auto& mipDesc = mips[mip];
        const uint8_t* mipSrc = data + mipDesc.offset;
        const uint64_t mipSrcSize = mipDesc.size;

        // Compute row pitch and number of rows (block rows for BC, pixel rows otherwise)
        // in 64-bit so oversized dimensions cannot wrap.
        uint64_t srcRowPitch64;
        uint64_t numRows64;
        if (isBC) {
            srcRowPitch64 = ((uint64_t{mipDesc.width} + 3) / 4) * blockBytes;
            numRows64 = (uint64_t{mipDesc.height} + 3) / 4;
        } else {
            srcRowPitch64 = uint64_t{mipDesc.width} * bpp;
            numRows64 = mipDesc.height;
        }

        // DX12 requires 256-byte row alignment for texture copy operations.
        constexpr uint64_t kRowAlign = 256;
        const uint64_t alignedRowPitch64 = (srcRowPitch64 + kRowAlign - 1) & ~(kRowAlign - 1);
        const uint64_t stagingSize64 = alignedRowPitch64 * numRows64;

        // Defense in depth at the copy sink: the rows we read must fit in the
        // mip blob, and the staging layout must fit the ring's 32-bit sizes.
        if (srcRowPitch64 * numRows64 > mipSrcSize || stagingSize64 > UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "LoadUzTexViaRing: mip {} of '{}' needs {} source bytes ({} staging) "
                   "but the blob holds {}",
                   mip, debugName ? debugName : "unknown", srcRowPitch64 * numRows64,
                   stagingSize64, mipSrcSize);
            device->DestroyTexture(texture);
            return {};
        }
        const auto srcRowPitch = static_cast<uint32_t>(srcRowPitch64);
        const auto numRows = static_cast<uint32_t>(numRows64);
        const auto alignedRowPitch = static_cast<uint32_t>(alignedRowPitch64);
        const auto stagingSize = static_cast<uint32_t>(stagingSize64);

        // Suballocate from ring — no buffer creation, no mapping overhead.
        auto alloc = ring.Allocate(stagingSize, kRowAlign);
        if (!alloc.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "TextureLoader::LoadUzTexViaRing: ring exhausted at mip {} for '{}' "
                   "(needed {} bytes, {} remaining)",
                   mip, debugName ? debugName : "?",
                   stagingSize, ring.GetRemainingBytes());
            device->DestroyTexture(texture);
            return {};
        }

        // Copy mip data into the ring's persistently mapped memory.
        // Row-by-row copy when alignment padding is needed; single memcpy otherwise.
        auto* dst = static_cast<uint8_t*>(alloc.cpuPtr);
        const auto* src = mipSrc;
        if (srcRowPitch == alignedRowPitch) {
            std::memcpy(dst, src, static_cast<size_t>(srcRowPitch) * numRows);
        } else {
            for (uint32_t row = 0; row < numRows; ++row) {
                std::memcpy(dst + static_cast<size_t>(row) * alignedRowPitch,
                            src + static_cast<size_t>(row) * srcRowPitch,
                            srcRowPitch);
            }
        }

        // Record the GPU copy command. The ring's backing buffer handle and byte
        // offset are used directly — no intermediate staging buffer involved.
        cmdList->CopyBufferToTexture(
            alloc.bufferHandle, alloc.offsetBytes, alignedRowPitch,
            texture, mip, 0);
    }

    // Transition to ShaderResource state for sampling.
    // Skipped when using the copy queue — the caller issues the barrier on the
    // graphics queue after the copy fence wait completes.
    if (!skipBarrier) {
        RHIBarrierDesc barrier{};
        barrier.texture = texture;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = header.width;
    result.height = header.height;
    result.mipLevels = static_cast<uint16_t>(header.mipCount);

    auto t3 = std::chrono::high_resolution_clock::now();
    auto parseMs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0f;
    auto createMs = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0f;
    auto uploadMs = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0f;
    auto totalMs = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count() / 1000.0f;

    UZ_LOG(Renderer, Info,
           "Loaded .uztexture via ring: name='{}', {}x{}, {} mips, fmt={}, sRGB={}, SRV={}, "
           "timing: parse={:.1f}ms create={:.1f}ms upload={:.1f}ms total={:.1f}ms",
           debugName ? debugName : "unknown", result.width, result.height,
           result.mipLevels, header.format, isSRGB, result.srvIndex,
           parseMs, createMs, uploadMs, totalMs);

    return result;
}

} // namespace uz
