/// PBR Test Demo — Entry Point
///
/// Renders 5 PBR-lit cubes using Cook-Torrance shading with deferred/forward
/// path toggle. Creates a PBRTestDemo instance and delegates lifecycle to
/// the DemoApplication base class.
///
/// Supported flags:
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5)
/// - `--frames <N>` — Total frames to render (0 = run until window closed)

#include "pbr_test_demo.h"
#include "log.h"


int main(int argc, char* argv[]) {
    uz::LogInit();

    // Check for --compose-grid mode (grid composition, no GPU needed)
    int gridResult = uz::DemoApplication::TryComposeGrid(argc, argv);
    if (gridResult >= 0) {
        uz::LogShutdown();
        return gridResult;
    }

    // Check for --diff mode (image comparison, no GPU needed)
    int diffResult = uz::DemoApplication::TryDiffImages(argc, argv);
    if (diffResult >= 0) {
        uz::LogShutdown();
        return diffResult;
    }

    // Parse common CLI flags (--backend, --sm, --screenshot, --frames, etc.)
    uz::DemoConfig baseConfig{};
    uz::DemoApplication::ParseCommonArgs(argc, argv, baseConfig);

    uz::PBRTestConfig config{};
    config.screenshotPath  = baseConfig.screenshotPath;
    config.screenshotFrame = baseConfig.screenshotFrame;
    config.frameCount      = baseConfig.maxFrames;
    config.backend         = baseConfig.backend;
    config.shaderModel     = baseConfig.shaderModel;

    uz::PBRTestDemo demo(config);
    const int result = demo.Run();

    uz::LogShutdown();
    return result;
}
