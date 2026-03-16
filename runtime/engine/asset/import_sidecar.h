/// UzEngine Import Sidecar (.uzimport)
///
/// JSON metadata file stored alongside each source asset. Tracks importer
/// identity, source content hash, options, and output paths — enabling
/// automatic reimport when anything changes.
///
/// # Schema
/// ```json
/// {
///     "importer": "shader",
///     "version": 1,
///     "source_hash": "a1b2c3d4e5f67890",
///     "options": { "entry_point": "main", "stage": "vertex", "model": "sm6_6" },
///     "outputs": { "sm6_6": ".cache/imported/a1b2c3d4e5f67890.bin" },
///     "dependencies": ["common.hlsli"]
/// }
/// ```
///
/// # Reimport Triggers
/// - Source content hash changed
/// - Importer version bumped
/// - Sidecar file missing
/// - Cached output file missing
///
/// # Usage
/// ```cpp
/// // Write sidecar after successful import
/// ImportSidecar sidecar;
/// sidecar.importer = "shader";
/// sidecar.version = 1;
/// sidecar.sourceHash = HashToHex(xxhash64(sourceBytes));
/// sidecar.outputs = {{"sm6_6", ".cache/imported/abc123.bin"}};
/// WriteSidecarFile("shaders/pbr.hlsl.uzimport", sidecar);
///
/// // Check if reimport is needed
/// auto result = ReadSidecarFile("shaders/pbr.hlsl.uzimport");
/// if (!result.success || NeedsReimport(result.sidecar, currentHash, currentVersion)) {
///     // Reimport needed
/// }
/// ```

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

/// Parsed .uzimport sidecar data.
///
/// Represents the complete metadata for a single imported asset,
/// including which importer produced it, what version was used,
/// and where the compiled outputs are stored.
struct ImportSidecar {
    std::string importer;                                    ///< Importer name (e.g., "shader", "texture")
    uint32_t version = 0;                                    ///< Importer version at the time of import
    std::string sourceHash;                                  ///< xxHash64 hex string (16 chars) of source file content
    std::unordered_map<std::string, std::string> options;    ///< Import options as key-value pairs (e.g., "stage"="vertex")
    std::unordered_map<std::string, std::string> outputs;    ///< Variant name → cached output path (e.g., "sm6_6" → ".cache/imported/abc.bin")
    std::vector<std::string> dependencies;                   ///< Relative paths of files this asset depends on (e.g., included headers)
};

/// Serializes an ImportSidecar struct to a pretty-printed JSON string.
///
/// The output is human-readable (4-space indent) and suitable for writing
/// to a .uzimport file.
///
/// # Arguments
/// - `sidecar`: The sidecar data to serialize. All fields are written,
///   including empty maps/vectors (as `{}` / `[]`).
///
/// # Returns
/// A JSON string. Never fails — always returns valid JSON.
[[nodiscard]] std::string WriteSidecarJson(const ImportSidecar& sidecar);

/// Result of parsing a .uzimport JSON string or file.
///
/// On success, `success` is `true` and `sidecar` contains the parsed data.
/// On failure, `success` is `false` and `error` contains a human-readable
/// description of the parse error.
struct SidecarParseResult {
    bool success = false;       ///< Whether parsing succeeded
    ImportSidecar sidecar;      ///< Parsed data (valid only if success == true)
    std::string error;          ///< Error message (non-empty only if success == false)
};

/// Parses a JSON string into an ImportSidecar.
///
/// Tolerant of missing fields — unrecognized keys are ignored, missing
/// keys get default values (empty string, 0, empty containers).
///
/// # Arguments
/// - `json`: Raw JSON string to parse.
///
/// # Returns
/// `SidecarParseResult` with `success == true` on valid JSON,
/// `success == false` with `error` message on malformed JSON.
[[nodiscard]] SidecarParseResult ReadSidecarJson(const std::string& json);

/// Writes a sidecar to a .uzimport file on disk.
///
/// Serializes the sidecar to JSON and writes it atomically to the given path.
///
/// # Arguments
/// - `path`: Absolute or relative file path for the .uzimport output.
/// - `sidecar`: The sidecar data to write.
///
/// # Returns
/// `true` if the file was written successfully, `false` on I/O error.
///
/// # Side Effects
/// - Logs `Asset::Debug` with path, importer name, and hash on success.
/// - Logs `Asset::Error` with path on failure.
bool WriteSidecarFile(const std::string& path, const ImportSidecar& sidecar);

/// Reads and parses a .uzimport file from disk.
///
/// # Arguments
/// - `path`: Absolute or relative path to the .uzimport file.
///
/// # Returns
/// `SidecarParseResult` with `success == true` if the file was read and
/// parsed. `success == false` with `error` if the file cannot be opened
/// or contains invalid JSON.
[[nodiscard]] SidecarParseResult ReadSidecarFile(const std::string& path);

/// Checks if a reimport is needed based on the sidecar state.
///
/// Compares the current source hash and importer version against the
/// values stored in the sidecar, and verifies all output files exist on disk.
///
/// # Reimport Triggers (any one triggers reimport)
/// - `currentSourceHash != sidecar.sourceHash` — source file was modified
/// - `currentVersion != sidecar.version` — importer was updated
/// - Any output file listed in `sidecar.outputs` is missing from disk
///
/// # Arguments
/// - `sidecar`: Previously saved sidecar data to check against.
/// - `currentSourceHash`: xxHash64 hex string of the current source content.
/// - `currentVersion`: Current version of the importer.
///
/// # Returns
/// `true` if reimport is needed, `false` if the cached output is still valid.
///
/// # Side Effects
/// Logs `Asset::Debug` with the specific reason when reimport is needed.
[[nodiscard]] bool NeedsReimport(
    const ImportSidecar& sidecar,
    const std::string& currentSourceHash,
    uint32_t currentVersion);

/// Formats a uint64_t hash value as a 16-character lowercase hex string.
///
/// # Example
/// ```cpp
/// HashToHex(0xDEADBEEFCAFE1234) → "deadbeefcafe1234"
/// HashToHex(0)                   → "0000000000000000"
/// ```
///
/// # Returns
/// Always returns exactly 16 characters, zero-padded on the left.
[[nodiscard]] std::string HashToHex(uint64_t hash);

/// Parses a 16-character lowercase hex string back to a uint64_t.
///
/// # Arguments
/// - `hex`: Must be exactly 16 characters of valid hex digits [0-9a-f].
///
/// # Returns
/// - The parsed hash value on success.
/// - `0` if the string is not exactly 16 characters or contains invalid digits.
[[nodiscard]] uint64_t HexToHash(const std::string& hex);

} // namespace uz
