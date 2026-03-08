// Tests for uz::SystemScheduler — tick groups, dependency ordering, lifecycle.

#include "engine/ecs/system_scheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// MockSystem — records lifecycle calls with a shared counter for ordering.
// ---------------------------------------------------------------------------
class MockSystem : public uz::IEcsSystem {
public:
    explicit MockSystem(const char* sys_name, int* counter = nullptr)
        : name_(sys_name), counter_(counter) {}

    void on_register() override {
        registered = true;
        if (counter_) register_order = (*counter_)++;
    }

    void tick(float dt) override {
        tick_count++;
        last_dt = dt;
        if (counter_) tick_order = (*counter_)++;
    }

    void on_shutdown() override {
        shutdown_called = true;
        if (counter_) shutdown_order = (*counter_)++;
    }

    [[nodiscard]] const char* name() const override { return name_; }

    // Observables
    bool  registered      = false;
    int   register_order  = -1;
    int   tick_count      = 0;
    float last_dt         = 0.0f;
    int   tick_order      = -1;
    bool  shutdown_called = false;
    int   shutdown_order  = -1;

private:
    const char* name_;
    int*        counter_;
};

// ---------------------------------------------------------------------------
// Helper: extract names from execution order
// ---------------------------------------------------------------------------
std::vector<std::string> names(const std::vector<const char*>& order) {
    std::vector<std::string> result;
    result.reserve(order.size());
    for (auto* n : order) {
        result.emplace_back(n);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class SystemSchedulerTest : public ::testing::Test {
protected:
    uz::SystemScheduler scheduler;
    int counter = 0;
};

// ===========================================================================
// Basic lifecycle
// ===========================================================================

TEST_F(SystemSchedulerTest, EmptyScheduler_BuildSucceeds) {
    EXPECT_TRUE(scheduler.build());
    EXPECT_EQ(scheduler.system_count(), 0u);
    EXPECT_TRUE(scheduler.get_execution_order().empty());
}

TEST_F(SystemSchedulerTest, EmptyScheduler_TickIsNoOp) {
    EXPECT_TRUE(scheduler.build());
    // Should not crash.
    scheduler.tick(0.016f);
}

TEST_F(SystemSchedulerTest, EmptyScheduler_ShutdownIsNoOp) {
    // Shutdown without build — should not crash.
    scheduler.shutdown();
}

TEST_F(SystemSchedulerTest, SingleSystem_RegisterTickShutdown) {
    MockSystem sys("SysA", &counter);

    scheduler.add_system({&sys, uz::TickGroup::Update, {}, {}});
    EXPECT_TRUE(sys.registered);
    EXPECT_EQ(scheduler.system_count(), 1u);

    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], "SysA");

    scheduler.tick(0.016f);
    EXPECT_EQ(sys.tick_count, 1);
    EXPECT_FLOAT_EQ(sys.last_dt, 0.016f);

    scheduler.shutdown();
    EXPECT_TRUE(sys.shutdown_called);
}

TEST_F(SystemSchedulerTest, AddSystem_NullSystemIgnored) {
    scheduler.add_system({nullptr, uz::TickGroup::Update, {}, {}});
    EXPECT_EQ(scheduler.system_count(), 0u);
}

// ===========================================================================
// Before / After ordering within a single group
// ===========================================================================

TEST_F(SystemSchedulerTest, BeforeConstraint_ABeforeB_BBeforeC) {
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);
    MockSystem c("C", &counter);

    scheduler.add_system({&a, uz::TickGroup::Update, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {"C"}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "A");
    EXPECT_EQ(order[1], "B");
    EXPECT_EQ(order[2], "C");
}

TEST_F(SystemSchedulerTest, AfterConstraint_AAfterB_BAfterC) {
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);
    MockSystem c("C", &counter);

    scheduler.add_system({&a, uz::TickGroup::Update, {}, {"B"}});
    scheduler.add_system({&b, uz::TickGroup::Update, {}, {"C"}});
    scheduler.add_system({&c, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "C");
    EXPECT_EQ(order[1], "B");
    EXPECT_EQ(order[2], "A");
}

