#pragma once

/// VFX Curve Loader -- deserializes .uzcurve JSON files into VFXCurve objects.
///
/// Parses JSON with the following schema:
/// ```json
/// {
///   "type": "float" | "vec3" | "vec4",
///   "interpolation": "linear" | "cubic" | "smoothstep",
///   "keys": [
///     { "time": 0.0, "value": 1.0 },           // float
///     { "time": 0.0, "value": [1,0,0,1] }      // vec4
///   ]
/// }
/// ```
///
/// # Validation
/// - Keys must be sorted by ascending time.
/// - Time values must be in [0, 1].
/// - At least 2 keys are required.

#include "vfx_curve.h"

#include <optional>
#include <string>

namespace uz { class IVFS; }

namespace uz::VFXCurveLoader {

/// Parses a .uzcurve JSON string into a VFXCurve.
///
/// # Arguments
/// * `jsonString` - UTF-8 JSON string following the .uzcurve schema.
///
/// # Returns
/// - `std::optional<VFXCurve>` containing the parsed curve on success.
/// - `std::nullopt` if parsing fails, validation fails, or the JSON is malformed.
///
/// # Errors
/// Logs `VFX::Error` with details on parse/validation failure.
[[nodiscard]] std::optional<VFXCurve> LoadFromJson(const std::string& jsonString);

/// Reads a .uzcurve file from disk and parses it.
///
/// # Arguments
/// * `path` - Filesystem path to the .uzcurve JSON file.
///
/// # Returns
/// - `std::optional<VFXCurve>` containing the parsed curve on success.
/// - `std::nullopt` if the file cannot be read or parsing fails.
///
/// # Errors
/// Logs `VFX::Error` if the file cannot be read.
[[nodiscard]] std::optional<VFXCurve> LoadFromFile(const std::string& path, IVFS* vfs = nullptr);

} // namespace uz::VFXCurveLoader
