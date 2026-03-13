#pragma once

/// UzEngine DLL Loader — Dynamic library loading for Windows.
///
/// Wraps Win32 LoadLibrary/FreeLibrary/GetProcAddress behind a
/// platform-agnostic API using `NativeDllHandle` from platform_types.h.
///
/// # Thread Safety
/// Each function is thread-safe (Win32 loader lock serializes internally).

#include "platform/platform_types.h"

namespace uz {

/// Loads a DLL from a UTF-8 file path.
///
/// Internally converts to wide string and calls LoadLibraryW.
/// Logs an error with GetLastError() on failure.
///
/// # Arguments
/// * `path` — UTF-8 encoded path to the DLL. Must not be nullptr.
///
/// # Returns
/// - Valid handle on success (cast of HMODULE).
/// - `nullptr` on failure (DLL not found, dependency missing, etc.).
NativeDllHandle LoadDll(const char* path);

/// Frees a previously loaded DLL.
///
/// Calls FreeLibrary. Null-safe (no-op if handle is nullptr).
///
/// # Arguments
/// * `handle` — Handle returned by LoadDll(), or nullptr.
void FreeDll(NativeDllHandle handle);

/// Retrieves the address of an exported symbol from a loaded DLL.
///
/// Calls GetProcAddress. Logs a warning on failure.
///
/// # Arguments
/// * `handle` — Handle returned by LoadDll(). Must not be nullptr.
/// * `symbolName` — Null-terminated ASCII symbol name.
///
/// # Returns
/// - Non-null pointer to the symbol on success.
/// - `nullptr` if the symbol is not found.
void* GetDllSymbol(NativeDllHandle handle, const char* symbolName);

} // namespace uz
