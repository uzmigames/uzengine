#pragma once

/// UzEngine Environment Variables — Get/set OS environment on Windows.
///
/// Wraps Win32 wide-char environment APIs with UTF-8 conversion.
///
/// # Thread Safety
/// Win32 environment functions are not thread-safe. Call from a single thread
/// or provide external synchronization.

#include <string>

namespace uz {

/// Gets the value of an environment variable.
///
/// # Arguments
/// * `name` — UTF-8 variable name. Must not be nullptr.
///
/// # Returns
/// - Variable value as a UTF-8 string on success.
/// - Empty string if the variable is not set or on conversion failure.
std::string GetEnv(const char* name);

/// Sets an environment variable for the current process.
///
/// # Arguments
/// * `name` — UTF-8 variable name. Must not be nullptr.
/// * `value` — UTF-8 value to set. Must not be nullptr.
///
/// # Returns
/// - `true` on success.
/// - `false` on failure (check logs).
bool SetEnv(const char* name, const char* value);

} // namespace uz
