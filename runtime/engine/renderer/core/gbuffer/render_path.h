/// UzEngine Render Path Selection
///
/// Defines the available rendering paths and the default selection.
/// The render path determines how opaque geometry is lit:
///
/// - **Deferred**: G-Buffer pass (geometry → MRT) + fullscreen lighting pass.
///   Best for scenes with many lights. Default path.
/// - **Forward**: Single-pass PBR with per-object lighting evaluation.
///   Simpler, supports transparency natively, fewer GPU resources.
///
/// # Usage
/// ```cpp
/// RenderPath path = GetDefaultRenderPath();  // Deferred
/// if (path == RenderPath::Forward) { ... }
/// ```
///
/// The render path can be changed at runtime between frames. Switching mid-frame
/// is not supported — the path must be set before render graph construction.

#pragma once

#include <cstdint>

namespace uz {

/// Rendering pipeline strategy for opaque geometry.
///
/// Each enumerant selects a complete set of render graph passes for the frame.
/// Transparent objects always use forward rendering regardless of this setting.
///
/// # Enumerants
/// - `Deferred` — G-Buffer geometry pass + fullscreen deferred lighting pass.
///   Supports up to 64 point lights per pixel with constant cost per light.
///   Uses ~40MB additional VRAM for G-Buffer textures at 1080p.
/// - `Forward` — Single-pass forward PBR with per-object light evaluation.
///   Lower VRAM usage, native transparency support, but O(objects * lights) cost.
enum class RenderPath : uint8_t {
    Deferred = 0, ///< G-Buffer + deferred lighting (default).
    Forward  = 1, ///< Single-pass forward PBR.
};

/// Returns the engine's default render path (Deferred).
///
/// This is the recommended path for most scenes. Use Forward only when:
/// - The scene has very few lights (< 4)
/// - Transparency is the primary rendering concern
/// - GPU memory is severely constrained
inline constexpr RenderPath GetDefaultRenderPath() { return RenderPath::Deferred; }

/// Returns a human-readable name for the render path.
///
/// # Returns
/// - `"Deferred"` for `RenderPath::Deferred`
/// - `"Forward"` for `RenderPath::Forward`
/// - `"Unknown"` for any other value
inline constexpr const char* RenderPathToString(RenderPath path) {
    switch (path) {
        case RenderPath::Deferred: return "Deferred";
        case RenderPath::Forward:  return "Forward";
    }
    return "Unknown";
}

} // namespace uz
