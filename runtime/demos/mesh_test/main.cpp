/// Mesh Test Demo — Entry Point
///
/// Renders a lit cube using mesh_vs/mesh_ps shaders with directional lighting.
/// Creates a MeshTestDemo instance and delegates lifecycle to the
/// DemoApplication base class.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5)
/// - `--frames <N>` — Total frames to render (0 = run until window closed)

#include "mesh_test_demo.h"
#include "log.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::MeshTestConfig config{};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.frameCount = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
    }

    // Auto-exit after screenshot capture
    if (!config.screenshotPath.empty() && config.frameCount == 0) {
        config.frameCount = config.screenshotFrame + 2;
    }

    uz::MeshTestDemo demo(config);
    const int result = demo.Run();

    uz::LogShutdown();
    return result;
}
