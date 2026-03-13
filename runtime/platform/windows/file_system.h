/// UzEngine Platform File I/O — Windows Implementation
///
/// Provides file and directory operations using Win32 APIs.
/// All paths are UTF-8 encoded and converted to wide strings internally.
/// Functions log errors via the FileSystem log category.
///
/// # Design
/// - Stateless free functions (no global state, no singleton)
/// - UTF-8 input paths, Win32 wide-string APIs internally
/// - Parent directories created automatically on write operations
/// - All I/O errors logged with full path context

#pragma once

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
/// Opens the file with `GENERIC_READ` and `FILE_SHARE_READ`, reads all bytes
/// into a contiguous vector, then closes the handle.
///
/// # Arguments
/// * `path` — UTF-8 file path. May be absolute or relative to CWD.
///
/// # Returns
/// - Non-empty vector containing the file bytes on success.
/// - Empty vector if the file does not exist, is empty, or an I/O error occurs.
///
/// # Errors
/// Logs `FileSystem::Error` with the path and Win32 error code on failure.
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
/// Logs `FileSystem::Error` with the path and Win32 error code on failure.
[[nodiscard]] std::string FileReadAllText(const char* path);

// ─────────────────────────────────────────────────────────────────────────────
// Asynchronous file read
// ─────────────────────────────────────────────────────────────────────────────

/// Handle to a pending asynchronous file read operation.
///
/// Created by `FileReadAsync()`. The caller polls or waits via
/// `FileReadAsyncComplete()`. The handle must eventually be completed
/// or cancelled — failing to do so leaks the OS file handle.
struct AsyncReadHandle {
    void* internal = nullptr;  ///< Opaque pointer to OS resources. nullptr = invalid.

    /// Returns true if this handle refers to a pending operation.
    [[nodiscard]] bool IsValid() const { return internal != nullptr; }
};

/// Starts an asynchronous file read using Win32 OVERLAPPED I/O.
///
/// Opens the file with `FILE_FLAG_OVERLAPPED`, issues `ReadFile` with an
/// OVERLAPPED structure, and returns immediately. The caller completes
/// the operation via `FileReadAsyncComplete()`.
///
/// # Arguments
/// * `path` — UTF-8 file path.
///
/// # Returns
/// A valid `AsyncReadHandle` on success. An invalid handle (`IsValid() == false`)
/// if the file does not exist or cannot be opened.
///
/// # Errors
/// Logs `FileSystem::Error` on failure.
[[nodiscard]] AsyncReadHandle FileReadAsync(const char* path);

/// Result of an asynchronous file read.
struct AsyncReadResult {
    FileResult status = FileResult::IoError;  ///< Outcome of the operation.
    std::vector<uint8_t> data;                ///< File contents on success.
};

/// Waits for an asynchronous file read to complete and returns the data.
///
/// Blocks until the I/O completes. After this call, the handle is invalidated
/// (internal resources freed). Calling with an invalid handle returns IoError.
///
/// # Arguments
/// * `handle` — Handle from `FileReadAsync()`. Invalidated after this call.
///
/// # Returns
/// `AsyncReadResult` with status and data. On success, `status` is
/// `FileResult::Success` and `data` contains the file bytes. On failure,
/// `status` describes the error and `data` is empty.
AsyncReadResult FileReadAsyncComplete(AsyncReadHandle& handle);

/// Cancels a pending async read and frees all associated OS resources.
///
/// Issues `CancelIoEx` on the pending operation, waits for the OS to confirm
/// cancellation, then closes handles and frees memory. Safe to call on an
/// invalid handle (no-op). After this call `handle.IsValid()` returns false.
///
/// # Arguments
/// * `handle` — Handle from `FileReadAsync()`. Invalidated after this call.
void FileReadAsyncCancel(AsyncReadHandle& handle);

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
/// Logs `FileSystem::Error` with the path, size, and Win32 error code on failure.
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
/// Uses `GetFileAttributesW` — does not open the file.
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
/// Uses `FindFirstFileW` / `FindNextFileW` with the pattern.
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
/// Resolves `%LOCALAPPDATA%/UzEngine/`. Creates the directory if it
/// does not exist. Uses `SHGetFolderPathW(CSIDL_LOCAL_APPDATA)`.
///
/// # Returns
/// UTF-8 path string (e.g., "C:/Users/Name/AppData/Local/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetUserDataDir();

/// Returns the roaming application data directory for UzEngine.
///
/// Resolves `%APPDATA%/UzEngine/`. Creates the directory if it
/// does not exist. Uses `SHGetFolderPathW(CSIDL_APPDATA)`.
///
/// # Returns
/// UTF-8 path string (e.g., "C:/Users/Name/AppData/Roaming/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetAppDataDir();

/// Returns the temporary directory for UzEngine.
///
/// Resolves `%TEMP%/UzEngine/`. Creates the directory if it
/// does not exist. Uses `GetTempPathW`.
///
/// # Returns
/// UTF-8 path string (e.g., "C:/Users/Name/AppData/Local/Temp/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetTempDir();

} // namespace uz
