/// Unit tests for EngineLoop, ParseCommandLine, and ModuleRegistry.
///
/// Tests cover: CLI parsing for all supported flags, unknown-flag ignoring,
/// EngineLoop::Run with maxFrames, RegisterSubsystem participation, RequestExit,
/// module self-registration via UZ_REGISTER_MODULE, and headless mode checks.

#include "engine_loop.h"
#include "module_registry.h"
#include "subsystem.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — argv builder
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a mutable argv array from a list of string literals.
/// The array is null-terminated (one past the last element).
struct Argv {
    explicit Argv(std::vector<const char*> args) {
        for (const char* a : args) {
            ptrs_.push_back(const_cast<char*>(a));
        }
        ptrs_.push_back(nullptr);
    }

    char** data() { return ptrs_.data(); }
    int    argc() { return static_cast<int>(ptrs_.size()) - 1; }

private:
    std::vector<char*> ptrs_;
};

// ─────────────────────────────────────────────────────────────────────────────
// MockSubsystem for EngineLoop tests
// ─────────────────────────────────────────────────────────────────────────────

class LoopMockSubsystem : public ISubsystem {
public:
    explicit LoopMockSubsystem(const char* name,
                               TickGroup   group   = TickGroup::Update,
                               bool        succeed = true)
        : name_(name), group_(group), succeed_(succeed) {}

    [[nodiscard]] const char* GetName()      const override { return name_; }
    [[nodiscard]] InitPhase   GetInitPhase() const override { return InitPhase::PreInit; }
    [[nodiscard]] TickGroup   GetTickGroup() const override { return group_; }

    [[nodiscard]] bool Init()     override { ++initCount;  return succeed_; }
    void               Shutdown() override { ++shutdownCount; }
    void               Tick(float dt) override { ++tickCount; lastDt = dt; }

