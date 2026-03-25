/// UzEngine .uztexture Binary Format
///
/// Runtime-ready texture format containing mipmap chain and format metadata.
/// Supports uncompressed RGBA8 and block-compressed formats (BC1/3/5/7).
/// Mip data is stored sequentially from largest (mip 0) to smallest, with
/// each mip blob referenced by an absolute file offset in its descriptor.
///
/// # Version History
/// - **v1** (initial): Header + mip descriptors + concatenated mip blobs.
///   Future versions will maintain backward compatibility by using reserved
///   header fields or appending new sections after the last mip blob.
///
/// # Endianness
/// All multi-byte fields are little-endian (native x86/ARM LE).
///
/// # Mipmap Storage
/// Mips are stored sequentially in descending size order (mip 0 = base/largest).
/// Each mip's dimensions are `max(1, base >> level)` for both width and height.
/// There is no padding between mip blobs — they are tightly packed.
/// Each `UzTextureMipLevel` descriptor stores an absolute file offset, so
/// readers can seek directly to any mip level without scanning.
///
/// # Compression Formats
/// - **RGBA8**: 4 bytes/pixel, uncompressed. Used for small textures or when
///   block compression artifacts are unacceptable (e.g., UI textures).
/// - **BC1**: 8 bytes per 4x4 block (0.5 bytes/pixel). RGB + optional 1-bit alpha.
///   Best for opaque diffuse textures.
/// - **BC3**: 16 bytes per 4x4 block (1 byte/pixel). RGB + interpolated 8-bit alpha.
///   Best for textures with smooth alpha gradients (foliage, particles).
/// - **BC5**: 16 bytes per 4x4 block (1 byte/pixel). Two independent channels.
///   Best for tangent-space normal maps (store X,Y; reconstruct Z in shader).
/// - **BC7**: 16 bytes per 4x4 block (1 byte/pixel). High-quality all channels.
///   Best for albedo with alpha, or any texture needing maximum quality.
///
/// For block-compressed formats, mip dimensions are rounded up to 4x4 block
/// boundaries when computing data size: `ceil(width/4) * ceil(height/4) * blockSize`.
///
/// # Binary Layout (little-endian)
/// ```
/// Offset   Size                           Section
/// ──────── ────────────────────────────── ──────────────────────────────
/// 0x0000   64 bytes                       [Header] UzTextureHeader
/// 0x0040   mipCount * 24 bytes            [MipLevels] UzTextureMipLevel array
///          variable                        [Mip 0 Data] largest mip blob
///          variable                        [Mip 1 Data] next mip blob
///          ...                             ...
///          variable                        [Mip N Data] smallest (1x1) mip blob
/// ```
///
/// # Validation Rules
/// Readers MUST validate in this order:
/// 1. File size >= 64 bytes (header fits).
/// 2. `magic == 0x5A585455` ("UZTX" in LE).
/// 3. `version == 1`.
/// 4. `flags == 0`.
/// 5. `width > 0` and `height > 0`.
/// 6. `mipCount >= 1` and `mipCount <= floor(log2(max(width,height))) + 1`.
/// 7. `format` is a valid `TextureFormat` enum value (0..4).
/// 8. File size >= 64 + mipCount * 24 (mip descriptors fit).
/// 9. For each mip: `offset + size <= fileSize`.
/// 10. For block-compressed formats: dimensions are valid for the block size.
///
/// # Example: Writing an uncompressed texture
/// ```cpp
/// uz::UzTextureHeader header;
/// header.width = 256;
/// header.height = 256;
/// header.mipCount = 1;
/// header.format = static_cast<uint32_t>(uz::TextureFormat::Uncompressed_RGBA8);
///
/// std::vector<uint8_t> pixels(256 * 256 * 4);  // RGBA8
/// auto blob = uz::WriteUzTexture(header, {pixels});
/// ```
///
/// # Example: Reading a compressed texture
/// ```cpp
/// auto result = uz::ReadUzTexture(fileBytes);
/// if (!result.success) { UZ_LOG(Asset, Error, "Bad tex: {}", result.error); }
/// for (uint32_t i = 0; i < result.file.header.mipCount; ++i) {
///     const auto& mip = result.file.mips[i];
///     UploadToGPU(result.file.mipData[i], mip.width, mip.height, i);
/// }
/// ```

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Magic number for .uztexture files: ASCII "UZTX" stored as little-endian uint32.
/// Byte sequence on disk: `0x55 0x54 0x58 0x5A` → 'U' 'T' 'X' 'Z'.
static constexpr uint32_t kUzTextureMagic = 0x5A585455;

