// file_watcher.cpp -- FileWatcher implementation.
//
// Linux:   inotify with poll() for non-blocking reads + pipe for stop signal.
// Windows: ReadDirectoryChangesW with overlapped I/O + event for stop signal.
// macOS:   stub (returns false on watch()).
//
// Debounce is implemented via a per-path timestamp map. Successive events
// within the debounce window are silently dropped.

#include "engine/core/file_watcher.h"
#include "engine/core/platform.h"

#include <atomic>
#include <string>
#include <unordered_map>

// ============================================================================
// Linux implementation (inotify)
// ============================================================================
#if defined(UZ_PLATFORM_LINUX)

#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <climits>

namespace uz {

struct FileWatcher::Impl {
    int                  inotify_fd = -1;
    int                  watch_fd   = -1;
    int                  pipe_fds[2] = {-1, -1};  // pipe for stop signal
    platform::Thread*    thread     = nullptr;
    FileWatchCallback    callback;
    std::string          directory;
    std::atomic<bool>    running{false};
    uint32_t             debounce_ms = 200;

    // Debounce: last event time (microseconds) per filename
    std::unordered_map<std::string, uint64_t> last_event_time;
};

static FileWatchEvent inotify_mask_to_event(uint32_t mask) {
    if (mask & (IN_CREATE | IN_MOVED_TO)) {
        return FileWatchEvent::Created;
    }
    if (mask & (IN_DELETE | IN_MOVED_FROM)) {
        return FileWatchEvent::Deleted;
    }
    // IN_CLOSE_WRITE, IN_MODIFY, or anything else -> Modified
    return FileWatchEvent::Modified;
}

void FileWatcher::thread_entry(void* user_data) {
    auto* impl = static_cast<Impl*>(user_data);

    // Buffer sized for multiple inotify events
    constexpr size_t kBufSize = 4096;
    alignas(struct inotify_event) char buf[kBufSize];

    struct pollfd fds[2];
    fds[0].fd     = impl->inotify_fd;
    fds[0].events = POLLIN;
    fds[1].fd     = impl->pipe_fds[0];  // read end of stop pipe
    fds[1].events = POLLIN;

    while (impl->running.load(std::memory_order_relaxed)) {
        int ret = poll(fds, 2, 500 /* ms timeout */);
        if (ret <= 0) {
            continue;  // timeout or error -- just loop
        }

        // Stop signal received?
        if (fds[1].revents & POLLIN) {
            break;
        }

        if (!(fds[0].revents & POLLIN)) {
            continue;
        }

        ssize_t len = read(impl->inotify_fd, buf, kBufSize);
        if (len <= 0) {
            continue;
        }

        const uint64_t debounce_us = static_cast<uint64_t>(impl->debounce_ms) * 1000u;

        for (const char* ptr = buf; ptr < buf + len; ) {
            const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);
            ptr += sizeof(struct inotify_event) + event->len;

            if (event->len == 0) {
                continue;  // directory-level event, skip
            }

            std::string filename(event->name);
            const uint64_t now = platform::get_time_microseconds();

            // Debounce: skip if last event for this path was within the window
            auto it = impl->last_event_time.find(filename);
            if (it != impl->last_event_time.end()) {
                if (now - it->second < debounce_us) {
                    continue;
                }
            }
            impl->last_event_time[filename] = now;

            FileChange change;
            change.path  = impl->directory + "/" + filename;
            change.event = inotify_mask_to_event(event->mask);

            if (impl->callback) {
                impl->callback(change);
            }
        }
    }
}

FileWatcher::FileWatcher()
    : impl_(new Impl) {}

FileWatcher::~FileWatcher() {
    stop();
    delete impl_;
}