TEST_F(SystemSchedulerTest, MixedBeforeAndAfter) {
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);
    MockSystem c("C", &counter);

    // A before B, C after B  =>  A, B, C
    scheduler.add_system({&a, uz::TickGroup::Update, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {}, {"B"}});

    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "A");
    EXPECT_EQ(order[1], "B");
    EXPECT_EQ(order[2], "C");
}

// ===========================================================================
// Cycle detection
// ===========================================================================

TEST_F(SystemSchedulerTest, Cycle_ABeforeB_BBeforeA_Fails) {
    MockSystem a("A");
    MockSystem b("B");

    scheduler.add_system({&a, uz::TickGroup::Update, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {"A"}, {}});

    EXPECT_FALSE(scheduler.build());
    EXPECT_TRUE(scheduler.get_execution_order().empty());
}

TEST_F(SystemSchedulerTest, Cycle_ThreeWay_Fails) {
    MockSystem a("A");
    MockSystem b("B");
    MockSystem c("C");

    scheduler.add_system({&a, uz::TickGroup::Update, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {"C"}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {"A"}, {}});

    EXPECT_FALSE(scheduler.build());
    EXPECT_TRUE(scheduler.get_execution_order().empty());
}

// ===========================================================================
// Tick group ordering
// ===========================================================================

TEST_F(SystemSchedulerTest, DifferentGroups_ExecuteInGroupOrder) {
    MockSystem pre("Pre", &counter);
    MockSystem phys("Phys", &counter);
    MockSystem upd("Upd", &counter);
    MockSystem late("Late", &counter);
    MockSystem render("Render", &counter);

    // Register in reverse group order to prove group ordering dominates.
    scheduler.add_system({&render, uz::TickGroup::PreRender, {}, {}});
    scheduler.add_system({&late,   uz::TickGroup::LateUpdate, {}, {}});
    scheduler.add_system({&upd,    uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&phys,   uz::TickGroup::PhysicsUpdate, {}, {}});
    scheduler.add_system({&pre,    uz::TickGroup::PreUpdate, {}, {}});

    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.016f);

    EXPECT_LT(pre.tick_order, phys.tick_order);
    EXPECT_LT(phys.tick_order, upd.tick_order);
    EXPECT_LT(upd.tick_order, late.tick_order);
    EXPECT_LT(late.tick_order, render.tick_order);
}

TEST_F(SystemSchedulerTest, DifferentGroups_IgnoreCrossGroupDeps) {
    // "before" referencing a system in another group should be silently ignored.
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);

    // A in PreUpdate says "before B", but B is in LateUpdate.
    // A should still run first because PreUpdate < LateUpdate.
    scheduler.add_system({&a, uz::TickGroup::PreUpdate, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::LateUpdate, {}, {}});

    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "A");
    EXPECT_EQ(order[1], "B");
}

TEST_F(SystemSchedulerTest, MultipleSystemsSameGroup_NoDeps_RegistrationOrder) {
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);
    MockSystem c("C", &counter);

    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 3u);
    // Kahn's BFS with FIFO queue preserves registration order for independent nodes.
    EXPECT_EQ(order[0], "A");
    EXPECT_EQ(order[1], "B");
    EXPECT_EQ(order[2], "C");
}

// ===========================================================================
// Tick behavior
// ===========================================================================

TEST_F(SystemSchedulerTest, TickCallsAllSystemsExactlyOnce) {
    MockSystem a("A");
    MockSystem b("B");
    MockSystem c("C");

    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.033f);

    EXPECT_EQ(a.tick_count, 1);
    EXPECT_EQ(b.tick_count, 1);
    EXPECT_EQ(c.tick_count, 1);
    EXPECT_FLOAT_EQ(a.last_dt, 0.033f);
    EXPECT_FLOAT_EQ(b.last_dt, 0.033f);
    EXPECT_FLOAT_EQ(c.last_dt, 0.033f);
}

TEST_F(SystemSchedulerTest, MultipleTicks_AccumulateCount) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    EXPECT_TRUE(scheduler.build());

    scheduler.tick(0.016f);
    scheduler.tick(0.016f);
    scheduler.tick(0.016f);

    EXPECT_EQ(a.tick_count, 3);
}

TEST_F(SystemSchedulerTest, TickBeforeBuild_IsNoOp) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});

    // No build() called.
    scheduler.tick(0.016f);
    EXPECT_EQ(a.tick_count, 0);
}

