#pragma once

// scene_saver.h — serialize a Scene to .uzscene text format.
//
// Writes the [uzscene] header and one [node] section per alive node in slot
// order. The output is round-trip safe with the scene_loader parser.

#include "engine/scene/scene.h"
#include "engine/core/result.h"

#include <string>

namespace uz {

/// Result type for scene saving operations.
using SceneSaveResult = Result<bool>;

/// Write the scene to a .uzscene file on disk.
/// Returns Err(message) on IO error.
[[nodiscard]] SceneSaveResult scene_save_file(const Scene* scene, const char* path);

/// Serialize scene to a std::string (the .uzscene text).
[[nodiscard]] std::string scene_save_to_string(const Scene* scene);

} // namespace uz
