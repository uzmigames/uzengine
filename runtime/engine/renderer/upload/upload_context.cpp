/// GPU upload context implementation.
///
/// Creates temporary upload-heap buffers for staging CPU data, then copies
/// to default-heap destinations via command list CopyBuffer commands.

#include "upload_context.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

UZ_DEFINE_STAT(IO, BufferUploads);

bool UploadContext::UploadBuffer(IRHICommandList* cmdList, RHIBufferHandle dst,
                                 const void* data, uint64_t sizeBytes) {
    UZ_PROFILE_FUNCTION();
    UZ_STAT_INCREMENT(IO, BufferUploads);
    if (!m_device || !cmdList || !data || sizeBytes == 0) {
        UZ_LOG(Renderer, Error, "UploadBuffer: invalid arguments");
        return false;
    }

    // Create a staging buffer in upload heap
    RHIBufferDesc stagingDesc{};
    stagingDesc.sizeBytes = sizeBytes;
    stagingDesc.usage = RHIBufferUsage::CopySrc;
    stagingDesc.memory = RHIMemoryType::Upload;
    stagingDesc.debugName = "StagingBuffer";

    RHIBufferHandle staging = m_device->CreateBuffer(stagingDesc);
    if (!staging.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadBuffer: failed to create staging buffer ({} bytes)", sizeBytes);
        return false;
    }

    // Map and copy CPU data into staging buffer
    void* mapped = m_device->MapBuffer(staging);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "UploadBuffer: failed to map staging buffer");
        m_device->DestroyBuffer(staging);
        return false;
    }
    std::memcpy(mapped, data, sizeBytes);
    m_device->UnmapBuffer(staging);

    // Record GPU copy: staging → destination
    cmdList->CopyBuffer(staging, 0, dst, 0, sizeBytes);

    // Keep staging alive until GPU copy completes
    m_stagingBuffers.push_back(staging);
    return true;
}

