/// UzEngine Shared Memory Implementation (Windows)
///
/// CreateFileMapping/OpenFileMapping + MapViewOfFile for named IPC regions.

#include "shared_memory.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

namespace uz {

namespace {

/// Converts a UTF-8 string to wide. Returns empty on failure.
std::wstring Utf8ToWide(const char* str) {
    if (!str || str[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, result.data(), len);
    return result;
}

} // anonymous namespace

SharedMemoryHandle SharedMemoryCreate(const char* name, size_t size) {
    SharedMemoryHandle result{};

    if (!name || size == 0) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: invalid arguments (name={}, size={})",
               name ? name : "null", size);
        return result;
    }

    std::wstring wideName = Utf8ToWide(name);
    if (wideName.empty()) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: UTF-8 conversion failed for '{}'", name);
        return result;
    }

    DWORD sizeHigh = static_cast<DWORD>(size >> 32);
    DWORD sizeLow = static_cast<DWORD>(size & 0xFFFFFFFF);

    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,  // page-file backed
        nullptr,               // default security
        PAGE_READWRITE,
        sizeHigh,
        sizeLow,
        wideName.c_str()
    );

    if (!mapping) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: CreateFileMappingW failed for '{}', error={}",
               name, GetLastError());
        return result;
    }

    void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!data) {
        UZ_LOG(Platform, Error, "SharedMemoryCreate: MapViewOfFile failed for '{}', error={}",
               name, GetLastError());
        CloseHandle(mapping);
        return result;
    }

    result.mapping = static_cast<void*>(mapping);
    result.data = data;
    result.size = size;

    UZ_LOG(Platform, Info, "Shared memory created: '{}' ({} bytes)", name, size);
    return result;
}

SharedMemoryHandle SharedMemoryOpen(const char* name, size_t size) {
    SharedMemoryHandle result{};

    if (!name || size == 0) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: invalid arguments");
        return result;
    }

    std::wstring wideName = Utf8ToWide(name);
    if (wideName.empty()) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: UTF-8 conversion failed for '{}'", name);
        return result;
    }

    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wideName.c_str());
    if (!mapping) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: OpenFileMappingW failed for '{}', error={}",
               name, GetLastError());
        return result;
    }

    void* data = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!data) {
        UZ_LOG(Platform, Error, "SharedMemoryOpen: MapViewOfFile failed for '{}', error={}",
               name, GetLastError());
        CloseHandle(mapping);
        return result;
    }

    result.mapping = static_cast<void*>(mapping);
    result.data = data;
    result.size = size;

    UZ_LOG(Platform, Info, "Shared memory opened: '{}' ({} bytes)", name, size);
    return result;
}

void SharedMemoryClose(SharedMemoryHandle& handle) {
    if (handle.data) {
        UnmapViewOfFile(handle.data);
    }
    if (handle.mapping) {
        CloseHandle(static_cast<HANDLE>(handle.mapping));
    }
    handle = {};
}

} // namespace uz

#endif // _WIN32
