/// UzEngine DLL Loader Implementation (Windows)
///
/// UTF-8 path conversion and Win32 LoadLibrary/FreeLibrary/GetProcAddress.

#include "dll_loader.h"

#ifdef _WIN32

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace uz {

NativeDllHandle LoadDll(const char* path) {
    if (!path) {
        UZ_LOG(Platform, Error, "LoadDll called with nullptr path");
        return nullptr;
    }

    // UTF-8 -> wide conversion
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wideLen <= 0) {
        UZ_LOG(Platform, Error, "LoadDll: UTF-8 conversion failed for '{}'", path);
        return nullptr;
    }

    // Stack buffer for short paths, heap fallback for long ones
    constexpr int kStackBufSize = 260;
    wchar_t stackBuf[kStackBufSize];
    wchar_t* widePath = stackBuf;
    bool heapAllocated = false;

    if (wideLen > kStackBufSize) {
        widePath = static_cast<wchar_t*>(::malloc(static_cast<size_t>(wideLen) * sizeof(wchar_t)));
        if (!widePath) {
            UZ_LOG(Platform, Error, "LoadDll: allocation failed for wide path");
            return nullptr;
        }
        heapAllocated = true;
    }

    MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, wideLen);
    HMODULE mod = LoadLibraryW(widePath);

    if (heapAllocated) {
        ::free(widePath);
    }

    if (!mod) {
        UZ_LOG(Platform, Error, "LoadDll: LoadLibraryW failed for '{}', error={}", path, GetLastError());
        return nullptr;
    }

    UZ_LOG(Platform, Info, "Loaded DLL: '{}'", path);
    return static_cast<NativeDllHandle>(mod);
}

void FreeDll(NativeDllHandle handle) {
    if (!handle) return;
    FreeLibrary(static_cast<HMODULE>(handle));
}

void* GetDllSymbol(NativeDllHandle handle, const char* symbolName) {
    if (!handle || !symbolName) {
        UZ_LOG(Platform, Warning, "GetDllSymbol called with null handle or symbol name");
        return nullptr;
    }

    void* addr = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbolName));
    if (!addr) {
        UZ_LOG(Platform, Warning, "GetDllSymbol: '{}' not found, error={}", symbolName, GetLastError());
    }
    return addr;
}

} // namespace uz

#endif // _WIN32