bool FileWatcher::watch(const char* directory, FileWatchCallback callback) {
    if (!directory || !callback) {
        return false;
    }

    // Already watching? Stop first.
    if (impl_->running.load(std::memory_order_relaxed)) {
        stop();
    }

    // Verify directory exists
    if (!platform::path_exists(directory)) {
        return false;
    }

    // Create inotify instance
    impl_->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (impl_->inotify_fd < 0) {
        return false;
    }

    // Watch the directory for relevant events
    const uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MOVED_FROM;
    impl_->watch_fd = inotify_add_watch(impl_->inotify_fd, directory, mask);
    if (impl_->watch_fd < 0) {
        close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
        return false;
    }

    // Create pipe for stop signal
    if (pipe(impl_->pipe_fds) != 0) {
        inotify_rm_watch(impl_->inotify_fd, impl_->watch_fd);
        close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
        impl_->watch_fd   = -1;
        return false;
    }

    impl_->directory = directory;
    impl_->callback  = std::move(callback);
    impl_->running.store(true, std::memory_order_release);
    impl_->last_event_time.clear();

    impl_->thread = platform::thread_create(thread_entry, impl_, "FileWatcher");
    if (!impl_->thread) {
        impl_->running.store(false, std::memory_order_relaxed);
        close(impl_->pipe_fds[0]);
        close(impl_->pipe_fds[1]);
        impl_->pipe_fds[0] = impl_->pipe_fds[1] = -1;
        inotify_rm_watch(impl_->inotify_fd, impl_->watch_fd);
        close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
        impl_->watch_fd   = -1;
        return false;
    }

    return true;
}

void FileWatcher::stop() {
    if (!impl_->running.load(std::memory_order_acquire)) {
        return;
    }

    impl_->running.store(false, std::memory_order_release);

    // Signal the thread to wake up via the pipe
    if (impl_->pipe_fds[1] >= 0) {
        char sig = 1;
        (void)write(impl_->pipe_fds[1], &sig, 1);
    }

    // Join the thread
    platform::thread_join(impl_->thread);
    impl_->thread = nullptr;

    // Clean up pipe
    if (impl_->pipe_fds[0] >= 0) {
        close(impl_->pipe_fds[0]);
        impl_->pipe_fds[0] = -1;
    }
    if (impl_->pipe_fds[1] >= 0) {
        close(impl_->pipe_fds[1]);
        impl_->pipe_fds[1] = -1;
    }

    // Clean up inotify
    if (impl_->watch_fd >= 0) {
        inotify_rm_watch(impl_->inotify_fd, impl_->watch_fd);
        impl_->watch_fd = -1;
    }
    if (impl_->inotify_fd >= 0) {
        close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
    }

    impl_->callback = nullptr;
    impl_->last_event_time.clear();
}

bool FileWatcher::is_watching() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

void FileWatcher::set_debounce_ms(uint32_t ms) noexcept {
    impl_->debounce_ms = ms;
}

} // namespace uz

// ============================================================================
// Windows implementation (ReadDirectoryChangesW)
// ============================================================================
#elif defined(UZ_PLATFORM_WINDOWS)

#include <windows.h>
#include <cstring>

namespace uz {

struct FileWatcher::Impl {
    HANDLE               dir_handle  = INVALID_HANDLE_VALUE;
    HANDLE               stop_event  = nullptr;
    platform::Thread*    thread      = nullptr;
    FileWatchCallback    callback;
    std::string          directory;
    std::atomic<bool>    running{false};
    uint32_t             debounce_ms = 200;

    std::unordered_map<std::string, uint64_t> last_event_time;
};

static FileWatchEvent action_to_event(DWORD action) {
    switch (action) {
        case FILE_ACTION_ADDED:
        case FILE_ACTION_RENAMED_NEW_NAME:
            return FileWatchEvent::Created;
        case FILE_ACTION_REMOVED:
        case FILE_ACTION_RENAMED_OLD_NAME:
            return FileWatchEvent::Deleted;
        default: // FILE_ACTION_MODIFIED
            return FileWatchEvent::Modified;
    }
}

void FileWatcher::thread_entry(void* user_data) {
    auto* impl = static_cast<Impl*>(user_data);

    constexpr DWORD kBufSize = 4096;
    alignas(DWORD) char buf[kBufSize];
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    HANDLE handles[2] = { overlapped.hEvent, impl->stop_event };

    while (impl->running.load(std::memory_order_relaxed)) {
        DWORD bytes_returned = 0;
        BOOL ok = ReadDirectoryChangesW(
            impl->dir_handle,
            buf, kBufSize,
            FALSE, // no subtree
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            &bytes_returned,
            &overlapped,
            nullptr
        );

        if (!ok) {
            break;
        }

        DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_result == WAIT_OBJECT_0 + 1) {
            // Stop event signaled
            CancelIo(impl->dir_handle);
            break;
        }

        if (wait_result != WAIT_OBJECT_0) {
            break;
        }

        if (!GetOverlappedResult(impl->dir_handle, &overlapped, &bytes_returned, FALSE)) {
            ResetEvent(overlapped.hEvent);
            continue;
        }

        const uint64_t debounce_us = static_cast<uint64_t>(impl->debounce_ms) * 1000u;

        const auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf);
        for (;;) {
            // Convert wide filename to narrow
            int narrow_len = WideCharToMultiByte(CP_UTF8, 0,
                info->FileName, static_cast<int>(info->FileNameLength / sizeof(WCHAR)),
                nullptr, 0, nullptr, nullptr);
            std::string filename(static_cast<size_t>(narrow_len), '\0');
            WideCharToMultiByte(CP_UTF8, 0,
                info->FileName, static_cast<int>(info->FileNameLength / sizeof(WCHAR)),
                filename.data(), narrow_len, nullptr, nullptr);

            // Replace backslashes with forward slashes
            for (char& c : filename) {
                if (c == '\\') c = '/';
            }

            const uint64_t now = platform::get_time_microseconds();
            auto it = impl->last_event_time.find(filename);
            bool debounced = false;
            if (it != impl->last_event_time.end()) {
                if (now - it->second < debounce_us) {
                    debounced = true;
                }
            }

            if (!debounced) {
                impl->last_event_time[filename] = now;
                FileChange change;
                change.path  = impl->directory + "/" + filename;
                change.event = action_to_event(info->Action);
                if (impl->callback) {
                    impl->callback(change);
                }
            }

            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<const char*>(info) + info->NextEntryOffset);
        }

        ResetEvent(overlapped.hEvent);
    }

    CloseHandle(overlapped.hEvent);
}

