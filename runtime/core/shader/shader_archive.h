/// UzEngine Shader Archive
///
/// Binary archive format (.uzpak) for shipping precompiled shader bytecode.
/// Entries are indexed by content hash (xxHash64 hex string) for O(1) lookup.
///
/// # File Format
/// ```
/// Header:   magic(u32) | version(u32) | count(u32) | reserved(u32)
/// Entries:  [hash(char[64]) | offset(u64) | size(u64)] * count
/// Data:     [bytecode bytes] ...
/// ```
///
/// # Usage
/// Build-time: `ShaderArchive::Build(path, entries)` creates the archive.
/// Runtime: `archive.Load(path)` then `archive.Lookup(hash)` for bytecode.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

/// Magic number for .uzpak shader archive files ('UZSH').
static constexpr uint32_t kShaderArchiveMagic = 0x555A5348;

/// Current archive format version.
static constexpr uint32_t kShaderArchiveVersion = 1;

/// Fixed size of the hash field in the entry table.
static constexpr size_t kHashFieldSize = 64;

/// A single entry to be written into a shader archive.
struct ShaderArchiveEntry {
    std::string hash;                ///< Content hash key (xxHash64 hex string)
    std::vector<uint8_t> bytecode;   ///< Compiled shader bytecode
};

/// Binary shader archive for shipping builds.
///
/// Loads all bytecode into memory for fast lookup by hash.
/// Build() is a static factory that creates archive files at build time.
class ShaderArchive {
public:
    /// Build an archive from a list of entries and write to disk.
    ///
    /// All entries must have non-empty hashes that fit within kHashFieldSize.
    /// Returns true on success.
    [[nodiscard]] static bool Build(const std::filesystem::path& outputPath,
                                    const std::vector<ShaderArchiveEntry>& entries);

    /// Load an archive from disk into memory.
    ///
    /// Validates magic number and version. Replaces any previously loaded data.
    /// Returns true on success.
    [[nodiscard]] bool Load(const std::filesystem::path& archivePath);

    /// Unload the archive, freeing all memory.
    void Unload();

    /// Lookup bytecode by hash.
    ///
    /// Returns a copy of the bytecode, or an empty vector if not found.
    [[nodiscard]] std::vector<uint8_t> Lookup(const std::string& hash) const;

    /// Check if a hash exists in the archive.
    [[nodiscard]] bool Contains(const std::string& hash) const;

    /// Get the number of entries in the archive.
    [[nodiscard]] size_t EntryCount() const;

    /// Get all hashes in the archive.
    [[nodiscard]] std::vector<std::string> GetHashes() const;

    /// Whether an archive is currently loaded.
    [[nodiscard]] bool IsLoaded() const;

private:
    /// Index entry pointing into m_data.
    struct LoadedEntry {
        uint64_t offset;  ///< Byte offset into m_data
        uint64_t size;    ///< Bytecode size in bytes
    };

    std::unordered_map<std::string, LoadedEntry> m_index;
    std::vector<uint8_t> m_data;  ///< Entire data section held in memory
    bool m_loaded = false;
};

} // namespace uz
