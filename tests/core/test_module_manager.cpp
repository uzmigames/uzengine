#include <gtest/gtest.h>
#include "engine/core/module_manager.h"

#include <stdexcept>
#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// Test module that records lifecycle events
// ============================================================================

static std::vector<std::string> g_events;

class RecordingModule : public ModuleInterface {
public:
    explicit RecordingModule(const char* name, int prio = 0)
        : name_(name), priority_(prio) {}

    void startup() override {
        g_events.push_back(std::string(name_) + ".startup");
    }

    void shutdown() override {
        g_events.push_back(std::string(name_) + ".shutdown");
    }

    const char* name() const override { return name_; }
    int priority() const override { return priority_; }

private:
    const char* name_;
    int         priority_;
};

// Module that throws on startup
class FailingModule : public ModuleInterface {
public:
    explicit FailingModule(const char* name, int prio = 0)
        : name_(name), priority_(prio) {}

    void startup() override {
        g_events.push_back(std::string(name_) + ".startup_FAIL");
        throw std::runtime_error(std::string(name_) + " failed");
    }

    void shutdown() override {
        g_events.push_back(std::string(name_) + ".shutdown");
    }

    const char* name() const override { return name_; }
    int priority() const override { return priority_; }

private:
    const char* name_;
    int         priority_;
};

// Module that throws on shutdown (should be caught and continued)
class ShutdownFailModule : public ModuleInterface {
public:
    explicit ShutdownFailModule(const char* name, int prio = 0)
        : name_(name), priority_(prio) {}

    void startup() override {
        g_events.push_back(std::string(name_) + ".startup");
    }

    void shutdown() override {
        g_events.push_back(std::string(name_) + ".shutdown_FAIL");
        throw std::runtime_error(std::string(name_) + " shutdown failed");
    }

    const char* name() const override { return name_; }
    int priority() const override { return priority_; }

private:
    const char* name_;
    int         priority_;
};

class ModuleManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_events.clear();
    }
};

// ============================================================================
// Basic lifecycle
// ============================================================================

TEST_F(ModuleManagerTest, EmptyManagerReturnsfalse) {
    ModuleManager mgr;
    EXPECT_FALSE(mgr.startup_all());
    EXPECT_EQ(mgr.module_count(), 0u);
}

TEST_F(ModuleManagerTest, SingleModuleStartupShutdown) {
    ModuleManager mgr;
    RecordingModule mod("Core");
    mgr.register_module(&mod);

    EXPECT_EQ(mgr.module_count(), 1u);
    EXPECT_TRUE(mgr.startup_all());
    EXPECT_EQ(mgr.running_count(), 1u);
    EXPECT_EQ(mgr.module_state("Core"), ModuleState::Running);

    mgr.shutdown_all();
    EXPECT_EQ(mgr.running_count(), 0u);
    EXPECT_EQ(mgr.module_state("Core"), ModuleState::Stopped);

    ASSERT_EQ(g_events.size(), 2u);
    EXPECT_EQ(g_events[0], "Core.startup");
    EXPECT_EQ(g_events[1], "Core.shutdown");
}

// ============================================================================
// Priority ordering
// ============================================================================

TEST_F(ModuleManagerTest, PriorityOrderStartup) {
    ModuleManager mgr;
    RecordingModule low("Low", 10);
    RecordingModule mid("Mid", 50);
    RecordingModule high("High", 100);

    // Register in non-priority order
    mgr.register_module(&low);
    mgr.register_module(&high);
    mgr.register_module(&mid);

    mgr.startup_all();

    // Startup should be in descending priority order: High(100) → Mid(50) → Low(10)
    ASSERT_GE(g_events.size(), 3u);
    EXPECT_EQ(g_events[0], "High.startup");
    EXPECT_EQ(g_events[1], "Mid.startup");
    EXPECT_EQ(g_events[2], "Low.startup");

    mgr.shutdown_all();

    // Shutdown should be in REVERSE startup order: Low → Mid → High
    ASSERT_EQ(g_events.size(), 6u);
    EXPECT_EQ(g_events[3], "Low.shutdown");
    EXPECT_EQ(g_events[4], "Mid.shutdown");
    EXPECT_EQ(g_events[5], "High.shutdown");
}

