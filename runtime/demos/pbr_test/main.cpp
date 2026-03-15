/// PBR Test Demo — Entry Point
///
/// Two build configurations are supported:
///
/// **New (Application-based)**: Define `USE_APPLICATION` to use the new
/// uz::Application pattern. The old DemoApplication code in pbr_test_demo.cpp
/// is not compiled — all scene logic lives in pbr_test_app.cpp instead.
///
/// **Legacy (DemoApplication-based)**: Without `USE_APPLICATION`, this builds
/// the original PBRTestDemo with full render graph control.
///
/// Supported flags (both modes):
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5)
/// - `--frames <N>` — Total frames to render (0 = run until window closed)
/// - `--compose-grid output.png [--cell-size N] img.png "Label" "Sub" ...`
/// - `--diff output.png imgA.png "A" imgB.png "B"` — Pixel diff (no GPU)

#ifdef USE_APPLICATION

// ─────────────────────────────────────────────────────────────────────────────
// New Application-based entry point
// ─────────────────────────────────────────────────────────────────────────────
#include "pbr_test_app.h"

UZ_APPLICATION_MAIN(uz::PBRTestApp)

#else

// ─────────────────────────────────────────────────────────────────────────────
// Legacy DemoApplication-based entry point
// ─────────────────────────────────────────────────────────────────────────────
#include "pbr_test_demo.h"
#include "log.h"

int main(int argc, char* argv[]) {
    uz::LogInit();

    if (int r = uz::DemoApplication::TryComposeGrid(argc, argv); r >= 0) { uz::LogShutdown(); return r; }
    if (int r = uz::DemoApplication::TryDiffImages(argc, argv);  r >= 0) { uz::LogShutdown(); return r; }

    uz::DemoConfig baseConfig{};
    uz::DemoApplication::ParseCommonArgs(argc, argv, baseConfig);

    uz::PBRTestConfig c{};
    c.screenshotPath      = baseConfig.screenshotPath;
    c.screenshotFrame     = baseConfig.screenshotFrame;
    c.frameCount          = baseConfig.maxFrames;
    c.backend             = baseConfig.backend;
    c.shaderModel         = baseConfig.shaderModel;
    c.debugViewMode       = baseConfig.debugViewMode;
    c.allModesOutputPath  = baseConfig.allModesOutputPath;

    uz::PBRTestDemo demo(c);
    const int result = demo.Run();
    uz::LogShutdown();
    return result;
}

#endif
