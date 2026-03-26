/// UzEngine .uzshader Binary Format
///
/// Runtime-ready compiled shader format. Contains precompiled bytecode
/// (DXIL or SPIR-V) for a single shader stage, plus optional reflection
/// metadata. Each .uzshader file holds exactly one stage — a full pipeline
/// (e.g., VS+PS) requires multiple .uzshader files, typically bundled inside
/// a .uzpak archive or referenced by a material asset.
///
/// # Version History
/// - **v1** (initial): Single-stage bytecode + optional reflection.
///   Future versions may add include dependency hashes and permutation keys
///   using the reserved field.
///
/// # Endianness
/// All multi-byte fields are little-endian (native x86/ARM LE).
///
/// # Multi-Stage Shaders
/// Each .uzshader stores one pipeline stage (VS, PS, CS, etc.). A full
/// graphics pipeline is assembled by the renderer from multiple files:
/// ```
/// materials/pbr.vs.uzshader   → UzShaderStage::Vertex
/// materials/pbr.ps.uzshader   → UzShaderStage::Pixel
/// ```
/// The material asset or render pipeline descriptor references the set of
/// stages needed. The shader cache uses content-hash keys (see below).
///
/// # Cache Key Computation
/// The shader cache key is an xxHash64 of: `source_text + all_include_contents
/// + defines_sorted + target_stage + shader_model`. This ensures that any
/// change to source, includes, or compilation settings invalidates the cache.
/// The .uzshader file itself does not store the hash — the cache directory
/// uses the hash as the filename: `.cache/shaders/<xxhash64>.uzshader`.
///
/// # Feature Level Compatibility
/// The `model` field encodes the minimum shader model required:
/// `(major << 8) | minor`. For example, SM6.6 = 0x0606. The renderer checks
/// this against the device's maximum supported model and falls back to a
/// lower permutation if needed.
///
/// # Binary Layout (little-endian)
/// ```
/// Offset   Size               Section
/// ──────── ────────────────── ─────────────────────────────────────
/// 0x0000   32 bytes           [Header] UzShaderHeader
/// 0x0020   bytecodeSize       [Bytecode] DXIL or SPIR-V blob
///          reflectSize        [Reflection] optional metadata blob
/// ```
///
/// # Validation Rules
/// Readers MUST validate in this order:
/// 1. File size >= 32 bytes (header fits).
/// 2. `magic == 0x48535A55` ("UZSH" in LE).
/// 3. `version == 1`.
/// 4. `stage` is a valid `UzShaderStage` value (0..7).
/// 5. `format` is a valid `UzShaderFormat` value (0..1).
/// 6. `bytecodeSize > 0` (empty bytecode is invalid).
/// 7. `32 + bytecodeSize + reflectSize <= fileSize`.
/// 8. `reserved == 0`.
///
/// # Example: Compilation to cache
/// ```cpp
/// // 1. Compile HLSL source to DXIL
/// auto dxil = CompileShader("pbr.hlsl", "VSMain", "vs_6_6");
/// auto reflect = ExtractReflection(dxil);
///
/// // 2. Serialize to .uzshader
/// auto blob = uz::WriteUzShader(
///     uz::UzShaderStage::Vertex,
///     uz::PackShaderModel(6, 6),
///     uz::UzShaderFormat::DXIL,
///     dxil, reflect);
///
/// // 3. Write to cache: .cache/shaders/<hash>.uzshader
/// WriteFile(cachePath, blob);
/// ```
///
/// # Example: Loading from cache
/// ```cpp
/// auto result = uz::ReadUzShader(fileBytes);
/// if (!result.success) { UZ_LOG(Shader, Error, "Bad shader: {}", result.error); }
/// auto sm = uz::UnpackShaderModelMajor(result.file.header.model);
/// if (sm > deviceMaxSM) { /* fall back to lower permutation */ }
/// CreatePipelineState(result.file.bytecode);  // upload to GPU
/// ```

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Magic number for .uzshader files: ASCII "UZSH" stored as little-endian uint32.
/// Byte sequence on disk: `0x55 0x5A 0x53 0x48` → 'U' 'Z' 'S' 'H'.
static constexpr uint32_t kUzShaderMagic = 0x48535A55;

/// Current .uzshader format version. Increment when adding fields or changing
/// layout. Readers must reject files with `version > kUzShaderVersion`.
static constexpr uint32_t kUzShaderVersion = 1;

/// Shader pipeline stage stored in the .uzshader header.
///
/// Determines which programmable GPU stage this shader targets.
/// Values match the binary layout — do not reorder.
enum class UzShaderStage : uint32_t {
    Vertex        = 0, ///< Vertex shader — per-vertex transformation
    Pixel         = 1, ///< Pixel (fragment) shader — per-pixel shading
    Compute       = 2, ///< Compute shader — general-purpose GPU compute
    Geometry      = 3, ///< Geometry shader — per-primitive processing
    Hull          = 4, ///< Hull (tessellation control) shader
    Domain        = 5, ///< Domain (tessellation evaluation) shader
    Amplification = 6, ///< Amplification shader (mesh shader pipeline)
    Mesh          = 7, ///< Mesh shader — replaces VS+GS with meshlet processing
};

/// Compiled bytecode format stored in the .uzshader header.
///
/// Determines which GPU driver interface consumes the bytecode.
enum class UzShaderFormat : uint32_t {
    DXIL  = 0, ///< DirectX Intermediate Language (DX12, Windows)
    SPIRV = 1, ///< Standard Portable Intermediate Representation (Vulkan, cross-platform)
};

