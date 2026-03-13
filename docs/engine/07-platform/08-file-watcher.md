# Platform — File Watcher

> **Status**: Done (Phase 4.6.0)
> **Source**: `platform/windows/file_watcher.h`
> **Depends on**: Win32 `ReadDirectoryChangesW`, overlapped I/O

## Purpose

Hot-reload requires detecting file changes: when a shader is modified, the asset system recompiles it automatically; when a scene is saved, the editor reloads it. The `FileWatcher` class monitors a directory for create/modify/delete/rename events and invokes a callback on changes. The background thread handles all detection logic, decoupling file change detection from asset hot-reload logic.

## Architecture

```
FileWatcher (main thread API)
    │
    ├── Start(directory, recursive, callback, userData) → bool
    ├── Stop() → void
    └── IsWatching() → bool
        │
        └── Background Thread (WatchThread)
            ├── ReadDirectoryChangesW (overlapped I/O)
            ├── FILE_NOTIFY_INFORMATION parsing
            └── Callback invocation on main thread via queue
```

## File Watch Events

Events indicate what type of file system change occurred:

```cpp
/// Type of file system change detected by the watcher.
enum class FileWatchEvent : uint8_t {
    Created,   // A new file or directory was created
    Modified,  // An existing file was written to
    Deleted,   // A file or directory was deleted
    Renamed,   // A file or directory was renamed
};

/// Notification delivered to the watch callback.
struct FileWatchNotification {
    char path[512];       // Relative path from watched directory (UTF-8, null-terminated)
    FileWatchEvent event; // What happened to the file
};
```

## Callback Function Type

The callback is invoked when the watcher detects a file change. It runs on the watcher's background thread and must be thread-safe.

```cpp
/// Callback invoked when a file change is detected.
///
/// # Arguments
/// * notification — Details of the change (path and event type).
/// * userData — Opaque pointer passed to Start().
///
/// # Thread Safety
/// Called on the watcher's background thread. Must be safe to call
/// concurrently with the main thread.
using FileWatchCallback = void(*)(const FileWatchNotification& notification, void* userData);
```

## FileWatcher Class

Watches a directory for file changes using `ReadDirectoryChangesW`. Internally spawns a background thread that blocks on overlapped I/O. The thread exits cleanly when `Stop()` is called or the object is destroyed.

### Constructor and Destructor

```cpp
/// Constructs an idle watcher. IsWatching() returns false.
FileWatcher() = default;

/// Stops watching and joins the background thread if active.
~FileWatcher();

// Non-copyable: owns OS handles and a background thread
FileWatcher(const FileWatcher&) = delete;
FileWatcher& operator=(const FileWatcher&) = delete;
```

### Start — Begin monitoring a directory

```cpp
/// Starts watching a directory for file changes.
///
/// # Arguments
/// * directory — UTF-8 path to the directory to watch.
/// * recursive — If true, watches all subdirectories as well.
/// * callback — Function called on each file change. Must not be nullptr.
/// * userData — Opaque pointer forwarded to the callback.
///
/// # Returns
/// - true if watching started successfully.
/// - false if the directory could not be opened or the thread failed to start.
///
/// # Errors
/// Logs FileSystem::Error on failure.
/// If already watching, stops the previous watch first.
bool Start(const char* directory, bool recursive, FileWatchCallback callback, void* userData = nullptr);

// Usage
uz::FileWatcher watcher;
watcher.Start("shaders/", true, [](const uz::FileWatchNotification& n, void*) {
    UZ_LOG(Shader, Info, "File changed: {} (event={})", n.path, (int)n.event);
});
```

### Stop — Stop monitoring and join background thread

```cpp
/// Stops watching and joins the background thread.
///
/// Safe to call if not watching (no-op). Blocks until the background
/// thread exits. After this call, IsWatching() returns false.
void Stop();

// Usage
watcher.Stop();  // Safe to call multiple times
```

### IsWatching — Query if watcher is active

```cpp
/// Returns true if the watcher is currently active.
[[nodiscard]] bool IsWatching() const { return m_watching; }

// Usage
if (watcher.IsWatching()) {
    UZ_LOG(Asset, Info, "File watcher is running");
}
```

## Internal Implementation

The `FileWatcher` uses Win32's `ReadDirectoryChangesW` API to monitor directory changes efficiently:

```cpp
private:
    void* m_dirHandle = nullptr;       // Win32 HANDLE to the watched directory
    void* m_stopEvent = nullptr;       // Win32 manual-reset event to signal shutdown
    void* m_thread = nullptr;          // Pointer to std::thread (heap-allocated)
    FileWatchCallback m_callback = nullptr;
    void* m_userData = nullptr;
    bool m_recursive = false;
    bool m_watching = false;

    /// Background thread entry point — blocks on ReadDirectoryChangesW.
    void WatchThread();
```

### WatchThread — Background monitoring loop

The background thread:
1. Opens the watched directory with `CreateFileW`
2. Allocates a 4KB buffer for change notifications
3. Blocks on `ReadDirectoryChangesW` waiting for changes
4. Parses `FILE_NOTIFY_INFORMATION` structures
5. Converts change actions to `FileWatchEvent` types
6. Invokes the callback for each change
7. Loops until `Stop()` signals shutdown

The thread monitors these Win32 change flags:
- `FILE_NOTIFY_CHANGE_FILE_NAME` — file creation/deletion/rename
- `FILE_NOTIFY_CHANGE_DIR_NAME` — directory creation/deletion/rename
- `FILE_NOTIFY_CHANGE_LAST_WRITE` — file modification

