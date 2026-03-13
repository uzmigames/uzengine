/// UzEngine .uztexture Binary Format — Serialization and Parsing
///
/// Writes and reads the .uztexture binary format defined in uztexture_format.h.
/// All operations are little-endian and platform-independent.

#include "uztexture_format.h"
#include "profiler.h"

#include <cstring>

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

UzTextureParseResult ReadUzTexture(const std::vector<uint8_t>& data) {
    UZ_PROFILE_FUNCTION();
    UzTextureParseResult result;

    if (data.size() < sizeof(UzTextureHeader)) {
        result.error = "Data too small for .uztexture header";
        return result;
    }

    std::memcpy(&result.file.header, data.data(), sizeof(UzTextureHeader));

    if (result.file.header.magic != kUzTextureMagic) {
        result.error = "Invalid magic number — expected 'UZTX'";
        return result;
    }

    if (result.file.header.version != kUzTextureVersion) {
        result.error = "Unsupported .uztexture version: " +
                       std::to_string(result.file.header.version);
        return result;
    }

    const auto& h = result.file.header;
    const size_t mipDescStart = sizeof(UzTextureHeader);
    const size_t mipDescSize = h.mipCount * sizeof(UzTextureMipLevel);

    if (data.size() < mipDescStart + mipDescSize) {
        result.error = "Data truncated — mip descriptors exceed file size";
        return result;
    }

    result.file.mips.resize(h.mipCount);
    if (h.mipCount > 0) {
        std::memcpy(result.file.mips.data(), data.data() + mipDescStart, mipDescSize);
    }

    result.file.mipData.resize(h.mipCount);
    for (uint32_t i = 0; i < h.mipCount; ++i) {
        const auto& mip = result.file.mips[i];
        if (data.size() < mip.offset + mip.size) {
            result.error = "Data truncated — mip " + std::to_string(i) + " exceeds file size";
            return result;
        }
        result.file.mipData[i].assign(
            data.data() + mip.offset,
            data.data() + mip.offset + mip.size);
    }

    result.success = true;
    return result;
}

} // namespace uz
