#pragma once

/// UzEngine Clipboard — macOS NSPasteboard clipboard access (text and binary).
///
/// Provides UTF-8 text and custom binary format clipboard operations backed by
/// `NSPasteboard`. All functions wrap their operations in `@autoreleasepool`
/// blocks and use `[NSPasteboard generalPasteboard]` as the target. Callers do
/// not need to manage pasteboard state.
///
/// # Thread Safety
/// Must be called from the main thread. `NSPasteboard` is not thread-safe;
/// AppKit requires pasteboard access from the thread that owns the run loop.

#ifdef __APPLE__

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Copies UTF-8 text to the system clipboard via `NSPasteboardTypeString`.
///
/// Clears the pasteboard with `[pasteboard clearContents]` before writing,
/// replacing any existing contents. The input is converted to an `NSString`
/// using `NSUTF8StringEncoding`.
///
/// # Arguments
/// * `text` — Null-terminated UTF-8 string to copy. Must not be nullptr.
///
/// # Returns
/// - `true` if the text was successfully placed on the pasteboard.
/// - `false` if the NSString conversion failed or the pasteboard write returned
///   an error. Logs `Clipboard::Error` with a description on failure.
[[nodiscard]] bool ClipboardSetText(const char* text);

/// Retrieves UTF-8 text from the system clipboard.
///
/// Reads `NSPasteboardTypeString` data from the general pasteboard and
/// converts it to a `std::string` using `NSUTF8StringEncoding`.
///
/// # Returns
/// - The clipboard text as a UTF-8 string on success.
/// - Empty string if the pasteboard does not contain a string, the encoding
///   conversion fails, or the pasteboard cannot be read.
[[nodiscard]] std::string ClipboardGetText();

/// Copies binary data to the clipboard under a custom UTI-based format name.
///
/// Derives a reverse-DNS UTI string from `formatName` by prefixing it with
/// `"uz.engine."` (e.g., `"UzEngine/SceneNode"` becomes
/// `"uz.engine.UzEngine/SceneNode"`). Clears the pasteboard before writing.
/// Stores the raw bytes as an `NSData` object under the derived UTI type.
///
/// # Arguments
/// * `formatName` — Null-terminated UTF-8 format identifier
///   (e.g., `"UzEngine/SceneNode"`). Must not be nullptr.
/// * `data`       — Pointer to the raw bytes. May be nullptr if `size == 0`.
/// * `size`       — Byte count. Zero is accepted; sets an empty NSData entry.
///
/// # Returns
/// - `true` on success.
/// - `false` on failure (logs `Clipboard::Error` with the format name).
[[nodiscard]] bool ClipboardSetData(const char* formatName, const void* data, size_t size);

/// Retrieves binary data from the clipboard for a custom named format.
///
/// Derives the UTI string from `formatName` using the same `"uz.engine."`
/// prefix scheme as `ClipboardSetData`. Reads the `NSData` stored under that
/// UTI from the general pasteboard.
///
/// # Arguments
/// * `formatName` — Null-terminated UTF-8 format identifier (must match a
///   prior `ClipboardSetData` call).
///
/// # Returns
/// - Vector containing the raw bytes on success.
/// - Empty vector if the format is not present, the UTI is not on the
///   pasteboard, or the pasteboard cannot be read.
[[nodiscard]] std::vector<uint8_t> ClipboardGetData(const char* formatName);

} // namespace uz

#endif // __APPLE__
