/// UzEngine Runtime Implementation
///
/// This file implements the C API exported by runtime.h.
/// All functions are defined in extern "C" blocks for C calling convention compatibility.

#include "runtime.h"
#include "rhi_backend.h"

extern "C" {

/// Returns the engine version string.
/// Sourced from the build configuration — update here when bumping the version.
const char* uz_runtime_version(void) {
    return "0.7.0";
}

/// Returns the name of the active RHI backend ("dx12", "dx11", "vulkan", "metal", "none").
const char* uz_runtime_backend(void) {
    switch (uz::GetActiveRHIBackend()) {
        case uz::RHIBackend::DX12:   return "dx12";
        case uz::RHIBackend::DX11:   return "dx11";
        case uz::RHIBackend::Vulkan: return "vulkan";
        case uz::RHIBackend::Metal:  return "metal";
        default:                     return "none";
    }
}

/// Initializes the runtime.
///
/// The runtime C API is intended for plugin/scripting use. The actual engine
/// initialization happens via Application::Run(). This function returns success
/// for API compatibility.
int uz_runtime_init(void) {
    return 1;
}

/// Shuts down the runtime.
///
/// Cleanup is handled by Application::Shutdown().
void uz_runtime_shutdown(void) {
}

} // extern "C"
