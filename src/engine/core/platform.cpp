// platform.cpp -- Platform Abstraction Layer implementation.
//
// Each section is guarded by UZ_PLATFORM_* macros. Currently only the Linux
// backend is fully implemented (dev container target). The Windows backend
// stubs are provided for future use.

#include "engine/core/platform.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Linux implementation
// ---------------------------------------------------------------------------
#if defined(UZ_PLATFORM_LINUX) || defined(UZ_PLATFORM_MACOS)

#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>

namespace uz::platform {

// ===========================================================================
// Timer (Linux) -- CLOCK_MONOTONIC for jitter-free measurement
// ===========================================================================

double get_time_seconds() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

uint64_t get_time_microseconds() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL
         + static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL;
}

// ===========================================================================
// File I/O (Linux) -- POSIX stat + stdio
// ===========================================================================

bool path_exists(const char* path) noexcept {
    if (!path) return false;
    struct stat st{};
    return (stat(path, &st) == 0);
}

uint64_t file_size(const char* path) noexcept {
    if (!path) return 0;
    struct stat st{};
    if (stat(path, &st) != 0) return 0;
    // Only return size for regular files, not directories.
    if (!S_ISREG(st.st_mode)) return 0;
    return static_cast<uint64_t>(st.st_size);
}

char* file_read_all(const char* path, size_t* out_size) noexcept {
    if (out_size) *out_size = 0;
    if (!path) return nullptr;

    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;

    // Seek to end to determine file size.
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return nullptr;
    }
    const long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return nullptr;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return nullptr;
    }

    const auto size = static_cast<size_t>(len);
    // Allocate size+1 so callers can optionally NUL-terminate text files.
    char* buf = static_cast<char*>(std::malloc(size + 1));
    if (!buf) {
        fclose(f);
        return nullptr;
    }

    const size_t read = fread(buf, 1, size, f);
    fclose(f);

    if (read != size) {
        std::free(buf);
        return nullptr;
    }

    buf[size] = '\0'; // Convenience NUL terminator.
    if (out_size) *out_size = size;
    return buf;
}

bool file_write_all(const char* path, const void* data, size_t size) noexcept {
    if (!path) return false;
    // Allow writing an empty file (data can be nullptr when size == 0).
    if (size > 0 && !data) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    if (size > 0) {
        const size_t written = fwrite(data, 1, size, f);
        if (written != size) {
            fclose(f);
            return false;
        }
    }

    fclose(f);
    return true;
}

// Helper: create a single directory level (like mkdir -p for one component).
namespace {
bool mkdir_single(const char* path) noexcept {
    if (mkdir(path, 0755) == 0) return true;
    // If it already exists as a directory, that's fine.
    if (errno == EEXIST) {
        struct stat st{};
        return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    }
    return false;
}
} // anonymous namespace

bool mkdir_recursive(const char* path) noexcept {
    if (!path || path[0] == '\0') return false;

    // Check if it already exists.
    struct stat st{};
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return true;

    // Work on a mutable copy so we can insert NUL terminators.
    const size_t len = std::strlen(path);
    // +1 for the NUL terminator.
    char* buf = static_cast<char*>(std::malloc(len + 1));
    if (!buf) return false;
    std::memcpy(buf, path, len + 1);

    // Walk the path, creating each component.
    for (size_t i = 1; i <= len; ++i) {
        if (buf[i] == '/' || buf[i] == '\0') {
            const char saved = buf[i];
            buf[i] = '\0';
            if (!mkdir_single(buf)) {
                std::free(buf);
                return false;
            }
            buf[i] = saved;
        }
    }

    std::free(buf);
    return true;
}

// ===========================================================================
// Thread (Linux) -- pthreads
// ===========================================================================

struct Thread {
    pthread_t handle;
};

namespace {
// Trampoline: pthreads wants void*(*)(void*), we want void(*)(void*).
struct ThreadStartData {
    ThreadFunc func;
    void*      user_data;
};

void* thread_trampoline(void* arg) {
    auto* start = static_cast<ThreadStartData*>(arg);
    ThreadFunc fn = start->func;
    void* ud = start->user_data;
    std::free(start);
    fn(ud);
    return nullptr;
}
} // anonymous namespace

Thread* thread_create(ThreadFunc fn, void* user_data, const char* name) noexcept {
    if (!fn) return nullptr;

    auto* start = static_cast<ThreadStartData*>(std::malloc(sizeof(ThreadStartData)));
    if (!start) return nullptr;
    start->func = fn;
    start->user_data = user_data;

    auto* t = static_cast<Thread*>(std::malloc(sizeof(Thread)));
    if (!t) {
        std::free(start);
        return nullptr;
    }

    if (pthread_create(&t->handle, nullptr, thread_trampoline, start) != 0) {
        std::free(start);
        std::free(t);
        return nullptr;
    }

    // Best-effort thread naming (Linux has a 16-char limit including NUL).
#if defined(UZ_PLATFORM_LINUX)
    if (name && name[0] != '\0') {
        // pthread_setname_np requires the name to be <= 15 chars + NUL.
        char truncated[16];
        std::strncpy(truncated, name, 15);
        truncated[15] = '\0';
        pthread_setname_np(t->handle, truncated);
    }
#else
    (void)name;
#endif

    return t;
}

