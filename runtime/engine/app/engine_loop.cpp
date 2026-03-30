/// UzEngine Engine Loop — Implementation
///
/// Drives the full engine lifecycle: CLI parsing, module self-registration,
/// dependency-sorted subsystem init, fixed + variable timestep frame loop,
/// and ordered shutdown.

#include "engine_loop.h"

#include "../assertions/engine_assert.h"
#include "../logging/log.h"
#include "../profiling/profiler.h"

#include <cstdlib>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommandLine
// ─────────────────────────────────────────────────────────────────────────────

EngineConfig ParseCommandLine(int argc, char* argv[]) {
    EngineConfig config;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            config.headless = true;
        } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            config.screenshotPath = argv[++i];
        } else if (std::strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            config.screenshotFrame = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config.configPath = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            config.maxFrames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--shadow-debug") == 0 && i + 1 < argc) {
            config.shadowDebugMode = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--diag") == 0) {
            config.diagMode = true;
        } else if (std::strcmp(argv[i], "--pixel-debug") == 0 && i + 2 < argc) {
            config.pixelDebugX = static_cast<uint32_t>(std::atoi(argv[++i]));
            config.pixelDebugY = static_cast<uint32_t>(std::atoi(argv[++i]));
            config.pixelDebugEnabled = true;
        }
    }

    // --diag implies auto-exit so CI pipelines don't hang
    if (config.diagMode && config.maxFrames == 0) {
        config.maxFrames = 120;
    }

    // --pixel-debug implies auto-exit
    if (config.pixelDebugEnabled && config.maxFrames == 0) {
        config.maxFrames = 120;
    }

    return config;
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop — construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

EngineLoop::EngineLoop() {
    auto timeManagerPtr = std::make_unique<TimeManager>();
    timeManager_ = timeManagerPtr.get();
    registry_.Register(std::move(timeManagerPtr));
}

EngineLoop::~EngineLoop() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void EngineLoop::RegisterSubsystem(std::unique_ptr<ISubsystem> subsystem) {
    registry_.Register(std::move(subsystem));
}

// ─────────────────────────────────────────────────────────────────────────────
// Run — entry points
// ─────────────────────────────────────────────────────────────────────────────

int EngineLoop::Run(int argc, char* argv[]) {
    config_ = ParseCommandLine(argc, argv);
    return Run(config_);
}

int EngineLoop::Run(const EngineConfig& config) {
    config_ = config;

    UZ_LOG(Lifecycle, Info, "EngineLoop starting (headless={}, maxFrames={})",
           config_.headless, config_.maxFrames);

    if (!Initialize()) {
        UZ_LOG(Lifecycle, Error, "EngineLoop initialization failed: headless={}, maxFrames={}",
               config_.headless, config_.maxFrames);
        Shutdown();
        return 1;
    }

    MainLoop();
    Shutdown();

    UZ_LOG(Lifecycle, Info, "EngineLoop exited cleanly (frames={})",
           timeManager_->GetFrameCount());
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize — module registration + subsystem init
// ─────────────────────────────────────────────────────────────────────────────

bool EngineLoop::Initialize() {
    UZ_PROFILE_FUNCTION();

    // Run all static module registrars (from UZ_REGISTER_MODULE)
    detail::RunModuleRegistrars(*this);

    UZ_LOG(Lifecycle, Info, "Registered {} subsystems, initializing...",
           registry_.GetCount());

    // Init all subsystems in dependency-sorted, phase-grouped order
    if (!registry_.InitAll()) {
        return false;
    }

    UZ_LOG(Lifecycle, Info, "All subsystems initialized successfully");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MainLoop — fixed + variable timestep frame loop
// ─────────────────────────────────────────────────────────────────────────────

void EngineLoop::MainLoop() {
    while (!exitRequested_) {
        RunOneFrame();
    }
}

void EngineLoop::RunOneFrame() {
    UZ_PROFILE_FUNCTION();

    timeManager_->BeginFrame();

    // Fixed-timestep physics ticks
    const int fixedTicks = timeManager_->ConsumeFixedTicks();
    const float fixedDt = timeManager_->GetFixedDeltaTime();
    for (int i = 0; i < fixedTicks; ++i) {
        registry_.RunTickGroup(TickGroup::PrePhysics, fixedDt);
        registry_.RunTickGroup(TickGroup::PostPhysics, fixedDt);
    }

    // Variable-rate update
    const float dt = timeManager_->GetDeltaTime();
    registry_.RunTickGroup(TickGroup::Update, dt);
    registry_.RunTickGroup(TickGroup::PostUpdate, dt);

    // Frame limit check
    if (config_.maxFrames > 0 &&
        timeManager_->GetFrameCount() >= static_cast<uint64_t>(config_.maxFrames)) {
        exitRequested_ = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void EngineLoop::Shutdown() {
    UZ_LOG(Lifecycle, Info, "EngineLoop shutting down...");
    registry_.ShutdownAll();
}

// ─────────────────────────────────────────────────────────────────────────────
// RequestExit
// ─────────────────────────────────────────────────────────────────────────────

void EngineLoop::RequestExit() {
    UZ_LOG(Lifecycle, Info, "Exit requested");
    exitRequested_ = true;
}

} // namespace uz
