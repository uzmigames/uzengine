/// UzEngine File Watcher — macOS Implementation
///
/// Uses FSEvents (`FSEventStreamCreate` + `kFSEventStreamCreateFlagFileEvents`)
/// for file-level change notifications. A dedicated background thread runs a
/// CFRunLoop that the FSEventStream is scheduled on, keeping the main thread
/// completely free. `Stop()` calls `FSEventStreamStop()` then `CFRunLoopStop()`
/// to cleanly unblock `CFRunLoopRun()`, after which the thread is joined.
///
/// Raw FSEventStreamEventFlags are mapped to the engine's `FileWatchEvent` enum
/// with the following priority (highest wins when multiple flags are set):
///   kFSEventStreamEventFlagItemRemoved   → FileWatchEvent::Deleted
///   kFSEventStreamEventFlagItemRenamed   → FileWatchEvent::Renamed
///   kFSEventStreamEventFlagItemCreated   → FileWatchEvent::Created
///   (anything else)                      → FileWatchEvent::Modified

#ifdef __APPLE__

#include "file_watcher.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>
#include <thread>

#include <CoreServices/CoreServices.h>

UZ_DECLARE_LOG_CATEGORY(FileSystem);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal context type
// ─────────────────────────────────────────────────────────────────────────────

/// Opaque context block carried through `FSEventStreamContext.info`.
///
/// Heap-allocated in `Start()` and freed by the `FSEventStreamContext.release`
/// callback when `FSEventStreamRelease()` is called in `Stop()`. Using heap
/// allocation decouples the context lifetime from the `DirectoryWatcher` stack
/// frame — the stream may call the release callback after `Stop()` returns.
struct FsEventContext {
    DirectoryWatcher* watcher; ///< Back-pointer to the owning DirectoryWatcher.
};

// ─────────────────────────────────────────────────────────────────────────────
// FSEvents callback (static member)
// ─────────────────────────────────────────────────────────────────────────────

/// Maps an FSEvents flags word to the most semantically significant `FileWatchEvent`.
///
/// FSEvents may set multiple flags simultaneously (e.g., Created + Modified on
/// an atomic-save rename). Priority order — Deleted > Renamed > Created > Modified
/// — ensures callers receive the most meaningful event per notification.
///
/// # Arguments
/// * `flags` — FSEventStreamEventFlags bitmask from the OS.
///
/// # Returns
/// The best-fitting `FileWatchEvent` for the flags combination.
static FileWatchEvent MapFsFlags(FSEventStreamEventFlags flags) {
    if (flags & kFSEventStreamEventFlagItemRemoved) return FileWatchEvent::Deleted;
    if (flags & kFSEventStreamEventFlagItemRenamed)  return FileWatchEvent::Renamed;
    if (flags & kFSEventStreamEventFlagItemCreated)  return FileWatchEvent::Created;
    return FileWatchEvent::Modified;
}

