#pragma once

// scene_loader.h — parse .uzscene INI-like text files into a Scene.
//
// The .uzscene format is an INI-like text format:
//
//   [uzscene]
//   name = MyScene
//   version = 1
//
//   [node]
//   name = Root
//   type = Node3D
//   parent =
//   position = 0.0 0.0 0.0
//   rotation = 0.0 0.0 0.0 1.0
//   scale = 1.0 1.0 1.0
//   active = true
//   visible = true
//
// Parser rules:
//   - Lines starting with '#' or ';' are comments (skipped).
//   - Blank lines are skipped.
//   - [uzscene] sets scene-level metadata (name, version).
//   - [node] starts a new node definition; flushed on next [node] or EOF.
//   - Property lines: "key = value" (trim whitespace from both).
//   - Parents must appear before children in the file.
//   - Unknown keys are silently ignored (forward-compatible).

#include "engine/scene/scene.h"
#include "engine/core/result.h"

#include <string>

namespace uz {

/// Result type for scene loading operations.
using SceneLoadResult = Result<bool>;

/// Load a .uzscene file from disk into an already-initialized Scene.
/// Clears the scene before loading.
/// Returns Err(message) on parse/IO error.
[[nodiscard]] SceneLoadResult scene_load_file(Scene* scene, const char* path);

/// Parse .uzscene text from an in-memory buffer (null-terminated).
/// Clears the scene before parsing.
[[nodiscard]] SceneLoadResult scene_load_from_string(Scene* scene, const char* text);

} // namespace uz
