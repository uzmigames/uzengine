/// UzEngine Orthographic View Presets
///
/// Defines the OrthoView enum used by CameraManager::SetOrthographicView()
/// and DemoApplication::ApplyOrthoCameraKeyboard().
///
/// Coordinate convention: Z-up, X-forward, Y-right (UzEngine standard).
///   - Top    = camera at +Z, looking down  (-Z), up = +X
///   - Bottom = camera at -Z, looking up    (+Z), up = +X
///   - Front  = camera at -X, looking front (+X), up = +Z
///   - Back   = camera at +X, looking back  (-X), up = +Z
///   - Left   = camera at +Y, looking right (-Y), up = +Z
///   - Right  = camera at -Y, looking left  (+Y), up = +Z

#pragma once

#include <cstdint>
#include <string_view>

namespace uz {

/// Orthographic camera preset identifier.
///
/// `None` means the camera is in its normal (perspective or user-configured)
/// mode and no ortho preset is active. All other values correspond to one of
/// the six standard axis-aligned views.
enum class OrthoView : uint8_t {
    None   = 0, ///< Not in an ortho preset — camera in default (perspective) mode.
    Top    = 1, ///< Top-down view — camera above scene, looking along -Z.
    Bottom = 2, ///< Bottom-up view — camera below scene, looking along +Z.
    Front  = 3, ///< Front view — camera behind scene, looking along +X.
    Back   = 4, ///< Back view — camera in front of scene, looking along -X.
    Left   = 5, ///< Left side view — camera at +Y, looking along -Y.
    Right  = 6, ///< Right side view — camera at -Y, looking along +Y.
};

/// Returns the human-readable name for an OrthoView value.
///
/// Returns "None" for OrthoView::None and for any unrecognized value.
[[nodiscard]] const char* OrthoViewName(OrthoView view) noexcept;

/// Parses an OrthoView from a string (case-insensitive).
///
/// Recognized names: "top", "bottom", "front", "back", "left", "right".
/// Returns OrthoView::None for unrecognized input.
[[nodiscard]] OrthoView OrthoViewFromString(std::string_view str) noexcept;

} // namespace uz
