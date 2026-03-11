/// UzEngine .uzshader Binary Format
///
/// Runtime-ready compiled shader format. Contains precompiled bytecode
/// (DXIL or SPIR-V) and optional reflection metadata.
///
/// # Layout (little-endian)
/// ```
/// [Header]     32 bytes
///   magic:          4 bytes   "UZSH" (0x48535A55)
///   version:        4 bytes   uint32 (1)
///   stage:          4 bytes   uint32 (0=VS, 1=PS, 2=CS, 3=GS, 4=HS, 5=DS, 6=AS, 7=MS)
///   model:          4 bytes   uint32 (packed: major<<8 | minor, e.g., 0x0606 = SM6.6)
///   bytecodeSize:   4 bytes   uint32
///   reflectSize:    4 bytes   uint32 (0 if no reflection)
///   format:         4 bytes   uint32 (0=DXIL, 1=SPIRV)
///   reserved:       4 bytes
///
/// [Bytecode]   bytecodeSize bytes
/// [Reflection] reflectSize bytes (optional)
/// ```

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Magic number for .uzshader files: "UZSH" in little-endian.
static constexpr uint32_t kUzShaderMagic = 0x48535A55;

/// Current .uzshader format version.
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

/// .uzshader file header — exactly 32 bytes, little-endian.
///
/// All fields are uint32_t for alignment simplicity. The header is
/// immediately followed by `bytecodeSize` bytes of compiled shader
/// bytecode, then `reflectSize` bytes of optional reflection metadata.
#pragma pack(push, 1)
struct UzShaderHeader {
    uint32_t magic = kUzShaderMagic;         ///< Must be `kUzShaderMagic` (0x48535A55, "UZSH")
    uint32_t version = kUzShaderVersion;     ///< Format version (currently 1)
    uint32_t stage = 0;                      ///< `UzShaderStage` cast to uint32
    uint32_t model = 0;                      ///< Packed shader model: `(major << 8) | minor`
    uint32_t bytecodeSize = 0;               ///< Size of compiled bytecode blob in bytes
    uint32_t reflectSize = 0;                ///< Size of reflection blob (0 if none)
    uint32_t format = 0;                     ///< `UzShaderFormat` cast to uint32 (0=DXIL, 1=SPIRV)
    uint32_t reserved = 0;                   ///< Reserved for future use, must be 0
};
#pragma pack(pop)

static_assert(sizeof(UzShaderHeader) == 32, "UzShaderHeader must be exactly 32 bytes");

/// Packs a shader model version into the header format.
///
/// Example: PackShaderModel(6, 6) → 0x0606
[[nodiscard]] constexpr uint32_t PackShaderModel(uint32_t major, uint32_t minor) {
    return (major << 8) | minor;
}

/// Unpacks shader model major version from header.
[[nodiscard]] constexpr uint32_t UnpackShaderModelMajor(uint32_t packed) {
    return (packed >> 8) & 0xFF;
}

/// Unpacks shader model minor version from header.
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
