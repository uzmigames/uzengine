/// project_loader.h — .uzproject file parser.
///
/// Parses a JSON-formatted `.uzproject` file from the real filesystem (not VFS)
/// and returns a `ProjectConfig` with all project-level settings. This is the
/// first thing the engine reads at startup before any VFS or asset system is
/// available.
///
/// # File Format
/// The `.uzproject` file is standard JSON. All top-level fields are optional;
/// missing fields receive safe defaults. Unknown keys are silently ignored.
///
/// # Error Policy
/// Parse errors and missing files are logged at `Config::Error` level and
/// return `std::nullopt`. The caller is responsible for handling a missing
/// project gracefully (typically by falling back to built-in defaults).

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ProjectConfig — parsed .uzproject settings
// ─────────────────────────────────────────────────────────────────────────────

/// Parsed representation of a `.uzproject` configuration file.
///
/// All fields correspond directly to JSON keys in the project file.
/// Fields not present in the file retain their default values defined here.
///
/// # JSON Schema (all keys optional)
/// ```json
/// {
///   "name":           "MyGame",
///   "version":        "1.0.0",
///   "engineVersion":  "0.1.0",
///   "defaultScene":   "scenes/main.uzscene",
///   "contentRoot":    "content",
///   "window": {
///     "title":      "MyGame",
///     "width":      1280,
///     "height":     720,
///     "fullscreen": false
///   },
///   "render": {
///     "backend": "dx12",
///     "vsync":   false
///   },
///   "cvars": {
///     "r.AutoExposure":          0,
///     "r.AutoExposure.SpeedDown": 5.0
///   }
/// }
/// ```
///
/// The optional `cvars` block lets a project pin engine CVars without
/// requiring `--cvar` CLI args. Values are applied via the same
/// `Config::AddPendingCvarOverride` path used by the CLI, so they take
/// effect before `SceneRenderer::Init` reads init-time CVars. CLI
/// `--cvar` arguments still take precedence because they are pushed
/// onto the override queue after the project values.
struct ProjectConfig {
    /// Display name of the project.
    std::string name;

    /// Project version string (semver recommended, e.g., "1.0.0").
    std::string version;

    /// Minimum compatible engine version string (e.g., "0.1.0").
    std::string engineVersion;

    /// VFS path to the default scene loaded at startup (e.g., "scenes/main.uzscene").
    ///
    /// Interpreted relative to `contentRoot` when the VFS is mounted.
    /// Empty string means no default scene is loaded automatically.
    std::string defaultScene;

    /// Root directory for content assets, relative to the project file location.
    ///
    /// This directory is used as the VFS mount root. Default: "content".
    std::string contentRoot = "content";

    // ── Window configuration ─────────────────────────────────────────────────

    /// Title bar text for the main application window. Default: "UzEngine".
    std::string windowTitle = "UzEngine";

    /// Initial window width in pixels. Default: 1280.
    int windowWidth = 1280;

    /// Initial window height in pixels. Default: 720.
    int windowHeight = 720;

    /// Whether to start in fullscreen mode. Default: false.
    bool fullscreen = false;

    // ── Render configuration ─────────────────────────────────────────────────

    /// Render backend identifier. Valid values: "dx12", "dx11", "vulkan".
    ///
    /// The engine will attempt to initialize the named backend. If the backend
    /// fails to initialize, startup aborts. Default: "dx12".
    std::string renderBackend = "dx12";

    /// Whether to synchronize presentation with the display refresh rate.
    ///
    /// When false the engine renders as fast as possible. Default: false.
    bool vsync = false;

    // ── CVar overrides ───────────────────────────────────────────────────────

    /// Project-level CVar overrides parsed from the optional "cvars" object.
    ///
    /// Each entry is `(name, value)`. Names are looked up in the global
    /// `CVarRegistry` after registration. Values are stored as `float` to
    /// match `Config::AddPendingCvarOverride`; bools should be supplied as
    /// `0` or `1`.
    ///
    /// Order is preserved (parse order), so later entries override earlier
    /// ones for the same name.
    std::vector<std::pair<std::string, float>> cvarOverrides;
};

// ─────────────────────────────────────────────────────────────────────────────
// LoadProject — parse a .uzproject file
// ─────────────────────────────────────────────────────────────────────────────

/// Loads and parses a `.uzproject` JSON file from the real filesystem.
///
/// Reads the file at `path` using `std::ifstream`, then parses it with
/// nlohmann::json. All fields are optional; missing or null fields in the JSON
/// receive the `ProjectConfig` defaults defined in the struct.
///
/// This function reads from the host filesystem, NOT the VFS, because the
/// project file must be resolved before the VFS root (contentRoot) is known.
///
/// # Parameters
/// - `path`: Absolute or relative path to the `.uzproject` file on disk.
///   Platform-native separators are accepted.
///
/// # Returns
/// - Populated `ProjectConfig` on success.
/// - `std::nullopt` if the file cannot be opened, is empty, or contains
///   malformed JSON. Errors are logged at `Config::Error` level.
///
/// # Side Effects
/// - Logs `Config::Info` on success with the project name and default scene.
/// - Logs `Config::Error` on I/O failure or JSON parse error with details.
///
/// # Examples
/// ```cpp
/// auto cfg = uz::LoadProject("E:/MyGame/game.uzproject");
/// if (!cfg) {
///     // Failed — engine should fall back to defaults or abort.
/// }
/// UZ_LOG(Config, Info, "Loaded project: {}", cfg->name);
/// ```
[[nodiscard]] std::optional<ProjectConfig> LoadProject(const std::string& path);

} // namespace uz
