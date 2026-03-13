/// UzEngine DLL Loader Implementation (Linux)
///
/// POSIX dlopen/dlclose/dlsym with RTLD_NOW for immediate symbol resolution.

#include "dll_loader.h"

#ifdef __linux__

#include "log.h"

#include <dlfcn.h>

namespace uz {

NativeDllHandle LoadDll(const char* path) {
    if (!path) {
        UZ_LOG(Platform, Error, "LoadDll called with nullptr path");
        return nullptr;
    }

    // RTLD_NOW: resolve all symbols immediately — fail-fast on missing deps
    void* handle = ::dlopen(path, RTLD_NOW);
    if (!handle) {
        UZ_LOG(Platform, Error, "LoadDll: dlopen failed for '{}': {}", path, ::dlerror());
        return nullptr;
    }

    UZ_LOG(Platform, Info, "Loaded shared library: '{}'", path);
    return handle;
}

void FreeDll(NativeDllHandle handle) {
    if (!handle) return;
    ::dlclose(handle);
}

void* GetDllSymbol(NativeDllHandle handle, const char* symbolName) {
    if (!handle || !symbolName) {
        UZ_LOG(Platform, Warning, "GetDllSymbol called with null handle or symbol name");
        return nullptr;
    }

    // Clear any prior error state before dlsym
    ::dlerror();
    void* addr = ::dlsym(handle, symbolName);

    // dlsym can legitimately return nullptr for a symbol whose value IS nullptr,
    // so we must check dlerror() rather than addr to detect true failures.
    const char* err = ::dlerror();
    if (err) {
        UZ_LOG(Platform, Warning, "GetDllSymbol: '{}' not found: {}", symbolName, err);
        return nullptr;
    }

    return addr;
}

} // namespace uz

#endif // __linux__
