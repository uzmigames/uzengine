/// UzEngine Textured Quad Demo — Entry Point
///
/// Validates texture upload and sampling across any RHI backend.
/// Opens a window and renders a procedural checkerboard quad.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG.
/// - `--screenshot-frame <N>` — Frame at which to capture (default: 60).
/// - `--frames <N>` — Total frames to render (0 = run until window closed).

#include "textured_quad_demo.h"
#include "log.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::TexturedQuadConfig config{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.maxFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
    }

    // Auto-exit after screenshot so CI pipelines don't block
    if (!config.screenshotPath.empty() && config.maxFrames == 0) {
        config.maxFrames = config.screenshotFrame + 2;
    }

    uz::TexturedQuadDemo demo(config);
    const int result = demo.Run();

    uz::LogShutdown();
    return result;
}