TEST_F(ModuleManagerTest, StartupOrderQuery) {
    ModuleManager mgr;
    RecordingModule a("Alpha", 300);
    RecordingModule b("Beta", 100);
    RecordingModule c("Gamma", 200);

    mgr.register_module(&a);
    mgr.register_module(&b);
    mgr.register_module(&c);

    mgr.startup_all();

    auto order = mgr.startup_order();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_STREQ(order[0], "Alpha");
    EXPECT_STREQ(order[1], "Gamma");
    EXPECT_STREQ(order[2], "Beta");

    mgr.shutdown_all();
}

// ============================================================================
// Reverse-order shutdown
// ============================================================================

TEST_F(ModuleManagerTest, FiveModulesReverseShutdown) {
    ModuleManager mgr;
    RecordingModule mods[5] = {
        RecordingModule("A", 50),
        RecordingModule("B", 40),
        RecordingModule("C", 30),
        RecordingModule("D", 20),
        RecordingModule("E", 10),
    };
    for (auto& m : mods) mgr.register_module(&m);

    mgr.startup_all();
    mgr.shutdown_all();

    // Startup order: A(50), B(40), C(30), D(20), E(10)
    // Shutdown order: E, D, C, B, A (reverse)
    ASSERT_EQ(g_events.size(), 10u);
    EXPECT_EQ(g_events[5], "E.shutdown");
    EXPECT_EQ(g_events[6], "D.shutdown");
    EXPECT_EQ(g_events[7], "C.shutdown");
    EXPECT_EQ(g_events[8], "B.shutdown");
    EXPECT_EQ(g_events[9], "A.shutdown");
}

// ============================================================================
// Exception safety — startup failure triggers reverse rollback
// ============================================================================

TEST_F(ModuleManagerTest, FailedStartupRollsBackPriorModules) {
    ModuleManager mgr;
    RecordingModule a("Core", 100);
    RecordingModule b("Scene", 50);
    FailingModule   c("Renderer", 25);  // This one will fail
    RecordingModule d("Editor", 10);     // This should never start

    mgr.register_module(&a);
    mgr.register_module(&b);
    mgr.register_module(&c);
    mgr.register_module(&d);

    EXPECT_THROW(mgr.startup_all(), std::runtime_error);

    // Core and Scene should have been started then rolled back.
    // Renderer failed. Editor should never have started.
    EXPECT_FALSE(mgr.is_started());

    // Expected events:
    // Core.startup, Scene.startup, Renderer.startup_FAIL,
    // Scene.shutdown (rollback), Core.shutdown (rollback)
    ASSERT_EQ(g_events.size(), 5u);
    EXPECT_EQ(g_events[0], "Core.startup");
    EXPECT_EQ(g_events[1], "Scene.startup");
    EXPECT_EQ(g_events[2], "Renderer.startup_FAIL");
    EXPECT_EQ(g_events[3], "Scene.shutdown");
    EXPECT_EQ(g_events[4], "Core.shutdown");
}

TEST_F(ModuleManagerTest, FirstModuleFailsNoRollback) {
    ModuleManager mgr;
    FailingModule a("FirstFail", 100);
    RecordingModule b("NeverStart", 50);

    mgr.register_module(&a);
    mgr.register_module(&b);

    EXPECT_THROW(mgr.startup_all(), std::runtime_error);

    // Only the failing startup should have happened — no rollback needed.
    ASSERT_EQ(g_events.size(), 1u);
    EXPECT_EQ(g_events[0], "FirstFail.startup_FAIL");
}

// ============================================================================
// Shutdown exceptions are caught and continued
// ============================================================================

