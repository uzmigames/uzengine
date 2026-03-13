/// UzEngine Platform File I/O — Windows Implementation
///
/// Win32-based file operations with UTF-8 path input and wide-string
/// conversion. All errors logged via the FileSystem log category.

#include "file_system.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>

UZ_DEFINE_LOG_CATEGORY(FileSystem, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — UTF-8 to wide string conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Replaces forward slashes with backslashes in-place.
/// SHCreateDirectoryExW fails with ERROR_PATH_NOT_FOUND on forward slashes.
static void NormalizeSlashes(std::wstring& path) {
    for (auto& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
}

/// Converts a UTF-8 path to a wide string for Win32 APIs.
static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
    // Remove trailing null that MultiByteToWideChar includes in the count
    wide.pop_back();
    return wide;
}

/// Converts a wide string to UTF-8.
static std::string WideToUtf8(const wchar_t* wide, int wideLen = -1) {
    if (!wide || (wideLen == 0)) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, utf8.data(), len, nullptr, nullptr);
    // If wideLen was -1, the result includes a trailing null — remove it
    if (wideLen == -1 && !utf8.empty() && utf8.back() == '\0') {
        utf8.pop_back();
    }
    return utf8;
}

/// Maps a Win32 error code to a FileResult.
static FileResult Win32ErrorToResult(DWORD err) {
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return FileResult::NotFound;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return FileResult::AccessDenied;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return FileResult::AlreadyExists;
    default:
        return FileResult::IoError;
    }
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

    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        UZ_LOG(FileSystem, Error, "FileReadAll: empty or invalid path");
        return {};
    }

    HANDLE file = CreateFileW(
        widePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "FileReadAll: failed to open '{}', win32 error={}", path, err);
        return {};
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize)) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "FileReadAll: GetFileSizeEx failed for '{}', win32 error={}", path, err);
        CloseHandle(file);
        return {};
    }

    if (fileSize.QuadPart == 0) {
        CloseHandle(file);
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;

    // ReadFile takes DWORD (4GB max). For files larger than 4GB, read in chunks.
    size_t totalRead = 0;
    while (totalRead < buffer.size()) {
        const DWORD chunkSize = static_cast<DWORD>(
            std::min<size_t>(buffer.size() - totalRead, 0xFFFFFFFFu)
        );
        if (!ReadFile(file, buffer.data() + totalRead, chunkSize, &bytesRead, nullptr)) {
            const DWORD err = GetLastError();
            UZ_LOG(FileSystem, Error, "FileReadAll: ReadFile failed for '{}', win32 error={}", path, err);
            CloseHandle(file);
            return {};
        }
        totalRead += bytesRead;
        if (bytesRead == 0) break; // EOF
    }

    CloseHandle(file);
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

/// Ensures all parent directories for a path exist.
static FileResult EnsureParentDirectories(const std::wstring& widePath) {
    // Find last separator
    auto lastSep = widePath.find_last_of(L"\\/");
    if (lastSep == std::wstring::npos || lastSep == 0) {
        return FileResult::Success; // No parent or root
    }

    std::wstring parentDir = widePath.substr(0, lastSep);
    NormalizeSlashes(parentDir);

    // Check if parent already exists
    DWORD attrs = GetFileAttributesW(parentDir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return FileResult::Success;
    }

    // SHCreateDirectoryExW creates the full chain of missing directories
    int result = SHCreateDirectoryExW(nullptr, parentDir.c_str(), nullptr);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS && result != ERROR_FILE_EXISTS) {
        return FileResult::IoError;
    }

    return FileResult::Success;
}

