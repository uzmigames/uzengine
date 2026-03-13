# Platform — File I/O

> **Status**: Done (Phase 4.6.0)
> **Source**: `platform/windows/file_system.h`, `platform/windows/memory_mapped_file.h`
> **Depends on**: Win32 `CreateFileW`/`ReadFile`/`FindFileW`, Shell `SHGetFolderPathW`

## Purpose

The engine loads assets, shaders, configs, and scenes from disk. Instead of scattered `std::ifstream` calls, a unified file I/O system provides:
- Stateless, platform-agnostic free functions (no global state, no singleton)
- Automatic parent directory creation on writes
- Memory-mapped file RAII wrapper for zero-copy access
- UTF-8 path handling with Win32 wide-string conversion
- Comprehensive error logging with full path context

This enables hot-reload (file watching), directory enumeration, and efficient asset access via memory mapping.

## Architecture

```
uz::FileMode (bitwise flags)
    │
    ├── FileResult (success/not found/access denied/already exists/io error)
    │
    ├── File Read Operations
    │   ├── FileReadAll(path) → vector<uint8_t>
    │   └── FileReadAllText(path) → string
    │
    ├── File Write Operations
    │   ├── FileWriteAll(path, data, size) → FileResult
    │   └── FileWriteAllText(path, text) → FileResult
    │
    ├── File Queries
    │   ├── FileExists(path) → bool
    │   ├── FileGetSize(path) → int64_t
    │   └── FileDelete(path) → FileResult
    │
    ├── Directory Operations
    │   ├── DirectoryCreate(path) → FileResult
    │   ├── IsDirectory(path) → bool
    │   ├── DirectoryEnumerate(dir, pattern) → vector<FileEntry>
    │   └── DirectoryEnumerateRecursive(dir, pattern) → vector<FileEntry>
    │
    ├── Memory-Mapped Files
    │   └── MemoryMappedFile (RAII, move-only)
    │
    └── Platform Paths (auto-create on success)
        ├── GetUserDataDir() → C:/Users/Name/AppData/Local/UzEngine/
        ├── GetAppDataDir() → C:/Users/Name/AppData/Roaming/UzEngine/
        └── GetTempDir() → C:/Users/Name/AppData/Local/Temp/UzEngine/
```

## File Mode Flags

Bitwise flags controlling file open operations:

```cpp
/// Bitwise flags for file open operations.
/// Combine with bitwise OR for compound modes.
/// Example: FileMode::Write | FileMode::Create | FileMode::Truncate
enum class FileMode : uint32_t {
    Read     = 1 << 0,  // Open for reading
    Write    = 1 << 1,  // Open for writing
    Append   = 1 << 2,  // Seek to end before each write
    Create   = 1 << 3,  // Create the file if it does not exist
    Truncate = 1 << 4,  // Truncate existing file to zero length on open
};

/// Bitwise OR for FileMode flags
inline FileMode operator|(FileMode a, FileMode b);

/// Bitwise AND for FileMode flags
inline FileMode operator&(FileMode a, FileMode b);

/// Returns true if flags contains flag
inline bool HasFlag(FileMode flags, FileMode flag);
```

## File Result Codes

```cpp
/// Outcome of a file or directory operation.
enum class FileResult : uint8_t {
    Success,       // Operation completed successfully
    NotFound,      // File or directory does not exist
    AccessDenied,  // Insufficient permissions (read-only, locked, etc.)
    AlreadyExists, // Target already exists when exclusive creation was requested
    IoError,       // General I/O failure (disk full, device removed, etc.)
};

/// Returns human-readable string for FileResult value.
const char* FileResultToString(FileResult result);
```

## File Read Operations

### FileReadAll — Read entire file as bytes