// ===========================================================================
// Shutdown
// ===========================================================================

TEST_F(SystemSchedulerTest, Shutdown_ReverseExecutionOrder) {
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);
    MockSystem c("C", &counter);

    scheduler.add_system({&a, uz::TickGroup::Update, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {"C"}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());

    // Execution order: A, B, C. Shutdown order: C, B, A.
    scheduler.shutdown();

    EXPECT_TRUE(a.shutdown_called);
    EXPECT_TRUE(b.shutdown_called);
    EXPECT_TRUE(c.shutdown_called);

    EXPECT_GT(a.shutdown_order, b.shutdown_order);
    EXPECT_GT(b.shutdown_order, c.shutdown_order);
}

TEST_F(SystemSchedulerTest, Shutdown_ClearsBuiltState) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    EXPECT_TRUE(scheduler.build());

    scheduler.shutdown();

    // After shutdown, tick should be a no-op (built_ is false).
    scheduler.tick(0.016f);
    EXPECT_EQ(a.tick_count, 0);
}

TEST_F(SystemSchedulerTest, Shutdown_AcrossMultipleGroups_ReverseOrder) {
    MockSystem pre("Pre", &counter);
    MockSystem upd("Upd", &counter);
    MockSystem render("Render", &counter);

    scheduler.add_system({&pre,    uz::TickGroup::PreUpdate, {}, {}});
    scheduler.add_system({&upd,    uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&render, uz::TickGroup::PreRender, {}, {}});

    EXPECT_TRUE(scheduler.build());
    scheduler.shutdown();

    // Execution: Pre, Upd, Render. Shutdown: Render, Upd, Pre.
    EXPECT_GT(pre.shutdown_order, upd.shutdown_order);
    EXPECT_GT(upd.shutdown_order, render.shutdown_order);
}

// ===========================================================================
// get_execution_order / system_count
// ===========================================================================

TEST_F(SystemSchedulerTest, GetExecutionOrder_BeforeBuild_Empty) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    EXPECT_TRUE(scheduler.get_execution_order().empty());
}

TEST_F(SystemSchedulerTest, SystemCount_MatchesRegisteredSystems) {
    MockSystem a("A");
    MockSystem b("B");
    MockSystem c("C");

    EXPECT_EQ(scheduler.system_count(), 0u);
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    EXPECT_EQ(scheduler.system_count(), 1u);
    scheduler.add_system({&b, uz::TickGroup::PhysicsUpdate, {}, {}});
    EXPECT_EQ(scheduler.system_count(), 2u);
    scheduler.add_system({&c, uz::TickGroup::PreRender, {}, {}});
    EXPECT_EQ(scheduler.system_count(), 3u);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_F(SystemSchedulerTest, OnRegister_CalledOnAdd) {
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);

    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});
    EXPECT_TRUE(a.registered);
    EXPECT_EQ(a.register_order, 0);

    scheduler.add_system({&b, uz::TickGroup::Update, {}, {}});
    EXPECT_TRUE(b.registered);
    EXPECT_EQ(b.register_order, 1);
}

TEST_F(SystemSchedulerTest, BeforeNonexistentSystem_Ignored) {
    MockSystem a("A");
    // "before" references a system that doesn't exist — should be ignored.
    scheduler.add_system({&a, uz::TickGroup::Update, {"DoesNotExist"}, {}});
    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], "A");
}

TEST_F(SystemSchedulerTest, AfterNonexistentSystem_Ignored) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {"Ghost"}});
    EXPECT_TRUE(scheduler.build());

    auto order = names(scheduler.get_execution_order());
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], "A");
}

TEST_F(SystemSchedulerTest, SelfDependency_Before_CycleDetected) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {"A"}, {}});
    EXPECT_FALSE(scheduler.build());
}

TEST_F(SystemSchedulerTest, SelfDependency_After_CycleDetected) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {"A"}});
    EXPECT_FALSE(scheduler.build());
}