FileResult FileWriteAll(const char* path, const void* data, size_t size) {
    UZ_PROFILE_FUNCTION();

    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        UZ_LOG(FileSystem, Error, "FileWriteAll: empty or invalid path");
        return FileResult::IoError;
    }

    // Create parent directories
    FileResult parentResult = EnsureParentDirectories(widePath);
    if (parentResult != FileResult::Success) {
        UZ_LOG(FileSystem, Error, "FileWriteAll: failed to create parent directories for '{}'", path);
        return parentResult;
    }

    HANDLE file = CreateFileW(
        widePath.c_str(),
        GENERIC_WRITE,
        0, // No sharing during write
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "FileWriteAll: failed to create '{}', win32 error={}", path, err);
        return Win32ErrorToResult(err);
    }

    if (size > 0 && data != nullptr) {
        size_t totalWritten = 0;
        while (totalWritten < size) {
            const DWORD chunkSize = static_cast<DWORD>(
                std::min<size_t>(size - totalWritten, 0xFFFFFFFFu)
            );
            DWORD bytesWritten = 0;
            if (!WriteFile(file, static_cast<const uint8_t*>(data) + totalWritten, chunkSize, &bytesWritten, nullptr)) {
                const DWORD err = GetLastError();
                UZ_LOG(FileSystem, Error, "FileWriteAll: WriteFile failed for '{}', size={}, win32 error={}", path, size, err);
                CloseHandle(file);
                return Win32ErrorToResult(err);
            }
            totalWritten += bytesWritten;
        }
    }

    CloseHandle(file);
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
    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) return false;
    return GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

int64_t FileGetSize(const char* path) {
    UZ_PROFILE_FUNCTION();

    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) return -1;

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "FileGetSize: failed for '{}', win32 error={}", path, err);
        return -1;
    }

    LARGE_INTEGER size;
    size.HighPart = static_cast<LONG>(fileInfo.nFileSizeHigh);
    size.LowPart = fileInfo.nFileSizeLow;
    return size.QuadPart;
}

FileResult FileDelete(const char* path) {
    UZ_PROFILE_FUNCTION();

    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        UZ_LOG(FileSystem, Error, "FileDelete: empty or invalid path");
        return FileResult::IoError;
    }

    if (DeleteFileW(widePath.c_str())) {
        return FileResult::Success;
    }

    const DWORD err = GetLastError();
    UZ_LOG(FileSystem, Error, "FileDelete: failed for '{}', win32 error={}", path, err);
    return Win32ErrorToResult(err);
}

// ─────────────────────────────────────────────────────────────────────────────
// Directory operations
// ─────────────────────────────────────────────────────────────────────────────

FileResult DirectoryCreate(const char* path) {
    UZ_PROFILE_FUNCTION();

    auto widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        UZ_LOG(FileSystem, Error, "DirectoryCreate: empty or invalid path");
        return FileResult::IoError;
    }
    NormalizeSlashes(widePath);

    // Check if already exists
    DWORD attrs = GetFileAttributesW(widePath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return FileResult::Success;
    }

    int result = SHCreateDirectoryExW(nullptr, widePath.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS) {
        return FileResult::Success;
    }

    UZ_LOG(FileSystem, Error, "DirectoryCreate: failed for '{}', result={}", path, result);
    return (result == ERROR_ACCESS_DENIED) ? FileResult::AccessDenied : FileResult::IoError;
}

bool IsDirectory(const char* path) {
    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) return false;
    DWORD attrs = GetFileAttributesW(widePath.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ─────────────────────────────────────────────────────────────────────────────
// Directory enumeration
// ─────────────────────────────────────────────────────────────────────────────

/// Populates a FileEntry from a WIN32_FIND_DATAW and its parent directory.
static void FillFileEntry(FileEntry& entry, const WIN32_FIND_DATAW& findData, const std::string& parentDir) {
    std::string filename = WideToUtf8(findData.cFileName);
    std::string fullPath = parentDir;
    if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\') {
        fullPath += '/';
    }
    fullPath += filename;

    // Truncate to buffer size, always null-terminate
    const size_t copyLen = std::min(fullPath.size(), sizeof(entry.path) - 1);
    std::memcpy(entry.path, fullPath.c_str(), copyLen);
    entry.path[copyLen] = '\0';

    entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (entry.isDirectory) {
        entry.size = 0;
    } else {
        LARGE_INTEGER sz;
        sz.HighPart = static_cast<LONG>(findData.nFileSizeHigh);
        sz.LowPart = findData.nFileSizeLow;
        entry.size = sz.QuadPart;
    }
}

/// Returns true if name is "." or ".."
static bool IsDotEntry(const wchar_t* name) {
    return (name[0] == L'.' && name[1] == L'\0') ||
           (name[0] == L'.' && name[1] == L'.' && name[2] == L'\0');
}

