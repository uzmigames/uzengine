/// UzEngine Platform File I/O — Linux Implementation
///
/// POSIX-based file operations with UTF-8 path input (native on Linux).
/// All errors logged via the FileSystem log category with errno context.

#ifdef __linux__

#include "file_system.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

UZ_DEFINE_LOG_CATEGORY(FileSystem, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — errno mapping and path utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Maps errno to the closest FileResult.
static FileResult ErrnoToResult(int err) {
    switch (err) {
    case ENOENT:
    case ENOTDIR:
        return FileResult::NotFound;
    case EACCES:
    case EPERM:
    case EROFS:
        return FileResult::AccessDenied;
    case EEXIST:
        return FileResult::AlreadyExists;
    default:
        return FileResult::IoError;
    }
}

/// Ensures all parent directories for a given file path exist.
///
/// Walks the path component by component and calls mkdir(2) for each
/// missing segment. Equivalent to `mkdir -p $(dirname path)`.
static FileResult EnsureParentDirectories(const char* path) {
    // Find last separator — no parent means root or CWD, nothing to create.
    const char* lastSep = std::strrchr(path, '/');
    if (!lastSep || lastSep == path) {
        return FileResult::Success;
    }

    // Build a mutable copy of just the parent portion.
    std::string parent(path, static_cast<size_t>(lastSep - path));

    // Walk the path left-to-right, mkdir each component.
    for (size_t i = 1; i <= parent.size(); ++i) {
        if (i == parent.size() || parent[i] == '/') {
            parent[i] = '\0'; // Temporarily terminate
            if (mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) {
                const int err = errno;
                UZ_LOG(FileSystem, Error, "EnsureParentDirectories: mkdir failed for '{}', errno={}", parent.c_str(), err);
                parent[i] = '/';
                return ErrnoToResult(err);
            }
            if (i < parent.size()) {
                parent[i] = '/'; // Restore separator
            }
        }
    }

    return FileResult::Success;
}

// ─────────────────────────────────────────────────────────────────────────────
// FileResult helpers
// ─────────────────────────────────────────────────────────────────────────────

const char* FileResultToString(FileResult result) {
    switch (result) {
    case FileResult::Success:       return "Success";
    case FileResult::NotFound:      return "NotFound";
    case FileResult::AccessDenied:  return "AccessDenied";
    case FileResult::AlreadyExists: return "AlreadyExists";
    case FileResult::IoError:       return "IoError";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// File read operations
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> FileReadAll(const char* path) {
    UZ_PROFILE_FUNCTION();

    if (!path || path[0] == '\0') {
        UZ_LOG(FileSystem, Error, "FileReadAll: empty or invalid path");
        return {};
    }

    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "FileReadAll: failed to open '{}', errno={}", path, err);
        return {};
    }

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "FileReadAll: fstat failed for '{}', errno={}", path, err);
        close(fd);
        return {};
    }

    if (st.st_size == 0) {
        close(fd);
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(st.st_size));
    size_t totalRead = 0;
    while (totalRead < buffer.size()) {
        const ssize_t n = read(fd, buffer.data() + totalRead, buffer.size() - totalRead);
        if (n < 0) {
            if (errno == EINTR) continue; // Interrupted by signal — retry
            const int err = errno;
            UZ_LOG(FileSystem, Error, "FileReadAll: read failed for '{}', errno={}", path, err);
            close(fd);
            return {};
        }
        if (n == 0) break; // EOF before expected — sparse file or race; truncate
        totalRead += static_cast<size_t>(n);
    }

    close(fd);
    buffer.resize(totalRead);
    return buffer;
}

std::string FileReadAllText(const char* path) {
    auto bytes = FileReadAll(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// File write operations
// ─────────────────────────────────────────────────────────────────────────────

FileResult FileWriteAll(const char* path, const void* data, size_t size) {
    UZ_PROFILE_FUNCTION();

    if (!path || path[0] == '\0') {
        UZ_LOG(FileSystem, Error, "FileWriteAll: empty or invalid path");
        return FileResult::IoError;
    }

    // Create parent directories before opening the file.
    const FileResult parentResult = EnsureParentDirectories(path);
    if (parentResult != FileResult::Success) {
        UZ_LOG(FileSystem, Error, "FileWriteAll: failed to create parent directories for '{}'", path);
        return parentResult;
    }

    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd == -1) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "FileWriteAll: failed to create '{}', errno={}", path, err);
        return ErrnoToResult(err);
    }

    if (size > 0 && data != nullptr) {
        size_t totalWritten = 0;
        while (totalWritten < size) {
            const ssize_t n = write(
                fd,
                static_cast<const uint8_t*>(data) + totalWritten,
                size - totalWritten
            );
            if (n < 0) {
                if (errno == EINTR) continue; // Signal interrupted write — retry
                const int err = errno;
                UZ_LOG(FileSystem, Error, "FileWriteAll: write failed for '{}', size={}, errno={}", path, size, err);
                close(fd);
                return ErrnoToResult(err);
            }
            totalWritten += static_cast<size_t>(n);
        }
    }

    close(fd);
    return FileResult::Success;
}

