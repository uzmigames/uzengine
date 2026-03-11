/// Camera Test Demo — entry point.
///
/// Supports command-line flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG.
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5).
/// - `--frames <N>` — Total frames to render (0 = infinite).

#include "camera_test_demo.h"
#include "log.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::CameraTestConfig config{};

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