TEST_F(SystemSchedulerTest, ComplexDag_DiamondDependency) {
    //   A
    //  / \
    // B   C
    //  \ /
    //   D
    MockSystem a("A", &counter);
    MockSystem b("B", &counter);
    MockSystem c("C", &counter);
    MockSystem d("D", &counter);

    scheduler.add_system({&a, uz::TickGroup::Update, {"B", "C"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {"D"}, {}});
    scheduler.add_system({&c, uz::TickGroup::Update, {"D"}, {}});
    scheduler.add_system({&d, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.016f);

    // A must be before B and C; B and C must be before D.
    EXPECT_LT(a.tick_order, b.tick_order);
    EXPECT_LT(a.tick_order, c.tick_order);
    EXPECT_LT(b.tick_order, d.tick_order);
    EXPECT_LT(c.tick_order, d.tick_order);
}

TEST_F(SystemSchedulerTest, MultipleGroupsWithDepsWithinEach) {
    MockSystem a1("A1", &counter);
    MockSystem a2("A2", &counter);
    MockSystem b1("B1", &counter);
    MockSystem b2("B2", &counter);

    // PreUpdate: A1 before A2
    scheduler.add_system({&a1, uz::TickGroup::PreUpdate, {"A2"}, {}});
    scheduler.add_system({&a2, uz::TickGroup::PreUpdate, {}, {}});

    // Update: B2 after B1
    scheduler.add_system({&b1, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&b2, uz::TickGroup::Update, {}, {"B1"}});

    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.016f);

    // Within PreUpdate: A1 < A2
    EXPECT_LT(a1.tick_order, a2.tick_order);
    // Within Update: B1 < B2
    EXPECT_LT(b1.tick_order, b2.tick_order);
    // All PreUpdate before all Update
    EXPECT_LT(a2.tick_order, b1.tick_order);
}

TEST_F(SystemSchedulerTest, RebuildAfterShutdown) {
    MockSystem a("A");
    scheduler.add_system({&a, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.016f);
    EXPECT_EQ(a.tick_count, 1);

    scheduler.shutdown();

    // Re-build and tick again.
    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.016f);
    EXPECT_EQ(a.tick_count, 2);
}

TEST_F(SystemSchedulerTest, CycleInOneGroup_OtherGroupsUnaffected) {
    // Even though Update group has a cycle, the entire build fails.
    MockSystem pre("Pre");
    MockSystem a("A");
    MockSystem b("B");

    scheduler.add_system({&pre, uz::TickGroup::PreUpdate, {}, {}});
    scheduler.add_system({&a, uz::TickGroup::Update, {"B"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {"A"}, {}});

    EXPECT_FALSE(scheduler.build());
    EXPECT_TRUE(scheduler.get_execution_order().empty());
}

TEST_F(SystemSchedulerTest, TickGroup_AllFiveGroupsPresent) {
    MockSystem s0("S0", &counter);
    MockSystem s1("S1", &counter);
    MockSystem s2("S2", &counter);
    MockSystem s3("S3", &counter);
    MockSystem s4("S4", &counter);

    scheduler.add_system({&s4, uz::TickGroup::PreRender, {}, {}});
    scheduler.add_system({&s3, uz::TickGroup::LateUpdate, {}, {}});
    scheduler.add_system({&s2, uz::TickGroup::Update, {}, {}});
    scheduler.add_system({&s1, uz::TickGroup::PhysicsUpdate, {}, {}});
    scheduler.add_system({&s0, uz::TickGroup::PreUpdate, {}, {}});

    EXPECT_TRUE(scheduler.build());
    scheduler.tick(0.016f);

    // Verify strict group ordering.
    EXPECT_LT(s0.tick_order, s1.tick_order);
    EXPECT_LT(s1.tick_order, s2.tick_order);
    EXPECT_LT(s2.tick_order, s3.tick_order);
    EXPECT_LT(s3.tick_order, s4.tick_order);
}

TEST_F(SystemSchedulerTest, GetExecutionOrder_NamesMatchAfterBuild) {
    MockSystem a("Alpha");
    MockSystem b("Beta");

    scheduler.add_system({&a, uz::TickGroup::Update, {"Beta"}, {}});
    scheduler.add_system({&b, uz::TickGroup::Update, {}, {}});

    EXPECT_TRUE(scheduler.build());

    auto order = scheduler.get_execution_order();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_STREQ(order[0], "Alpha");
    EXPECT_STREQ(order[1], "Beta");
}

} // anonymous namespace
