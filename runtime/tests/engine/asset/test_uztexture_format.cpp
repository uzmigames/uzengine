/// UzEngine .uztexture Format — Unit Tests
///
/// Tests WriteUzTexture/ReadUzTexture roundtrip correctness, binary layout
/// constraints (magic, version, mip descriptors), and all error paths.

#include "uztexture_format.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a pre-filled header for a 4x4 RGBA8 texture with 3 mip levels.
static UzTextureHeader Make4x4Header() {
    UzTextureHeader h;
    h.width = 4;
    h.height = 4;
    h.mipCount = 3; // 4x4, 2x2, 1x1
    h.format = static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8);
    return h;
}

/// Returns a 4-byte RGBA pixel repeated to fill `w * h` pixels.
static std::vector<uint8_t> MakeSolidMip(uint32_t w, uint32_t h,
                                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> data(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < data.size(); i += 4) {
        data[i + 0] = r;
        data[i + 1] = g;
        data[i + 2] = b;
        data[i + 3] = a;
    }
    return data;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header Default Initialization
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, DefaultHeader_HasCorrectMagicAndVersion) {
    UzTextureHeader h;
    EXPECT_EQ(h.magic, kUzTextureMagic);
    EXPECT_EQ(h.magic, 0x5A585455u);
    EXPECT_EQ(h.version, kUzTextureVersion);
    EXPECT_EQ(h.version, 1u);
    EXPECT_EQ(h.width, 0u);
    EXPECT_EQ(h.height, 0u);
    EXPECT_EQ(h.mipCount, 0u);
    EXPECT_EQ(h.format, 0u);
    EXPECT_EQ(h.flags, 0u);
}

TEST(UzTextureFormat, HeaderSize_IsExactly64Bytes) {
    EXPECT_EQ(sizeof(UzTextureHeader), 64u);
}

TEST(UzTextureFormat, MipLevelSize_IsExactly24Bytes) {
    EXPECT_EQ(sizeof(UzTextureMipLevel), 24u);
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteUzTexture — binary layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, Write_OutputStartsWithMagicAndVersion) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    ASSERT_GE(blob.size(), sizeof(UzTextureHeader));
    UzTextureHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzTextureHeader));
    EXPECT_EQ(parsed.magic, kUzTextureMagic);
    EXPECT_EQ(parsed.version, kUzTextureVersion);
}

TEST(UzTextureFormat, Write_OutputContainsMipCount_MatchingMipDataSize) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    UzTextureHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzTextureHeader));
    EXPECT_EQ(parsed.mipCount, 3u);
}

TEST(UzTextureFormat, Write_MipDescriptors_OffsetAndSizeAreNonZero) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    const size_t mipDescStart = sizeof(UzTextureHeader);
    UzTextureMipLevel mip0;
    std::memcpy(&mip0, blob.data() + mipDescStart, sizeof(UzTextureMipLevel));

    EXPECT_GT(mip0.offset, 0u) << "Mip 0 offset must be past the descriptor table";
    EXPECT_EQ(mip0.size, 4u * 4u * 4u) << "Mip 0 size: 4x4 RGBA8 = 64 bytes";
    EXPECT_EQ(mip0.width, 4u);
    EXPECT_EQ(mip0.height, 4u);
}

TEST(UzTextureFormat, Write_Mip1Descriptor_HasCorrectDimensions) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    const size_t mip1DescOffset = sizeof(UzTextureHeader) + sizeof(UzTextureMipLevel);
    UzTextureMipLevel mip1;
    std::memcpy(&mip1, blob.data() + mip1DescOffset, sizeof(UzTextureMipLevel));

    EXPECT_EQ(mip1.width, 2u);
    EXPECT_EQ(mip1.height, 2u);
    EXPECT_EQ(mip1.size, 2u * 2u * 4u);
}

