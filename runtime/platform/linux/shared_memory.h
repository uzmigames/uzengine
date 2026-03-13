#pragma once

/// UzEngine Shared Memory — Named shared memory regions on Linux.
///
/// Wraps POSIX shm_open/mmap for inter-process communication.
/// Named regions live in `/dev/shm/` (tmpfs) and are visible to all
/// processes that open them by the same name, enabling data sharing between
/// the engine runtime and extension processes.
///
/// # Lifecycle
/// 1. Creator: `SharedMemoryCreate("MyRegion", size)` — creates and maps
/// 2. Consumer: `SharedMemoryOpen("MyRegion", size)` — opens and maps existing region
/// 3. Both: read/write via `handle.data` pointer
/// 4. Both: `SharedMemoryClose(handle)` when done
///
/// # Thread Safety
/// Create/Open/Close are thread-safe (kernel-serialized shm_open).
/// The mapped `data` pointer requires external synchronization for concurrent
/// access from multiple threads or processes.

#ifdef __linux__

#include <cstddef>

namespace uz {

/// Handle to a named POSIX shared memory region.
///
/// `mapping` holds the shm file descriptor as an intptr_t-in-void* (NOT an
/// OS HANDLE — cast back with `(int)(intptr_t)mapping`).
/// `data` points to the mmap'd region. `size` is the requested region size.
/// All fields are zero/null when invalid or closed.
struct SharedMemoryHandle {
    void*  mapping = nullptr; ///< shm file descriptor cast to void* (intptr_t).
    void*  data    = nullptr; ///< Pointer to the mmap'd memory region.
    size_t size    = 0;       ///< Size of the mapped region in bytes.
};

/// Creates a new named shared memory region.
///
/// Calls `shm_open(name, O_CREAT | O_RDWR, 0600)` then `ftruncate()` to set
/// the size, then `mmap(PROT_READ | PROT_WRITE)`. If a region with this name
/// already exists, it is opened instead and the existing content is preserved
/// (shm_open O_CREAT semantics — no truncation if already at correct size).
///
/// # Arguments
/// * `name` — POSIX shared memory name, must start with '/'. Must not be nullptr.
///             Example: "/uz_frame_buffer".
/// * `size` — Size in bytes. Must be > 0.
///
/// # Returns
/// - Valid handle with non-null `data` on success.
/// - Zero-initialized handle on failure (check logs).
SharedMemoryHandle SharedMemoryCreate(const char* name, size_t size);

/// Opens an existing named shared memory region.
///
/// Calls `shm_open(name, O_RDWR)` (no O_CREAT) and maps it.
/// Fails if no region with this name exists.
///
/// # Arguments
/// * `name` — POSIX shared memory name (must start with '/'). Must not be nullptr.
/// * `size` — Number of bytes to map. Must be > 0.
///
/// # Returns
/// - Valid handle with non-null `data` on success.
/// - Zero-initialized handle on failure (check logs).
SharedMemoryHandle SharedMemoryOpen(const char* name, size_t size);

/// Closes a shared memory mapping and releases all resources.
///
/// Calls `munmap()` and `close()` on the fd. Does NOT call `shm_unlink()`
/// — the named region persists until an explicit unlink. Safe to call on an
/// already-closed or zero-initialized handle.
///
/// # Behavior
/// After this call, all fields in `handle` are zeroed.
/// The backing region in /dev/shm persists until all handles across all
/// processes are closed AND shm_unlink() is called by the creator.
void SharedMemoryClose(SharedMemoryHandle& handle);

} // namespace uz

#endif // __linux__