/// Current .uztexture format version. Increment when adding fields or changing
/// layout. Readers must reject files with `version > kUzTextureVersion`.
static constexpr uint32_t kUzTextureVersion = 1;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel format stored in the .uztexture header.
///
/// Determines how the GPU interprets the mip data. Values are stable across
/// versions — do not reorder or reassign. New formats are appended.
///
/// # Block-Compressed Size Formula
/// For BC formats, mip data size = `ceil(w/4) * ceil(h/4) * blockSize` where:
/// - BC1: blockSize = 8 bytes per 4x4 block.
/// - BC3/BC5/BC7: blockSize = 16 bytes per 4x4 block.
enum class TextureFormat : uint32_t {
    Uncompressed_RGBA8 = 0, ///< 4 bytes/pixel (R8G8B8A8), uncompressed. Maps to DXGI_FORMAT_R8G8B8A8_UNORM.
    BC1                = 1, ///< 8 bytes per 4x4 block. RGB + optional 1-bit alpha. Best for opaque diffuse.
    BC3                = 2, ///< 16 bytes per 4x4 block. RGB + interpolated 8-bit alpha. Best for foliage/particles.
    BC5                = 3, ///< 16 bytes per 4x4 block. Two independent channels (RG). Best for normal maps.
    BC7                = 4, ///< 16 bytes per 4x4 block. High-quality RGBA. Best when max quality is needed.
    BC4                = 5, ///< 8 bytes per 4x4 block. Single channel. Best for roughness/metallic/AO.
};

/// Flag bits for UzTextureHeader::flags.
static constexpr uint32_t kUzTextureFlag_sRGB = 0x1; ///< Bit 0: texture data is sRGB-encoded.

