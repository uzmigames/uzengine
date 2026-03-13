/// Deferred Rendering Test Demo — EngineLoop Entry Point
///
/// Standalone executable that validates the multi-pass deferred shading pipeline:
/// G-Buffer fill (5 cubes, 3 MRTs + depth), deferred lighting (4 point lights +
/// directional sun), and G-Buffer debug visualization (keys 1-7). Backend is
/// selectable at runtime — defaults to DX12.
///
/// Uses EngineLoop for lifecycle management and CLI parsing.
///
/// Supported flags:
/// - `--rhi <dx12|dx11|vulkan|metal|null>` — Select RHI backend (default: dx12)
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-frame <N>` — Frame to capture on (default: 60)
/// - `--frames <N>` — Total frames to render (0 = run until window closed)

#include "deferred_test_demo.h"
#include "engine_loop.h"
#include "rhi_backend.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

/// Parses the `--rhi` argument from the command line.
///
/// # Arguments
/// * `argc` — Argument count from main().
/// * `argv` — Argument vector from main().
///
/// # Returns
/// The selected backend, or `DX12` if no `--rhi` flag is present or the
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

// ─────────────────────────────────────────────────────────────────────────────
// DeferredTestSubsystem — wraps the deferred rendering demo as an engine subsystem
// ─────────────────────────────────────────────────────────────────────────────

/// Subsystem that runs the deferred rendering test demo within the EngineLoop lifecycle.
///
/// Captures command-line arguments at construction. During `Init()`, parses the
/// RHI backend selection and screenshot flags, builds a `DeferredTestConfig`,
/// and runs the full demo render loop. The demo's internal loop handles window
/// creation, mesh loading, frame pacing, and shutdown.
///
/// # Lifecycle
/// - `Init()` — Parses CLI args, runs the demo (blocking), returns success/failure.
/// - `Tick()` — Not used (demo runs its own loop in Init).
/// - `Shutdown()` — No-op (demo cleans up internally).
class DeferredTestSubsystem final : public uz::ISubsystem {
public:
    /// Constructs the subsystem, capturing CLI args for later parsing in Init().
    ///
    /// # Arguments
    /// * `argc` — Argument count from main().
    /// * `argv` — Argument vector from main(). Must remain valid until Init() completes.
    DeferredTestSubsystem(int argc, char* argv[])
        : argc_(argc), argv_(argv) {}

    [[nodiscard]] const char*    GetName()      const override { return "DeferredTest"; }
    [[nodiscard]] uz::InitPhase  GetInitPhase() const override { return uz::InitPhase::Init; }
    [[nodiscard]] uz::TickGroup  GetTickGroup() const override { return uz::TickGroup::None; }

    /// Parses CLI arguments, builds demo config, and runs the full deferred rendering demo.
    ///
    /// # Returns
    /// `true` if the demo rendered all frames successfully, `false` on RHI failure.
    [[nodiscard]] bool Init() override {
        uz::DeferredTestConfig config{};
        config.backend = ParseBackend(argc_, argv_);

        for (int i = 1; i < argc_; ++i) {
            if (std::strcmp(argv_[i], "--screenshot") == 0 && i + 1 < argc_) {
                config.screenshotPath = argv_[++i];
            } else if (std::strcmp(argv_[i], "--screenshot-frame") == 0 && i + 1 < argc_) {
                config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv_[++i]));
            } else if (std::strcmp(argv_[i], "--frames") == 0 && i + 1 < argc_) {
                config.frameCount = static_cast<uint32_t>(std::atoi(argv_[++i]));
            }
        }

        // Auto-exit after screenshot is captured so CI pipelines don't block
        if (!config.screenshotPath.empty() && config.frameCount == 0) {
            config.frameCount = config.screenshotFrame + 2;
        }

        return uz::RunDeferredTestDemo(config);
    }

    /// No-op — the demo runs its own render loop in Init().
    void Shutdown() override {}

private:
    int    argc_ = 0;
    char** argv_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::EngineLoop loop;
    loop.RegisterSubsystem(std::make_unique<DeferredTestSubsystem>(argc, argv));

    uz::EngineConfig engineConfig = uz::ParseCommandLine(argc, argv);
    engineConfig.maxFrames = 1;
    int result = loop.Run(engineConfig);

    uz::LogShutdown();
    return result;
}