void thread_join(Thread* t) noexcept {
    if (!t) return;
    pthread_join(t->handle, nullptr);
    std::free(t);
}

void thread_detach(Thread* t) noexcept {
    if (!t) return;
    pthread_detach(t->handle);
    std::free(t);
}

uint64_t thread_current_id() noexcept {
    // pthread_self() returns an opaque type. On Linux it is typically an
    // unsigned long (same size as uint64_t on x86_64).
    return static_cast<uint64_t>(pthread_self());
}

void thread_sleep_ms(uint32_t ms) noexcept {
    struct timespec ts{};
    ts.tv_sec  = static_cast<time_t>(ms / 1000);
    ts.tv_nsec = static_cast<long>((ms % 1000) * 1'000'000L);
    nanosleep(&ts, nullptr);
}

// ===========================================================================
// Mutex (Linux) -- pthreads mutex (non-recursive, error-checking type)
// ===========================================================================

struct Mutex {
    pthread_mutex_t handle;
};

Mutex* mutex_create() noexcept {
    auto* m = static_cast<Mutex*>(std::malloc(sizeof(Mutex)));
    if (!m) return nullptr;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // ERRORCHECK type helps catch misuse (double-lock, unlock-from-wrong-thread)
    // during development. Slightly slower than NORMAL but worth it for safety.
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);

    if (pthread_mutex_init(&m->handle, &attr) != 0) {
        pthread_mutexattr_destroy(&attr);
        std::free(m);
        return nullptr;
    }
    pthread_mutexattr_destroy(&attr);
    return m;
}

void mutex_destroy(Mutex* m) noexcept {
    if (!m) return;
    pthread_mutex_destroy(&m->handle);
    std::free(m);
}

void mutex_lock(Mutex* m) noexcept {
    if (!m) return;
    pthread_mutex_lock(&m->handle);
}

void mutex_unlock(Mutex* m) noexcept {
    if (!m) return;
    pthread_mutex_unlock(&m->handle);
}

bool mutex_try_lock(Mutex* m) noexcept {
    if (!m) return false;
    return (pthread_mutex_trylock(&m->handle) == 0);
}

// ===========================================================================
// Dynamic library loading (Linux) -- dlopen/dlsym/dlclose
// ===========================================================================

struct DynLib {
    void* handle;
};

DynLib* dynlib_open(const char* path) noexcept {
    if (!path) return nullptr;

    void* h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) return nullptr;

    auto* lib = static_cast<DynLib*>(std::malloc(sizeof(DynLib)));
    if (!lib) {
        dlclose(h);
        return nullptr;
    }
    lib->handle = h;
    return lib;
}

void dynlib_close(DynLib* lib) noexcept {
    if (!lib) return;
    dlclose(lib->handle);
    std::free(lib);
}

void* dynlib_symbol(DynLib* lib, const char* name) noexcept {
    if (!lib || !name) return nullptr;
    return dlsym(lib->handle, name);
}

} // namespace uz::platform

// ---------------------------------------------------------------------------
// Windows implementation (stub -- will be completed when targeting Windows)
// ---------------------------------------------------------------------------
#elif defined(UZ_PLATFORM_WINDOWS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace uz::platform {

// ===========================================================================
// Timer (Windows) -- QueryPerformanceCounter
// ===========================================================================

namespace {
// Lazy-initialized QPC frequency. Guaranteed to be set before first use
// because get_time_seconds/get_time_microseconds both call qpc_frequency().
double qpc_frequency() noexcept {
    static double freq = 0.0;
    if (freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = static_cast<double>(f.QuadPart);
    }
    return freq;
}
} // anonymous namespace

double get_time_seconds() noexcept {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return static_cast<double>(count.QuadPart) / qpc_frequency();
}

uint64_t get_time_microseconds() noexcept {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return static_cast<uint64_t>(
        static_cast<double>(count.QuadPart) * 1'000'000.0 / qpc_frequency());
}

// ===========================================================================
// File I/O (Windows)
// ===========================================================================

bool path_exists(const char* path) noexcept {
    if (!path) return false;
    const DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES);
}

uint64_t file_size(const char* path) noexcept {
    if (!path) return 0;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return 0;
    // Only regular files.
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32)
         | static_cast<uint64_t>(data.nFileSizeLow);
}

char* file_read_all(const char* path, size_t* out_size) noexcept {
    if (out_size) *out_size = 0;
    if (!path) return nullptr;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;

    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li)) { CloseHandle(h); return nullptr; }
    const auto size = static_cast<size_t>(li.QuadPart);

    char* buf = static_cast<char*>(std::malloc(size + 1));
    if (!buf) { CloseHandle(h); return nullptr; }

    DWORD read = 0;
    if (!ReadFile(h, buf, static_cast<DWORD>(size), &read, nullptr)
        || read != static_cast<DWORD>(size)) {
        std::free(buf);
        CloseHandle(h);
        return nullptr;
    }
    CloseHandle(h);

    buf[size] = '\0';
    if (out_size) *out_size = size;
    return buf;
}