TEST_F(ModuleManagerTest, ShutdownExceptionDoesNotAbort) {
    ModuleManager mgr;
    RecordingModule a("Core", 100);
    ShutdownFailModule b("BadShutdown", 50);
    RecordingModule c("Editor", 10);

    mgr.register_module(&a);
    mgr.register_module(&b);
    mgr.register_module(&c);

    mgr.startup_all();
    EXPECT_EQ(mgr.running_count(), 3u);

    // shutdown_all() should NOT throw — exceptions are caught internally.
    EXPECT_NO_THROW(mgr.shutdown_all());
    EXPECT_EQ(mgr.running_count(), 0u);

    // All three should have attempted shutdown, even though BadShutdown threw.
    ASSERT_EQ(g_events.size(), 6u);
    EXPECT_EQ(g_events[3], "Editor.shutdown");
    EXPECT_EQ(g_events[4], "BadShutdown.shutdown_FAIL");
    EXPECT_EQ(g_events[5], "Core.shutdown");
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(ModuleManagerTest, DuplicateRegistrationIgnored) {
    ModuleManager mgr;
    RecordingModule a("Dup");
    mgr.register_module(&a);
    mgr.register_module(&a);  // Should be ignored

    EXPECT_EQ(mgr.module_count(), 1u);

    mgr.startup_all();
    ASSERT_EQ(g_events.size(), 1u);  // Only one startup
    mgr.shutdown_all();
}

TEST_F(ModuleManagerTest, ShutdownWithoutStartupIsNoop) {
    ModuleManager mgr;
    RecordingModule a("NotStarted");
    mgr.register_module(&a);

    // Should be a no-op — no crash, no events.
    EXPECT_NO_THROW(mgr.shutdown_all());
    EXPECT_EQ(g_events.size(), 0u);
}

TEST_F(ModuleManagerTest, DoubleShutdownIsIdempotent) {
    ModuleManager mgr;
    RecordingModule a("Core");
    mgr.register_module(&a);

    mgr.startup_all();
    mgr.shutdown_all();
    g_events.clear();

    // Second shutdown should be a no-op.
    mgr.shutdown_all();
    EXPECT_EQ(g_events.size(), 0u);
}

TEST_F(ModuleManagerTest, ModuleStateUnknownName) {
    ModuleManager mgr;
    EXPECT_EQ(mgr.module_state("NonExistent"), ModuleState::Stopped);
}

TEST_F(ModuleManagerTest, DestructorCallsShutdown) {
    RecordingModule a("AutoShutdown");
    {
        ModuleManager mgr;
        mgr.register_module(&a);
        mgr.startup_all();
        // mgr goes out of scope — destructor should call shutdown_all()
    }

    ASSERT_EQ(g_events.size(), 2u);
    EXPECT_EQ(g_events[0], "AutoShutdown.startup");
    EXPECT_EQ(g_events[1], "AutoShutdown.shutdown");
}

// ============================================================================
// Same priority respects registration order (stable sort)
// ============================================================================

TEST_F(ModuleManagerTest, SamePriorityStableOrder) {
    ModuleManager mgr;
    RecordingModule a("First", 0);
    RecordingModule b("Second", 0);
    RecordingModule c("Third", 0);

    mgr.register_module(&a);
    mgr.register_module(&b);
    mgr.register_module(&c);

    mgr.startup_all();

    // Same priority — should maintain registration order.
    ASSERT_GE(g_events.size(), 3u);
    EXPECT_EQ(g_events[0], "First.startup");
    EXPECT_EQ(g_events[1], "Second.startup");
    EXPECT_EQ(g_events[2], "Third.startup");

    mgr.shutdown_all();

    // Reverse: Third, Second, First
    EXPECT_EQ(g_events[3], "Third.shutdown");
    EXPECT_EQ(g_events[4], "Second.shutdown");
    EXPECT_EQ(g_events[5], "First.shutdown");
}
