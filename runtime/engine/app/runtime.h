/// UzEngine Runtime C API
///
/// This header defines the stable C interface exported by the UzEngine runtime.
/// All functions use C calling convention for compatibility with N-API bindings.
///
/// # Thread Safety
/// - `uz_runtime_init()` and `uz_runtime_shutdown()` are NOT thread-safe
/// - Call init once at startup, shutdown once at exit
/// - All other functions are reentrant
///
/// # Memory Ownership
/// - Returned strings are statically allocated (lifetime = program lifetime)
/// - Caller must not free or modify returned strings

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Returns the version string of the UzEngine runtime.
///
/// # Returns
/// A pointer to a null-terminated string (e.g., "0.2.0").
/// The string is statically allocated and never freed.
///
/// # Example
/// ```c
/// const char* version = uz_runtime_version();
/// printf("UzEngine %s\n", version);
/// ```
const char* uz_runtime_version(void);

/// Returns the name of the active rendering backend.
///
/// # Returns
/// Backend identifier string (e.g., "uzengine", "dx12", "vulkan").
/// The string is statically allocated and never freed.
const char* uz_runtime_backend(void);

/// Initializes the UzEngine runtime.
///
/// # Behavior
/// - Initializes GPU device and rendering context
/// - Sets up memory allocators and logging
/// - Prepares the frame graph and command queues
///
/// # Returns
/// - 1 on success
/// - 0 on failure (check logs for details)
///
/// # Safety
/// - Must be called exactly once before any other runtime function
/// - NOT thread-safe: call from main thread only
/// - Failure does NOT require shutdown()
int uz_runtime_init(void);

/// Shuts down the UzEngine runtime.
///
/// # Behavior
/// - Flushes all pending GPU work
/// - Releases GPU resources and device
/// - Cleans up allocators and logging
///
/// # Safety
/// - Must be called exactly once if init() succeeded
/// - NOT thread-safe: call from main thread only
/// - Safe to call if init() failed or never called
void uz_runtime_shutdown(void);

#ifdef __cplusplus
}
#endif
