#pragma once

/// UzEngine Clipboard — Win32 clipboard access (text and binary).
///
/// Provides UTF-8 text and custom binary format clipboard operations.
/// All functions open/close the clipboard internally — callers do not
/// need to manage clipboard state.
///
/// # Thread Safety
/// Must be called from the thread that owns the window (typically main thread).
/// Win32 clipboard is inherently single-access.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Copies UTF-8 text to the system clipboard.
///
/// Converts the input from UTF-8 to UTF-16 and stores it as CF_UNICODETEXT.
/// Replaces any existing clipboard contents.
///
/// # Arguments
/// * `text` — Null-terminated UTF-8 string to copy. Must not be nullptr.
///
/// # Returns
/// - `true` if the text was successfully placed on the clipboard.
/// - `false` if the clipboard could not be opened or the allocation failed.
///   Logs `Clipboard::Error` with the Win32 error code on failure.
[[nodiscard]] bool ClipboardSetText(const char* text);

/// Retrieves UTF-8 text from the system clipboard.
///
/// Reads CF_UNICODETEXT data and converts it to a UTF-8 std::string.
///
/// # Returns
/// - The clipboard text as a UTF-8 string on success.
/// - Empty string if the clipboard does not contain text or cannot be opened.
[[nodiscard]] std::string ClipboardGetText();

/// Copies binary data to the clipboard under a custom named format.
///
/// Registers (or retrieves) a clipboard format via `RegisterClipboardFormatW`
/// and stores the raw bytes. Replaces any existing clipboard contents.
///
/// # Arguments
/// * `formatName` — Null-terminated UTF-8 format identifier (e.g. "UzEngine/SceneNode").
///   Must not be nullptr.
/// * `data` — Pointer to the raw bytes. Must not be nullptr if `size > 0`.
/// * `size` — Byte count. Zero is a valid no-op (returns true).
///
/// # Returns
/// - `true` on success.
/// - `false` on failure (logs `Clipboard::Error`).
[[nodiscard]] bool ClipboardSetData(const char* formatName, const void* data, size_t size);

/// Retrieves binary data from the clipboard for a custom named format.
///
/// # Arguments
/// * `formatName` — Null-terminated UTF-8 format identifier (must match a prior SetData call).
///
/// # Returns
/// - Vector containing the raw bytes on success.
/// - Empty vector if the format is not present or the clipboard cannot be opened.
[[nodiscard]] std::vector<uint8_t> ClipboardGetData(const char* formatName);

} // namespace uz
