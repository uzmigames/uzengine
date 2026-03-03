#pragma once

// platform.h -- Platform detection macros and minimal PAL (Platform Abstraction Layer).
//
// Platform macros (mutually exclusive):
//   UZ_PLATFORM_WINDOWS  -- Windows
//   UZ_PLATFORM_LINUX    -- Linux / Steam Deck
//   UZ_PLATFORM_MACOS    -- macOS / iOS
//
// Usage: all engine code uses uz::platform:: functions instead of OS-specific APIs.
// This keeps platform #ifdefs confined to platform.cpp, and the rest of the engine
// codes against a clean, portable interface.
//
// Memory ownership:
//   Thread*, Mutex*, DynLib* are opaque PAL objects allocated with malloc/free.
//   file_read_all() returns a malloc'd buffer -- caller must free().
//   These are infrastructure objects, not gameplay data (which uses arenas).

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define UZ_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define UZ_PLATFORM_LINUX 1
#elif defined(__APPLE__)
    #define UZ_PLATFORM_MACOS 1
#else
    #error "Unsupported platform"
#endif

namespace uz::platform {

// ===========================================================================
// Timer -- monotonic high-resolution timing
// ===========================================================================

/// Returns elapsed seconds since some unspecified epoch (monotonic clock).
/// Guaranteed to never go backwards.
[[nodiscard]] double get_time_seconds() noexcept;

/// Returns elapsed time in microseconds (high resolution, monotonic).
[[nodiscard]] uint64_t get_time_microseconds() noexcept;

// ===========================================================================
// File I/O -- minimal synchronous file operations
// ===========================================================================

/// Returns true if path exists (file or directory).
[[nodiscard]] bool path_exists(const char* path) noexcept;

/// Returns file size in bytes, or 0 on error / nonexistent path.
[[nodiscard]] uint64_t file_size(const char* path) noexcept;

/// Read entire file into a newly heap-allocated buffer.
/// Caller must free() the returned pointer. Returns nullptr on error.
/// *out_size receives the number of bytes read (set to 0 on error).
[[nodiscard]] char* file_read_all(const char* path, size_t* out_size) noexcept;

/// Write bytes to file (creates or truncates). Returns true on success.
bool file_write_all(const char* path, const void* data, size_t size) noexcept;

/// Create directory (including parents). Returns true if the directory
/// exists after the call (idempotent).
bool mkdir_recursive(const char* path) noexcept;

// ===========================================================================
// Thread -- lightweight OS thread wrappers
// ===========================================================================

struct Thread;
using ThreadFunc = void(*)(void* user_data);

/// Create and start a new OS thread. Returns opaque handle.
/// Caller must eventually call thread_join() or thread_detach().
/// `name` is a debug label (may be truncated by the OS). Can be nullptr.
[[nodiscard]] Thread* thread_create(ThreadFunc fn, void* user_data,
                                     const char* name) noexcept;

/// Block until the thread finishes. Releases the handle. No-op if t is nullptr.
void thread_join(Thread* t) noexcept;

/// Detach the thread (it will clean up on its own). Releases the handle.
/// No-op if t is nullptr.
void thread_detach(Thread* t) noexcept;

/// Returns current thread ID as a uint64_t.
[[nodiscard]] uint64_t thread_current_id() noexcept;

/// Sleep the calling thread for at least `ms` milliseconds.
void thread_sleep_ms(uint32_t ms) noexcept;

// ===========================================================================
// Mutex -- non-recursive mutual exclusion
// ===========================================================================

struct Mutex;

/// Create a new mutex. Returns nullptr on failure.
[[nodiscard]] Mutex* mutex_create() noexcept;

/// Destroy a mutex. No-op if m is nullptr.
void mutex_destroy(Mutex* m) noexcept;

/// Lock the mutex (blocks until acquired). No-op if m is nullptr.
void mutex_lock(Mutex* m) noexcept;

/// Unlock the mutex. No-op if m is nullptr.
void mutex_unlock(Mutex* m) noexcept;

/// Try to lock without blocking. Returns true if lock was acquired.
/// Returns false if m is nullptr or the mutex is already held.
[[nodiscard]] bool mutex_try_lock(Mutex* m) noexcept;

// ===========================================================================
// Dynamic library loading
// ===========================================================================

struct DynLib;

/// Open a dynamic library by path. Returns nullptr on failure.
/// On Windows, path should be a .dll; on Linux, a .so; on macOS, a .dylib.
[[nodiscard]] DynLib* dynlib_open(const char* path) noexcept;

/// Close a previously opened dynamic library. No-op if lib is nullptr.
void dynlib_close(DynLib* lib) noexcept;

/// Look up a symbol by name. Returns nullptr if not found or lib is nullptr.
[[nodiscard]] void* dynlib_symbol(DynLib* lib, const char* name) noexcept;

} // namespace uz::platform
