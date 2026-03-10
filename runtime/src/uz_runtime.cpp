/// UzEngine Runtime Implementation
///
/// This file implements the C API exported by uz_runtime.h.
/// All functions are defined in extern "C" blocks for C calling convention compatibility.
///
/// # Current Status (Phase 1)
/// - Version and backend queries: ✓ implemented
/// - Init/shutdown stubs: temporary (to be replaced with GPU init)
/// - Full GPU pipeline: in development

#include "uz_runtime.h"

extern "C" {

/// Implements uz_runtime_version().
/// Currently returns a hardcoded version string.
/// TODO: Generate this from build system (CMake or Zig)
const char* uz_runtime_version(void) {
    return "0.2.0";
}

/// Implements uz_runtime_backend().
/// Returns the rendering backend identifier.
/// TODO: Detect and return actual backend name (dx12, vulkan, metal, etc.)
const char* uz_runtime_backend(void) {
    return "uzengine";
}

/// Implements uz_runtime_init().
///
/// # Current Implementation
/// - Stub: returns success (1)
/// - Phase 1: Placeholder for GPU initialization
///
/// # TODO (Phase 2)
/// - Create D3D12 device (if Windows)
/// - Initialize memory allocators
/// - Set up logging and profiling
/// - Create swapchain and frame graph
/// - Compile shaders and load pipelines
int uz_runtime_init(void) {
    return 1;
}

/// Implements uz_runtime_shutdown().
///
/// # Current Implementation
/// - Stub: no-op
///
/// # TODO (Phase 2)
/// - Flush GPU work
/// - Release command queues
/// - Destroy swapchain
/// - Release device and resources
/// - Clean up allocators
void uz_runtime_shutdown(void) {}

} // extern "C"
