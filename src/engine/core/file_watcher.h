#pragma once

// file_watcher.h -- Monitors a directory for file changes and invokes a callback.
//
// Platform support:
//   Linux:   inotify (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE)
//   Windows: ReadDirectoryChangesW (FILE_NOTIFY_CHANGE_LAST_WRITE)
//   macOS:   stub (watch() returns false, is_watching() returns false)
//
// Threading: the watcher runs on its own background thread created via
// uz::platform::thread_create. The callback is dispatched on the WATCHER
// thread -- callers must ensure thread-safety in their callback.
//
// Debounce: successive events for the same path within the debounce window
// (default: 200 ms) are collapsed into a single callback invocation.

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

enum class FileWatchEvent : uint8_t {
    Modified = 0,
    Created  = 1,
    Deleted  = 2,
};

struct FileChange {
    std::string    path;   // Absolute or relative path to changed file
    FileWatchEvent event;
};

using FileWatchCallback = std::function<void(const FileChange&)>;

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    // Non-copyable, non-movable
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    /// Start watching |directory| for changes. Returns false if the directory
    /// does not exist or the OS watch mechanism cannot be established.
    /// Only one directory can be watched at a time; call stop() first to switch.
    bool watch(const char* directory, FileWatchCallback callback);

    /// Stop watching and join the background thread. Safe to call multiple times.
    void stop();

    /// Returns true if a directory is currently being watched.
    [[nodiscard]] bool is_watching() const noexcept;

    /// Set the debounce window in milliseconds (default: 200).
    /// Must be called before watch() to take effect.
    void set_debounce_ms(uint32_t ms) noexcept;

private:
    static void thread_entry(void* user_data);

    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace uz