#pragma pack(push, 1)
/// .uzshader file header — exactly 32 bytes, little-endian.
///
/// All fields are uint32_t for alignment simplicity. The header is
/// immediately followed by `bytecodeSize` bytes of compiled shader
/// bytecode, then `reflectSize` bytes of optional reflection metadata.
///
/// # Memory Layout (32 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ─────────────────────────────────────────────
/// 0x00    4     magic          "UZSH" = 0x48535A55
/// 0x04    4     version        must be 1
/// 0x08    4     stage          UzShaderStage enum (0..7)
/// 0x0C    4     model          packed: (major<<8)|minor
/// 0x10    4     bytecodeSize   must be > 0
/// 0x14    4     reflectSize    0 if no reflection data
/// 0x18    4     format         UzShaderFormat enum (0..1)
/// 0x1C    4     reserved       must be 0
/// ```
struct UzShaderHeader {
    uint32_t magic = kUzShaderMagic;         ///< Must be `kUzShaderMagic` (0x48535A55, "UZSH"). First validation check.
    uint32_t version = kUzShaderVersion;     ///< Format version (currently 1). Reject if > kUzShaderVersion.
    uint32_t stage = 0;                      ///< `UzShaderStage` cast to uint32. Valid range: 0..7.
    uint32_t model = 0;                      ///< Packed shader model: `(major << 8) | minor`. E.g., 0x0606 = SM6.6.
    uint32_t bytecodeSize = 0;               ///< Size of compiled bytecode blob in bytes. Must be > 0.
    uint32_t reflectSize = 0;                ///< Size of reflection blob in bytes. 0 if no reflection metadata present.
    uint32_t format = 0;                     ///< `UzShaderFormat` cast to uint32. 0 = DXIL (DX12), 1 = SPIR-V (Vulkan).
    uint32_t reserved = 0;                   ///< Reserved for future use, must be 0. Reject if non-zero.
};
#pragma pack(pop)

static_assert(sizeof(UzShaderHeader) == 32, "UzShaderHeader must be exactly 32 bytes");

/// Packs a shader model version into the uint32 format used by UzShaderHeader::model.
///
/// # Arguments
/// * `major` - Shader model major version (e.g., 6). Must fit in 8 bits.
/// * `minor` - Shader model minor version (e.g., 6). Must fit in 8 bits.
///
/// # Returns
/// Packed value: `(major << 8) | minor`. E.g., PackShaderModel(6, 6) → 0x0606.
[[nodiscard]] constexpr uint32_t PackShaderModel(uint32_t major, uint32_t minor) {
    return (major << 8) | minor;
}

/// Unpacks shader model major version from a packed model field.
///
/// # Returns
/// Major version (0..255). E.g., UnpackShaderModelMajor(0x0606) → 6.
[[nodiscard]] constexpr uint32_t UnpackShaderModelMajor(uint32_t packed) {
    return (packed >> 8) & 0xFF;
}

/// Unpacks shader model minor version from a packed model field.
///
/// # Returns
/// Minor version (0..255). E.g., UnpackShaderModelMinor(0x0606) → 6.
[[nodiscard]] constexpr uint32_t UnpackShaderModelMinor(uint32_t packed) {
    return packed & 0xFF;
}

/// Serializes a compiled shader into .uzshader binary format.
///
/// Produces a contiguous byte buffer: `[UzShaderHeader][bytecode][reflection]`.
///
/// # Arguments
/// * `stage` - Which pipeline stage this shader targets.
/// * `packedModel` - Shader model packed via `PackShaderModel()`.
/// * `format` - Bytecode format (DXIL or SPIRV).
/// * `bytecode` - Compiled shader bytecode blob.
/// * `reflection` - Optional reflection metadata (empty if not available).
///
/// # Returns
/// Complete .uzshader file bytes, ready to write to disk or store in a pak.
///
/// # Example
/// ```cpp
/// auto blob = WriteUzShader(UzShaderStage::Vertex, PackShaderModel(6, 6),
///                           UzShaderFormat::DXIL, compiledBytes);
/// ```
[[nodiscard]] std::vector<uint8_t> WriteUzShader(
    UzShaderStage stage,
    uint32_t packedModel,
    UzShaderFormat format,
    const std::vector<uint8_t>& bytecode,
    const std::vector<uint8_t>& reflection = {});

/// Deserialized contents of a .uzshader file.
struct UzShaderFile {
    UzShaderHeader header;               ///< Parsed 32-byte header
    std::vector<uint8_t> bytecode;       ///< Compiled shader bytecode (DXIL or SPIRV)
    std::vector<uint8_t> reflection;     ///< Optional reflection metadata (may be empty)
};

/// Result of parsing a .uzshader binary blob.
struct UzShaderParseResult {
    bool success = false;                ///< `true` if parsing completed without errors
    UzShaderFile file;                   ///< Parsed data (valid only if `success == true`)
    std::string error;                   ///< Human-readable error message (empty on success)
};

/// Parses a .uzshader binary blob into its header, bytecode, and reflection.
///
/// Validates magic number, version, and data size before extracting fields.
///
/// # Arguments
/// * `data` - Raw bytes of a .uzshader file (minimum 32 bytes for header).
///
/// # Returns
/// `UzShaderParseResult` with `success == true` and populated `file` on
/// valid input. `success == false` with `error` on invalid magic, wrong
/// version, or truncated data.
[[nodiscard]] UzShaderParseResult ReadUzShader(const std::vector<uint8_t>& data);

} // namespace uz