```cpp
/// Reads an entire file into a byte vector.
///
/// Opens the file with GENERIC_READ and FILE_SHARE_READ, reads all bytes
/// into a contiguous vector, then closes the handle.
///
/// # Arguments
/// * path — UTF-8 file path. May be absolute or relative to CWD.
///
/// # Returns
/// - Non-empty vector containing the file bytes on success.
/// - Empty vector if the file does not exist, is empty, or an I/O error occurs.
///
/// # Errors
/// Logs FileSystem::Error with the path and Win32 error code on failure.
[[nodiscard]] std::vector<uint8_t> FileReadAll(const char* path);

// Usage
std::vector<uint8_t> shader_src = uz::FileReadAll("shaders/pbr.hlsl");
if (shader_src.empty()) {
    UZ_LOG(Asset, Error, "Failed to load shader");
    return;
}
```

### FileReadAllText — Read entire file as UTF-8 string

```cpp
/// Reads an entire file into a UTF-8 string.
///
/// Behaves like FileReadAll but returns the bytes as a std::string.
/// No BOM stripping or encoding conversion is performed.
///
/// # Arguments
/// * path — UTF-8 file path.
///
/// # Returns
/// - File contents as a string on success.
/// - Empty string if the file does not exist or an I/O error occurs.
///
/// # Errors
/// Logs FileSystem::Error with the path and Win32 error code on failure.
[[nodiscard]] std::string FileReadAllText(const char* path);

// Usage
std::string config = uz::FileReadAllText("config/engine.ini");
if (!config.empty()) {
    ParseConfig(config);
}
```

## File Write Operations

### FileWriteAll — Write byte buffer to file

```cpp
/// Writes a byte buffer to a file, creating parent directories as needed.
///
/// Creates the file if it does not exist. Overwrites if it does.
/// Parent directories are created recursively before writing.
///
/// # Arguments
/// * path — UTF-8 destination file path.
/// * data — Pointer to bytes to write. May be nullptr if size is 0.
/// * size — Number of bytes to write.
///
/// # Returns
/// - FileResult::Success on successful write.
/// - FileResult::AccessDenied if the file is read-only or locked.
/// - FileResult::IoError on disk full, device error, or other I/O failure.
///
/// # Errors
/// Logs FileSystem::Error with the path, size, and Win32 error code on failure.
FileResult FileWriteAll(const char* path, const void* data, size_t size);

// Usage
std::vector<uint8_t> compiled = CompileShader(source);
if (uz::FileWriteAll("cache/pbr.spirv", compiled.data(), compiled.size()) == uz::FileResult::Success) {
    UZ_LOG(Asset, Info, "Shader compiled to cache");
}
```

### FileWriteAllText — Write UTF-8 string to file

```cpp
/// Writes a null-terminated UTF-8 string to a file.
///
/// Convenience wrapper over FileWriteAll. Does not write a null terminator
/// to the file — only the string content (strlen bytes).
///
/// # Arguments
/// * path — UTF-8 destination file path.
/// * text — Null-terminated string to write. Must not be nullptr.
///
/// # Returns
/// Same as FileWriteAll.
FileResult FileWriteAllText(const char* path, const char* text);

// Usage
std::string json = SerializeScene(scene);
uz::FileWriteAllText("scenes/main.uzscene", json.c_str());
```

## File Queries

### FileExists — Check if file or directory exists

```cpp
/// Checks whether a file or directory exists at the given path.
///
/// Uses GetFileAttributesW — does not open the file.
///
/// # Arguments
/// * path — UTF-8 file or directory path.
///
/// # Returns
/// - true if the path exists (file or directory).
/// - false if the path does not exist or an error occurred.
[[nodiscard]] bool FileExists(const char* path);

// Usage
if (uz::FileExists("config/user.ini")) {
    LoadUserConfig();
}
```

### FileGetSize — Get file size in bytes

