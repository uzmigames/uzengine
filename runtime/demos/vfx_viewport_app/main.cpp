/// VFX Viewport Application — Entry Point
///
/// Uses the engine's Application framework instead of the legacy DemoApplication.
/// All RHI management, window lifecycle, rendering, and VFX simulation are handled
/// by the engine. The application only creates ECS entities and handles input.
///
/// This is the canonical engine-integrated version of the VFX viewport.
/// The legacy DemoApplication version remains at demos/vfx_viewport/ for
/// standalone testing and as a reference implementation.
///
/// # Custom CLI Arguments
/// - `--sprite <path>`: Load a sprite texture for all emitters (PNG/TGA/JPEG).
/// - `--flipbook <rows> <cols>`: Flipbook grid dimensions (used with --sprite).
/// - `--flipbook-fps <float>`: Flipbook playback rate in frames per second (default 30).
/// - `--channel <0|1|2|3|4>`: Sprite channel mask (0=RGBA, 1=R, 2=G, 3=B, 4=A).
/// - `--vfxsys <path>`: DEPRECATED — accepts the flag for compatibility with
///   older harness scripts but logs a one-time warning and ignores the path.
///   The compound `.uzvfxsys` asset path was removed in
///   `phase17_vfx-viewport-default-scene-no-uzvfxsys`; the demo now uses
///   in-code presets only.
/// - `--vfx-asset <path>`: Load a single-emitter `.uzvfx` asset via the
///   production `UzfxLoader::InstantiateEmitter` path (used by the VFX
///   hardware-validation suite). Bypasses the default fireball + preset
///   cycling scene. Path is interpreted relative to the active VFS root.
/// - `--vfs-root <path>`: Override the VFS root directory (defaults to
///   `<exe_dir>/content` if it exists, otherwise the engine fallback).
///   Useful with `--vfx-asset` to point at `sample/content/` so test
///   assets resolve without bundling them into the demo build.

#include "vfx_viewport_app.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (int r = uz::Application::TryComposeGrid(argc, argv); r >= 0)
        return r;
    if (int r = uz::Application::TryDiffImages(argc, argv); r >= 0)
        return r;

    uz::VFXViewportApp app;

    // Parse VFX-specific CLI arguments before Run() calls Configure() + ParseArgs().
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sprite") == 0 && i + 1 < argc) {
            app.SetSpriteTexturePath(argv[++i]);
        } else if (std::strcmp(argv[i], "--flipbook") == 0 && i + 2 < argc) {
            const uint32_t rows = static_cast<uint32_t>(std::atoi(argv[++i]));
            const uint32_t cols = static_cast<uint32_t>(std::atoi(argv[++i]));
            app.SetFlipbookGrid(rows, cols);
        } else if (std::strcmp(argv[i], "--flipbook-fps") == 0 && i + 1 < argc) {
            const float fps = static_cast<float>(std::atof(argv[++i]));
            app.SetFlipbookFPS(fps);
        } else if (std::strcmp(argv[i], "--channel") == 0 && i + 1 < argc) {
            uint32_t mask = static_cast<uint32_t>(std::atoi(argv[++i]));
            if (mask > 4u) mask = 0u; // Clamp unknown values to RGBA pass-through.
            app.SetSpriteChannelMask(mask);
        } else if (std::strcmp(argv[i], "--vfxsys") == 0 && i + 1 < argc) {
            app.SetSystemAssetPath(argv[++i]);
        } else if (std::strcmp(argv[i], "--vfx-asset") == 0 && i + 1 < argc) {
            app.SetEmitterAssetPath(argv[++i]);
        } else if (std::strcmp(argv[i], "--vfs-root") == 0 && i + 1 < argc) {
            app.SetCustomVfsRoot(argv[++i]);
        } else if (std::strcmp(argv[i], "--window-size") == 0 && i + 1 < argc) {
            // Phase 13.5 — `--window-size WxH` override for headless
            // multi-resolution screenshot capture.
            const char* arg = argv[++i];
            const char* x = std::strchr(arg, 'x');
            if (x) {
                const int w = std::atoi(arg);
                const int h = std::atoi(x + 1);
                if (w > 0 && h > 0) {
                    app.SetWindowSize(static_cast<uint32_t>(w),
                                       static_cast<uint32_t>(h));
                }
            }
        }
    }

    return app.Run(argc, argv);
}