TEST(UzTextureFormat, Write_Mip2Descriptor_HasCorrectDimensions) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    const size_t mip2DescOffset = sizeof(UzTextureHeader) + 2 * sizeof(UzTextureMipLevel);
    UzTextureMipLevel mip2;
    std::memcpy(&mip2, blob.data() + mip2DescOffset, sizeof(UzTextureMipLevel));

    EXPECT_EQ(mip2.width, 1u);
    EXPECT_EQ(mip2.height, 1u);
    EXPECT_EQ(mip2.size, 1u * 1u * 4u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Roundtrip — 4x4 three-mip texture
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, Roundtrip_4x4_HeaderFieldsMatch) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.header.magic, kUzTextureMagic);
    EXPECT_EQ(r.file.header.version, kUzTextureVersion);
    EXPECT_EQ(r.file.header.width, 4u);
    EXPECT_EQ(r.file.header.height, 4u);
    EXPECT_EQ(r.file.header.mipCount, 3u);
    EXPECT_EQ(r.file.header.format, static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8));
    EXPECT_EQ(r.file.header.flags, 0u);
}

TEST(UzTextureFormat, Roundtrip_4x4_MipCountMatchesMipDataVectors) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.mips.size(), 3u);
    EXPECT_EQ(r.file.mipData.size(), 3u);
}

TEST(UzTextureFormat, Roundtrip_4x4_Mip0DataMatches) {
    auto header = Make4x4Header();
    auto mip0src = MakeSolidMip(4, 4, 255, 0, 0, 255);
    std::vector<std::vector<uint8_t>> mipData = {
        mip0src,
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.mipData[0].size(), mip0src.size());
    EXPECT_EQ(std::memcmp(r.file.mipData[0].data(), mip0src.data(), mip0src.size()), 0)
        << "Mip 0 pixel data did not survive roundtrip";
}

TEST(UzTextureFormat, Roundtrip_4x4_Mip1DataMatches) {
    auto header = Make4x4Header();
    auto mip1src = MakeSolidMip(2, 2, 128, 64, 32, 200);
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        mip1src,
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.mipData[1].size(), mip1src.size());
    EXPECT_EQ(std::memcmp(r.file.mipData[1].data(), mip1src.data(), mip1src.size()), 0)
        << "Mip 1 pixel data did not survive roundtrip";
}

TEST(UzTextureFormat, Roundtrip_4x4_MipDescriptorSizesMatch) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.mips.size(), 3u);
    EXPECT_EQ(r.file.mips[0].size, 4u * 4u * 4u);
    EXPECT_EQ(r.file.mips[1].size, 2u * 2u * 4u);
    EXPECT_EQ(r.file.mips[2].size, 1u * 1u * 4u);
}

TEST(UzTextureFormat, Roundtrip_4x4_MipDescriptorOffsetsAscending) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.mips.size(), 3u);
    EXPECT_LT(r.file.mips[0].offset, r.file.mips[1].offset);
    EXPECT_LT(r.file.mips[1].offset, r.file.mips[2].offset);
}

