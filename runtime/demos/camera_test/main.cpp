/// Camera Test Demo — Entry Point
///
/// Standalone executable that renders a 3D cube with FPS camera controls.
/// Supports command-line flags:
/// - `--rhi <backend>` — Select RHI backend: dx12 (default), dx11, vulkan, metal, null
/// - `--screenshot <path>` — Capture back buffer and save as PNG.
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5).
/// - `--frames <N>` — Total frames to render (0 = infinite).

#include "camera_test_demo.h"
#include "rhi_backend.h"
#include "log.h"

#include <cstdlib>
#include <cstring>

namespace {

/// Parses --rhi argument from command line to select RHI backend.
///
/// # Arguments
/// * `argc` - Argument count from main().
/// * `argv` - Argument vector from main().
///
/// # Returns
/// The selected backend, or DX12 if no --rhi flag is present or the
/// value is unrecognized.
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
    return uz::RHIBackend::DX12;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::CameraTestConfig config{};
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

    // If screenshot requested but no frame count, auto-exit after capture
    if (!config.screenshotPath.empty() && config.frameCount == 0) {
        config.frameCount = config.screenshotFrame + 2;
    }

    bool ok = uz::RunCameraTestDemo(config);
    uz::LogShutdown();
    return ok ? 0 : 1;
}