## Usage Patterns

### Basic Shader Hot-Reload

```cpp
uz::FileWatcher shader_watcher;

void InitShaderHotReload() {
    shader_watcher.Start("shaders/", true, OnShaderChanged);
}

void OnShaderChanged(const uz::FileWatchNotification& n, void*) {
    if (n.event != uz::FileWatchEvent::Modified) return;

    // Only recompile if it's a shader file
    if (!StringEndsWithIgnoreCase(n.path, ".hlsl")) return;

    UZ_LOG(Shader, Info, "Shader hot-reload: {}", n.path);
    g_shader_cache->InvalidateShader(n.path);
}

void Shutdown() {
    shader_watcher.Stop();
}
```

### Asset Hot-Reload with User Data

```cpp
struct AssetHotReloadContext {
    AssetSystem* asset_system;
    bool is_dirty = false;
};

uz::FileWatcher asset_watcher;

void InitAssetHotReload(AssetSystem* sys) {
    auto ctx = new AssetHotReloadContext{sys};
    asset_watcher.Start("assets/", true, OnAssetChanged, ctx);
}

void OnAssetChanged(const uz::FileWatchNotification& n, void* user_data) {
    auto ctx = static_cast<AssetHotReloadContext*>(user_data);

    switch (n.event) {
        case uz::FileWatchEvent::Modified:
            UZ_LOG(Asset, Info, "Asset modified: {}", n.path);
            ctx->asset_system->InvalidateAsset(n.path);
            ctx->is_dirty = true;
            break;

        case uz::FileWatchEvent::Deleted:
            UZ_LOG(Asset, Warning, "Asset deleted: {}", n.path);
            ctx->asset_system->UnloadAsset(n.path);
            break;

        case uz::FileWatchEvent::Created:
            UZ_LOG(Asset, Info, "Asset created: {}", n.path);
            // Will be loaded on next access
            break;

        case uz::FileWatchEvent::Renamed:
            // Handle renaming logic
            UZ_LOG(Asset, Info, "Asset renamed: {}", n.path);
            break;
    }
}
```

### Multiple Watched Directories

```cpp
uz::FileWatcher shader_watcher;
uz::FileWatcher texture_watcher;
uz::FileWatcher scene_watcher;

void InitAllWatchers() {
    shader_watcher.Start("shaders/", true, OnShaderChanged);
    texture_watcher.Start("textures/", true, OnTextureChanged);
    scene_watcher.Start("scenes/", false, OnSceneChanged);  // Non-recursive
}

void ShutdownAllWatchers() {
    shader_watcher.Stop();
    texture_watcher.Stop();
    scene_watcher.Stop();
}
```

## Thread Safety

**Critical**: The callback is invoked on the watcher's background thread, not the main thread.

```cpp
// ❌ WRONG: Directly modifying main thread state in callback
void OnAssetChanged(const uz::FileWatchNotification& n, void* ctx) {
    // UNSAFE: g_renderer is accessed from background thread!
    g_renderer->InvalidateShader(n.path);
}

// ✅ CORRECT: Mark dirty, let main thread handle cleanup
struct WatchContext {
    std::atomic<bool> is_dirty{false};
    std::string dirty_path;
};

void OnAssetChanged(const uz::FileWatchNotification& n, void* user_data) {
    auto ctx = static_cast<WatchContext*>(user_data);
    ctx->dirty_path = n.path;
    ctx->is_dirty = true;  // Atomic write
}

// In main thread's frame update:
if (watch_ctx.is_dirty) {
    g_renderer->InvalidateShader(watch_ctx.dirty_path);
    watch_ctx.is_dirty = false;
}
```

## Limitations and Behavior

1. **Recursive watching**: If `recursive=true`, the background thread monitors the entire directory tree including subdirectories added after watching starts.

2. **Path format**: Paths in notifications are relative to the watched directory and use the path separator as provided to `Start()`.

3. **Buffer overflow**: The background thread uses a 4KB buffer for notifications. If many files change rapidly and the buffer fills, some changes may be lost. In practice, this is rare for typical asset workflows.

4. **Non-atomic rename events**: Win32 sends two events for renames (old name + new name). The implementation currently reports both to the callback.

5. **Empty directory handling**: If no entries match the pattern, `DirectoryEnumerate()` returns an empty vector (not an error).

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| File watching | `FDirectoryWatcherWindows` + callback queue | Directory polling | `uz::FileWatcher` background thread |
| Event types | Created, Modified, Deleted, Moved | Polling-based | Created, Modified, Deleted, Renamed |
| Recursive | Yes (tree walk) | Manual | Yes (per-directory via `recursive` flag) |
| Thread safety | Queued to main thread | N/A | Callback from worker thread (user must sync) |
| Delay | Immediate (< 100ms) | Per-poll interval | Immediate (< 10ms typical on NTFS) |
| Multiple watches | Via separate instances | N/A | Yes, multiple independent watchers |

## Rules

1. **ALWAYS assume callbacks may come from worker threads** — synchronize with main thread using atomics or message queues
2. **NEVER block in watch callback** — just set a flag or queue a message and return
3. **NEVER watch root directory or system directories** — watch specific asset directories instead
4. **ALWAYS call `Stop()` before destroying watcher** — prevents dangling thread joins (auto-stopped in destructor)
5. **ALWAYS batch file changes in frame** — coalesce rapid changes to avoid asset thrashing
6. **Use `userData` parameter to pass context** — avoids static variables and thread-local storage
