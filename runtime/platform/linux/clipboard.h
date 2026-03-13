#pragma once

/// UzEngine Clipboard — X11/Wayland clipboard access via xclip/xsel subprocess.
///
/// Provides UTF-8 text and custom binary format clipboard operations on Linux.
/// All functions delegate to an external clipboard tool (`xclip` preferred,
/// `xsel` as fallback) via POSIX subprocess calls. No X11 headers or display
/// connection are required in the caller.
///
/// # Tool Discovery
/// Functions probe for `xclip` first by attempting the operation. If `xclip`
/// is not on PATH, they retry with `xsel`. If neither is found, the function
/// logs a `Clipboard::Error` and returns the failure value for that function.
///
/// # Binary Data
/// Binary data is encoded as raw bytes piped to/from the tool using a custom
/// MIME type derived from `formatName` (e.g. `"UzEngine/SceneNode"` becomes
/// the `-t UzEngine/SceneNode` argument). Not all desktop environments expose
/// custom MIME types between processes; this is primarily intended for
/// intra-process or intra-engine clipboard operations within the same session.
///
/// # Thread Safety
/// Functions are safe to call from any thread. Each call spawns an independent
/// subprocess with its own pipe; there is no shared clipboard handle.
///
/// # Limitations
/// - Requires `xclip` (xclip package) or `xsel` (xsel package) on PATH.
/// - Wayland compositors may not honour XAUTHORITY; set DISPLAY/WAYLAND_DISPLAY
///   appropriately in the process environment before calling.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Copies UTF-8 text to the system clipboard (X11 CLIPBOARD selection).
///
/// Pipes `text` to `xclip -selection clipboard` (or `xsel --clipboard --input`
/// as fallback). Replaces any existing clipboard contents.
///
/// # Arguments
/// * `text` — Null-terminated UTF-8 string to copy. Must not be nullptr.
///
/// # Returns
/// - `true` if the text was successfully placed on the clipboard.
/// - `false` if no clipboard tool was found or the subprocess exited non-zero.
///   Logs `Clipboard::Error` on failure.
[[nodiscard]] bool ClipboardSetText(const char* text);

/// Retrieves UTF-8 text from the system clipboard (X11 CLIPBOARD selection).
///
/// Reads stdout of `xclip -selection clipboard -o` (or `xsel --clipboard
/// --output` as fallback).
///
/// # Returns
/// - The clipboard text as a UTF-8 string on success.
/// - Empty string if the clipboard is empty, contains non-text data, no tool
///   is found, or the subprocess fails.
[[nodiscard]] std::string ClipboardGetText();

/// Copies binary data to the clipboard under a custom MIME type.
///
/// Pipes the raw bytes to `xclip -selection clipboard -t <formatName>` (or
/// the equivalent `xsel` command). Replaces any existing clipboard contents.
///
/// # Arguments
/// * `formatName` — Null-terminated MIME type string (e.g. `"UzEngine/SceneNode"`).
///   Must not be nullptr.
/// * `data` — Pointer to the raw bytes. Must not be nullptr if `size > 0`.
/// * `size` — Byte count. Zero is a valid no-op (returns true immediately).
///
/// # Returns
/// - `true` on success.
/// - `false` if no clipboard tool was found or the subprocess exited non-zero.
///   Logs `Clipboard::Error` on failure.
[[nodiscard]] bool ClipboardSetData(const char* formatName, const void* data, size_t size);

/// Retrieves binary data from the clipboard for a custom MIME type.
///
/// Reads stdout of `xclip -selection clipboard -o -t <formatName>` (or the
/// equivalent `xsel` command).
///
/// # Arguments
/// * `formatName` — Null-terminated MIME type string (must match a prior
///   `ClipboardSetData` call within the same X session).
///
/// # Returns
/// - Vector containing the raw bytes on success.
/// - Empty vector if the format is not present, no tool is found, or the
///   subprocess fails.
[[nodiscard]] std::vector<uint8_t> ClipboardGetData(const char* formatName);

} // namespace uz
