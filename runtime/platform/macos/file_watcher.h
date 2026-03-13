/// UzEngine File Watcher — macOS Implementation
///
/// Monitors a directory for file changes using the FSEvents API (CoreServices).
/// Runs a dedicated background thread hosting a CFRunLoop that receives
/// file-level events from the kernel and invokes a user callback.
///
/// # Use Cases
/// - Shader hot-reload: watch `shaders/` for `.hlsl` / `.metal` changes
/// - Asset hot-reload: watch `assets/` for texture/mesh changes
/// - Config reload: watch for `.json`/`.ini` modifications
///
/// # Thread Safety
/// - `Start()` / `Stop()` must be called from the same thread (main thread).
/// - The callback is invoked on the watcher's background thread (the CFRunLoop thread).
/// - The callback must be thread-safe if it accesses shared state.
///
/// # Platform Notes
/// FSEvents coalesces rapid changes with a configurable latency. This
/// implementation uses 100 ms, which is a good balance between responsiveness
/// and notification storm prevention for hot-reload workloads.
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

#ifdef __APPLE__

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
    char path[512];       ///< Absolute path of the changed file (UTF-8, null-terminated).
    FileWatchEvent event; ///< What happened to the file.
};

/// Callback invoked when a file change is detected.
///
/// # Arguments
/// * `notification` — Details of the change (path and event type).
/// * `userData` — Opaque pointer passed to `Start()`.
///
/// # Thread Safety
/// Called on the watcher's background CFRunLoop thread. Must be safe to call
/// concurrently with the main thread.
using FileWatchCallback = void(*)(const FileWatchNotification& notification, void* userData);

/// Watches a directory for file changes using the macOS FSEvents API.
///
/// Internally spawns a background thread that runs a CFRunLoop scheduled with
/// an FSEventStream. The stream delivers coalesced file-level events (using
/// `kFSEventStreamCreateFlagFileEvents`) with a 100 ms latency window.
/// `Stop()` stops the FSEventStream, stops the CFRunLoop, and joins the thread.
///
/// Non-copyable: owns OS handles, a CFRunLoop reference, and a background thread.
class DirectoryWatcher {
public:
    /// Constructs an idle watcher. IsWatching() returns false.
    DirectoryWatcher() = default;

    /// Stops watching and joins the background thread if active.
    ~DirectoryWatcher();

    DirectoryWatcher(const DirectoryWatcher&)            = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

    /// Starts watching a directory for file changes.
    ///
    /// Creates an FSEventStream scheduled on a dedicated CFRunLoop thread.
    /// If already watching, the previous watch is stopped first.
    ///
    /// # Arguments
    /// * `directory` — UTF-8 absolute path to the directory to watch.
    /// * `recursive` — If true, watches all subdirectories as well (FSEvents
    ///                  is inherently recursive; this flag is stored for
    ///                  informational purposes and future event filtering).
    /// * `callback`  — Function called on each file change. Must not be nullptr.
    /// * `userData`  — Opaque pointer forwarded to the callback.
    ///
    /// # Returns
    /// - true if the FSEventStream was created and the background thread started.
    /// - false if `directory` is empty, `callback` is nullptr, or FSEvents setup fails.
    ///
    /// # Errors
    /// Logs `FileSystem::Error` on failure with the reason.
    bool Start(const char* directory, bool recursive, FileWatchCallback callback, void* userData = nullptr);

    /// Stops watching and joins the background thread.
    ///
    /// Stops and invalidates the FSEventStream, stops the CFRunLoop, and joins
    /// the background thread. Safe to call if not watching (no-op).
    /// Blocks until the background thread exits. After this call, IsWatching()
    /// returns false.
    void Stop();

    /// Returns true if the watcher is currently active.
    [[nodiscard]] bool IsWatching() const { return m_watching; }

private:
    void* m_stream   = nullptr; ///< FSEventStreamRef (opaque; avoids CoreServices in public header).
    void* m_runLoop  = nullptr; ///< CFRunLoopRef of the background thread (set by WatchThread).
    void* m_thread   = nullptr; ///< Heap-allocated std::thread (avoids <thread> in public header).
    FileWatchCallback m_callback = nullptr;
    void* m_userData = nullptr;
    char  m_directory[512] = {}; ///< Watched root directory path (UTF-8).
    bool  m_recursive = false;
    bool  m_watching  = false;

    /// Background thread entry point — hosts the CFRunLoop that drives the FSEventStream.
    ///
    /// Captures this thread's `CFRunLoopRef` into `m_runLoop`, schedules and starts the
    /// stream, then calls `CFRunLoopRun()`. Returns only after `Stop()` calls
    /// `CFRunLoopStop()`.
    void WatchThread();

    /// FSEvents C callback — static member so it has access to private fields via context.
    ///
    /// The FSEvents API requires a plain C function pointer. A static member function
    /// satisfies this requirement. The `FsEventContext` passed as `clientCallBackInfo`
    /// carries a back-pointer to the owning `DirectoryWatcher`.
    ///
    /// # Arguments
    /// * `streamRef`        — The event stream (unused; context carries the watcher).
    /// * `clientCallBackInfo` — Pointer to `FsEventContext` (cast from void*).
    /// * `numEvents`        — Number of events in this batch.
    /// * `eventPaths`       — `char**` array of absolute file paths.
    /// * `eventFlags`       — Parallel array of FSEventStreamEventFlags bitmasks.
    /// * `eventIds`         — Parallel array of event IDs (unused).
    static void FsEventsCallback(
        const void*    streamRef,
        void*          clientCallBackInfo,
        size_t         numEvents,
        void*          eventPaths,
        const unsigned int* eventFlags,
        const uint64_t*     eventIds
    );
};

} // namespace uz

#endif // __APPLE__
