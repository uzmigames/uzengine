#pragma once

/// UzEngine Default Monospace System Font Resolver.
///
/// Provides a runtime-derived path to a monospaced system font so editor
/// overlays (stats, toolbar, VFX inspector) can render text without any
/// hardcoded filesystem path in engine or editor code.
///
/// # Design rationale
/// The engine is 100% path-agnostic — no drive letters, no hardcoded OS
/// paths. This helper queries the platform at runtime:
///   * Windows: uses `GetWindowsDirectoryW()` → `<Windows>/Fonts/consola.ttf`.
///   * macOS:   returns `/System/Library/Fonts/Monaco.ttf` (stable Apple
///              system font path, unchanged across recent macOS versions).
///   * Linux:   probes a short list of common distro font paths (DejaVu,
///              Liberation, Noto Mono) and returns the first that exists.
///
/// Callers SHOULD prefer an explicit configuration override (e.g., a
/// `.uzproject` / CLI key like `editor.font.mono_path`) when available, and
/// fall back to this helper only when no override is supplied.

#include <string>

namespace uz {

/// Returns a filesystem path to a default monospace system font.
///
/// # Returns
/// A path suitable for passing to `BitmapFont::Init` or
/// `FontManager::LoadFont`. Returns an empty string if no system monospace
/// font is available on the host platform.
///
/// # Thread Safety
/// Safe to call from any thread; performs read-only platform queries.
[[nodiscard]] std::string ResolveDefaultMonoFont();

} // namespace uz
