#pragma once

/// UzEngine DLL Loader — Dynamic library loading for macOS.
///
/// Wraps POSIX dlopen/dlclose/dlsym behind a platform-agnostic API using
/// `NativeDllHandle` from platform_types.h. Libraries are loaded with
/// `RTLD_NOW` so missing symbols are reported immediately at load time rather
/// than at first call.
///
/// # Thread Safety
/// dlopen/dlclose/dlsym are thread-safe on macOS (libdyld serializes
/// concurrent loads of the same library).

#ifdef __APPLE__

#include "platform/platform_types.h"

namespace uz {

/// Loads a shared library (.dylib) from a UTF-8 file path.
///
/// Calls `dlopen(path, RTLD_NOW)`. On failure, logs the `dlerror()` string
/// which describes the missing symbol or missing file.
///
/// # Arguments
/// * `path` — UTF-8 encoded path to the .dylib file. Must not be nullptr.
///
/// # Returns
/// - Valid handle (the void* returned by dlopen) on success.
/// - `nullptr` on failure (file not found, missing dependency, symbol error).
NativeDllHandle LoadDll(const char* path);

/// Frees a previously loaded shared library.
///
/// Calls `dlclose()`. Null-safe (no-op if handle is nullptr).
/// The library is only unloaded from the process when all dlopen reference
/// counts for it reach zero.
///
/// # Arguments
/// * `handle` — Handle returned by LoadDll(), or nullptr.
void FreeDll(NativeDllHandle handle);

/// Retrieves the address of an exported symbol from a loaded shared library.
///
/// Calls `dlsym()`. Logs a warning with `dlerror()` on failure.
///
/// # Arguments
/// * `handle` — Handle returned by LoadDll(). Must not be nullptr.
/// * `symbolName` — Null-terminated symbol name (C linkage).
///
/// # Returns
/// - Non-null pointer to the symbol on success.
/// - `nullptr` if the symbol is not found or handle is invalid.
void* GetDllSymbol(NativeDllHandle handle, const char* symbolName);

} // namespace uz

#endif // __APPLE__
