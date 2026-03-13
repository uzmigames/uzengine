/// UzEngine Platform File I/O — Linux Implementation
///
/// Provides file and directory operations using POSIX APIs.
/// All paths are UTF-8 encoded (native on Linux).
/// Functions log errors via the FileSystem log category.
///
/// # Design
/// - Stateless free functions (no global state, no singleton)
/// - UTF-8 input paths used directly (no conversion needed on Linux)
/// - Parent directories created automatically on write operations
/// - All I/O errors logged with full path context

#pragma once

#ifdef __linux__

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// FileMode — Bitwise flags for file open operations
// ─────────────────────────────────────────────────────────────────────────────

/// Bitwise flags controlling how a file is opened.
///
/// Combine with bitwise OR for compound modes.
/// Example: `FileMode::Write | FileMode::Create | FileMode::Truncate`
enum class FileMode : uint32_t {
    Read     = 1 << 0,  ///< Open for reading.
    Write    = 1 << 1,  ///< Open for writing.
    Append   = 1 << 2,  ///< Seek to end before each write.
    Create   = 1 << 3,  ///< Create the file if it does not exist.
    Truncate = 1 << 4,  ///< Truncate existing file to zero length on open.
};

/// Bitwise OR for FileMode flags.
inline FileMode operator|(FileMode a, FileMode b) {
    return static_cast<FileMode>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Bitwise AND for FileMode flags.
inline FileMode operator&(FileMode a, FileMode b) {
    return static_cast<FileMode>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// Returns true if `flags` contains `flag`.
inline bool HasFlag(FileMode flags, FileMode flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// FileResult — Outcome of file operations
// ─────────────────────────────────────────────────────────────────────────────

/// Outcome of a file or directory operation.
///
/// Success indicates the operation completed without error.
/// All other values describe the specific failure reason.
enum class FileResult : uint8_t {
    Success,       ///< Operation completed successfully.
    NotFound,      ///< File or directory does not exist.
    AccessDenied,  ///< Insufficient permissions (read-only, locked, etc.).
    AlreadyExists, ///< Target already exists when exclusive creation was requested.
    IoError,       ///< General I/O failure (disk full, device removed, etc.).
};

/// Returns a human-readable string for a FileResult value.
///
/// Useful for log messages. Never returns nullptr.
const char* FileResultToString(FileResult result);

// ─────────────────────────────────────────────────────────────────────────────
// File read operations
// ─────────────────────────────────────────────────────────────────────────────

/// Reads an entire file into a byte vector.
///
/// Opens the file with `O_RDONLY`, reads all bytes via `read()` into a
/// contiguous vector, then closes the file descriptor.
///
/// # Arguments
/// * `path` — UTF-8 file path. May be absolute or relative to CWD.
///
/// # Returns
/// - Non-empty vector containing the file bytes on success.
/// - Empty vector if the file does not exist, is empty, or an I/O error occurs.
///
/// # Errors
/// Logs `FileSystem::Error` with the path and errno description on failure.
[[nodiscard]] std::vector<uint8_t> FileReadAll(const char* path);

/// Reads an entire file into a UTF-8 string.
///
/// Behaves like FileReadAll but returns the bytes as a `std::string`.
/// No BOM stripping or encoding conversion is performed.
///
/// # Arguments
/// * `path` — UTF-8 file path.
///
/// # Returns
/// - File contents as a string on success.
/// - Empty string if the file does not exist or an I/O error occurs.
///
/// # Errors
/// Logs `FileSystem::Error` with the path and errno description on failure.
[[nodiscard]] std::string FileReadAllText(const char* path);

// ─────────────────────────────────────────────────────────────────────────────
// File write operations
// ─────────────────────────────────────────────────────────────────────────────

/// Writes a byte buffer to a file, creating parent directories as needed.
///
/// Creates the file if it does not exist. Overwrites if it does.
/// Parent directories are created recursively before writing.
///
/// # Arguments
/// * `path` — UTF-8 destination file path.
/// * `data` — Pointer to bytes to write. May be nullptr if `size` is 0.
/// * `size` — Number of bytes to write.
///
/// # Returns
/// - `FileResult::Success` on successful write.
/// - `FileResult::AccessDenied` if the file is read-only or locked.
/// - `FileResult::IoError` on disk full, device error, or other I/O failure.
///
/// # Errors
/// Logs `FileSystem::Error` with the path, size, and errno description on failure.
FileResult FileWriteAll(const char* path, const void* data, size_t size);

/// Writes a null-terminated UTF-8 string to a file.
///
/// Convenience wrapper over FileWriteAll. Does not write a null terminator
/// to the file — only the string content (strlen bytes).
///
/// # Arguments
/// * `path` — UTF-8 destination file path.
/// * `text` — Null-terminated string to write. Must not be nullptr.
///
/// # Returns
/// Same as FileWriteAll.
FileResult FileWriteAllText(const char* path, const char* text);

// ─────────────────────────────────────────────────────────────────────────────
// File queries
// ─────────────────────────────────────────────────────────────────────────────

/// Checks whether a file or directory exists at the given path.
///
/// Uses `access(F_OK)` — does not open the file.
///
/// # Arguments
/// * `path` — UTF-8 file or directory path.
///
/// # Returns
/// - true if the path exists (file or directory).
/// - false if the path does not exist or an error occurred.
[[nodiscard]] bool FileExists(const char* path);

/// Returns the size of a file in bytes.
///
/// # Arguments
/// * `path` — UTF-8 file path.
///
/// # Returns
/// - File size in bytes (>= 0) on success.
/// - -1 if the file does not exist or an error occurred.
///
/// # Errors
/// Logs `FileSystem::Error` on failure.
[[nodiscard]] int64_t FileGetSize(const char* path);

/// Deletes a file from disk.
///
/// Does not delete directories — use DirectoryDelete for that.
///
/// # Arguments
/// * `path` — UTF-8 file path.
///
/// # Returns
/// - `FileResult::Success` if the file was deleted.
/// - `FileResult::NotFound` if the file does not exist.
/// - `FileResult::AccessDenied` if the file is read-only or locked.
/// - `FileResult::IoError` on other failures.
FileResult FileDelete(const char* path);

// ─────────────────────────────────────────────────────────────────────────────
// Directory operations
// ─────────────────────────────────────────────────────────────────────────────

/// Creates a directory and all missing parent directories.
///
/// If the directory already exists, returns `Success` (idempotent).
///
/// # Arguments
/// * `path` — UTF-8 directory path to create.
///
/// # Returns
/// - `FileResult::Success` if created or already exists.
/// - `FileResult::AccessDenied` if permissions prevent creation.
/// - `FileResult::IoError` on other failures.
FileResult DirectoryCreate(const char* path);

/// Checks whether a path is an existing directory.
///
/// # Arguments
/// * `path` — UTF-8 path to test.
///
/// # Returns
/// - true if the path exists and is a directory.
/// - false otherwise.
[[nodiscard]] bool IsDirectory(const char* path);

/// Entry returned by directory enumeration.
struct FileEntry {
    char path[512];     ///< Full path to the entry (UTF-8, null-terminated).
    int64_t size;       ///< File size in bytes. 0 for directories.
    bool isDirectory;   ///< True if this entry is a directory.
};

/// Enumerates files and directories in a directory matching a glob pattern.
///
/// Non-recursive: only lists direct children of `dir`.
/// Uses `opendir()` / `readdir()` with `fnmatch()` for pattern filtering.
/// Skips `.` and `..` entries.
///
/// # Arguments
/// * `dir` — UTF-8 path to the directory to enumerate.
/// * `pattern` — Glob pattern (e.g., "*.cpp", "*.uzshader", "*" for all).
///   Defaults to "*" (all entries).
///
/// # Returns
/// Vector of FileEntry structs. Empty if the directory does not exist,
/// is empty, or no entries match the pattern.
[[nodiscard]] std::vector<FileEntry> DirectoryEnumerate(const char* dir, const char* pattern = "*");

/// Enumerates files and directories recursively, matching a glob pattern.
///
/// Traverses all subdirectories depth-first. The pattern is matched
/// against the filename only (not the full path).
///
/// # Arguments
/// * `dir` — UTF-8 path to the root directory.
/// * `pattern` — Glob pattern matched against filenames.
///
/// # Returns
/// Vector of all matching entries across the entire subtree.
[[nodiscard]] std::vector<FileEntry> DirectoryEnumerateRecursive(const char* dir, const char* pattern = "*");

// ─────────────────────────────────────────────────────────────────────────────
// Platform path queries
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the user-local application data directory for UzEngine.
///
/// Resolves `$XDG_DATA_HOME/UzEngine/` or `$HOME/.local/share/UzEngine/`
/// if XDG_DATA_HOME is not set. Creates the directory if it does not exist.
///
/// # Returns
/// UTF-8 path string (e.g., "/home/user/.local/share/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetUserDataDir();

/// Returns the roaming application data directory for UzEngine.
///
/// Resolves `$XDG_CONFIG_HOME/UzEngine/` or `$HOME/.config/UzEngine/`
/// if XDG_CONFIG_HOME is not set. Creates the directory if it does not exist.
///
/// # Returns
/// UTF-8 path string (e.g., "/home/user/.config/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetAppDataDir();

/// Returns the temporary directory for UzEngine.
///
/// Resolves `$TMPDIR/UzEngine/` or `/tmp/UzEngine/` if TMPDIR is not set.
/// Creates the directory if it does not exist.
///
/// # Returns
/// UTF-8 path string (e.g., "/tmp/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetTempDir();

} // namespace uz

#endif // __linux__