std::vector<FileEntry> DirectoryEnumerate(const char* dir, const char* pattern) {
    UZ_PROFILE_FUNCTION();

    std::vector<FileEntry> results;

    std::string searchPath = dir;
    if (!searchPath.empty() && searchPath.back() != '/' && searchPath.back() != '\\') {
        searchPath += '/';
    }
    searchPath += pattern;

    auto wideSearch = Utf8ToWide(searchPath.c_str());
    if (wideSearch.empty()) return results;

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(wideSearch.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return results;
    }

    do {
        if (IsDotEntry(findData.cFileName)) continue;

        FileEntry entry{};
        FillFileEntry(entry, findData, dir);
        results.push_back(entry);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return results;
}

/// Recursive helper for DirectoryEnumerateRecursive.
static void EnumerateRecursiveImpl(
    const std::string& dir,
    const char* pattern,
    std::vector<FileEntry>& results
) {
    // First, find all matching entries in this directory
    {
        std::string searchPath = dir;
        if (!searchPath.empty() && searchPath.back() != '/' && searchPath.back() != '\\') {
            searchPath += '/';
        }
        searchPath += pattern;

        auto wideSearch = Utf8ToWide(searchPath.c_str());
        if (wideSearch.empty()) return;

        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(wideSearch.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (IsDotEntry(findData.cFileName)) continue;

                FileEntry entry{};
                FillFileEntry(entry, findData, dir);
                results.push_back(entry);
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    // Then recurse into subdirectories
    {
        std::string allSearch = dir;
        if (!allSearch.empty() && allSearch.back() != '/' && allSearch.back() != '\\') {
            allSearch += '/';
        }
        allSearch += '*';

        auto wideAll = Utf8ToWide(allSearch.c_str());
        if (wideAll.empty()) return;

        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(wideAll.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (IsDotEntry(findData.cFileName)) continue;
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

                std::string subDir = dir;
                if (!subDir.empty() && subDir.back() != '/' && subDir.back() != '\\') {
                    subDir += '/';
                }
                subDir += WideToUtf8(findData.cFileName);
                EnumerateRecursiveImpl(subDir, pattern, results);
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
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

/// Resolves a known folder and appends "/UzEngine/", creating it if needed.
static std::string GetKnownFolderDir(int csidl) {
    wchar_t widePath[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, widePath);
    if (FAILED(hr)) {
        UZ_LOG(FileSystem, Error, "GetKnownFolderDir: SHGetFolderPathW failed, csidl={}, hr=0x{:08X}", csidl, static_cast<unsigned>(hr));
        return {};
    }

    std::string result = WideToUtf8(widePath);
    if (result.empty()) return {};

    // Normalize to forward slashes
    for (auto& c : result) {
        if (c == '\\') c = '/';
    }

    result += "/UzEngine/";

    // Ensure directory exists
    auto wideResult = Utf8ToWide(result.c_str());
    SHCreateDirectoryExW(nullptr, wideResult.c_str(), nullptr);

    return result;
}

std::string GetUserDataDir() {
    return GetKnownFolderDir(CSIDL_LOCAL_APPDATA);
}

std::string GetAppDataDir() {
    return GetKnownFolderDir(CSIDL_APPDATA);
}

std::string GetTempDir() {
    wchar_t widePath[MAX_PATH];
    DWORD len = GetTempPathW(MAX_PATH, widePath);
    if (len == 0 || len >= MAX_PATH) {
        UZ_LOG(FileSystem, Error, "GetTempDir: GetTempPathW failed, error={}", GetLastError());
        return {};
    }

    std::string result = WideToUtf8(widePath, static_cast<int>(len));

    // Normalize to forward slashes
    for (auto& c : result) {
        if (c == '\\') c = '/';
    }

    // Ensure trailing slash
    if (!result.empty() && result.back() != '/') {
        result += '/';
    }

    result += "UzEngine/";

    // Ensure directory exists
    auto wideResult = Utf8ToWide(result.c_str());
    SHCreateDirectoryExW(nullptr, wideResult.c_str(), nullptr);

    return result;
}

} // namespace uz
