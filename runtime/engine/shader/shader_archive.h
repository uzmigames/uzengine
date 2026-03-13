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

/// A single entry to be written into a shader archive via `ShaderArchive::Build()`.
///
/// The hash string serves as the lookup key at runtime. It is typically the
/// xxHash64 identity hash formatted as a 16-character lowercase hex string.
struct ShaderArchiveEntry {
    std::string hash;                ///< Content hash key (xxHash64 hex string, max `kHashFieldSize` chars)
    std::vector<uint8_t> bytecode;   ///< Compiled shader bytecode (DXIL, SPIRV, etc.)
};

/// Binary shader archive (`.uzpak`) for shipping builds.
///
/// At build time, `Build()` writes all compiled shader variants into a single
/// binary file. At runtime, `Load()` reads the entire archive into memory
/// for O(1) hash-based lookup with no disk I/O per shader load.
///
/// # Thread Safety
/// `Lookup()`, `Contains()`, `EntryCount()`, `GetHashes()`, and `IsLoaded()`
/// are safe to call from multiple threads after `Load()` completes.
/// `Load()`, `Unload()`, and `Build()` must not be called concurrently.
///
/// # Lifetime
/// Standalone. Holds all bytecode in memory after `Load()`. Call `Unload()`
/// to free memory, or let the destructor handle it.
///
/// # Examples
/// ```cpp
/// // Build time:
/// std::vector<ShaderArchiveEntry> entries = collectCompiledShaders();
/// ShaderArchive::Build("shaders.uzpak", entries);
///
/// // Runtime:
/// ShaderArchive archive;
/// archive.Load("shaders.uzpak");
/// auto bytecode = archive.Lookup("a1b2c3d4e5f6g7h8");
/// ```
class ShaderArchive {
public:
    /// Builds an archive from a list of entries and writes it to disk.
    ///
    /// # Arguments
    /// * `outputPath` — Filesystem path for the output `.uzpak` file.
    /// * `entries` — Shader entries to include. Each must have a non-empty hash
    ///   that fits within `kHashFieldSize` characters.
    ///
    /// # Returns
    /// `true` on success. `false` if any entry has an invalid hash or file I/O fails.
    [[nodiscard]] static bool Build(const std::filesystem::path& outputPath,
                                    const std::vector<ShaderArchiveEntry>& entries);

    /// Loads an archive from disk into memory.
    ///
    /// Validates the magic number (`kShaderArchiveMagic`) and format version
    /// (`kShaderArchiveVersion`). Replaces any previously loaded data.
    ///
    /// # Arguments
    /// * `archivePath` — Path to the `.uzpak` archive file.
    ///
    /// # Returns
    /// `true` on success. `false` if the file cannot be read, magic/version
    /// mismatch, or the entry table is corrupted.
    [[nodiscard]] bool Load(const std::filesystem::path& archivePath);

    /// Unloads the archive, freeing all bytecode memory and clearing the index.
    void Unload();

    /// Looks up bytecode by content hash.
    ///
    /// # Arguments
    /// * `hash` — The xxHash64 hex string key.
    ///
    /// # Returns
    /// A copy of the bytecode, or an empty vector if the hash is not found.
    [[nodiscard]] std::vector<uint8_t> Lookup(const std::string& hash) const;

    /// Checks if a hash exists in the loaded archive.
    ///
    /// # Arguments
    /// * `hash` — The xxHash64 hex string key to check.
    [[nodiscard]] bool Contains(const std::string& hash) const;

    /// Returns the number of entries in the loaded archive.
    [[nodiscard]] size_t EntryCount() const;

    /// Returns all hash keys in the loaded archive (unordered).
    [[nodiscard]] std::vector<std::string> GetHashes() const;

    /// Returns true if an archive is currently loaded in memory.
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
