#pragma once

// camera_preset_loader.h — .uzcam file parser.
//
// Loads camera presets from INI-like .uzcam files into CameraSystemState.
// Sections: [mode], [spring_arm], [fov], [dof], [shake preset=name]

#include "engine/gameplay/camera_system.h"

#include <string>

namespace uz {

/// Load a .uzcam file and populate the given CameraSystemState.
/// Returns true on success, false on file-not-found or parse error.
/// On error, error_out contains a description.
bool load_camera_preset(const std::string& path,
                        CameraSystemState& state,
                        std::string& error_out);

} // namespace uz