FileWatcher::FileWatcher()
    : impl_(new Impl) {}

FileWatcher::~FileWatcher() {
    stop();
    delete impl_;
}

bool FileWatcher::watch(const char* directory, FileWatchCallback callback) {
    if (!directory || !callback) {
        return false;
    }

    if (impl_->running.load(std::memory_order_relaxed)) {
        stop();
    }

    if (!platform::path_exists(directory)) {
        return false;
    }

    // Convert to wide string for CreateFileW
    int wlen = MultiByteToWideChar(CP_UTF8, 0, directory, -1, nullptr, 0);
    std::wstring wdir(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, directory, -1, wdir.data(), wlen);

    impl_->dir_handle = CreateFileW(
        wdir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (impl_->dir_handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!impl_->stop_event) {
        CloseHandle(impl_->dir_handle);
        impl_->dir_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    impl_->directory = directory;
    impl_->callback  = std::move(callback);
    impl_->running.store(true, std::memory_order_release);
    impl_->last_event_time.clear();

    impl_->thread = platform::thread_create(thread_entry, impl_, "FileWatcher");
    if (!impl_->thread) {
        impl_->running.store(false, std::memory_order_relaxed);
        CloseHandle(impl_->stop_event);
        impl_->stop_event = nullptr;
        CloseHandle(impl_->dir_handle);
        impl_->dir_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    return true;
}

void FileWatcher::stop() {
    if (!impl_->running.load(std::memory_order_acquire)) {
        return;
    }

    impl_->running.store(false, std::memory_order_release);

    if (impl_->stop_event) {
        SetEvent(impl_->stop_event);
    }

    platform::thread_join(impl_->thread);
    impl_->thread = nullptr;

    if (impl_->stop_event) {
        CloseHandle(impl_->stop_event);
        impl_->stop_event = nullptr;
    }

    if (impl_->dir_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->dir_handle);
        impl_->dir_handle = INVALID_HANDLE_VALUE;
    }

    impl_->callback = nullptr;
    impl_->last_event_time.clear();
}

bool FileWatcher::is_watching() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

void FileWatcher::set_debounce_ms(uint32_t ms) noexcept {
    impl_->debounce_ms = ms;
}

} // namespace uz

// ============================================================================
// macOS stub
// ============================================================================
#elif defined(UZ_PLATFORM_MACOS)

namespace uz {

struct FileWatcher::Impl {
    uint32_t debounce_ms = 200;
};

FileWatcher::FileWatcher()
    : impl_(new Impl) {}

FileWatcher::~FileWatcher() {
    delete impl_;
}

void FileWatcher::thread_entry(void* /*user_data*/) {
    // No-op on macOS
}

bool FileWatcher::watch(const char* /*directory*/, FileWatchCallback /*callback*/) {
    return false;  // Not implemented on macOS
}

void FileWatcher::stop() {
    // No-op
}

bool FileWatcher::is_watching() const noexcept {
    return false;
}

void FileWatcher::set_debounce_ms(uint32_t ms) noexcept {
    impl_->debounce_ms = ms;
}

} // namespace uz

#endif