// ─────────────────────────────────────────────────────────────────────────────
// Roundtrip — single mip (1x1 texture)
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, Roundtrip_1x1_SingleMip_DataPreserved) {
    UzTextureHeader header;
    header.width = 1;
    header.height = 1;
    header.mipCount = 1;
    header.format = static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8);

    uint8_t pixel[4] = {10, 20, 30, 255};
    std::vector<std::vector<uint8_t>> mipData = {
        std::vector<uint8_t>(pixel, pixel + 4)
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.header.width, 1u);
    EXPECT_EQ(r.file.header.height, 1u);
    EXPECT_EQ(r.file.header.mipCount, 1u);
    ASSERT_EQ(r.file.mipData.size(), 1u);
    ASSERT_EQ(r.file.mipData[0].size(), 4u);
    EXPECT_EQ(r.file.mipData[0][0], 10u);
    EXPECT_EQ(r.file.mipData[0][1], 20u);
    EXPECT_EQ(r.file.mipData[0][2], 30u);
    EXPECT_EQ(r.file.mipData[0][3], 255u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Roundtrip — compressed format stored correctly
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, Roundtrip_BC7Format_FormatFieldPreserved) {
    UzTextureHeader header;
    header.width = 4;
    header.height = 4;
    header.mipCount = 1;
    header.format = static_cast<uint32_t>(TextureFormat::BC7);

    // BC7 4x4 block = 16 bytes
    std::vector<uint8_t> bc7Block(16, 0xAB);
    std::vector<std::vector<uint8_t>> mipData = {bc7Block};
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.header.format, static_cast<uint32_t>(TextureFormat::BC7));
    ASSERT_EQ(r.file.mipData[0].size(), 16u);
    EXPECT_EQ(r.file.mipData[0][0], 0xABu);
    EXPECT_EQ(r.file.mipData[0][15], 0xABu);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error paths — ReadUzTexture
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, Read_EmptyData_ReturnsError) {
    std::vector<uint8_t> empty;
    auto r = ReadUzTexture(empty);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzTextureFormat, Read_TruncatedHeader_ReturnsError) {
    std::vector<uint8_t> tiny(32, 0); // Less than 64-byte header
    auto r = ReadUzTexture(tiny);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzTextureFormat, Read_WrongMagic_ReturnsErrorWithMessage) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 0, 0, 0, 255),
        MakeSolidMip(2, 2, 0, 0, 0, 255),
        MakeSolidMip(1, 1, 0, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    // Corrupt magic at bytes 0..3
    blob[0] = 0xDE;
    blob[1] = 0xAD;
    blob[2] = 0xBE;
    blob[3] = 0xEF;

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzTextureFormat, Read_WrongVersion_ReturnsErrorWithVersionNumber) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 0, 0, 0, 255),
        MakeSolidMip(2, 2, 0, 0, 0, 255),
        MakeSolidMip(1, 1, 0, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    // Overwrite version field (bytes 4..7) with version 42
    uint32_t badVersion = 42u;
    std::memcpy(blob.data() + 4, &badVersion, sizeof(badVersion));

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("42"), std::string::npos)
        << "Error message should contain the bad version number; got: " << r.error;
}

TEST(UzTextureFormat, Read_TruncatedMipDescriptors_ReturnsError) {
    // Craft a header claiming 100 mips but provide only the header
    UzTextureHeader h;
    h.width = 4;
    h.height = 4;
    h.mipCount = 100; // too many
    h.format = static_cast<uint32_t>(TextureFormat::Uncompressed_RGBA8);

    std::vector<uint8_t> blob(sizeof(UzTextureHeader), 0);
    std::memcpy(blob.data(), &h, sizeof(h));

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzTextureFormat, Read_TruncatedMipData_ReturnsError) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);

    // Truncate to remove most mip 0 data (keep only header + descriptors + 1 byte)
    const size_t mipDescEnd = sizeof(UzTextureHeader) + 3 * sizeof(UzTextureMipLevel);
    blob.resize(mipDescEnd + 1);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Crafted files — wrapped offsets, inconsistent mip descriptors
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a valid 4x4 RGBA8 three-mip .uztexture blob.
static std::vector<uint8_t> MakeValid4x4Blob() {
    return WriteUzTexture(Make4x4Header(), {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    });
}

/// Reads mip descriptor `level` from a .uztexture blob.
static UzTextureMipLevel GetMipDesc(const std::vector<uint8_t>& blob, uint32_t level) {
    UzTextureMipLevel mip;
    std::memcpy(&mip, blob.data() + sizeof(UzTextureHeader) + level * sizeof(UzTextureMipLevel),
                sizeof(mip));
    return mip;
}

/// Overwrites mip descriptor `level` in a .uztexture blob.
static void SetMipDesc(std::vector<uint8_t>& blob, uint32_t level, const UzTextureMipLevel& mip) {
    std::memcpy(blob.data() + sizeof(UzTextureHeader) + level * sizeof(UzTextureMipLevel),
                &mip, sizeof(mip));
}

/// Overwrites the header of a .uztexture blob.
static void SetHeader(std::vector<uint8_t>& blob, const UzTextureHeader& h) {
    std::memcpy(blob.data(), &h, sizeof(h));
}

TEST(UzTextureFormat, Read_MipOffsetWrapsPastUint64Max_Rejected) {
    // offset + size wraps to a small value, which passed the old additive check.
    auto blob = MakeValid4x4Blob();
    auto mip = GetMipDesc(blob, 0);
    mip.offset = UINT64_MAX - mip.size + 2;
    SetMipDesc(blob, 0, mip);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("exceeds file size"), std::string::npos) << r.error;
}

