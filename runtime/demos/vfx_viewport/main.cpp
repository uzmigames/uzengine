/// VFX Viewport Demo — Entry Point
///
/// Standalone executable that renders procedural PBR meshes, a procedural sky,
/// shadow mapping, HDR tonemapping, and GPU-driven particle effects using the
/// DemoApplication base class and the engine's RHI abstraction.
///
/// Replaces the legacy `tools/uz_viewport.cpp` monolith with standard engine
/// infrastructure (ShaderManager, PipelineLibrary, CBRingAllocator, etc.).

#include "vfx_viewport_demo.h"
#include "demo_application.h"
#include "renderdoc_capture.h"
#include "log.h"

#include <cstring>
#include <string>

int main(int argc, char* argv[]) {
    uz::LogInit();

    // RenderDoc must be loaded before device creation to intercept GPU API calls
    if (uz::RenderDocInit()) {
        uz::RenderDocSetCaptureOptions();
    }

    uz::DemoConfig config;
    config.title = "UzEngine VFX Viewport";
    config.cameraMoveSpeed = 15.f;
    config.cameraPositionX = 0.f;
    config.cameraPositionY = -15.f;
    config.cameraPositionZ = 8.f;
    config.cameraYawDeg = 90.f;
    config.cameraPitchDeg = -20.f;
    uz::DemoApplication::ParseCommonArgs(argc, argv, config);

    // Handle compose-grid / diff utility modes (exit early if triggered)
    int gridResult = uz::DemoApplication::TryComposeGrid(argc, argv);
    if (gridResult >= 0) { uz::LogShutdown(); return gridResult; }
    int diffResult = uz::DemoApplication::TryDiffImages(argc, argv);
    if (diffResult >= 0) { uz::LogShutdown(); return diffResult; }

    // Parse texture paths from command line (after common args)
    std::string albedoPath;
    std::string normalPath;
    std::string roughnessPath;
    std::string spritePath;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--albedo") == 0) {
            albedoPath = argv[++i];
        } else if (std::strcmp(argv[i], "--normal") == 0) {
            normalPath = argv[++i];
        } else if (std::strcmp(argv[i], "--roughness") == 0) {
            roughnessPath = argv[++i];
        } else if (std::strcmp(argv[i], "--sprite") == 0) {
            spritePath = argv[++i];
        }
    }

    uz::VFXViewportDemo demo(config);
    demo.SetTexturePaths(albedoPath, normalPath, roughnessPath);
    if (!spritePath.empty()) {
        demo.SetSpritePath(spritePath);
    }
    int result = demo.Run();

    uz::LogShutdown();
    return result;
}
