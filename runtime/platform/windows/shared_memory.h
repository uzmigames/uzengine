#pragma once

/// UzEngine Shared Memory — Named shared memory regions on Windows.
///
/// Wraps Win32 CreateFileMapping/MapViewOfFile for inter-process communication.
/// Named regions are visible system-wide, enabling data sharing between the
/// engine runtime and extension processes.
///
/// # Lifecycle
/// 1. Creator: `SharedMemoryCreate("MyRegion", size)` — allocates backing
/// 2. Consumer: `SharedMemoryOpen("MyRegion", size)` — maps existing region
/// 3. Both: read/write via `handle.data` pointer
/// 4. Both: `SharedMemoryClose(handle)` when done
///
/// # Thread Safety
/// Create/Open/Close are thread-safe (Win32 kernel objects).
/// The mapped `data` pointer requires external synchronization for concurrent access.

#include <cstddef>

namespace uz {

/// Handle to a named shared memory region.
///
/// `data` points to the mapped view. `size` is the requested region size.
/// All fields are zero/null when invalid or closed.
struct SharedMemoryHandle {
    void* mapping = nullptr;  ///< Win32 file mapping handle (HANDLE).
    void* data = nullptr;     ///< Pointer to the mapped memory view.
    size_t size = 0;          ///< Size of the mapped region in bytes.
};

/// Creates a new named shared memory region backed by the page file.
///
/// If a region with this name already exists, this opens it instead
/// (Win32 CreateFileMapping semantics).
///
/// # Arguments
/// * `name` — UTF-8 name for the region (globally visible). Must not be nullptr.
/// * `size` — Size in bytes. Must be > 0.
///
/// # Returns
/// - Valid handle with non-null `data` on success.
/// - Zero-initialized handle on failure (check logs).
SharedMemoryHandle SharedMemoryCreate(const char* name, size_t size);

/// Opens an existing named shared memory region.
///
/// Fails if no region with this name exists.
///
/// # Arguments
/// * `name` — UTF-8 name of the region to open. Must not be nullptr.
/// * `size` — Expected size for MapViewOfFile. Must be > 0.
///
/// # Returns
/// - Valid handle with non-null `data` on success.
/// - Zero-initialized handle on failure (check logs).
SharedMemoryHandle SharedMemoryOpen(const char* name, size_t size);

/// Closes a shared memory mapping and releases all resources.
///
/// Unmaps the view and closes the file mapping handle.
/// Safe to call on an already-closed or zero-initialized handle.
///
/// # Behavior
/// After this call, all fields in `handle` are zeroed.
/// The underlying region persists until ALL handles (in all processes) are closed.
void SharedMemoryClose(SharedMemoryHandle& handle);

} // namespace uz