FileResult FileWriteAllText(const char* path, const char* text) {
    if (!text) {
        UZ_LOG(FileSystem, Error, "FileWriteAllText: null text pointer for '{}'", path);
        return FileResult::IoError;
    }
    return FileWriteAll(path, text, std::strlen(text));
}

// ─────────────────────────────────────────────────────────────────────────────
// File queries
// ─────────────────────────────────────────────────────────────────────────────

bool FileExists(const char* path) {
    if (!path || path[0] == '\0') return false;
    return access(path, F_OK) == 0;
}

int64_t FileGetSize(const char* path) {
    UZ_PROFILE_FUNCTION();

    if (!path || path[0] == '\0') return -1;

    struct stat st{};
    if (stat(path, &st) != 0) {
        const int err = errno;
        UZ_LOG(FileSystem, Error, "FileGetSize: stat failed for '{}', errno={}", path, err);
        return -1;
    }

    return static_cast<int64_t>(st.st_size);
}

FileResult FileDelete(const char* path) {
    UZ_PROFILE_FUNCTION();

    if (!path || path[0] == '\0') {
        UZ_LOG(FileSystem, Error, "FileDelete: empty or invalid path");
        return FileResult::IoError;
    }

    if (unlink(path) == 0) {
        return FileResult::Success;
    }

    const int err = errno;
    UZ_LOG(FileSystem, Error, "FileDelete: unlink failed for '{}', errno={}", path, err);
    return ErrnoToResult(err);
}

// ─────────────────────────────────────────────────────────────────────────────
// Directory operations
// ─────────────────────────────────────────────────────────────────────────────

FileResult DirectoryCreate(const char* path) {
    UZ_PROFILE_FUNCTION();

    if (!path || path[0] == '\0') {
        UZ_LOG(FileSystem, Error, "DirectoryCreate: empty or invalid path");
        return FileResult::IoError;
    }

    // Check if already exists
    struct stat st{};
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return FileResult::Success;
        // Exists but is not a directory
        UZ_LOG(FileSystem, Error, "DirectoryCreate: path '{}' exists but is not a directory", path);
        return FileResult::IoError;
    }

    // Build a mutable copy for component-by-component mkdir.
    std::string mutable_path(path);

    for (size_t i = 1; i <= mutable_path.size(); ++i) {
        if (i == mutable_path.size() || mutable_path[i] == '/') {
            mutable_path[i] = '\0';
            if (mkdir(mutable_path.c_str(), 0755) != 0 && errno != EEXIST) {
                const int err = errno;
                UZ_LOG(FileSystem, Error, "DirectoryCreate: mkdir failed for '{}', errno={}", mutable_path.c_str(), err);
                if (i < mutable_path.size()) mutable_path[i] = '/';
                return ErrnoToResult(err);
            }
            if (i < mutable_path.size()) mutable_path[i] = '/';
        }
    }

    return FileResult::Success;
}

