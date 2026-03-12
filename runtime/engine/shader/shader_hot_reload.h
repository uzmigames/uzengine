/// UzEngine Shader Hot-Reload System
///
/// Watches shader source directories for file changes and propagates dirty
/// flags through the dependency graph. Uses lazy recompile: shaders are only
/// recompiled when next used, not immediately on change.
///
/// # Architecture
/// ```
/// FileWatcher (timestamp polling)
///     │
///     ▼  changed files
/// ShaderHotReload::Poll()
///     │
///     ▼  MarkDirty() + dependency graph propagation
/// Dirty set (consulted by shader cache / PSO cache on next use)
/// ```

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uz {

class ShaderDependencyGraph;

// ─────────────────────────────────────────────────────────────────────────────
// FileWatcher — timestamp-based polling file watcher
// ─────────────────────────────────────────────────────────────────────────────

/// Polls directories for file modifications using filesystem timestamps.
///
/// Tracks `.hlsl`, `.hlsli`, `.h`, `.fx`, `.fxh` files recursively.
/// Designed for simplicity and testability; a `ReadDirectoryChangesW` backend
/// can replace the polling internals later without changing the interface.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from a single thread.
///
/// # Lifetime
/// Standalone. Does not own any OS handles — purely stat-based polling.
///
/// # Examples
/// ```cpp
/// FileWatcher watcher;
/// watcher.Watch("shaders/engine");
/// watcher.Watch("shaders/game");
///
/// // Per-frame poll:
/// auto changed = watcher.PollChanges();
/// for (const auto& path : changed) {
///     // path is normalized (forward slashes, lowercase on Windows)
/// }
/// ```
class FileWatcher {
public:
    FileWatcher() = default;

    /// Adds a directory to watch recursively for shader file changes.
    ///
    /// Performs an initial scan to record baseline timestamps. Only files with
    /// shader extensions (`.hlsl`, `.hlsli`, `.h`, `.fx`, `.fxh`) are tracked.
    ///
    /// # Arguments
    /// * `dir` — Filesystem directory to watch. Must exist.
    void Watch(const std::filesystem::path& dir);

    /// Stops watching all directories and clears all internal timestamp state.
    void StopAll();

    /// Polls for changes since the last call.
    ///
    /// Scans all watched directories and compares timestamps against the
    /// previously recorded values. New files are also detected.
    ///
    /// # Returns
    /// Normalized paths (forward slashes, lowercase on Windows) of all files
    /// whose modification time changed since the last poll. Empty if no changes.
    [[nodiscard]] std::vector<std::string> PollChanges();

    /// Returns true if any directories are currently being watched.
    [[nodiscard]] bool IsWatching() const noexcept;

private:
    /// Scan a directory recursively and record timestamps for shader files.
    void ScanDirectory(const std::filesystem::path& dir);

    /// Check if a file extension is a shader source extension.
    [[nodiscard]] static bool IsShaderExtension(const std::string& ext);

    /// Normalize a path to forward slashes, lowercase on Windows.
    [[nodiscard]] static std::string NormalizePath(const std::filesystem::path& p);

    std::vector<std::filesystem::path> m_watchDirs;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_timestamps;
    bool m_watching = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShaderHotReload — dirty flag management with dependency propagation
// ─────────────────────────────────────────────────────────────────────────────

/// Manages shader hot-reload by combining file watching with dependency-aware
/// dirty flag propagation.
///
/// Call `Poll()` once per frame to detect changes. Query `IsDirty()` before
/// using a cached shader to decide whether recompilation is needed.
///
/// # Thread Safety
/// NOT thread-safe. All calls must be on the same thread (render/main thread).
///
/// # Lifetime
/// Optionally references a `ShaderDependencyGraph` via raw pointer (set by
/// `SetDependencyGraph()`). The graph must outlive the hot-reload instance
/// or be set to `nullptr` before the graph is destroyed.
///
/// # Examples
/// ```cpp
/// ShaderHotReload hotReload;
/// hotReload.SetDependencyGraph(&depGraph);
/// hotReload.Start({std::filesystem::path("shaders/engine")});
///
/// // Per-frame:
/// auto dirty = hotReload.Poll();
/// for (const auto& path : dirty) {
///     recompileShader(path);
///     hotReload.ClearDirty(path);
/// }
/// ```
class ShaderHotReload {
public:
    ShaderHotReload() = default;

    /// Starts watching the given directories for shader file changes.
    ///
    /// # Arguments
    /// * `watchDirs` — List of filesystem directories to watch recursively.
    void Start(const std::vector<std::filesystem::path>& watchDirs);

    /// Stops watching all directories and clears the file watcher state.
    /// Does NOT clear the dirty set — use `ClearAll()` for that.
    void Stop();

    /// Polls for file changes (non-blocking timestamp comparison).
    ///
    /// For each changed file, marks it dirty and propagates the dirty flag
    /// to all transitive dependents via the dependency graph (if set).
    ///
    /// # Returns
    /// Virtual paths of all shaders newly marked dirty in this poll cycle.
    [[nodiscard]] std::vector<std::string> Poll();

    /// Checks if a shader path is marked dirty (needs recompile).
    ///
    /// # Arguments
    /// * `shaderPath` — Virtual or normalized path to check.
    ///
    /// # Returns
    /// `true` if the shader is in the dirty set.
    [[nodiscard]] bool IsDirty(const std::string& shaderPath) const;

    /// Marks a shader and all its transitive dependents as dirty.
    ///
    /// If a dependency graph is set, walks the reverse graph to find all
    /// root shaders that include this file (directly or transitively).
    ///
    /// # Arguments
    /// * `shaderPath` — Path of the shader/include that changed.
    void MarkDirty(const std::string& shaderPath);

    /// Clears the dirty flag for a single shader (after successful recompile).
    ///
    /// # Arguments
    /// * `shaderPath` — Path to clear from the dirty set.
    void ClearDirty(const std::string& shaderPath);

    /// Clears all dirty flags.
    void ClearAll();

    /// Returns all currently dirty shader paths as a vector.
    [[nodiscard]] std::vector<std::string> GetDirtyShaders() const;

    /// Returns the number of currently dirty shaders.
    [[nodiscard]] size_t GetDirtyCount() const noexcept;

    /// Sets the dependency graph for transitive dirty propagation.
    ///
    /// # Arguments
    /// * `graph` — Pointer to a `ShaderDependencyGraph`, or `nullptr` to disable
    ///   transitive propagation (only the directly changed file is marked dirty).
    void SetDependencyGraph(ShaderDependencyGraph* graph) noexcept;

    /// Returns true if the file watcher is currently running.
    [[nodiscard]] bool IsRunning() const noexcept;

private:
    std::unordered_set<std::string> m_dirtySet;
    ShaderDependencyGraph* m_depGraph = nullptr;
    FileWatcher m_watcher;
    bool m_running = false;
};

} // namespace uz
