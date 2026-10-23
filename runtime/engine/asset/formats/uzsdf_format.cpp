/// UzEngine .uzsdf Binary Format — Implementation
///
/// Serializes and deserializes sparse SDF mesh assets to/from binary format.

#include "uzsdf_format.h"
#include "log.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Asset);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// WriteUzSdf
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> WriteUzSdf(const MeshSdfAsset& asset) {
    if (!asset.IsValid()) {
        UZ_LOG(Asset, Error, "WriteUzSdf: invalid SDF asset");
        return {};
    }

    // Compute total size.
    // Header (64) + mipLevels * MipDesc (16 each) + padding to 64 + per-MIP data.
    const uint32_t mipLevels = asset.mipLevels;

    // Collect per-MIP info for size calculation
    struct MipInfo {
        uint32_t resX, resY, resZ;
        uint32_t brickCount;
        uint32_t indirectionCount; // resX * resY * resZ
    };

    std::vector<MipInfo> mips(mipLevels);

    // MIP 0 = base
    mips[0].resX = asset.resolutionX;
    mips[0].resY = asset.resolutionY;
    mips[0].resZ = asset.resolutionZ;
    mips[0].brickCount = static_cast<uint32_t>(asset.bricks.size());
    mips[0].indirectionCount = asset.resolutionX * asset.resolutionY * asset.resolutionZ;

    for (uint32_t m = 1; m < mipLevels; ++m) {
        const auto& mip = asset.mipBricks[m - 1];
        mips[m].resX = mip.resolutionX;
        mips[m].resY = mip.resolutionY;
        mips[m].resZ = mip.resolutionZ;
        mips[m].brickCount = static_cast<uint32_t>(mip.bricks.size());
        mips[m].indirectionCount = mip.resolutionX * mip.resolutionY * mip.resolutionZ;
    }

    // Calculate total file size
    uint64_t offset = sizeof(UzSdfHeader);
    offset += mipLevels * sizeof(UzSdfMipDesc);

    // Align to 64 bytes after header + mip descs
    offset = (offset + 63) & ~static_cast<uint64_t>(63);

    // Per-MIP data: indirection table + brick data (no padding between MIPs)
    for (uint32_t m = 0; m < mipLevels; ++m) {
        offset += mips[m].indirectionCount * sizeof(uint32_t);
        offset += mips[m].brickCount * sizeof(SdfBrick);
    }

    const uint64_t totalSize = offset;

    std::vector<uint8_t> out(totalSize, 0);

    // Write header
    UzSdfHeader header;
    header.localBoundsMin[0] = asset.localBoundsMin[0];
    header.localBoundsMin[1] = asset.localBoundsMin[1];
    header.localBoundsMin[2] = asset.localBoundsMin[2];
    header.localBoundsMax[0] = asset.localBoundsMax[0];
    header.localBoundsMax[1] = asset.localBoundsMax[1];
    header.localBoundsMax[2] = asset.localBoundsMax[2];
    header.voxelSize = asset.voxelSize;
    header.mipLevels = mipLevels;

    std::memcpy(out.data(), &header, sizeof(header));

    // Write MIP descriptors
    uint64_t writePos = sizeof(UzSdfHeader);
    for (uint32_t m = 0; m < mipLevels; ++m) {
        UzSdfMipDesc desc;
        desc.resolutionX = mips[m].resX;
        desc.resolutionY = mips[m].resY;
        desc.resolutionZ = mips[m].resZ;
        desc.brickCount  = mips[m].brickCount;
        std::memcpy(out.data() + writePos, &desc, sizeof(desc));
        writePos += sizeof(desc);
    }

    // Align to 64 bytes
    writePos = (writePos + 63) & ~static_cast<uint64_t>(63);

    // Write per-MIP data
    for (uint32_t m = 0; m < mipLevels; ++m) {
        const std::vector<uint32_t>* indirection;
        const std::vector<SdfBrick>* bricks;

        if (m == 0) {
            indirection = &asset.indirection;
            bricks = &asset.bricks;
        } else {
            indirection = &asset.mipBricks[m - 1].indirection;
            bricks = &asset.mipBricks[m - 1].bricks;
        }

        // Write indirection table
        const uint64_t indirectionBytes = mips[m].indirectionCount * sizeof(uint32_t);
        std::memcpy(out.data() + writePos, indirection->data(), indirectionBytes);
        writePos += indirectionBytes;

        // Write brick data
        const uint64_t brickBytes = mips[m].brickCount * sizeof(SdfBrick);
        if (brickBytes > 0) {
            std::memcpy(out.data() + writePos, bricks->data(), brickBytes);
            writePos += brickBytes;
        }
    }

    UZ_LOG(Asset, Info, "WriteUzSdf: {} MIP levels, {} total bricks, {:.1f} KB",
           mipLevels, asset.GetTotalBrickCount(),
           static_cast<double>(totalSize) / 1024.0);

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadUzSdf
// ─────────────────────────────────────────────────────────────────────────────

UzSdfParseResult ReadUzSdf(const std::vector<uint8_t>& data) {
    UzSdfParseResult result;

    if (data.size() < sizeof(UzSdfHeader)) {
        result.error = "File too small for header";
        UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
        return result;
    }

    // Read header
    UzSdfHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    if (header.magic != kUzSdfMagic) {
        result.error = "Invalid magic number";
        UZ_LOG(Asset, Error, "ReadUzSdf: {} (got 0x{:08X}, expected 0x{:08X})",
               result.error, header.magic, kUzSdfMagic);
        return result;
    }

    if (header.version != kUzSdfVersion) {
        result.error = "Unsupported version " + std::to_string(header.version);
        UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
        return result;
    }

    if (header.mipLevels == 0 || header.mipLevels > MeshSdfAsset::kMaxMipLevels + 1) {
        result.error = "Invalid mipLevels: " + std::to_string(header.mipLevels);
        UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
        return result;
    }

    // Read MIP descriptors
    uint64_t readPos = sizeof(UzSdfHeader);
    const uint64_t mipDescsSize = header.mipLevels * sizeof(UzSdfMipDesc);

    if (readPos + mipDescsSize > data.size()) {
        result.error = "File too small for MIP descriptors";
        UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
        return result;
    }

    std::vector<UzSdfMipDesc> mipDescs(header.mipLevels);
    std::memcpy(mipDescs.data(), data.data() + readPos, mipDescsSize);
    readPos += mipDescsSize;

    // Align to 64 bytes
    readPos = (readPos + 63) & ~static_cast<uint64_t>(63);

    // Validate MIP 0 has valid resolution
    if (mipDescs[0].resolutionX == 0 || mipDescs[0].resolutionY == 0 || mipDescs[0].resolutionZ == 0) {
        result.error = "MIP 0 has zero resolution";
        UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
        return result;
    }

    // Read per-MIP data
    auto& asset = result.asset;
    asset.localBoundsMin[0] = header.localBoundsMin[0];
    asset.localBoundsMin[1] = header.localBoundsMin[1];
    asset.localBoundsMin[2] = header.localBoundsMin[2];
    asset.localBoundsMax[0] = header.localBoundsMax[0];
    asset.localBoundsMax[1] = header.localBoundsMax[1];
    asset.localBoundsMax[2] = header.localBoundsMax[2];
    asset.voxelSize = header.voxelSize;
    asset.mipLevels = header.mipLevels;

    for (uint32_t m = 0; m < header.mipLevels; ++m) {
        const auto& md = mipDescs[m];
        // Cell count in 64-bit: a 32-bit product of three file-controlled
        // resolutions wraps, leaving an indirection table smaller than the grid
        // that consumers (MeshSdfAsset::GetGridCellCount) iterate over.
        const uint64_t indirectionCount64 =
            uint64_t{md.resolutionX} * md.resolutionY * md.resolutionZ;
        if (indirectionCount64 > UINT32_MAX) {
            result.error = "MIP " + std::to_string(m) + " grid has " +
                           std::to_string(indirectionCount64) + " cells (exceeds 32-bit limit)";
            UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
            return result;
        }
        const auto indirectionCount = static_cast<uint32_t>(indirectionCount64);
        const uint64_t indirectionBytes = indirectionCount64 * sizeof(uint32_t);
        const uint64_t brickBytes = uint64_t{md.brickCount} * sizeof(SdfBrick);

        // Wrap-safe bounds check (readPos may sit past the end after alignment).
        const uint64_t remaining = readPos <= data.size() ? data.size() - readPos : 0;
        if (indirectionBytes > remaining || brickBytes > remaining - indirectionBytes) {
            result.error = "File truncated at MIP " + std::to_string(m);
            UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
            return result;
        }

        std::vector<uint32_t> indirection(indirectionCount);
        if (indirectionBytes > 0) {
            std::memcpy(indirection.data(), data.data() + readPos, indirectionBytes);
        }
        readPos += indirectionBytes;

        // Each cell must be empty (UINT32_MAX) or index a brick of this MIP;
        // the atlas adds these to its brick offsets without further checks.
        for (uint32_t cell = 0; cell < indirectionCount; ++cell) {
            if (indirection[cell] != UINT32_MAX && indirection[cell] >= md.brickCount) {
                result.error = "MIP " + std::to_string(m) + " cell " + std::to_string(cell) +
                               " references brick " + std::to_string(indirection[cell]) +
                               " but only " + std::to_string(md.brickCount) + " exist";
                UZ_LOG(Asset, Error, "ReadUzSdf: {}", result.error);
                return result;
            }
        }

        std::vector<SdfBrick> bricks(md.brickCount);
        if (md.brickCount > 0) {
            std::memcpy(bricks.data(), data.data() + readPos, brickBytes);
            readPos += brickBytes;
        }

        if (m == 0) {
            asset.resolutionX = md.resolutionX;
            asset.resolutionY = md.resolutionY;
            asset.resolutionZ = md.resolutionZ;
            asset.bricks = std::move(bricks);
            asset.indirection = std::move(indirection);
        } else {
            auto& mip = asset.mipBricks[m - 1];
            mip.resolutionX = md.resolutionX;
            mip.resolutionY = md.resolutionY;
            mip.resolutionZ = md.resolutionZ;
            mip.bricks = std::move(bricks);
            mip.indirection = std::move(indirection);
        }
    }

    result.success = true;

    UZ_LOG(Asset, Info, "ReadUzSdf: loaded {} MIP levels, {} total bricks",
           header.mipLevels, asset.GetTotalBrickCount());

    return result;
}

} // namespace uz