TEST(UzTextureFormat, Read_MipOffsetPastEnd_Rejected) {
    auto blob = MakeValid4x4Blob();
    auto mip = GetMipDesc(blob, 2);
    mip.offset = blob.size() - mip.size + 1;
    SetMipDesc(blob, 2, mip);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
}

TEST(UzTextureFormat, Read_MipSizeSmallerThanDimensionsImply_Rejected) {
    // A 4x4 RGBA8 mip claiming 4 bytes would let an uploader copy 64 bytes from a 4-byte blob.
    auto blob = MakeValid4x4Blob();
    auto mip = GetMipDesc(blob, 0);
    mip.size = 4;
    SetMipDesc(blob, 0, mip);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("mip 0 size"), std::string::npos) << r.error;
}

TEST(UzTextureFormat, Read_MipSizeLargerThanDimensionsImply_Rejected) {
    auto blob = MakeValid4x4Blob();
    auto mip = GetMipDesc(blob, 2);
    mip.size = 8; // 1x1 RGBA8 is exactly 4 bytes
    mip.offset -= 4; // keep the range inside the file so only the size rule fires
    SetMipDesc(blob, 2, mip);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("mip 2 size"), std::string::npos) << r.error;
}

TEST(UzTextureFormat, Read_MipDimensionsLargerThanBase_Rejected) {
    auto blob = MakeValid4x4Blob();
    auto mip = GetMipDesc(blob, 1);
    mip.width = 8; // base is 4x4, so mip 1 must be 2x2
    mip.height = 8;
    SetMipDesc(blob, 1, mip);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("mip 1 dimensions"), std::string::npos) << r.error;
}

TEST(UzTextureFormat, Read_MipDimensionsSmallerThanChain_Rejected) {
    // Uploaders derive the row count from the header, not the descriptor, so a mip
    // that under-declares its dimensions must be rejected as well.
    auto blob = MakeValid4x4Blob();
    auto mip = GetMipDesc(blob, 0);
    mip.width = 1;
    mip.height = 1;
    mip.size = 4;
    SetMipDesc(blob, 0, mip);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
}

TEST(UzTextureFormat, Read_MipCountZero_Rejected) {
    auto blob = MakeValid4x4Blob();
    auto h = Make4x4Header();
    h.mipCount = 0;
    SetHeader(blob, h);

    EXPECT_FALSE(ReadUzTexture(blob).success);
}

TEST(UzTextureFormat, Read_MipCountBeyondFullChain_Rejected) {
    // 4x4 has at most 3 levels (4, 2, 1); a 4th descriptor fits in the file but is invalid.
    auto h = Make4x4Header();
    auto blob = WriteUzTexture(h, {
        MakeSolidMip(4, 4, 1, 1, 1, 1), MakeSolidMip(2, 2, 1, 1, 1, 1),
        MakeSolidMip(1, 1, 1, 1, 1, 1), MakeSolidMip(1, 1, 1, 1, 1, 1),
    });
    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("mipCount"), std::string::npos) << r.error;
}

TEST(UzTextureFormat, Read_ZeroWidth_Rejected) {
    auto blob = MakeValid4x4Blob();
    auto h = Make4x4Header();
    h.width = 0;
    SetHeader(blob, h);

    EXPECT_FALSE(ReadUzTexture(blob).success);
}

TEST(UzTextureFormat, Read_UnknownFormat_Rejected) {
    auto blob = MakeValid4x4Blob();
    auto h = Make4x4Header();
    h.format = 0xDEAD;
    SetHeader(blob, h);

    auto r = ReadUzTexture(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("format"), std::string::npos) << r.error;
}

TEST(UzTextureFormat, Read_HugeBaseDimensions_RejectedWithoutOverflow) {
    // width * height * 4 overflows uint64; the size check must not wrap into a match.
    auto blob = MakeValid4x4Blob();
    auto h = Make4x4Header();
    h.width = UINT32_MAX;
    h.height = UINT32_MAX;
    h.mipCount = 1;
    SetHeader(blob, h);
    auto mip = GetMipDesc(blob, 0);
    mip.width = UINT32_MAX;
    mip.height = UINT32_MAX;
    SetMipDesc(blob, 0, mip);

    EXPECT_FALSE(ReadUzTexture(blob).success);
}

