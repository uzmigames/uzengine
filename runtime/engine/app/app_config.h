/// ApplicationConfig — Configuration struct for uz::Application.
///
/// Returned by `Application::Configure()` to control window creation,
/// RHI backend selection, render path, camera defaults, and screenshot capture.
/// All fields have sensible defaults for a windowed demo or game.
///
/// # Usage
/// ```cpp
/// class MyGame : public uz::Application {
///     ApplicationConfig Configure() override {
///         return { .title = "My Game", .width = 1920, .height = 1080 };
///     }
/// };
/// ```

#pragma once

#include "debug_view_mode.h"
#include "render_path.h"

#include <cstdint>
#include <optional>
#include <string>

namespace uz {

enum class RHIBackend  : uint8_t;
enum class ShaderModel : uint8_t;

// ─────────────────────────────────────────────────────────────────────────────
// ApplicationConfig
// ─────────────────────────────────────────────────────────────────────────────

/// Full configuration for Application initialization.
///
/// Returned by `Application::Configure()` before the window and device are
/// created. CLI arguments parsed in `Run()` may override individual fields.
struct ApplicationConfig {
    // ── Window ────────────────────────────────────────────────────────────

    /// Window title bar text.
    const char* title = "UzEngine";

    /// Initial window width in pixels.
    uint32_t width = 1280;

    /// Initial window height in pixels.
    uint32_t height = 720;

    /// Enable vertical sync on present.
    bool vsync = false;

    /// Enable GPU validation layer. Forced off in NDEBUG builds.
    bool enableValidation = true;

    // ── Renderer ─────────────────────────────────────────────────────────

    /// Default render path for SceneRenderer.
    RenderPath renderPath = RenderPath::Deferred;

    // ── Asset System ─────────────────────────────────────────────────────

    /// Root directory for the VFS. If empty, defaults to `<exeDir>/assets/`.
    std::string vfsRoot;

    // ── Frame Control ─────────────────────────────────────────────────────

    /// Maximum frames to render before exiting. 0 = unlimited.
    uint32_t maxFrames = 0;

    // ── Screenshot ────────────────────────────────────────────────────────

    /// If non-empty, capture a screenshot to this path after `screenshotFrame` frames.
    std::string screenshotPath;

    /// Frames to wait after OnStart() before capturing the screenshot.
    uint32_t screenshotFrame = 5;

    /// If non-empty, capture all debug view modes and compose into a labeled grid PNG.
    std::string allModesOutputPath;

    // ── RHI Overrides ────────────────────────────────────────────────────

    /// If set, overrides the default DX12 backend.
    std::optional<RHIBackend> backend;

    /// If set, forces a specific shader model (overrides auto-detect).
    std::optional<ShaderModel> shaderModel;

    // ── Debug View ───────────────────────────────────────────────────────

    /// Initial debug view mode (overridable via --view-mode CLI arg).
    DebugViewMode debugViewMode = DebugViewMode::Lit;

    // ── Camera Defaults ───────────────────────────────────────────────────

    /// FPS camera movement speed in cm/second (UE5 parity: 1 unit = 1 cm).
    float cameraMoveSpeed = 1000.f;

    /// Default camera X position (cm).
    float cameraPositionX = 0.f;

    /// Default camera Y position (cm).
    float cameraPositionY = 500.f;

    /// Default camera Z position (cm).
    float cameraPositionZ = 200.f;

    /// Default camera yaw in degrees (-90 = facing -Z).
    float cameraYawDeg = -90.f;

    /// Default camera pitch in degrees (negative = looking slightly down).
    float cameraPitchDeg = -20.f;
};

} // namespace uz
