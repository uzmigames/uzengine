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
/// Designed for simplicity and testability; a ReadDirectoryChangesW backend
/// can replace the polling internals later without changing the interface.
class FileWatcher {
public:
    FileWatcher() = default;

    /// Add a directory to watch recursively for shader file changes.
    void Watch(const std::filesystem::path& dir);

    /// Stop watching all directories and clear internal state.
    void StopAll();

    /// Poll for changes since last call. Returns normalized paths of changed files.
    [[nodiscard]] std::vector<std::string> PollChanges();

    /// Whether any directories are currently being watched.
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
class ShaderHotReload {
public:
    ShaderHotReload() = default;

    /// Start watching the given directories for shader file changes.
    void Start(const std::vector<std::filesystem::path>& watchDirs);

    /// Stop watching all directories.
    void Stop();

    /// Poll for file changes (non-blocking). Returns shaders marked dirty this poll.
    [[nodiscard]] std::vector<std::string> Poll();

    /// Check if a shader path is marked dirty (needs recompile).
    [[nodiscard]] bool IsDirty(const std::string& shaderPath) const;

    /// Mark a shader and all its transitive dependents as dirty.
    void MarkDirty(const std::string& shaderPath);

    /// Clear the dirty flag for a single shader (after successful recompile).
    void ClearDirty(const std::string& shaderPath);

    /// Clear all dirty flags.
    void ClearAll();

    /// Get all currently dirty shader paths.
    [[nodiscard]] std::vector<std::string> GetDirtyShaders() const;

    /// Get the number of currently dirty shaders.
    [[nodiscard]] size_t GetDirtyCount() const noexcept;

    /// Set the dependency graph for transitive dirty propagation.
    /// Null is valid — disables transitive propagation.
    void SetDependencyGraph(ShaderDependencyGraph* graph) noexcept;

    /// Whether the file watcher is currently running.
    [[nodiscard]] bool IsRunning() const noexcept;

private:
    std::unordered_set<std::string> m_dirtySet;
    ShaderDependencyGraph* m_depGraph = nullptr;
    FileWatcher m_watcher;
    bool m_running = false;
};

} // namespace uz