TEST(UzTextureFormat, Roundtrip_BC7_NonMultipleOf4_BlockSizesAccepted) {
    // 5x3 BC7: mip0 = 2x1 blocks (32 B), mip1 2x1 px = 1 block (16 B), mip2 1x1 = 1 block.
    UzTextureHeader h;
    h.width = 5;
    h.height = 3;
    h.mipCount = 3;
    h.format = static_cast<uint32_t>(TextureFormat::BC7);
    auto blob = WriteUzTexture(h, {std::vector<uint8_t>(32, 0x11),
                                   std::vector<uint8_t>(16, 0x22),
                                   std::vector<uint8_t>(16, 0x33)});
    auto r = ReadUzTexture(blob);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.mips[1].width, 2u);
    EXPECT_EQ(r.file.mips[1].height, 1u);
    EXPECT_EQ(r.file.mipData[2][0], 0x33u);
}

TEST(UzTextureFormat, Read_BC1_MipSizeUsingBC7BlockBytes_Rejected) {
    // BC1 uses 8-byte blocks; a 16-byte single-block mip is inconsistent.
    UzTextureHeader h;
    h.width = 4;
    h.height = 4;
    h.mipCount = 1;
    h.format = static_cast<uint32_t>(TextureFormat::BC1);
    auto blob = WriteUzTexture(h, {std::vector<uint8_t>(16, 0)});
    EXPECT_FALSE(ReadUzTexture(blob).success);
}

TEST(UzTextureFormat, ParseLayout_ValidBlob_ReturnsValidatedMipTable) {
    auto blob = MakeValid4x4Blob();
    UzTextureLayout layout;
    std::string error;
    ASSERT_TRUE(ParseUzTextureLayout(blob.data(), blob.size(), layout, error)) << error;
    ASSERT_EQ(layout.mips.size(), 3u);
    EXPECT_EQ(layout.mips[0].size, 64u);
    EXPECT_LE(layout.mips[2].offset + layout.mips[2].size, blob.size());
}

TEST(UzTextureFormat, ParseLayout_NullData_Rejected) {
    UzTextureLayout layout;
    std::string error;
    EXPECT_FALSE(ParseUzTextureLayout(nullptr, 1024, layout, error));
    EXPECT_FALSE(error.empty());
}

TEST(UzTextureFormat, ComputeMipSize_FormatsAndOverflow) {
    uint64_t size = 0;
    ASSERT_TRUE(ComputeUzTextureMipSize(TextureFormat::Uncompressed_RGBA8, 3, 5, size));
    EXPECT_EQ(size, 3u * 5u * 4u);
    ASSERT_TRUE(ComputeUzTextureMipSize(TextureFormat::BC1, 5, 5, size));
    EXPECT_EQ(size, 2u * 2u * 8u);
    ASSERT_TRUE(ComputeUzTextureMipSize(TextureFormat::BC4, 1, 1, size));
    EXPECT_EQ(size, 8u);
    ASSERT_TRUE(ComputeUzTextureMipSize(TextureFormat::BC5, 8, 4, size));
    EXPECT_EQ(size, 2u * 1u * 16u);
    EXPECT_FALSE(ComputeUzTextureMipSize(TextureFormat::Uncompressed_RGBA8,
                                         UINT32_MAX, UINT32_MAX, size));
    EXPECT_FALSE(ComputeUzTextureMipSize(static_cast<TextureFormat>(99), 4, 4, size));
}

TEST(UzTextureFormat, Read_OnSuccess_ErrorStringIsEmpty) {
    auto header = Make4x4Header();
    std::vector<std::vector<uint8_t>> mipData = {
        MakeSolidMip(4, 4, 255, 0, 0, 255),
        MakeSolidMip(2, 2, 128, 0, 0, 255),
        MakeSolidMip(1, 1, 64, 0, 0, 255),
    };
    auto blob = WriteUzTexture(header, mipData);
    auto r = ReadUzTexture(blob);

    ASSERT_TRUE(r.success);
    EXPECT_TRUE(r.error.empty()) << "On success, error should be empty; got: " << r.error;
}

