/// UzEngine .uztexture Binary Format — Serialization and Parsing
///
/// Writes and reads the .uztexture binary format defined in uztexture_format.h.
/// All operations are little-endian and platform-independent.

#include "uztexture_format.h"
#include "profiler.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace uz {

std::vector<uint8_t> WriteUzTexture(
    UzTextureHeader header,
    const std::vector<std::vector<uint8_t>>& mipData) {
    UZ_PROFILE_FUNCTION();

    header.magic = kUzTextureMagic;
    header.version = kUzTextureVersion;
    header.mipCount = static_cast<uint32_t>(mipData.size());

    // Compute layout: header + mip descriptors + mip blobs
    const size_t mipDescStart = sizeof(UzTextureHeader);
    const size_t mipDescSize = mipData.size() * sizeof(UzTextureMipLevel);
    size_t dataStart = mipDescStart + mipDescSize;

    // Build mip descriptors and compute total size
    std::vector<UzTextureMipLevel> mips(mipData.size());
    uint32_t mipW = header.width;
    uint32_t mipH = header.height;
    size_t currentOffset = dataStart;

    for (size_t i = 0; i < mipData.size(); ++i) {
        mips[i].offset = static_cast<uint64_t>(currentOffset);
        mips[i].size = static_cast<uint64_t>(mipData[i].size());
        mips[i].width = mipW;
        mips[i].height = mipH;
        currentOffset += mipData[i].size();
        mipW = (mipW > 1) ? mipW / 2 : 1;
        mipH = (mipH > 1) ? mipH / 2 : 1;
    }

    std::vector<uint8_t> out(currentOffset, 0);

    std::memcpy(out.data(), &header, sizeof(header));

    if (!mips.empty()) {
        std::memcpy(out.data() + mipDescStart, mips.data(), mipDescSize);
    }

    for (size_t i = 0; i < mipData.size(); ++i) {
        if (!mipData[i].empty()) {
            std::memcpy(out.data() + mips[i].offset, mipData[i].data(), mipData[i].size());
        }
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout validation
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Multiplies two uint64 values, returning false instead of wrapping.
[[nodiscard]] bool CheckedMul(uint64_t a, uint64_t b, uint64_t& out) {
    if (a != 0 && b > UINT64_MAX / a) return false;
    out = a * b;
    return true;
}

/// Validates the header fields that the mip table depends on.
[[nodiscard]] bool ValidateUzTextureHeader(const UzTextureHeader& h, std::string& error) {
    if (h.magic != kUzTextureMagic) {
        error = "Invalid magic number — expected 'UZTX'";
        return false;
    }
    if (h.version != kUzTextureVersion) {
        error = "Unsupported .uztexture version: " + std::to_string(h.version);
        return false;
    }
    if (h.width == 0 || h.height == 0) {
        error = "Invalid dimensions " + std::to_string(h.width) + "x" +
                std::to_string(h.height) + " — width and height must be > 0";
        return false;
    }
    if (TextureFormatToRHI(static_cast<TextureFormat>(h.format), false) == RHIFormat::Unknown) {
        error = "Unknown texture format: " + std::to_string(h.format);
        return false;
    }
    // A full chain down to 1x1 has floor(log2(max(w, h))) + 1 levels. Fields of
    // the packed header are copied first: std::max binds references.
    const uint32_t width = h.width;
    const uint32_t height = h.height;
    const uint32_t maxMipCount = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
    if (h.mipCount == 0 || h.mipCount > maxMipCount) {
        error = "Invalid mipCount " + std::to_string(h.mipCount) + " for " +
                std::to_string(h.width) + "x" + std::to_string(h.height) +
                " (expected 1.." + std::to_string(maxMipCount) + ")";
        return false;
    }
    return true;
}

/// Validates mip `level`'s descriptor against the header and the buffer size.
[[nodiscard]] bool ValidateUzTextureMip(const UzTextureHeader& h, uint32_t level,
                                        const UzTextureMipLevel& mip, size_t size,
                                        std::string& error) {
    const std::string tag = "mip " + std::to_string(level);
    const uint32_t shiftedW = h.width >> level;   // prvalues: no packed-field references
    const uint32_t shiftedH = h.height >> level;
    const uint32_t expectedW = std::max(shiftedW, 1u);
    const uint32_t expectedH = std::max(shiftedH, 1u);
    if (mip.width != expectedW || mip.height != expectedH) {
        error = tag + " dimensions " + std::to_string(mip.width) + "x" +
                std::to_string(mip.height) + " do not match expected " +
                std::to_string(expectedW) + "x" + std::to_string(expectedH);
        return false;
    }

    uint64_t expectedSize = 0;
    if (!ComputeUzTextureMipSize(static_cast<TextureFormat>(h.format), expectedW, expectedH,
                                 expectedSize)) {
        error = tag + " size overflows for format " + std::to_string(h.format);
        return false;
    }
    if (mip.size != expectedSize) {
        error = tag + " size " + std::to_string(mip.size) + " does not match the " +
                std::to_string(expectedSize) + " bytes its format and dimensions require";
        return false;
    }

    // Wrap-safe: offset and size are file-controlled uint64 values.
    if (mip.offset > size || mip.size > size - mip.offset) {
        error = "Data truncated — " + tag + " exceeds file size";
        return false;
    }
    return true;
}

} // anonymous namespace

bool ComputeUzTextureMipSize(TextureFormat format, uint32_t width, uint32_t height,
                             uint64_t& outSize) {
    const RHIFormat rhiFormat = TextureFormatToRHI(format, false);
    if (rhiFormat == RHIFormat::Unknown) return false;

    if (IsBlockCompressed(rhiFormat)) {
        const uint64_t blocksX = (uint64_t{width} + 3) / 4;
        const uint64_t blocksY = (uint64_t{height} + 3) / 4;
        uint64_t blocks = 0;
        return CheckedMul(blocksX, blocksY, blocks) &&
               CheckedMul(blocks, BytesPerBlock(rhiFormat), outSize);
    }

    uint64_t texels = 0;
    return CheckedMul(width, height, texels) &&
           CheckedMul(texels, BytesPerTexel(rhiFormat), outSize);
}

bool ParseUzTextureLayout(const uint8_t* data, size_t size,
                          UzTextureLayout& out, std::string& error) {
    if (data == nullptr || size < sizeof(UzTextureHeader)) {
        error = "Data too small for .uztexture header";
        return false;
    }

    std::memcpy(&out.header, data, sizeof(UzTextureHeader));
    const UzTextureHeader& h = out.header;
    if (!ValidateUzTextureHeader(h, error)) return false;

    // mipCount <= 32 after header validation, so this product cannot overflow.
    const size_t mipDescStart = sizeof(UzTextureHeader);
    const size_t mipDescSize = size_t{h.mipCount} * sizeof(UzTextureMipLevel);
    if (mipDescSize > size - mipDescStart) {
        error = "Data truncated — mip descriptors exceed file size";
        return false;
    }

    out.mips.resize(h.mipCount);
    std::memcpy(out.mips.data(), data + mipDescStart, mipDescSize);

    for (uint32_t i = 0; i < h.mipCount; ++i) {
        if (!ValidateUzTextureMip(h, i, out.mips[i], size, error)) return false;
    }
    return true;
}

UzTextureParseResult ReadUzTexture(const std::vector<uint8_t>& data) {
    UZ_PROFILE_FUNCTION();
    UzTextureParseResult result;

    UzTextureLayout layout;
    if (!ParseUzTextureLayout(data.data(), data.size(), layout, result.error)) {
        // Keep the header visible to callers that report what was rejected.
        result.file.header = layout.header;
        return result;
    }

    result.file.header = layout.header;
    result.file.mips = std::move(layout.mips);
    result.file.mipData.resize(result.file.header.mipCount);
    for (uint32_t i = 0; i < result.file.header.mipCount; ++i) {
        const auto& mip = result.file.mips[i];
        result.file.mipData[i].assign(
            data.data() + mip.offset,
            data.data() + mip.offset + mip.size);
    }

    result.success = true;
    return result;
}

} // namespace uz