```cpp
/// Returns the size of a file in bytes.
///
/// # Arguments
/// * path — UTF-8 file path.
///
/// # Returns
/// - File size in bytes (>= 0) on success.
/// - -1 if the file does not exist or an error occurred.
///
/// # Errors
/// Logs FileSystem::Error on failure.
[[nodiscard]] int64_t FileGetSize(const char* path);

// Usage
int64_t size = uz::FileGetSize("assets/texture.uztexture");
if (size > 0) {
    UZ_LOG(Asset, Info, "Texture size: {} bytes", size);
}
```

### FileDelete — Delete a file

```cpp
/// Deletes a file from disk.
///
/// Does not delete directories — use DirectoryCreate for that.
///
/// # Arguments
/// * path — UTF-8 file path.
///
/// # Returns
/// - FileResult::Success if the file was deleted.
/// - FileResult::NotFound if the file does not exist.
/// - FileResult::AccessDenied if the file is read-only or locked.
/// - FileResult::IoError on other failures.
FileResult FileDelete(const char* path);

// Usage
if (uz::FileDelete("cache/stale_shader.spirv") != uz::FileResult::Success) {
    UZ_LOG(Asset, Warning, "Failed to delete stale cache");
}
```

## Directory Operations

### DirectoryCreate — Create directory recursively

```cpp
/// Creates a directory and all missing parent directories.
///
/// If the directory already exists, returns Success (idempotent).
///
/// # Arguments
/// * path — UTF-8 directory path to create.
///
/// # Returns
/// - FileResult::Success if created or already exists.
/// - FileResult::AccessDenied if permissions prevent creation.
/// - FileResult::IoError on other failures.
FileResult DirectoryCreate(const char* path);

// Usage
uz::DirectoryCreate("cache/shaders/compiled");
```

### IsDirectory — Check if path is a directory

```cpp
/// Checks whether a path is an existing directory.
///
/// # Arguments
/// * path — UTF-8 path to test.
///
/// # Returns
/// - true if the path exists and is a directory.
/// - false otherwise.
[[nodiscard]] bool IsDirectory(const char* path);

// Usage
if (uz::IsDirectory("assets/meshes")) {
    // Enumerate mesh directory
}
```

### FileEntry — Directory enumeration result

```cpp
/// Entry returned by directory enumeration.
struct FileEntry {
    char path[512];     // Full path to the entry (UTF-8, null-terminated)
    int64_t size;       // File size in bytes. 0 for directories.
    bool isDirectory;   // True if this entry is a directory
};
```

### DirectoryEnumerate — Non-recursive directory enumeration

```cpp
/// Enumerates files and directories in a directory matching a glob pattern.
///
/// Non-recursive: only lists direct children of dir.
/// Uses FindFirstFileW / FindNextFileW with the pattern.
/// Skips . and .. entries.
///
/// # Arguments
/// * dir — UTF-8 path to the directory to enumerate.
/// * pattern — Glob pattern (e.g., "*.cpp", "*.uzshader", "*" for all).
///   Defaults to "*" (all entries).
///
/// # Returns
/// Vector of FileEntry structs. Empty if the directory does not exist,
/// is empty, or no entries match the pattern.
[[nodiscard]] std::vector<FileEntry> DirectoryEnumerate(const char* dir, const char* pattern = "*");

// Usage
auto entries = uz::DirectoryEnumerate("shaders/", "*.hlsl");
for (const auto& entry : entries) {
    if (!entry.isDirectory) {
        CompileShader(entry.path);
    }
}
```

### DirectoryEnumerateRecursive — Recursive directory enumeration

```cpp
/// Enumerates files and directories recursively, matching a glob pattern.
///
/// Traverses all subdirectories depth-first. The pattern is matched
/// against the filename only (not the full path).
///
/// # Arguments
/// * dir — UTF-8 path to the root directory.
/// * pattern — Glob pattern matched against filenames.
///
/// # Returns
/// Vector of all matching entries across the entire subtree.
[[nodiscard]] std::vector<FileEntry> DirectoryEnumerateRecursive(const char* dir, const char* pattern = "*");

// Usage
auto all_shaders = uz::DirectoryEnumerateRecursive("shaders/", "*.hlsl");
for (const auto& shader : all_shaders) {
    UZ_LOG(Asset, Info, "Found shader: {}", shader.path);
}
```