// ─────────────────────────────────────────────────────────────────────────────
// sRGB flag
// ─────────────────────────────────────────────────────────────────────────────

/// Returns 3 mip levels for a 4x4 RGBA8 texture (4x4 + 2x2 + 1x1).
static std::vector<std::vector<uint8_t>> Make4x4Mips() {
    return {
        MakeSolidMip(4, 4, 200, 100, 50, 255),
        MakeSolidMip(2, 2, 150, 75, 37, 255),
        MakeSolidMip(1, 1, 100, 50, 25, 255),
    };
}

TEST(UzTextureFormat, sRGBFlag_SetInHeader_PreservedOnRoundtrip) {
    UzTextureHeader h = Make4x4Header();
    h.flags |= kUzTextureFlag_sRGB;
    auto mips = Make4x4Mips();
    auto blob = WriteUzTexture(h, mips);
    auto r = ReadUzTexture(blob);
    ASSERT_TRUE(r.success);
    EXPECT_TRUE(r.file.header.flags & kUzTextureFlag_sRGB);
}

TEST(UzTextureFormat, sRGBFlag_NotSet_ReturnsZeroFlags) {
    UzTextureHeader h = Make4x4Header();
    auto mips = Make4x4Mips();
    auto blob = WriteUzTexture(h, mips);
    auto r = ReadUzTexture(blob);
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.file.header.flags & kUzTextureFlag_sRGB, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureFormatToRHI conversion
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, FormatToRHI_RGBA8_Linear) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::Uncompressed_RGBA8, false), RHIFormat::RGBA8Unorm);
}

TEST(UzTextureFormat, FormatToRHI_RGBA8_sRGB) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::Uncompressed_RGBA8, true), RHIFormat::RGBA8Srgb);
}

TEST(UzTextureFormat, FormatToRHI_BC7_Linear) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC7, false), RHIFormat::BC7Unorm);
}

TEST(UzTextureFormat, FormatToRHI_BC7_sRGB) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC7, true), RHIFormat::BC7Srgb);
}

TEST(UzTextureFormat, FormatToRHI_BC5_AlwaysLinear) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC5, false), RHIFormat::BC5Unorm);
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC5, true), RHIFormat::BC5Unorm);
}

TEST(UzTextureFormat, FormatToRHI_BC4_AlwaysLinear) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC4, false), RHIFormat::BC4Unorm);
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC4, true), RHIFormat::BC4Unorm);
}

TEST(UzTextureFormat, FormatToRHI_BC1_sRGB) {
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC1, false), RHIFormat::BC1Unorm);
    EXPECT_EQ(TextureFormatToRHI(TextureFormat::BC1, true), RHIFormat::BC1Srgb);
}

// ─────────────────────────────────────────────────────────────────────────────
// BC format helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzTextureFormat, IsBlockCompressed_BC7) {
    EXPECT_TRUE(IsBlockCompressed(RHIFormat::BC7Unorm));
    EXPECT_TRUE(IsBlockCompressed(RHIFormat::BC7Srgb));
    EXPECT_TRUE(IsBlockCompressed(RHIFormat::BC1Unorm));
    EXPECT_TRUE(IsBlockCompressed(RHIFormat::BC5Unorm));
    EXPECT_FALSE(IsBlockCompressed(RHIFormat::RGBA8Unorm));
    EXPECT_FALSE(IsBlockCompressed(RHIFormat::D32Float));
}

TEST(UzTextureFormat, BytesPerBlock_BC7_Is16) {
    EXPECT_EQ(BytesPerBlock(RHIFormat::BC7Unorm), 16u);
    EXPECT_EQ(BytesPerBlock(RHIFormat::BC7Srgb), 16u);
}

TEST(UzTextureFormat, BytesPerBlock_BC1_Is8) {
    EXPECT_EQ(BytesPerBlock(RHIFormat::BC1Unorm), 8u);
    EXPECT_EQ(BytesPerBlock(RHIFormat::BC4Unorm), 8u);
}

TEST(UzTextureFormat, BytesPerBlock_NonBC_IsZero) {
    EXPECT_EQ(BytesPerBlock(RHIFormat::RGBA8Unorm), 0u);
}
