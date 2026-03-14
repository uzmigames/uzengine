/// UzEngine Deferred Rendering Test Demo — Entry Point
///
/// Validates the multi-pass deferred shading pipeline: G-Buffer fill,
/// deferred lighting, G-Buffer debug visualization, and forward PBR fallback.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG.
/// - `--screenshot-frame <N>` — Frame at which to capture (default: 60).
/// - `--frames <N>` — Total frames to render (0 = run until window closed).

#include "deferred_test_demo.h"
#include "log.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::DeferredTestConfig config{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--debug-mode") == 0 && i + 1 < argc) {
            config.debugMode = std::atoi(argv[++i]);
        }
    }

    // Auto-exit after screenshot so CI pipelines don't block
    if (!config.screenshotPath.empty() && config.maxFrames == 0) {
        config.maxFrames = config.screenshotFrame + 2;
    }

    uz::DeferredTestDemo demo(config);
    const int result = demo.Run();

    uz::LogShutdown();
    return result;
}
