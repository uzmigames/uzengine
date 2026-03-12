/// UzEngine Clear Screen Demo — Backend-Agnostic Entry Point
///
/// Standalone executable to validate any RHI backend.
/// Opens a window and renders an animated color-cycling clear screen.
///
/// Supported flags:
/// - `--rhi <dx12|dx11|vulkan>` — Select RHI backend (default: dx12)
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5)
/// - `--frames <N>` — Total frames to render (0 = run until window closed)

#include "clear_screen.h"
#include "rhi_backend.h"
#include "log.h"

#include <cstring>
#include <cstdlib>

namespace {

/// Parses --rhi argument from command line to select RHI backend.
///
/// # Returns
/// The selected backend, or `DX12` if no `--rhi` flag is present.
uz::RHIBackend ParseBackend(int argc, char* argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--rhi") == 0) {
            const char* val = argv[i + 1];
            if (std::strcmp(val, "dx11")   == 0) return uz::RHIBackend::DX11;
            if (std::strcmp(val, "dx12")   == 0) return uz::RHIBackend::DX12;
            if (std::strcmp(val, "vulkan") == 0) return uz::RHIBackend::Vulkan;
            if (std::strcmp(val, "metal")  == 0) return uz::RHIBackend::Metal;
            if (std::strcmp(val, "null")   == 0) return uz::RHIBackend::Null;
        }
    }
#if defined(__APPLE__)
    return uz::RHIBackend::Metal;
#elif defined(_WIN32)
    return uz::RHIBackend::DX12;
#else
    return uz::RHIBackend::Vulkan;
#endif
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::ClearScreenConfig config{};
    config.backend = ParseBackend(argc, argv);

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.frameCount = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
    }

    // Ensure the demo terminates after the screenshot is captured
    if (!config.screenshotPath.empty() && config.frameCount == 0) {
        config.frameCount = config.screenshotFrame + 2;
    }

    bool ok = uz::RunClearScreen(config);
    uz::LogShutdown();
    return ok ? 0 : 1;
}
