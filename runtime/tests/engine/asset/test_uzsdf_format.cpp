/// UzEngine .uzsdf Format — Unit Tests
///
/// Round-trips a small sparse SDF asset and feeds crafted buffers (wrapped grid
/// resolutions, oversized brick counts, out-of-range indirection entries,
/// truncation) that ReadUzSdf must reject without reading out of bounds.

#include "uzsdf_format.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace uz;

namespace {

/// Byte offset of the first MIP's indirection table for a single-MIP file:
/// header (64) + one MIP descriptor (16), aligned up to 64.
constexpr size_t kSingleMipDataStart = 128;

/// Builds a valid 2x2x2-brick asset with two surface bricks (cells 0 and 7).
MeshSdfAsset MakeSmallAsset() {
    MeshSdfAsset asset;
    asset.localBoundsMin[0] = -1.0f;
    asset.localBoundsMin[1] = -1.0f;
    asset.localBoundsMin[2] = -1.0f;
    asset.localBoundsMax[0] = 1.0f;
    asset.localBoundsMax[1] = 1.0f;
    asset.localBoundsMax[2] = 1.0f;
    asset.voxelSize = 0.125f;
    asset.resolutionX = 2;
    asset.resolutionY = 2;
    asset.resolutionZ = 2;
    asset.mipLevels = 1;
    asset.bricks.resize(2);
    std::memset(asset.bricks[0].data, 0x40, sizeof(asset.bricks[0].data));
    std::memset(asset.bricks[1].data, 0xC0, sizeof(asset.bricks[1].data));
    asset.indirection.assign(8, UINT32_MAX);
    asset.indirection[0] = 0;
    asset.indirection[7] = 1;
    return asset;
}

/// Reads the MIP 0 descriptor of a .uzsdf blob.
UzSdfMipDesc GetMip0(const std::vector<uint8_t>& blob) {
    UzSdfMipDesc desc;
    std::memcpy(&desc, blob.data() + sizeof(UzSdfHeader), sizeof(desc));
    return desc;
}

/// Overwrites the MIP 0 descriptor of a .uzsdf blob.
void SetMip0(std::vector<uint8_t>& blob, const UzSdfMipDesc& desc) {
    std::memcpy(blob.data() + sizeof(UzSdfHeader), &desc, sizeof(desc));
}

} // anonymous namespace

TEST(UzSdfFormat, Roundtrip_SmallAsset_PreservesBricksAndIndirection) {
    const MeshSdfAsset original = MakeSmallAsset();
    const auto blob = WriteUzSdf(original);
    ASSERT_FALSE(blob.empty());

    const auto r = ReadUzSdf(blob);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_TRUE(r.asset.IsValid());
    EXPECT_EQ(r.asset.resolutionX, 2u);
    EXPECT_EQ(r.asset.indirection, original.indirection);
    ASSERT_EQ(r.asset.bricks.size(), 2u);
    EXPECT_EQ(r.asset.bricks[1].data[511], 0xC0u);
}

TEST(UzSdfFormat, Read_GridCellCountWrapsUint32_Rejected) {
    // 2048^3 = 2^33 cells: the old 32-bit product wrapped to 0 and the parser
    // accepted an empty indirection table for a non-empty grid.
    auto blob = WriteUzSdf(MakeSmallAsset());
    auto desc = GetMip0(blob);
    desc.resolutionX = 2048;
    desc.resolutionY = 2048;
    desc.resolutionZ = 2048;
    desc.brickCount = 0;
    SetMip0(blob, desc);

    const auto r = ReadUzSdf(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("32-bit"), std::string::npos) << r.error;
}

TEST(UzSdfFormat, Read_GridCellCountExactly2Pow32_Rejected) {
    auto blob = WriteUzSdf(MakeSmallAsset());
    auto desc = GetMip0(blob);
    desc.resolutionX = 65536;
    desc.resolutionY = 65536;
    desc.resolutionZ = 1;
    SetMip0(blob, desc);

    EXPECT_FALSE(ReadUzSdf(blob).success);
}

TEST(UzSdfFormat, Read_BrickCountBeyondFile_Rejected) {
    auto blob = WriteUzSdf(MakeSmallAsset());
    auto desc = GetMip0(blob);
    desc.brickCount = UINT32_MAX;
    SetMip0(blob, desc);

    const auto r = ReadUzSdf(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("truncated"), std::string::npos) << r.error;
}

TEST(UzSdfFormat, Read_IndirectionReferencesMissingBrick_Rejected) {
    auto blob = WriteUzSdf(MakeSmallAsset());
    const uint32_t badIndex = 2; // only bricks 0 and 1 exist
    std::memcpy(blob.data() + kSingleMipDataStart + 3 * sizeof(uint32_t),
                &badIndex, sizeof(badIndex));

    const auto r = ReadUzSdf(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("references brick"), std::string::npos) << r.error;
}

TEST(UzSdfFormat, Read_TruncatedBrickData_Rejected) {
    auto blob = WriteUzSdf(MakeSmallAsset());
    blob.resize(blob.size() - 1);

    EXPECT_FALSE(ReadUzSdf(blob).success);
}

TEST(UzSdfFormat, Read_FileEndsBeforeAlignedDataStart_Rejected) {
    // Header + descriptor fit, but the 64-byte aligned data start lies past the end.
    auto blob = WriteUzSdf(MakeSmallAsset());
    blob.resize(sizeof(UzSdfHeader) + sizeof(UzSdfMipDesc));

    EXPECT_FALSE(ReadUzSdf(blob).success);
}
