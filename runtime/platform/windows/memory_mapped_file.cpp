/// UzEngine Memory-Mapped File — Windows Implementation
///
/// Uses CreateFileW + CreateFileMappingW + MapViewOfFile for zero-copy reads.

#include "memory_mapped_file.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "log.h"
#include "profiler.h"

#include <utility>

UZ_DECLARE_LOG_CATEGORY(FileSystem);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper — UTF-8 to wide (duplicated from file_system.cpp to avoid
// cross-TU dependency; the compiler will inline/deduplicate in the static lib)
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
    wide.pop_back();
    return wide;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

MemoryMappedFile::~MemoryMappedFile() {
    Close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : m_fileHandle(std::exchange(other.m_fileHandle, nullptr))
    , m_mappingHandle(std::exchange(other.m_mappingHandle, nullptr))
    , m_data(std::exchange(other.m_data, nullptr))
    , m_size(std::exchange(other.m_size, 0)) {
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        Close();
        m_fileHandle = std::exchange(other.m_fileHandle, nullptr);
        m_mappingHandle = std::exchange(other.m_mappingHandle, nullptr);
        m_data = std::exchange(other.m_data, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Open / Close
// ─────────────────────────────────────────────────────────────────────────────

bool MemoryMappedFile::Open(const char* path) {
    UZ_PROFILE_FUNCTION();

    // Close any existing mapping
    if (IsOpen()) {
        Close();
    }

    const auto widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: empty or invalid path");
        return false;
    }

    // Open the file for reading
    HANDLE fileHandle = CreateFileW(
        widePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (fileHandle == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: CreateFileW failed for '{}', win32 error={}", path, err);
        return false;
    }

    // Get file size
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(fileHandle, &fileSize)) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: GetFileSizeEx failed for '{}', win32 error={}", path, err);
        CloseHandle(fileHandle);
        return false;
    }

    // Cannot map a zero-byte file
    if (fileSize.QuadPart == 0) {
        UZ_LOG(FileSystem, Warning, "MemoryMappedFile::Open: file '{}' is empty, cannot map", path);
        CloseHandle(fileHandle);
        return false;
    }

    // Create file mapping
    HANDLE mappingHandle = CreateFileMappingW(
        fileHandle,
        nullptr,
        PAGE_READONLY,
        fileSize.HighPart,
        fileSize.LowPart,
        nullptr
    );

    if (mappingHandle == nullptr) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: CreateFileMappingW failed for '{}', win32 error={}", path, err);
        CloseHandle(fileHandle);
        return false;
    }

    // Map the view
    void* data = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, 0);
    if (data == nullptr) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "MemoryMappedFile::Open: MapViewOfFile failed for '{}', win32 error={}", path, err);
        CloseHandle(mappingHandle);
        CloseHandle(fileHandle);
        return false;
    }

    m_fileHandle = fileHandle;
    m_mappingHandle = mappingHandle;
    m_data = data;
    m_size = static_cast<size_t>(fileSize.QuadPart);
    return true;
}

void MemoryMappedFile::Close() {
    if (m_data) {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_mappingHandle) {
        CloseHandle(static_cast<HANDLE>(m_mappingHandle));
        m_mappingHandle = nullptr;
    }
    if (m_fileHandle) {
        CloseHandle(static_cast<HANDLE>(m_fileHandle));
        m_fileHandle = nullptr;
    }
    m_size = 0;
}

} // namespace uz