/// Converts a .uztexture TextureFormat + sRGB flag to the runtime RHIFormat.
[[nodiscard]] constexpr RHIFormat TextureFormatToRHI(TextureFormat fmt, bool isSRGB) {
    switch (fmt) {
        case TextureFormat::Uncompressed_RGBA8:
            return isSRGB ? RHIFormat::RGBA8Srgb : RHIFormat::RGBA8Unorm;
        case TextureFormat::BC1:
            return isSRGB ? RHIFormat::BC1Srgb : RHIFormat::BC1Unorm;
        case TextureFormat::BC3:
            return isSRGB ? RHIFormat::BC3Srgb : RHIFormat::BC3Unorm;
        case TextureFormat::BC5:
            return RHIFormat::BC5Unorm; // BC5 is always linear (normal maps)
        case TextureFormat::BC7:
            return isSRGB ? RHIFormat::BC7Srgb : RHIFormat::BC7Unorm;
        case TextureFormat::BC4:
            return RHIFormat::BC4Unorm; // BC4 is always linear (single channel)
        default:
            return RHIFormat::Unknown;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Binary Structs
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)
/// .uztexture file header — exactly 64 bytes, little-endian.
///
/// Immediately followed by `mipCount` mip level descriptors, then the
/// concatenated mip data blobs (largest mip first).
///
/// # Memory Layout (64 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ───────────────────────────────────────
/// 0x00    4     magic          "UZTX" = 0x5A585455
/// 0x04    4     version        must be 1
/// 0x08    4     width          base mip width (> 0)
/// 0x0C    4     height         base mip height (> 0)
/// 0x10    4     mipCount       [1, floor(log2(max(w,h)))+1]
/// 0x14    4     format         TextureFormat enum (0..4)
/// 0x18    4     flags          reserved, must be 0
/// 0x1C    36    reserved[9]    must be all zeros
/// ```
struct UzTextureHeader {
    uint32_t magic = kUzTextureMagic;       ///< Must be `kUzTextureMagic` (0x5A585455, "UZTX"). First validation check.
    uint32_t version = kUzTextureVersion;   ///< Format version (currently 1). Reject if > kUzTextureVersion.
    uint32_t width = 0;                     ///< Base mip width in pixels. Must be > 0.
    uint32_t height = 0;                    ///< Base mip height in pixels. Must be > 0.
    uint32_t mipCount = 0;                  ///< Number of mip levels. Must be >= 1. Max = floor(log2(max(width,height))) + 1.
    uint32_t format = 0;                    ///< `TextureFormat` cast to uint32. Valid range: 0..4.
    uint32_t flags = 0;                     ///< Bit 0: sRGB (1=sRGB, 0=linear). Bit 1: reserved (cubemap). Bits 2-31: reserved, must be 0.
    uint32_t reserved[9] = {};              ///< Reserved for future use, must be all zeros.
};
#pragma pack(pop)

static_assert(sizeof(UzTextureHeader) == 64, "UzTextureHeader must be exactly 64 bytes");

#pragma pack(push, 1)
/// Describes a single mip level within the texture.
///
/// `offset` is an absolute byte offset from the start of the file, allowing
/// direct seeking without scanning previous mips. `size` is the byte count
/// of the mip data blob. Mip 0 is the largest (base) level.
///
/// # Memory Layout (24 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ────────────────────────────────────
/// 0x00    8     offset   absolute file offset (uint64)
/// 0x08    8     size     mip blob byte count (uint64)
/// 0x10    4     width    mip width in pixels
/// 0x14    4     height   mip height in pixels
/// ```
///
/// # Validation Rules
/// - `offset + size <= fileSize`.
/// - `width == max(1, header.width >> mipIndex)`.
/// - `height == max(1, header.height >> mipIndex)`.
/// - For RGBA8: `size == width * height * 4`.
/// - For BC formats: `size == ceil(width/4) * ceil(height/4) * blockSize`.
///
/// # Notes
/// Uses uint64 for offset/size to support textures larger than 4 GB
/// (e.g., 8K virtual textures with full mip chains).
struct UzTextureMipLevel {
    uint64_t offset = 0;                    ///< Absolute byte offset from file start to this mip's data.
    uint64_t size = 0;                      ///< Mip data size in bytes. See format-specific size formulas above.
    uint32_t width = 0;                     ///< Mip width in pixels: `max(1, baseWidth >> mipIndex)`.
    uint32_t height = 0;                    ///< Mip height in pixels: `max(1, baseHeight >> mipIndex)`.
};
#pragma pack(pop)

static_assert(sizeof(UzTextureMipLevel) == 24, "UzTextureMipLevel must be exactly 24 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Serialization
// ─────────────────────────────────────────────────────────────────────────────

/// Serializes texture data into .uztexture binary format.
///
/// Each element in `mipData` corresponds to one mip level, starting from
/// the largest (mip 0). The header's width/height/mipCount/format must be
/// pre-filled by the caller. Mip level descriptors (offsets/sizes) are
/// computed and written automatically.
///
/// # Arguments
/// * `header` - Pre-filled header with width, height, mipCount, format.
/// * `mipData` - Vector of mip blobs, one per level (mip 0 = largest).
///
/// # Returns
/// Complete .uztexture file bytes, ready to write to disk.
[[nodiscard]] std::vector<uint8_t> WriteUzTexture(
    UzTextureHeader header,
    const std::vector<std::vector<uint8_t>>& mipData);

/// Deserialized contents of a .uztexture file.
struct UzTextureFile {
    UzTextureHeader header;                 ///< Parsed 64-byte header
    std::vector<UzTextureMipLevel> mips;    ///< Mip level descriptors
    std::vector<std::vector<uint8_t>> mipData; ///< Mip data blobs
};

/// Result of parsing a .uztexture binary blob.
struct UzTextureParseResult {
    bool success = false;                   ///< `true` if parsing completed without errors
    UzTextureFile file;                     ///< Parsed data (valid only if `success == true`)
    std::string error;                      ///< Human-readable error message (empty on success)
};

/// Parses a .uztexture binary blob into its header, mip descriptors, and pixel data.
///
/// Validates magic number, version, and data sizes before extracting fields.
///
/// # Arguments
/// * `data` - Raw bytes of a .uztexture file (minimum 64 bytes for header).
///
/// # Returns
/// `UzTextureParseResult` with `success == true` and populated `file` on
/// valid input. `success == false` with `error` on invalid magic, wrong
/// version, or truncated data.
[[nodiscard]] UzTextureParseResult ReadUzTexture(const std::vector<uint8_t>& data);

} // namespace uz
