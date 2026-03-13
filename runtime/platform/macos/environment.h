#pragma once

/// UzEngine Environment Variables — Get/set OS environment on macOS.
///
/// Wraps POSIX getenv/setenv behind a UTF-8 string interface.
/// No encoding conversion is needed on macOS as the environment is inherently
/// byte-string based and UTF-8 by convention.
///
/// # Thread Safety
/// POSIX getenv/setenv are NOT thread-safe. Call from a single thread
/// (typically during engine startup/shutdown) or provide external
/// synchronization. Concurrent getenv and setenv from multiple threads can
/// cause undefined behaviour on the Darwin libc.

#ifdef __APPLE__

#include <string>

namespace uz {

/// Gets the value of an environment variable.
///
/// Calls POSIX `getenv()`. The returned string is a copy — safe to use
/// after subsequent setenv() calls.
///
/// # Arguments
/// * `name` — UTF-8 variable name. Must not be nullptr.
///
/// # Returns
/// - Variable value as a UTF-8 string on success.
/// - Empty string if the variable is not set or name is nullptr.
std::string GetEnv(const char* name);

/// Sets an environment variable for the current process.
///
/// Calls `setenv(name, value, /*overwrite=*/1)`. The new value is visible
/// to all subsequent calls to getenv() in the same process.
///
/// # Arguments
/// * `name` — UTF-8 variable name. Must not be nullptr.
/// * `value` — UTF-8 value to assign. Must not be nullptr.
///
/// # Returns
/// - `true` on success.
/// - `false` on failure (check logs for errno description).
bool SetEnv(const char* name, const char* value);

} // namespace uz

#endif // __APPLE__