bool file_write_all(const char* path, const void* data, size_t size) noexcept {
    if (!path) return false;
    if (size > 0 && !data) return false;

    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    if (size > 0) {
        DWORD written = 0;
        if (!WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr)
            || written != static_cast<DWORD>(size)) {
            CloseHandle(h);
            return false;
        }
    }
    CloseHandle(h);
    return true;
}

bool mkdir_recursive(const char* path) noexcept {
    if (!path || path[0] == '\0') return false;

    const DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES
        && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;

    const size_t len = std::strlen(path);
    char* buf = static_cast<char*>(std::malloc(len + 1));
    if (!buf) return false;
    std::memcpy(buf, path, len + 1);

    for (size_t i = 1; i <= len; ++i) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == '\0') {
            const char saved = buf[i];
            buf[i] = '\0';
            // Skip drive letter component (e.g. "C:").
            if (i >= 2 && buf[i - 1] == ':') {
                buf[i] = saved;
                continue;
            }
            CreateDirectoryA(buf, nullptr);
            buf[i] = saved;
        }
    }
    std::free(buf);

    // Verify the final directory exists.
    const DWORD finalAttr = GetFileAttributesA(path);
    return (finalAttr != INVALID_FILE_ATTRIBUTES
            && (finalAttr & FILE_ATTRIBUTE_DIRECTORY));
}

// ===========================================================================
// Thread (Windows)
// ===========================================================================

struct Thread {
    HANDLE handle;
};

namespace {
struct ThreadStartData {
    ThreadFunc func;
    void*      user_data;
};

DWORD WINAPI thread_trampoline(LPVOID arg) {
    auto* start = static_cast<ThreadStartData*>(arg);
    ThreadFunc fn = start->func;
    void* ud = start->user_data;
    std::free(start);
    fn(ud);
    return 0;
}
} // anonymous namespace

Thread* thread_create(ThreadFunc fn, void* user_data, const char* name) noexcept {
    if (!fn) return nullptr;
    (void)name; // Windows thread naming requires SetThreadDescription (Win10+).

    auto* start = static_cast<ThreadStartData*>(std::malloc(sizeof(ThreadStartData)));
    if (!start) return nullptr;
    start->func = fn;
    start->user_data = user_data;

    auto* t = static_cast<Thread*>(std::malloc(sizeof(Thread)));
    if (!t) { std::free(start); return nullptr; }

    t->handle = CreateThread(nullptr, 0, thread_trampoline, start, 0, nullptr);
    if (!t->handle) {
        std::free(start);
        std::free(t);
        return nullptr;
    }
    return t;
}

void thread_join(Thread* t) noexcept {
    if (!t) return;
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    std::free(t);
}

void thread_detach(Thread* t) noexcept {
    if (!t) return;
    CloseHandle(t->handle);
    std::free(t);
}

uint64_t thread_current_id() noexcept {
    return static_cast<uint64_t>(GetCurrentThreadId());
}

void thread_sleep_ms(uint32_t ms) noexcept {
    Sleep(static_cast<DWORD>(ms));
}

// ===========================================================================
// Mutex (Windows) -- CRITICAL_SECTION (fast, non-recursive by convention)
// ===========================================================================

struct Mutex {
    CRITICAL_SECTION cs;
};

Mutex* mutex_create() noexcept {
    auto* m = static_cast<Mutex*>(std::malloc(sizeof(Mutex)));
    if (!m) return nullptr;
    InitializeCriticalSection(&m->cs);
    return m;
}

void mutex_destroy(Mutex* m) noexcept {
    if (!m) return;
    DeleteCriticalSection(&m->cs);
    std::free(m);
}

void mutex_lock(Mutex* m) noexcept {
    if (!m) return;
    EnterCriticalSection(&m->cs);
}

void mutex_unlock(Mutex* m) noexcept {
    if (!m) return;
    LeaveCriticalSection(&m->cs);
}

bool mutex_try_lock(Mutex* m) noexcept {
    if (!m) return false;
    return (TryEnterCriticalSection(&m->cs) != 0);
}

// ===========================================================================
// Dynamic library (Windows)
// ===========================================================================

struct DynLib {
    HMODULE handle;
};

DynLib* dynlib_open(const char* path) noexcept {
    if (!path) return nullptr;
    HMODULE h = LoadLibraryA(path);
    if (!h) return nullptr;

    auto* lib = static_cast<DynLib*>(std::malloc(sizeof(DynLib)));
    if (!lib) { FreeLibrary(h); return nullptr; }
    lib->handle = h;
    return lib;
}

void dynlib_close(DynLib* lib) noexcept {
    if (!lib) return;
    FreeLibrary(lib->handle);
    std::free(lib);
}

void* dynlib_symbol(DynLib* lib, const char* name) noexcept {
    if (!lib || !name) return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(lib->handle, name));
}

} // namespace uz::platform

#endif // UZ_PLATFORM_*