    std::atomic<int> initCount     = 0;
    std::atomic<int> shutdownCount = 0;
    std::atomic<int> tickCount     = 0;
    float            lastDt        = 0.0f;

private:
    const char* name_;
    TickGroup   group_;
    bool        succeed_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommandLine — supported flags
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseCommandLine, NoArgs_AllDefaults) {
    Argv argv({"uzengine"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_FALSE(cfg.headless);
    EXPECT_EQ(cfg.maxFrames,      0);
    EXPECT_EQ(cfg.screenshotFrame, -1);
    EXPECT_TRUE(cfg.screenshotPath.empty());
    EXPECT_TRUE(cfg.configPath.empty());
}

TEST(ParseCommandLine, HeadlessFlag_SetsHeadlessTrue) {
    Argv argv({"uzengine", "--headless"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_TRUE(cfg.headless);
}

TEST(ParseCommandLine, FramesFlag_SetsMaxFrames) {
    Argv argv({"uzengine", "--frames", "10"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_EQ(cfg.maxFrames, 10);
}

TEST(ParseCommandLine, FramesFlag_LargeValue) {
    Argv argv({"uzengine", "--frames", "99999"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_EQ(cfg.maxFrames, 99999);
}

TEST(ParseCommandLine, ScreenshotFlag_SetsPath) {
    Argv argv({"uzengine", "--screenshot", "out/frame.png"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_EQ(cfg.screenshotPath, "out/frame.png");
}

TEST(ParseCommandLine, ScreenshotFrameFlag_SetsFrame) {
    Argv argv({"uzengine", "--screenshot-frame", "5"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_EQ(cfg.screenshotFrame, 5);
}

TEST(ParseCommandLine, ConfigFlag_SetsConfigPath) {
    Argv argv({"uzengine", "--config", "engine.cfg"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_EQ(cfg.configPath, "engine.cfg");
}

TEST(ParseCommandLine, AllFlagsAtOnce_AllFieldsSet) {
    Argv argv({"uzengine",
               "--headless",
               "--frames",           "42",
               "--screenshot",       "shot.png",
               "--screenshot-frame", "7",
               "--config",           "custom.cfg"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_TRUE(cfg.headless);
    EXPECT_EQ(cfg.maxFrames,        42);
    EXPECT_EQ(cfg.screenshotPath,   "shot.png");
    EXPECT_EQ(cfg.screenshotFrame,  7);
    EXPECT_EQ(cfg.configPath,       "custom.cfg");
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommandLine — unknown flags are ignored
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseCommandLine, UnknownFlag_IsIgnored_DefaultsUnchanged) {
    Argv argv({"uzengine", "--unknown-flag", "--also-unknown", "value"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_FALSE(cfg.headless);
    EXPECT_EQ(cfg.maxFrames, 0);
    EXPECT_TRUE(cfg.screenshotPath.empty());
    EXPECT_TRUE(cfg.configPath.empty());
}

TEST(ParseCommandLine, MixedKnownAndUnknown_KnownFlagsApplied) {
    Argv argv({"uzengine", "--headless", "--bogus", "--frames", "3"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_TRUE(cfg.headless);
    EXPECT_EQ(cfg.maxFrames, 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// ParseCommandLine — edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST(ParseCommandLine, FramesFlag_MissingValue_NotParsed) {
    // --frames without a following argument should not crash and leave maxFrames=0
    Argv argv({"uzengine", "--frames"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_EQ(cfg.maxFrames, 0);
}

TEST(ParseCommandLine, ScreenshotFlag_MissingValue_NotParsed) {
    Argv argv({"uzengine", "--screenshot"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_TRUE(cfg.screenshotPath.empty());
}

TEST(ParseCommandLine, ConfigFlag_MissingValue_NotParsed) {
    Argv argv({"uzengine", "--config"});
    EngineConfig cfg = ParseCommandLine(argv.argc(), argv.data());

    EXPECT_TRUE(cfg.configPath.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop — construction
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, Constructor_TimeManagerIsAvailable) {
    EngineLoop loop;
    // TimeManager is registered as a subsystem — GetRegistry().Find should return it.
    EXPECT_NE(loop.GetRegistry().Find("TimeManager"), nullptr);
}

TEST(EngineLoop, Constructor_RegistryHasOneSubsystem) {
    EngineLoop loop;
    // Only TimeManager pre-registered
    EXPECT_EQ(loop.GetRegistry().GetCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop::RegisterSubsystem
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, RegisterSubsystem_AddsToRegistry) {
    EngineLoop loop;
    loop.RegisterSubsystem(std::make_unique<LoopMockSubsystem>("TestSub"));

    EXPECT_NE(loop.GetRegistry().Find("TestSub"), nullptr);
    EXPECT_EQ(loop.GetRegistry().GetCount(), 2u); // TimeManager + TestSub
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop::Run with maxFrames
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, Run_MaxFrames10_ExitsAfterTenFrames) {
    EngineLoop loop;

    auto* counter = new LoopMockSubsystem("Counter", TickGroup::Update);
    loop.RegisterSubsystem(std::unique_ptr<ISubsystem>(counter));

    EngineConfig cfg;
    cfg.maxFrames = 10;

    int result = loop.Run(cfg);

    EXPECT_EQ(result, 0);
    // The engine ran 10 frames. The counter subsystem must have been ticked on each
    // variable-rate update pass. frameCount is maintained by TimeManager.
    EXPECT_EQ(loop.GetTimeManager().GetFrameCount(), 10u);
}

TEST(EngineLoop, Run_MaxFrames1_ExitsAfterOneFrame) {
    EngineLoop loop;

    EngineConfig cfg;
    cfg.maxFrames = 1;

    int result = loop.Run(cfg);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(loop.GetTimeManager().GetFrameCount(), 1u);
}

TEST(EngineLoop, Run_MaxFrames5_SubsystemIsTickedFiveTimes) {
    EngineLoop loop;

    auto* sub = new LoopMockSubsystem("S", TickGroup::Update);
    loop.RegisterSubsystem(std::unique_ptr<ISubsystem>(sub));

    EngineConfig cfg;
    cfg.maxFrames = 5;

    loop.Run(cfg);

    // The first BeginFrame records the base timestamp without incrementing frameCount.
    // Each subsequent BeginFrame increments frameCount. With maxFrames=5, the loop runs
    // 6 RunOneFrame calls total (first is the zero-delta frame) → 6 variable-rate ticks.
    EXPECT_EQ(sub->tickCount.load(), 6);
}

TEST(EngineLoop, Run_ReturnsZeroOnCleanExit) {
    EngineLoop loop;

    EngineConfig cfg;
    cfg.maxFrames = 3;

    EXPECT_EQ(loop.Run(cfg), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop::Run — init failure returns 1
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, Run_SubsystemInitFails_ReturnsOne) {
    EngineLoop loop;

    loop.RegisterSubsystem(
        std::make_unique<LoopMockSubsystem>("FailSub", TickGroup::None, /*succeed=*/false));

    EngineConfig cfg;
    cfg.maxFrames = 10;

    EXPECT_EQ(loop.Run(cfg), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop::RequestExit — stops the loop
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, RequestExit_StopsLoopOnNextFrame) {
    EngineLoop loop;

    // Register a subsystem that calls RequestExit on its first tick.
    class ExitOnFirstTick : public ISubsystem {
    public:
        explicit ExitOnFirstTick(EngineLoop* owner) : owner_(owner) {}
        [[nodiscard]] const char* GetName()      const override { return "ExitOnFirstTick"; }
        [[nodiscard]] InitPhase   GetInitPhase() const override { return InitPhase::PreInit; }
        [[nodiscard]] TickGroup   GetTickGroup() const override { return TickGroup::Update; }
        [[nodiscard]] bool        Init()         override { return true; }
        void                      Shutdown()     override {}
        void Tick(float) override {
            ++tickCount;
            owner_->RequestExit();
        }
        int tickCount = 0;
    private:
        EngineLoop* owner_;
    };

    auto* exiter = new ExitOnFirstTick(&loop);
    loop.RegisterSubsystem(std::unique_ptr<ISubsystem>(exiter));

    // No maxFrames limit — loop must exit via RequestExit
    EngineConfig cfg;
    cfg.maxFrames = 0; // unlimited

    // Give it a hard limit in case RequestExit fails (prevents infinite loop in tests)
    // We do this by using a separate thread watchdog OR by accepting that if the impl
    // is broken the test will time out. For safety, we override with a large frame count.
    // In practice, the exiter calls RequestExit on first tick → exits after 1 frame.
    // We trust the implementation and test with maxFrames=1000 as safety net.
    cfg.maxFrames = 1000;

    int result = loop.Run(cfg);

    EXPECT_EQ(result, 0);
    // RequestExit was called on the first Tick. The loop exits after that RunOneFrame.
    // The first BeginFrame records the base timestamp but does NOT increment frameCount
    // (implementation detail: frameCount only increments on non-first BeginFrame calls).
    // So after exactly one RunOneFrame call, frameCount=0 and tickCount=1.
    EXPECT_EQ(loop.GetTimeManager().GetFrameCount(), 0u);
    EXPECT_EQ(exiter->tickCount, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop — config accessors
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, IsHeadless_WhenConfigHeadlessTrue_ReturnsTrue) {
    EngineLoop loop;

    EngineConfig cfg;
    cfg.headless  = true;
    cfg.maxFrames = 1;

    loop.Run(cfg);

    EXPECT_TRUE(loop.IsHeadless());
}

TEST(EngineLoop, IsHeadless_WhenConfigHeadlessFalse_ReturnsFalse) {
    EngineLoop loop;

    EngineConfig cfg;
    cfg.headless  = false;
    cfg.maxFrames = 1;

    loop.Run(cfg);

    EXPECT_FALSE(loop.IsHeadless());
}

TEST(EngineLoop, GetConfig_AfterRun_ReflectsPassedConfig) {
    EngineLoop loop;

    EngineConfig cfg;
    cfg.maxFrames    = 3;
    cfg.headless     = true;
    cfg.screenshotPath = "test.png";

    loop.Run(cfg);

    EXPECT_EQ(loop.GetConfig().maxFrames,      3);
    EXPECT_TRUE(loop.GetConfig().headless);
    EXPECT_EQ(loop.GetConfig().screenshotPath, "test.png");
}

// ─────────────────────────────────────────────────────────────────────────────
// EngineLoop — subsystem lifecycle participation
// ─────────────────────────────────────────────────────────────────────────────

TEST(EngineLoop, RegisteredSubsystem_IsInitializedAndShutDown) {
    EngineLoop loop;

    auto* sub = new LoopMockSubsystem("LifecycleSub");
    loop.RegisterSubsystem(std::unique_ptr<ISubsystem>(sub));

    EngineConfig cfg;
    cfg.maxFrames = 2;

    loop.Run(cfg);

    EXPECT_EQ(sub->initCount.load(),     1) << "Subsystem must be init'd exactly once";
    EXPECT_EQ(sub->shutdownCount.load(), 1) << "Subsystem must be shut down exactly once";
}

TEST(EngineLoop, Run_ViaArgcArgv_ParsesMaxFrames) {
    EngineLoop loop;

    Argv argv({"uzengine", "--frames", "5"});
    int result = loop.Run(argv.argc(), argv.data());

    EXPECT_EQ(result, 0);
    EXPECT_EQ(loop.GetTimeManager().GetFrameCount(), 5u);
}

// ─────────────────────────────────────────────────────────────────────────────
// UZ_REGISTER_MODULE — static self-registration
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Track whether the module callback was invoked and which loop it received.
static EngineLoop* g_registeredLoop = nullptr;
static int         g_moduleCallCount = 0;

void TestModuleRegistrar(EngineLoop& loop) {
    g_registeredLoop = &loop;
    ++g_moduleCallCount;
    // Register a subsystem from the module
    loop.RegisterSubsystem(std::make_unique<LoopMockSubsystem>("ModuleSubsystem", TickGroup::None));
}

UZ_REGISTER_MODULE(TestModule, TestModuleRegistrar);

} // namespace

TEST(ModuleRegistry, RegisterModule_CallbackInvokedDuringInit) {
    g_registeredLoop  = nullptr;
    g_moduleCallCount = 0;

    EngineLoop loop;

    EngineConfig cfg;
    cfg.maxFrames = 1;

    loop.Run(cfg);

    EXPECT_NE(g_registeredLoop, nullptr)
        << "UZ_REGISTER_MODULE callback must be invoked during EngineLoop::Initialize()";
    EXPECT_EQ(g_moduleCallCount, 1)
        << "Module registrar must be called exactly once per Run()";
}

TEST(ModuleRegistry, RegisterModule_SubsystemAddedToRegistry) {
    g_registeredLoop  = nullptr;
    g_moduleCallCount = 0;

    EngineLoop loop;

    EngineConfig cfg;
    cfg.maxFrames = 1;

    loop.Run(cfg);

    // The module registrar adds "ModuleSubsystem" — it must be findable.
    EXPECT_NE(loop.GetRegistry().Find("ModuleSubsystem"), nullptr)
        << "Subsystem registered by UZ_REGISTER_MODULE must be found in the registry";
}

TEST(ModuleRegistry, RegisterModule_LoopPassedToCallbackMatchesLoop) {
    g_registeredLoop = nullptr;

    EngineLoop loop;

    EngineConfig cfg;
    cfg.maxFrames = 1;

    loop.Run(cfg);

    EXPECT_EQ(g_registeredLoop, &loop)
        << "Callback must receive a reference to the same EngineLoop that called Run()";
}
