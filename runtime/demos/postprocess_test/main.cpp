/// UzEngine Post-Process Test Demo — EngineLoop Entry Point
///
/// Opens a window, renders bright HDR cubes, applies tonemap + bloom + FXAA.
/// Toggle effects with T/B/A keys.
///
/// Supported flags:
/// - `--rhi <dx12|dx11|vulkan>` — Select RHI backend (default: dx12)
/// - `--screenshot <path>` — Capture back buffer and save as PNG
/// - `--screenshot-frame <N>` — Frame to capture on (default: 5)
/// - `--frames <N>` — Total frames to render (0 = run until window closed)

#include "postprocess_test_demo.h"
#include "engine_loop.h"
#include "rhi_backend.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

/// Parses --rhi argument from command line to select RHI backend.
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

/// Subsystem that runs the post-process test demo within the EngineLoop lifecycle.
class PostProcessSubsystem final : public uz::ISubsystem {
public:
    PostProcessSubsystem(int argc, char* argv[])
        : argc_(argc), argv_(argv) {}

    [[nodiscard]] const char* GetName() const override { return "PostProcessTest"; }
    [[nodiscard]] uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    [[nodiscard]] uz::TickGroup GetTickGroup() const override { return uz::TickGroup::None; }

    [[nodiscard]] bool Init() override {
        uz::PostProcessTestConfig config{};
        config.backend = ParseBackend(argc_, argv_);

        for (int i = 1; i < argc_; ++i) {
            if (std::strcmp(argv_[i], "--screenshot") == 0 && i + 1 < argc_) {
                config.screenshotPath = argv_[++i];
            } else if (std::strcmp(argv_[i], "--screenshot-frame") == 0 && i + 1 < argc_) {
                config.screenshotFrame = static_cast<uint32_t>(std::atoi(argv_[++i]));
            } else if (std::strcmp(argv_[i], "--frames") == 0 && i + 1 < argc_) {
                config.frameCount = static_cast<uint32_t>(std::atoi(argv_[++i]));
            } else if (std::strcmp(argv_[i], "--no-tonemap") == 0) {
                config.enableTonemap = false;
            } else if (std::strcmp(argv_[i], "--no-fxaa") == 0) {
                config.enableFXAA = false;
            }
        }

        if (!config.screenshotPath.empty() && config.frameCount == 0) {
            config.frameCount = config.screenshotFrame + 2;
        }

        return uz::RunPostProcessTest(config);
    }

    void Shutdown() override {}

private:
    int argc_ = 0;
    char** argv_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    uz::LogInit();

    uz::EngineLoop loop;
    loop.RegisterSubsystem(std::make_unique<PostProcessSubsystem>(argc, argv));

    uz::EngineConfig engineConfig = uz::ParseCommandLine(argc, argv);
    engineConfig.maxFrames = 1;
    int result = loop.Run(engineConfig);

    uz::LogShutdown();
    return result;
}