bool IsDirectory(const char* path) {
    if (!path || path[0] == '\0') return false;
    struct stat st{};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// ─────────────────────────────────────────────────────────────────────────────
// Directory enumeration
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a FileEntry from a dirent and its parent directory path.
static FileEntry MakeFileEntry(const char* parentDir, const char* name) {
    FileEntry entry{};

    // Build full path
    std::string fullPath = parentDir;
    if (!fullPath.empty() && fullPath.back() != '/') {
        fullPath += '/';
    }
    fullPath += name;

    const size_t copyLen = std::min(fullPath.size(), sizeof(entry.path) - 1);
    std::memcpy(entry.path, fullPath.c_str(), copyLen);
    entry.path[copyLen] = '\0';

    // stat to determine type and size
    struct stat st{};
    if (stat(fullPath.c_str(), &st) == 0) {
        entry.isDirectory = S_ISDIR(st.st_mode);
        entry.size = entry.isDirectory ? 0 : static_cast<int64_t>(st.st_size);
    } else {
        entry.isDirectory = false;
        entry.size = 0;
    }

    return entry;
}

/// Returns true if name is "." or ".."
static bool IsDotEntry(const char* name) {
    return (name[0] == '.' && name[1] == '\0') ||
           (name[0] == '.' && name[1] == '.' && name[2] == '\0');
}

std::vector<FileEntry> DirectoryEnumerate(const char* dir, const char* pattern) {
    UZ_PROFILE_FUNCTION();

    std::vector<FileEntry> results;

    if (!dir || dir[0] == '\0') return results;

    DIR* dh = opendir(dir);
    if (!dh) {
        // Silently return empty for non-existent directories (matches Windows behavior)
        return results;
    }

    struct dirent* entry;
    while ((entry = readdir(dh)) != nullptr) {
        if (IsDotEntry(entry->d_name)) continue;

        // Match only the filename portion against the glob pattern
        if (fnmatch(pattern, entry->d_name, FNM_NOESCAPE) != 0) continue;

        results.push_back(MakeFileEntry(dir, entry->d_name));
    }

    closedir(dh);
    return results;
}

/// Recursive helper for DirectoryEnumerateRecursive.
static void EnumerateRecursiveImpl(
    const std::string& dir,
    const char* pattern,
    std::vector<FileEntry>& results
) {
    DIR* dh = opendir(dir.c_str());
    if (!dh) return;

    std::vector<std::string> subdirs;

    struct dirent* de;
    while ((de = readdir(dh)) != nullptr) {
        if (IsDotEntry(de->d_name)) continue;

        // Determine if this entry is a directory by stat
        std::string fullPath = dir;
        if (!fullPath.empty() && fullPath.back() != '/') fullPath += '/';
        fullPath += de->d_name;

        struct stat st{};
        if (stat(fullPath.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Always recurse into subdirectories
            subdirs.push_back(fullPath);
            // Also emit directory entry if it matches pattern
            if (fnmatch(pattern, de->d_name, FNM_NOESCAPE) == 0) {
                FileEntry entry{};
                const size_t copyLen = std::min(fullPath.size(), sizeof(entry.path) - 1);
                std::memcpy(entry.path, fullPath.c_str(), copyLen);
                entry.path[copyLen] = '\0';
                entry.isDirectory = true;
                entry.size = 0;
                results.push_back(entry);
            }
        } else {
            // File — emit if it matches pattern
            if (fnmatch(pattern, de->d_name, FNM_NOESCAPE) == 0) {
                FileEntry entry{};
                const size_t copyLen = std::min(fullPath.size(), sizeof(entry.path) - 1);
                std::memcpy(entry.path, fullPath.c_str(), copyLen);
                entry.path[copyLen] = '\0';
                entry.isDirectory = false;
                entry.size = static_cast<int64_t>(st.st_size);
                results.push_back(entry);
            }
        }
    }

    closedir(dh);

    // Recurse after closing the directory handle to avoid descriptor exhaustion
    for (const auto& subdir : subdirs) {
        EnumerateRecursiveImpl(subdir, pattern, results);
    }
}

std::vector<FileEntry> DirectoryEnumerateRecursive(const char* dir, const char* pattern) {
    UZ_PROFILE_FUNCTION();

    std::vector<FileEntry> results;
    EnumerateRecursiveImpl(dir, pattern, results);
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform path queries
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a platform data dir from an XDG env var or fallback default,
/// appends "/UzEngine/", creates it, and returns the path.
///
/// Falls back to `$HOME/<fallback>/UzEngine/` when the env var is absent.
static std::string GetXdgDir(const char* xdgEnvVar, const char* homeFallback) {
    // Prefer explicit XDG env var
    const char* xdgDir = std::getenv(xdgEnvVar);
    std::string base;

    if (xdgDir && xdgDir[0] != '\0') {
        base = xdgDir;
    } else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') {
            UZ_LOG(FileSystem, Error, "GetXdgDir: HOME not set and {} not set", xdgEnvVar);
            return {};
        }
        base = home;
        base += '/';
        base += homeFallback;
    }

    // Normalize trailing slash
    if (!base.empty() && base.back() != '/') base += '/';
    base += "UzEngine/";

    // Create the directory if needed (idempotent)
    DirectoryCreate(base.c_str());

    return base;
}

std::string GetUserDataDir() {
    return GetXdgDir("XDG_DATA_HOME", ".local/share");
}

std::string GetAppDataDir() {
    return GetXdgDir("XDG_CONFIG_HOME", ".config");
}

std::string GetTempDir() {
    const char* tmpDir = std::getenv("TMPDIR");
    std::string base;

    if (tmpDir && tmpDir[0] != '\0') {
        base = tmpDir;
    } else {
        base = "/tmp";
    }

    if (!base.empty() && base.back() != '/') base += '/';
    base += "UzEngine/";

    DirectoryCreate(base.c_str());

    return base;
}

} // namespace uz

#endif // __linux__