## Memory-Mapped Files

For large files (textures, meshes, VFS archives), memory-mapping is faster than buffered I/O. The `MemoryMappedFile` class provides RAII semantics with automatic cleanup on destruction.

### MemoryMappedFile — RAII zero-copy file access

```cpp
/// Memory-mapped file for zero-copy read access.
///
/// Maps an entire file into the process address space using Win32
/// CreateFileMappingW + MapViewOfFile. The file remains mapped
/// until Close() is called or the object is destroyed.
///
/// Move-only: ownership of the OS handles transfers on move.
/// Not thread-safe: concurrent Open/Close calls require external sync.
/// Data pointer is safe to read from multiple threads after Open.
class MemoryMappedFile {
public:
    /// Constructs an unmapped file. IsOpen() returns false.
    MemoryMappedFile() = default;

    /// Unmaps and closes the file if currently open.
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    /// Move constructor — transfers ownership of handles from other.
    MemoryMappedFile(MemoryMappedFile&& other) noexcept;

    /// Move assignment — closes current mapping, takes ownership from other.
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    /// Opens and maps a file for reading.
    ///
    /// # Arguments
    /// * path — UTF-8 file path.
    ///
    /// # Returns
    /// - true if the file was successfully mapped. Data() and Size() are valid.
    /// - false if the file could not be opened or mapped (logged).
    ///
    /// # Errors
    /// Logs FileSystem::Error with path and Win32 error on failure.
    /// If already open, closes the previous mapping first.
    [[nodiscard]] bool Open(const char* path);

    /// Unmaps and closes the file.
    ///
    /// Safe to call even if not open (no-op). After this call,
    /// Data() returns nullptr and Size() returns 0.
    void Close();

    /// Returns pointer to the mapped file data.
    ///
    /// # Returns
    /// - Non-null pointer to the first byte of the file if open.
    /// - nullptr if the file is not open.
    [[nodiscard]] const void* Data() const { return m_data; }

    /// Returns the size of the mapped file in bytes.
    [[nodiscard]] size_t Size() const { return m_size; }

    /// Returns true if a file is currently mapped.
    [[nodiscard]] bool IsOpen() const { return m_data != nullptr; }

private:
    void* m_fileHandle = nullptr;    // Win32 HANDLE from CreateFileW
    void* m_mappingHandle = nullptr; // Win32 HANDLE from CreateFileMappingW
    void* m_data = nullptr;          // Base address from MapViewOfFile
    size_t m_size = 0;               // File size in bytes
};
```

### Usage Pattern

```cpp
// Stack-allocated, auto-cleaned up on scope exit
uz::MemoryMappedFile mmap;
if (mmap.Open("assets/large_texture.uztexture")) {
    // Zero-copy access to header
    const TextureHeader* header = static_cast<const TextureHeader*>(mmap.Data());
    UZ_LOG(Asset, Info, "Texture: {}x{}", header->width, header->height);

    // Read pixel data without copying
    const uint8_t* pixels = static_cast<const uint8_t*>(mmap.Data()) + sizeof(TextureHeader);
    size_t pixel_size = mmap.Size() - sizeof(TextureHeader);
    ProcessPixels(pixels, pixel_size);
}
// File automatically unmapped when mmap goes out of scope
```

## Platform Paths

Helper functions for standard directories. Each function resolves the appropriate system path using `SHGetFolderPathW`, creates the directory if it doesn't exist, and returns a UTF-8 path.

### GetUserDataDir — User-local application data directory

```cpp
/// Returns the user-local application data directory for UzEngine.
///
/// Resolves %LOCALAPPDATA%/UzEngine/. Creates the directory if it
/// does not exist. Uses SHGetFolderPathW(CSIDL_LOCAL_APPDATA).
///
/// # Returns
/// UTF-8 path string (e.g., "C:/Users/Name/AppData/Local/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetUserDataDir();

// Usage
std::string user_data = uz::GetUserDataDir();
std::string config_path = user_data + "engine.ini";
std::string settings = uz::FileReadAllText(config_path.c_str());
```