/// Static FSEvents C callback — dispatched on the CFRunLoop thread for each event batch.
///
/// Iterates all paths/flags in the batch. Pure directory-modify events (a
/// directory's mtime changing because a child file changed) are suppressed — we
/// care about file-level events. All other events are forwarded to the engine
/// callback stored on the owning `DirectoryWatcher`.
///
/// The static member signature is compatible with the FSEvents C function-pointer
/// type. The `clientCallBackInfo` carries a `FsEventContext*` with the watcher.
void DirectoryWatcher::FsEventsCallback(
    const void* /*streamRef*/,
    void*       clientCallBackInfo,
    size_t      numEvents,
    void*       eventPaths,
    const unsigned int* eventFlags,
    const uint64_t*     /*eventIds*/
) {
    auto* ctx    = static_cast<FsEventContext*>(clientCallBackInfo);
    auto** paths = static_cast<char**>(eventPaths);

    DirectoryWatcher* self = ctx->watcher;

    for (size_t i = 0; i < numEvents; ++i) {
        const FSEventStreamEventFlags flags = eventFlags[i];

        // Skip kernel meta-events (history-done, dropped events, mount/unmount).
        // These carry no actionable file path and would produce spurious callbacks.
        constexpr FSEventStreamEventFlags kMetaMask =
            kFSEventStreamEventFlagRootChanged   |
            kFSEventStreamEventFlagHistoryDone   |
            kFSEventStreamEventFlagKernelDropped |
            kFSEventStreamEventFlagUserDropped   |
            kFSEventStreamEventFlagMount         |
            kFSEventStreamEventFlagUnmount;
        if (flags & kMetaMask) continue;

        // Suppress pure directory-modify events (directory mtime bump caused by
        // a child file change). Allow Created/Deleted/Renamed on directories
        // through because those represent real structural changes.
        const bool isDir = (flags & kFSEventStreamEventFlagItemIsDir) != 0;
        const bool isDirOnlyModify =
            isDir &&
            !(flags & kFSEventStreamEventFlagItemCreated) &&
            !(flags & kFSEventStreamEventFlagItemRemoved) &&
            !(flags & kFSEventStreamEventFlagItemRenamed);
        if (isDirOnlyModify) continue;

        const char* path = paths[i];
        if (!path || path[0] == '\0') continue;

        FileWatchNotification notif{};
        const size_t copyLen = std::min(std::strlen(path), sizeof(notif.path) - 1);
        std::memcpy(notif.path, path, copyLen);
        notif.path[copyLen] = '\0';
        notif.event = MapFsFlags(flags);

        self->m_callback(notif, self->m_userData);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WatchThread — hosts the CFRunLoop that drives the FSEventStream
// ─────────────────────────────────────────────────────────────────────────────

/// Background thread entry point.
///
/// Captures this thread's CFRunLoopRef into `m_runLoop` (enabling `Stop()` to
/// call `CFRunLoopStop()` from outside), then schedules and starts the stream,
/// and finally blocks in `CFRunLoopRun()`. Returns only after `Stop()` calls
/// `CFRunLoopStop()`.
///
/// The `m_runLoop` assignment happens before `CFRunLoopRun()` so `Stop()` can
/// observe a non-null `m_runLoop` and safely call `CFRunLoopStop()`.
void DirectoryWatcher::WatchThread() {
    UZ_PROFILE_FUNCTION();

    // CFRunLoopGetCurrent() is only valid on the calling thread — store it so
    // Stop() can call CFRunLoopStop() from the main thread.
    m_runLoop = CFRunLoopGetCurrent();

    auto* stream  = static_cast<FSEventStreamRef>(m_stream);
    auto* runLoop = static_cast<CFRunLoopRef>(m_runLoop);

    // Schedule on this thread's run loop. Must be done here — run loops are
    // thread-local on macOS; scheduling from Start() (main thread) would attach
    // the stream to the wrong run loop.
    FSEventStreamScheduleWithRunLoop(stream, runLoop, kCFRunLoopDefaultMode);
    FSEventStreamStart(stream);

    UZ_LOG(FileSystem, Info, "DirectoryWatcher: FSEvents run loop started for '{}'", m_directory);

    // Blocks until CFRunLoopStop() is called from Stop().
    CFRunLoopRun();

    UZ_LOG(FileSystem, Info, "DirectoryWatcher: FSEvents run loop exited for '{}'", m_directory);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DirectoryWatcher::~DirectoryWatcher() {
    Stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Start / Stop
// ─────────────────────────────────────────────────────────────────────────────

bool DirectoryWatcher::Start(
    const char*       directory,
    bool              recursive,
    FileWatchCallback callback,
    void*             userData
) {
    UZ_PROFILE_FUNCTION();

    if (!callback) {
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: callback must not be nullptr");
        return false;
    }

    if (!directory || directory[0] == '\0') {
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: empty or invalid directory path");
        return false;
    }

    // Stop the previous watch if already active.
    if (m_watching) {
        Stop();
    }

    // Persist directory for logging and the WatchThread.
    const size_t copyLen = std::min(std::strlen(directory), sizeof(m_directory) - 1);
    std::memcpy(m_directory, directory, copyLen);
    m_directory[copyLen] = '\0';

    m_callback  = callback;
    m_userData  = userData;
    m_recursive = recursive;

    // Build the single-element CFArray of paths for FSEventStreamCreate.
    CFStringRef pathStr = CFStringCreateWithCString(
        kCFAllocatorDefault, directory, kCFStringEncodingUTF8);
    if (!pathStr) {
        UZ_LOG(FileSystem, Error,
            "DirectoryWatcher::Start: CFStringCreateWithCString failed for '{}'", directory);
        return false;
    }

    CFArrayRef pathsToWatch = CFArrayCreate(
        kCFAllocatorDefault,
        reinterpret_cast<const void**>(&pathStr),
        1,
        &kCFTypeArrayCallBacks);
    CFRelease(pathStr);

    if (!pathsToWatch) {
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: CFArrayCreate failed");
        return false;
    }

    // Heap-allocate the context so it outlives this function frame.
    // The FSEventStreamContext.release callback deletes it when the stream is
    // released in Stop(), which may be after the DirectoryWatcher stack frame
    // has already exited in edge-case shutdown sequences.
    auto* ctx = new FsEventContext{ this };

    FSEventStreamContext streamCtx{};
    streamCtx.version = 0;
    streamCtx.info    = ctx;
    streamCtx.retain  = nullptr;
    streamCtx.release = [](const void* info) noexcept {
        // Called by FSEventStreamRelease. Frees the context block.
        delete static_cast<const FsEventContext*>(info);
    };
    streamCtx.copyDescription = nullptr;

    // kFSEventStreamCreateFlagFileEvents: one event per file (not per directory).
    // kFSEventStreamCreateFlagNoDefer: flush after latency even if the run loop
    //   is idle, ensuring hot-reload triggers within ~100 ms of the write.
    constexpr FSEventStreamCreateFlags kStreamFlags =
        kFSEventStreamCreateFlagFileEvents |
        kFSEventStreamCreateFlagNoDefer;

    // 100 ms coalescing: responsive enough for hot-reload, avoids storms on
    // bulk writes (e.g., `git checkout`, shader batch compilation).
    constexpr CFAbsoluteTime kLatencySeconds = 0.1;

    FSEventStreamRef stream = FSEventStreamCreate(
        kCFAllocatorDefault,
        // Static member function — decays to a plain C function pointer,
        // satisfying the FSEvents C callback requirement.
        reinterpret_cast<FSEventStreamCallback>(&DirectoryWatcher::FsEventsCallback),
        &streamCtx,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow, // Deliver events from this moment forward.
        kLatencySeconds,
        kStreamFlags
    );

    CFRelease(pathsToWatch);

    if (!stream) {
        UZ_LOG(FileSystem, Error,
            "DirectoryWatcher::Start: FSEventStreamCreate failed for '{}'", directory);
        delete ctx; // Release context — stream never took ownership.
        return false;
    }

    m_stream   = stream;
    m_watching = true;

    // Spawn the background thread. WatchThread schedules the stream on its own
    // CFRunLoop — run loops are thread-local, so scheduling must happen there.
    auto* thread = new std::thread(&DirectoryWatcher::WatchThread, this);
    m_thread = thread;

    UZ_LOG(FileSystem, Info,
        "DirectoryWatcher: watching '{}' (recursive={}, latency=100ms)",
        directory, recursive);
    return true;
}

/// Stops the FSEventStream, the CFRunLoop, and the background thread.
///
/// Sequence matters here:
///  1. Set `m_watching = false` — intent signal.
///  2. Stop the stream — no more callbacks will be dispatched.
///  3. Spin-wait for `m_runLoop` — the background thread sets it before entering
///     `CFRunLoopRun()`. The wait is purely defensive and completes in < 1 ms.
///  4. `CFRunLoopStop()` — unblocks `CFRunLoopRun()` in WatchThread.
///  5. Join the thread.
///  6. Invalidate and release the stream (triggers the context release callback).
void DirectoryWatcher::Stop() {
    if (!m_watching) return;

    m_watching = false;

    auto* stream = static_cast<FSEventStreamRef>(m_stream);

    // Stop delivering events before touching the run loop.
    if (stream) {
        FSEventStreamStop(stream);
    }

    // Wait for the background thread to publish its CFRunLoopRef.
    // This covers the narrow window between `new std::thread(...)` in Start()
    // and the `m_runLoop = CFRunLoopGetCurrent()` line in WatchThread().
    if (m_thread) {
        while (!m_runLoop) {
            std::this_thread::yield();
        }

        auto* runLoop = static_cast<CFRunLoopRef>(m_runLoop);
        CFRunLoopStop(runLoop);

        auto* thread = static_cast<std::thread*>(m_thread);
        if (thread->joinable()) {
            thread->join();
        }
        delete thread;
        m_thread  = nullptr;
        m_runLoop = nullptr;
    }

    // Invalidate + release after the thread exits — the context release callback
    // fires here, freeing the FsEventContext.
    if (stream) {
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        m_stream = nullptr;
    }

    m_callback = nullptr;
    m_userData = nullptr;
}

} // namespace uz

#endif // __APPLE__
