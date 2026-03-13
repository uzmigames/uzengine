/// UzEngine File Watcher — Windows Implementation
///
/// Uses ReadDirectoryChangesW with overlapped I/O on a background thread.
/// The stop event signals the thread to exit cleanly.

#include "file_watcher.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cstring>
#include <thread>

UZ_DECLARE_LOG_CATEGORY(FileSystem);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper — UTF-8 to wide conversion
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring Utf8ToWide(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
    wide.pop_back();
    return wide;
}

static std::string WideToUtf8(const wchar_t* wide, int wideLen) {
    if (!wide || wideLen == 0) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string utf8(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, utf8.data(), len, nullptr, nullptr);
    return utf8;
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

bool DirectoryWatcher::Start(const char* directory, bool recursive, FileWatchCallback callback, void* userData) {
    UZ_PROFILE_FUNCTION();

    if (!callback) {
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: callback must not be nullptr");
        return false;
    }

    // Stop previous watch if active
    if (m_watching) {
        Stop();
    }

    const auto wideDir = Utf8ToWide(directory);
    if (wideDir.empty()) {
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: empty or invalid directory path");
        return false;
    }

    // Open directory handle for overlapped I/O
    HANDLE dirHandle = CreateFileW(
        wideDir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (dirHandle == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: failed to open directory '{}', win32 error={}", directory, err);
        return false;
    }

    // Manual-reset event to signal the watch thread to stop
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr) {
        const DWORD err = GetLastError();
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::Start: CreateEventW failed, win32 error={}", err);
        CloseHandle(dirHandle);
        return false;
    }

    m_dirHandle = dirHandle;
    m_stopEvent = stopEvent;
    m_callback = callback;
    m_userData = userData;
    m_recursive = recursive;
    m_watching = true;

    // Launch background thread
    auto* thread = new std::thread(&DirectoryWatcher::WatchThread, this);
    m_thread = thread;

    UZ_LOG(FileSystem, Info, "DirectoryWatcher: watching '{}' (recursive={})", directory, recursive);
    return true;
}

void DirectoryWatcher::Stop() {
    if (!m_watching) return;

    m_watching = false;

    // Signal the thread to exit
    if (m_stopEvent) {
        SetEvent(static_cast<HANDLE>(m_stopEvent));
    }

    // Join the background thread
    if (m_thread) {
        auto* thread = static_cast<std::thread*>(m_thread);
        if (thread->joinable()) {
            thread->join();
        }
        delete thread;
        m_thread = nullptr;
    }

    // Close handles
    if (m_stopEvent) {
        CloseHandle(static_cast<HANDLE>(m_stopEvent));
        m_stopEvent = nullptr;
    }
    if (m_dirHandle) {
        CloseHandle(static_cast<HANDLE>(m_dirHandle));
        m_dirHandle = nullptr;
    }

    m_callback = nullptr;
    m_userData = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Watch thread — blocks on ReadDirectoryChangesW + WaitForMultipleObjects
// ─────────────────────────────────────────────────────────────────────────────

void DirectoryWatcher::WatchThread() {
    constexpr DWORD kBufferSize = 32768;
    alignas(DWORD) uint8_t buffer[kBufferSize];

    HANDLE dirHandle = static_cast<HANDLE>(m_dirHandle);
    HANDLE stopEvent = static_cast<HANDLE>(m_stopEvent);

    const DWORD notifyFilter =
        FILE_NOTIFY_CHANGE_FILE_NAME |
        FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_CREATION;

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        UZ_LOG(FileSystem, Error, "DirectoryWatcher::WatchThread: failed to create overlapped event");
        return;
    }

    HANDLE waitHandles[2] = { stopEvent, overlapped.hEvent };

    while (m_watching) {
        ResetEvent(overlapped.hEvent);

        BOOL success = ReadDirectoryChangesW(
            dirHandle,
            buffer,
            kBufferSize,
            m_recursive ? TRUE : FALSE,
            notifyFilter,
            nullptr, // Not used with overlapped
            &overlapped,
            nullptr
        );

        if (!success) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                UZ_LOG(FileSystem, Error, "DirectoryWatcher::WatchThread: ReadDirectoryChangesW failed, win32 error={}", err);
                break;
            }
        }

        // Wait for either stop event or directory change
        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) {
            // Stop event signaled — cancel pending I/O and exit
            CancelIoEx(dirHandle, &overlapped);
            break;
        }

        if (waitResult == WAIT_OBJECT_0 + 1) {
            // Overlapped I/O completed — process results
            DWORD bytesReturned = 0;
            if (!GetOverlappedResult(dirHandle, &overlapped, &bytesReturned, FALSE)) {
                const DWORD err = GetLastError();
                if (err == ERROR_OPERATION_ABORTED) break; // Cancelled by Stop()
                UZ_LOG(FileSystem, Error, "DirectoryWatcher::WatchThread: GetOverlappedResult failed, win32 error={}", err);
                continue;
            }

            if (bytesReturned == 0) continue;

            // Parse the FILE_NOTIFY_INFORMATION chain
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
            for (;;) {
                FileWatchEvent event;
                switch (info->Action) {
                case FILE_ACTION_ADDED:
                    event = FileWatchEvent::Created;
                    break;
                case FILE_ACTION_REMOVED:
                    event = FileWatchEvent::Deleted;
                    break;
                case FILE_ACTION_MODIFIED:
                    event = FileWatchEvent::Modified;
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    event = FileWatchEvent::Renamed;
                    break;
                default:
                    event = FileWatchEvent::Modified;
                    break;
                }

                // Convert wide filename to UTF-8
                const int charCount = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
                std::string filename = WideToUtf8(info->FileName, charCount);

                // Normalize backslashes to forward slashes
                for (auto& c : filename) {
                    if (c == '\\') c = '/';
                }

                FileWatchNotification notification{};
                const size_t copyLen = std::min(filename.size(), sizeof(notification.path) - 1);
                std::memcpy(notification.path, filename.c_str(), copyLen);
                notification.path[copyLen] = '\0';
                notification.event = event;

                m_callback(notification, m_userData);

                // Advance to next entry in the chain
                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<const uint8_t*>(info) + info->NextEntryOffset
                );
            }
        }
    }

    CloseHandle(overlapped.hEvent);
}

} // namespace uz