### GetAppDataDir — Roaming application data directory

```cpp
/// Returns the roaming application data directory for UzEngine.
///
/// Resolves %APPDATA%/UzEngine/. Creates the directory if it
/// does not exist. Uses SHGetFolderPathW(CSIDL_APPDATA).
///
/// # Returns
/// UTF-8 path string (e.g., "C:/Users/Name/AppData/Roaming/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetAppDataDir();

// Usage
std::string app_data = uz::GetAppDataDir();
// For cloud-synced settings (OneDrive/Dropbox)
SaveCloudSettings(app_data);
```

### GetTempDir — Temporary directory for scratch files

```cpp
/// Returns the temporary directory for UzEngine.
///
/// Resolves %TEMP%/UzEngine/. Creates the directory if it
/// does not exist. Uses GetTempPathW.
///
/// # Returns
/// UTF-8 path string (e.g., "C:/Users/Name/AppData/Local/Temp/UzEngine/").
/// Empty string on failure (logged).
[[nodiscard]] std::string GetTempDir();

// Usage
std::string temp_dir = uz::GetTempDir();
std::string temp_file = temp_dir + "shader_compile_log.txt";
uz::FileWriteAllText(temp_file.c_str(), compile_output.c_str());
```

## UTF-8 Path Handling

All functions accept UTF-8 paths and handle Win32 wide-string conversion internally. Path separators can be forward slashes (`/`) or backslashes (`\`) — the Win32 API accepts both.

```cpp
// All of these work identically:
uz::FileReadAll("shaders/pbr.hlsl");
uz::FileReadAll("shaders\\pbr.hlsl");
uz::FileReadAll("shaders\\pbr.hlsl");  // Mixed separators also OK
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| File read | `FFileHelper::LoadFileToArray()` | `FileAccess::get_file_as_bytes()` | `uz::FileReadAll()` |
| File write | `FFileHelper::SaveArrayToFile()` | `FileAccess::get_file_as_text()` | `uz::FileWriteAll()` |
| File exists | `FPaths::FileExists()` | `FileAccess::file_exists()` | `uz::FileExists()` |
| File size | `IFileManager::FileSize()` | `FileAccess::get_file_size()` | `uz::FileGetSize()` |
| File delete | `IPlatformFile::DeleteFile()` | `DirAccess::remove()` | `uz::FileDelete()` |
| Memory-map | `FMemoryMappedFile` | FileAccess + caching | `uz::MemoryMappedFile` |
| Directory create | `FPlatformFileManager::CreateDirectory()` | `DirAccess::make_dir()` | `uz::DirectoryCreate()` |
| Directory enum | `FFileManagerGeneric::IterateDirectory` | `DirAccess::list_dir_begin()` | `uz::DirectoryEnumerate()` |
| Recursive enum | Manual recursion | Manual recursion | `uz::DirectoryEnumerateRecursive()` |
| Standard paths | `FPaths::UserDir()`, `EngineSavedDir()` | `ProjectSettings.res_path` | `uz::GetUserDataDir()`, `uz::GetAppDataDir()`, `uz::GetTempDir()` |

## Rules

1. **NEVER use `std::ifstream`/`std::ofstream` in engine code** — use free functions only
2. **ALWAYS handle empty returns** — don't assume `FileReadAll()` succeeds, check size
3. **ALWAYS memory-map large files (>10MB)** — faster and uses OS paging
4. **NEVER read shader sources at runtime in shipping builds** — should be baked into .uzpak
5. **ALWAYS use platform path helpers** — `GetUserDataDir()` not hardcoded `%APPDATA%`
6. **UTF-8 paths always** — all functions accept UTF-8, convert to wide-string internally
