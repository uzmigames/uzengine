#pragma once

/// VFX graph hot-reload -- watches .uzvfx files and reloads on change.
///
/// When a .uzvfx file is modified on disk, the hot-reload system:
/// 1. Reloads the VFXGraph from the modified file
/// 2. Recomposes compute shaders via ModuleCompositor
/// 3. Iterates all active emitters using that graph
/// 4. Swaps their module stack and marks composition as dirty
///
/// Only active in dev builds (`UZ_DEV_BUILD` or `!NDEBUG`).
///
/// # Thread Safety
/// Not thread-safe. Poll() must be called from the main thread.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class VFXSystem;
class ModuleRegistry;

/// Callback invoked when a VFX graph is reloaded.
///
/// # Parameters
/// - `path`: Normalized filesystem path of the reloaded .uzvfx file.
using VFXReloadCallback = std::function<void(const std::string& path)>;

// ---------------------------------------------------------------------------
// VFXHotReload -- file watcher for .uzvfx graphs
// ---------------------------------------------------------------------------

/// Watches .uzvfx files for modifications and triggers graph reload.
///
/// Uses `std::filesystem::last_write_time` for timestamp-based change
/// detection. New files added after initialization are NOT detected --
/// only files present at scan time are tracked.
///
/// # Lifetime
/// - `vfxSystem` and `registry` must outlive this object.
/// - Call `Shutdown()` before destroying the VFXSystem.
///
/// # Examples
/// ```cpp
/// VFXHotReload hotReload;
/// hotReload.Initialize("content/vfx/", &vfxSystem, &registry);
/// // Per-frame:
/// hotReload.Poll();
/// hotReload.Shutdown();
/// ```
class VFXHotReload {
public:
    VFXHotReload() = default;
    ~VFXHotReload();

    /// Non-copyable (owns file watch state).
    VFXHotReload(const VFXHotReload&) = delete;
    VFXHotReload& operator=(const VFXHotReload&) = delete;

    /// Initializes the hot-reload watcher for the given content directory.
    ///
    /// Recursively scans `contentDir` for `.uzvfx` files and records their
    /// initial modification timestamps. Subsequent `Poll()` calls detect
    /// changes relative to these baselines.
    ///
    /// # Parameters
    /// - `contentDir`: Root content directory to watch (e.g., "content/vfx/").
    ///   Must exist on disk; logs a warning and returns early if not found.
    /// - `vfxSystem`: VFX system for accessing active emitters. Non-null.
    /// - `registry`: Module registry for graph reloading. Non-null.
    ///
    /// # Side Effects
    /// - Logs `VFX/Info` with the directory path and number of tracked files.
    /// - Logs `VFX/Warning` if the directory does not exist.
    void Initialize(const std::string& contentDir, VFXSystem* vfxSystem,
                    const ModuleRegistry* registry);

    /// Shuts down the file watcher and clears all internal state.
    ///
    /// Safe to call multiple times or on an uninitialized instance.
    ///
    /// # Side Effects
    /// Logs `VFX/Info` on shutdown.
    void Shutdown();

    /// Polls for file changes and triggers reloads. Call once per frame.
    ///
    /// Iterates all watched `.uzvfx` files and compares their current
    /// modification timestamp against the stored baseline. For each changed
    /// file, calls `ReloadGraph()`.
    ///
    /// # Side Effects
    /// - Logs `VFX/Info` for each reloaded graph.
    /// - Invokes all registered `VFXReloadCallback`s.
    void Poll();

    /// Registers a callback invoked after each successful graph reload.
    ///
    /// Multiple callbacks can be registered. They are invoked in
    /// registration order with the path of the reloaded file.
    ///
    /// # Parameters
    /// - `callback`: Non-null callback. Moved into internal storage.
    void OnReload(VFXReloadCallback callback);

    /// Returns the total number of reloads performed since initialization.
    [[nodiscard]] uint32_t GetReloadCount() const noexcept { return reloadCount_; }

    /// Returns true if `Initialize()` has been called and `Shutdown()` has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

    /// Returns the number of .uzvfx files currently being watched.
    [[nodiscard]] uint32_t GetWatchedFileCount() const noexcept {
        return static_cast<uint32_t>(watchedFiles_.size());
    }

private:
    /// Reloads a single VFX graph file and notifies callbacks.
    void ReloadGraph(const std::string& path);

    /// Scans a directory recursively for .uzvfx files, recording timestamps.
    void ScanDirectory(const std::string& dir);

    /// Normalizes a filesystem path (forward slashes, lowercase on Windows).
    [[nodiscard]] static std::string NormalizePath(const std::string& rawPath);

    VFXSystem* vfxSystem_ = nullptr;
    const ModuleRegistry* registry_ = nullptr;
    std::string contentDir_;
    bool initialized_ = false;
    uint32_t reloadCount_ = 0;

    /// File modification timestamps for change detection.
    std::unordered_map<std::string, uint64_t> fileTimestamps_;

    /// Registered reload callbacks.
    std::vector<VFXReloadCallback> callbacks_;

    /// Known .uzvfx file paths being watched.
    std::vector<std::string> watchedFiles_;
};

} // namespace uz