bool UploadContext::UploadTexture(IRHICommandList* cmdList, RHITextureHandle dst,
                                  const void* data, uint64_t sizeBytes,
                                  uint32_t mipLevel, uint32_t arraySlice) {
    UZ_PROFILE_FUNCTION();
    UZ_STAT_INCREMENT(IO, BufferUploads);
    if (!m_device || !cmdList || !data || sizeBytes == 0) {
        UZ_LOG(Renderer, Error, "UploadTexture: invalid arguments");
        return false;
    }

    if (!dst.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadTexture: invalid destination texture handle");
        return false;
    }

    // Query texture desc to compute staging layout
    RHITextureDesc texDesc = m_device->GetTextureDesc(dst);
    const bool isBC = IsBlockCompressed(texDesc.format);
    const uint32_t bpp = BytesPerTexel(texDesc.format);
    if (!isBC && bpp == 0) {
        UZ_LOG(Renderer, Error, "UploadTexture: unsupported format (BytesPerTexel=0, not BC)");
        return false;
    }

    if (mipLevel >= texDesc.mipLevels) {
        UZ_LOG(Renderer, Error, "UploadTexture: mip level {} out of range (texture has {} mips)",
               mipLevel, texDesc.mipLevels);
        return false;
    }

    // Compute mip dimensions (mipLevel < mipLevels <= UINT16_MAX; shift stays defined)
    const uint32_t mipWidth = mipLevel < 32 && (texDesc.width >> mipLevel) > 0
                                  ? (texDesc.width >> mipLevel) : 1;
    const uint32_t mipHeight = mipLevel < 32 && (texDesc.height >> mipLevel) > 0
                                   ? (texDesc.height >> mipLevel) : 1;

    // For block-compressed formats, staging layout is in block rows:
    //   srcRowPitch = blocksPerRow * bytesPerBlock
    //   numRows     = blocksPerCol (each block covers 4 texels vertically)
    // For uncompressed: srcRowPitch = width * bpp, numRows = height
    // Computed in 64-bit so large dimensions cannot wrap.
    uint64_t srcRowPitch64;
    uint64_t numRows64;
    if (isBC) {
        const uint64_t blockBytes = BytesPerBlock(texDesc.format);
        srcRowPitch64 = ((uint64_t{mipWidth} + 3) / 4) * blockBytes;
        numRows64 = (uint64_t{mipHeight} + 3) / 4;
    } else {
        srcRowPitch64 = uint64_t{mipWidth} * bpp;
        numRows64 = mipHeight;
    }

    // DX12 requires 256-byte aligned row pitch for texture uploads
    constexpr uint64_t kRowAlignment = 256;
    const uint64_t alignedRowPitch64 = (srcRowPitch64 + kRowAlignment - 1) & ~(kRowAlignment - 1);
    if (alignedRowPitch64 > UINT32_MAX) {
        UZ_LOG(Renderer, Error, "UploadTexture: row pitch {} exceeds 32-bit copy limit",
               alignedRowPitch64);
        return false;
    }

    // Defense in depth: never read more source bytes than the caller supplied.
    // Pitch < 2^32 and rows < 2^31, so the product cannot overflow uint64.
    const uint64_t requiredBytes = srcRowPitch64 * numRows64;
    if (sizeBytes < requiredBytes) {
        UZ_LOG(Renderer, Error,
               "UploadTexture: source holds {} bytes but mip {} ({}x{}) needs {}",
               sizeBytes, mipLevel, mipWidth, mipHeight, requiredBytes);
        return false;
    }

    const auto srcRowPitch = static_cast<uint32_t>(srcRowPitch64);
    const auto numRows = static_cast<uint32_t>(numRows64);
    const auto alignedRowPitch = static_cast<uint32_t>(alignedRowPitch64);
    const uint64_t stagingSize = alignedRowPitch64 * numRows64;

    // Create staging buffer
    RHIBufferDesc stagingDesc{};
    stagingDesc.sizeBytes = stagingSize;
    stagingDesc.usage = RHIBufferUsage::CopySrc;
    stagingDesc.memory = RHIMemoryType::Upload;
    stagingDesc.debugName = "TextureStagingBuffer";

    RHIBufferHandle staging = m_device->CreateBuffer(stagingDesc);
    if (!staging.IsValid()) {
        UZ_LOG(Renderer, Error, "UploadTexture: failed to create staging buffer ({} bytes)", stagingSize);
        return false;
    }

    // Map and copy with row pitch alignment
    auto* mapped = static_cast<uint8_t*>(m_device->MapBuffer(staging));
    if (!mapped) {
        UZ_LOG(Renderer, Error, "UploadTexture: failed to map staging buffer");
        m_device->DestroyBuffer(staging);
        return false;
    }

    const auto* srcBytes = static_cast<const uint8_t*>(data);
    if (srcRowPitch == alignedRowPitch) {
        // Rows are already aligned — single memcpy
        std::memcpy(mapped, srcBytes, static_cast<size_t>(srcRowPitch) * numRows);
    } else {
        // Copy row-by-row with padding (rows = block rows for BC, pixel rows for uncompressed)
        for (uint32_t row = 0; row < numRows; ++row) {
            std::memcpy(mapped + static_cast<size_t>(row) * alignedRowPitch,
                        srcBytes + static_cast<size_t>(row) * srcRowPitch,
                        srcRowPitch);
        }
    }
    m_device->UnmapBuffer(staging);

    // Record GPU copy: staging buffer → destination texture
    cmdList->CopyBufferToTexture(staging, 0, alignedRowPitch, dst, mipLevel, arraySlice);

    m_stagingBuffers.push_back(staging);
    return true;
}

void UploadContext::ReleaseStaging() {
    UZ_PROFILE_FUNCTION();
    for (auto& buf : m_stagingBuffers) {
        m_device->DestroyBuffer(buf);
    }
    m_stagingBuffers.clear();
}

} // namespace uz
