/// Maps source file paths to subsystem names.
///
/// Subsystem is derived from the file's directory path relative to the repo root.
/// The most specific matching prefix wins (longest match first).

/// Path prefix → subsystem name mapping.
/// Ordered by specificity is not required — lookup uses longest-match.
const SUBSYSTEM_MAP: ReadonlyArray<[string, string]> = [
  // Renderer sub-layers (more specific first in the array, but lookup is longest-match)
  ["runtime/engine/renderer/rhi/", "rhi"],
  ["runtime/engine/renderer/passes/postprocess/", "postprocess"],
  ["runtime/engine/renderer/passes/atmosphere/", "atmosphere"],
  ["runtime/engine/renderer/passes/debug/", "debug-views"],
  ["runtime/engine/renderer/passes/", "render-passes"],
  ["runtime/engine/renderer/atmosphere/", "atmosphere"],
  ["runtime/engine/renderer/core/", "renderer-core"],
  ["runtime/engine/renderer/material/", "materials"],
  ["runtime/engine/renderer/mesh/", "mesh"],
  ["runtime/engine/renderer/upload/", "gpu-upload"],
  ["runtime/engine/renderer/memory/", "gpu-memory"],
  ["runtime/engine/renderer/", "renderer"],

  // Engine subsystems
  ["runtime/engine/app/", "application"],
  ["runtime/engine/ecs/environment/", "environment"],
  ["runtime/engine/ecs/core/", "ecs-core"],
  ["runtime/engine/ecs/update/", "ecs-update"],
  ["runtime/engine/ecs/metadata/", "ecs-metadata"],
  ["runtime/engine/ecs/", "ecs"],
  ["runtime/engine/shader/", "shader-system"],
  ["runtime/engine/asset/importers/", "asset-importers"],
  ["runtime/engine/asset/formats/", "asset-formats"],
  ["runtime/engine/asset/", "assets"],
  ["runtime/engine/scripting/", "scripting"],
  ["runtime/engine/math/", "math"],
  ["runtime/engine/job/", "job-system"],
  ["runtime/engine/profiling/", "profiling"],
  ["runtime/engine/ipc/", "ipc"],
  ["runtime/engine/config/", "config"],
  ["runtime/engine/containers/", "containers"],
  ["runtime/engine/event/", "events"],
  ["runtime/engine/property/", "property-system"],
  ["runtime/engine/random/", "random"],
  ["runtime/engine/core/", "engine-core"],
  ["runtime/engine/assertions/", "assertions"],
  ["runtime/engine/paths/", "paths"],

  // Editor
  ["runtime/editor/gizmo/", "gizmo"],
  ["runtime/editor/selection/", "selection"],
  ["runtime/editor/grid/", "editor-grid"],
  ["runtime/editor/outline/", "editor-outline"],
  ["runtime/editor/font/", "editor-font"],
  ["runtime/editor/screenshot/", "screenshot"],
  ["runtime/editor/stats/", "stats-overlay"],
  ["runtime/editor/input/", "editor-input"],
  ["runtime/editor/", "editor"],

  // Platform
  ["runtime/platform/windows/", "platform-windows"],
  ["runtime/platform/linux/", "platform-linux"],
  ["runtime/platform/macos/", "platform-macos"],
  ["runtime/platform/", "platform"],

  // Shaders
  ["runtime/shaders/runtime/postprocess/", "shader-postprocess"],
  ["runtime/shaders/runtime/atmosphere/", "shader-atmosphere"],
  ["runtime/shaders/runtime/lighting/", "shader-lighting"],
  ["runtime/shaders/runtime/geometry/", "shader-geometry"],
  ["runtime/shaders/runtime/compute/", "shader-compute"],
  ["runtime/shaders/runtime/ibl/", "shader-ibl"],
  ["runtime/shaders/runtime/shadows/", "shader-shadows"],
  ["runtime/shaders/runtime/include/", "shader-library"],
  ["runtime/shaders/runtime/gi/", "shader-gi"],
  ["runtime/shaders/runtime/ssgi/", "shader-ssgi"],
  ["runtime/shaders/runtime/debug/", "shader-debug"],
  ["runtime/shaders/runtime/", "shaders"],
  ["runtime/shaders/editor/", "editor-shaders"],
  ["runtime/shaders/", "shaders"],

  // Tools
  ["runtime/tools/", "tools"],
  ["runtime/demos/", "demos"],
  ["runtime/tests/", "tests"],

  // Extension + MCP
  ["mcp/src/", "mcp"],
  ["mcp/tools/", "mcp-tools"],
  ["extension/src/", "vscode-extension"],
];

/// Returns the subsystem name for a given file path.
///
/// Uses longest-prefix matching — the most specific directory wins.
/// Paths should use forward slashes and be relative to the repo root.
///
/// # Arguments
/// * `filePath` — file path relative to repo root (forward slashes)
///
/// # Returns
/// Subsystem name string, or `"other"` if no prefix matches.
export function mapSubsystem(filePath: string): string {
  const normalized = filePath.replace(/\\/g, "/");

  let bestMatch = "";
  let bestSubsystem = "other";

  for (const [prefix, subsystem] of SUBSYSTEM_MAP) {
    if (normalized.startsWith(prefix) && prefix.length > bestMatch.length) {
      bestMatch = prefix;
      bestSubsystem = subsystem;
    }
  }

  return bestSubsystem;
}

/// Returns all known subsystem names.
export function getAllSubsystems(): string[] {
  const unique = new Set(SUBSYSTEM_MAP.map(([, s]) => s));
  return [...unique].sort();
}
