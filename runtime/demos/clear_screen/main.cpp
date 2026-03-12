/// UzEngine Clear Screen Demo — Backend-Agnostic Entry Point
///
/// Standalone executable to validate any RHI backend.
/// Opens a window and renders an animated color-cycling clear screen.
/// Supports --rhi flag to select backend (dx12, dx11). Defaults to DX12.

#include "clear_screen.h"
#include "rhi_backend.h"
#include "log.h"

#include <cstring>

namespace {

/// Parses --rhi argument from command line to select RHI backend.
///
/// # Arguments
/// * `argc` - Argument count from main().
/// * `argv` - Argument vector from main().
///
/// # Returns
/// The selected backend, or DX12 if no --rhi flag is present or the value
/// is unrecognized.
uz::RHIBackend ParseBackend(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--rhi") == 0) {
            const char* val = argv[i + 1];
            if (std::strcmp(val, "dx11") == 0) return uz::RHIBackend::DX11;
            if (std::strcmp(val, "dx12") == 0) return uz::RHIBackend::DX12;
            if (std::strcmp(val, "vulkan") == 0) return uz::RHIBackend::Vulkan;
            if (std::strcmp(val, "metal") == 0) return uz::RHIBackend::Metal;
            if (std::strcmp(val, "null") == 0) return uz::RHIBackend::Null;
        }
    }
    return uz::RHIBackend::DX12;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::RHIBackend backend = ParseBackend(argc, argv);
    bool success = uz::RunClearScreen(backend, 0);

    uz::LogShutdown();
    return success ? 0 : 1;
}
