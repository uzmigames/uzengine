/// UzEngine File Watcher — Linux Implementation
///
/// Monitors a directory for file changes using Linux inotify.
/// Runs a dedicated background thread that invokes a user callback
/// when files are created, modified, deleted, or renamed.
///
/// # Use Cases
/// - Shader hot-reload: watch `shaders/` for `.hlsl` / `.glsl` changes
/// - Asset hot-reload: watch `assets/` for texture/mesh changes
/// - Config reload: watch for `.json`/`.ini` modifications
///
/// # Thread Safety
/// - `Start()` / `Stop()` must be called from the same thread (main thread).
/// - The callback is invoked on the watcher's background thread.
/// - The callback must be thread-safe if it accesses shared state.
///
/// # Example
/// ```cpp
/// uz::DirectoryWatcher watcher;
/// watcher.Start("shaders/", true, [](const uz::FileWatchNotification& n, void*) {
///     UZ_LOG(Shader, Info, "File changed: {} (event={})", n.path, (int)n.event);
/// });
/// // ... run game loop ...
/// watcher.Stop();
/// ```

#pragma once

#ifdef __linux__

#include <cstdint>

namespace uz {

/// Type of file system change detected by the watcher.
enum class FileWatchEvent : uint8_t {
    Created,   ///< A new file or directory was created.
    Modified,  ///< An existing file was written to.
    Deleted,   ///< A file or directory was deleted.
    Renamed,   ///< A file or directory was renamed.
};

/// Notification delivered to the watch callback.
struct FileWatchNotification {
    char path[512];       ///< Relative path from watched directory (UTF-8, null-terminated).
    FileWatchEvent event; ///< What happened to the file.
};

/// Callback invoked when a file change is detected.
///
/// # Arguments
/// * `notification` — Details of the change (path and event type).
/// * `userData` — Opaque pointer passed to `Start()`.
///
/// # Thread Safety
/// Called on the watcher's background thread. Must be safe to call
/// concurrently with the main thread.
using FileWatchCallback = void(*)(const FileWatchNotification& notification, void* userData);

/// Watches a directory for file changes using Linux inotify.
///
/// Internally spawns a background thread that blocks on `poll()` over
/// the inotify file descriptor and a self-pipe used as a stop signal.
/// The thread exits cleanly when `Stop()` is called or the object is destroyed.
///
/// Non-copyable: owns OS file descriptors and a background thread.
class DirectoryWatcher {
public:
    /// Constructs an idle watcher. IsWatching() returns false.
    DirectoryWatcher() = default;

    /// Stops watching and joins the background thread if active.
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    /// Starts watching a directory for file changes.
    ///
    /// # Arguments
    /// * `directory` — UTF-8 path to the directory to watch.
    /// * `recursive` — If true, watches all subdirectories as well.
    /// * `callback` — Function called on each file change. Must not be nullptr.
    /// * `userData` — Opaque pointer forwarded to the callback.
    ///
    /// # Returns
    /// - true if watching started successfully.
    /// - false if the directory could not be watched or the thread failed to start.
    ///
    /// # Errors
    /// Logs `FileSystem::Error` on failure.
    /// If already watching, stops the previous watch first.
    bool Start(const char* directory, bool recursive, FileWatchCallback callback, void* userData = nullptr);

    /// Stops watching and joins the background thread.
    ///
    /// Safe to call if not watching (no-op). Blocks until the background
    /// thread exits. After this call, IsWatching() returns false.
    void Stop();

    /// Returns true if the watcher is currently active.
    [[nodiscard]] bool IsWatching() const { return m_watching; }

private:
    int m_inotifyFd = -1;         ///< inotify instance file descriptor.
    int m_pipeFd[2] = {-1, -1};   ///< Self-pipe: [0]=read end, [1]=write end (stop signal).
    void* m_thread = nullptr;     ///< Pointer to std::thread (heap-allocated to avoid header include).
    FileWatchCallback m_callback = nullptr;
    void* m_userData = nullptr;
    char m_directory[512] = {};   ///< Watched root directory path (UTF-8).
    bool m_recursive = false;
    bool m_watching = false;

    /// Background thread entry point — blocks on poll() over inotify fd and pipe.
    void WatchThread();

    /// Adds inotify watches for `dir` and, if recursive, all subdirectories.
    ///
    /// # Returns
    /// true if at least the root directory was watched successfully.
    bool AddWatchRecursive(const char* dir);
};

} // namespace uz

#endif // __linux__
