/// Unit tests for the UzEngine Tick System (Phase 3b-3.3b).
///
/// Coverage targets:
///   - tickable_component.h/.cpp  → 100%
///   - tick_system.h/.cpp         → 100%
///   - tick_subsystem.h/.cpp      → 100%
///
/// No GPU, RHI, or audio hardware required. All entity validity checks use
/// a real uz::World (lightweight, no GPU). Tests are isolated per fixture.

#include "tickable_component.h"
#include "tick_system.h"
#include "tick_subsystem.h"
#include "world.h"
#include "property_macros.h"
#include "class_registry.h"

#include <gtest/gtest.h>

#include <vector>
#include <cmath>
#include <cstdint>
#include <string>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Concrete test tickables
// ─────────────────────────────────────────────────────────────────────────────

/// Basic tickable — records call count and last received delta time.
class TestTickable : public TickableComponent {
public:
    void OnTick(float dt) override {
        tickCount++;
        lastDt = dt;
    }

    int   tickCount = 0;
    float lastDt    = 0.F;
};

/// Tickable that overrides GetTickGroup to PrePhysics.
class PrePhysicsTickable : public TickableComponent {
public:
    void OnTick(float dt) override {
        (void)dt;
        dispatchOrder.push_back(TickGroup::PrePhysics);
    }

    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::PrePhysics; }

    std::vector<TickGroup> dispatchOrder;
};

/// Tickable that overrides GetTickGroup to PostPhysics.
class PostPhysicsTickable : public TickableComponent {
public:
    void OnTick(float dt) override {
        (void)dt;
        dispatchOrder.push_back(TickGroup::PostPhysics);
    }

    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::PostPhysics; }

    std::vector<TickGroup> dispatchOrder;
};

/// Tickable that overrides GetTickGroup to PostUpdate.
class PostUpdateTickable : public TickableComponent {
public:
    void OnTick(float dt) override {
        (void)dt;
        dispatchOrder.push_back(TickGroup::PostUpdate);
    }

    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::PostUpdate; }

    std::vector<TickGroup> dispatchOrder;
};

/// Shared global order log used to verify multi-group dispatch ordering.
static std::vector<TickGroup> gDispatchLog;

/// Tickable for cross-group ordering tests — appends to gDispatchLog.
class LoggingPrePhysicsTickable : public TickableComponent {
public:
    void OnTick(float) override { gDispatchLog.push_back(TickGroup::PrePhysics); }
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::PrePhysics; }
};

class LoggingPostPhysicsTickable : public TickableComponent {
public:
    void OnTick(float) override { gDispatchLog.push_back(TickGroup::PostPhysics); }
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::PostPhysics; }
};

class LoggingUpdateTickable : public TickableComponent {
public:
    void OnTick(float) override { gDispatchLog.push_back(TickGroup::Update); }
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::Update; }
};

class LoggingPostUpdateTickable : public TickableComponent {
public:
    void OnTick(float) override { gDispatchLog.push_back(TickGroup::PostUpdate); }
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::PostUpdate; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps = 1e-5F;

static bool NearlyEqual(float a, float b, float eps = kEps) {
    return std::fabs(a - b) < eps;
}

// ─────────────────────────────────────────────────────────────────────────────
// TickableComponent unit tests — no World needed
// ─────────────────────────────────────────────────────────────────────────────

class TickableComponentTest : public ::testing::Test {
protected:
    TestTickable comp_;
};

/// Default GetTickGroup returns TickGroup::Update.
TEST_F(TickableComponentTest, DefaultGetTickGroup_ReturnsUpdate) {
    EXPECT_EQ(comp_.GetTickGroup(), TickGroup::Update);
}

/// Default IsTickEnabled returns true.
TEST_F(TickableComponentTest, DefaultState_IsTickEnabled_IsTrue) {
    EXPECT_TRUE(comp_.IsTickEnabled());
}

/// Default GetTickInterval returns 0.
TEST_F(TickableComponentTest, DefaultState_GetTickInterval_IsZero) {
    EXPECT_FLOAT_EQ(comp_.GetTickInterval(), 0.F);
}

/// Default GetTimeSinceLastTick returns 0.
TEST_F(TickableComponentTest, DefaultState_GetTimeSinceLastTick_IsZero) {
    EXPECT_FLOAT_EQ(comp_.GetTimeSinceLastTick(), 0.F);
}

/// SetTickEnabled(false) → IsTickEnabled returns false.
TEST_F(TickableComponentTest, SetTickEnabled_False_IsTickEnabledReturnsFalse) {
    comp_.SetTickEnabled(false);
    EXPECT_FALSE(comp_.IsTickEnabled());
}

/// SetTickEnabled(false) then SetTickEnabled(true) → IsTickEnabled returns true.
TEST_F(TickableComponentTest, SetTickEnabled_Toggle_BackToTrue) {
    comp_.SetTickEnabled(false);
    EXPECT_FALSE(comp_.IsTickEnabled());

    comp_.SetTickEnabled(true);
    EXPECT_TRUE(comp_.IsTickEnabled());
}

/// SetTickInterval stores the value returned by GetTickInterval.
TEST_F(TickableComponentTest, SetTickInterval_StoresValue_GetTickIntervalReturnsIt) {
    comp_.SetTickInterval(0.5F);
    EXPECT_FLOAT_EQ(comp_.GetTickInterval(), 0.5F);
}

/// SetTickInterval to 0 clears interval throttling.
TEST_F(TickableComponentTest, SetTickInterval_Zero_ClearsThrottling) {
    comp_.SetTickInterval(1.0F);
    EXPECT_FLOAT_EQ(comp_.GetTickInterval(), 1.0F);

    comp_.SetTickInterval(0.F);
    EXPECT_FLOAT_EQ(comp_.GetTickInterval(), 0.F);
}

/// Overriding GetTickGroup to PrePhysics returns PrePhysics.
TEST(TickableComponentOverride, GetTickGroup_Override_ReturnsPrePhysics) {
    PrePhysicsTickable comp;
    EXPECT_EQ(comp.GetTickGroup(), TickGroup::PrePhysics);
}

/// Overriding GetTickGroup to PostPhysics returns PostPhysics.
TEST(TickableComponentOverride, GetTickGroup_Override_ReturnsPostPhysics) {
    PostPhysicsTickable comp;
    EXPECT_EQ(comp.GetTickGroup(), TickGroup::PostPhysics);
}

/// Overriding GetTickGroup to PostUpdate returns PostUpdate.
TEST(TickableComponentOverride, GetTickGroup_Override_ReturnsPostUpdate) {
    PostUpdateTickable comp;
    EXPECT_EQ(comp.GetTickGroup(), TickGroup::PostUpdate);
}

// ─────────────────────────────────────────────────────────────────────────────
// TickSystem unit tests
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture: fresh World and TickSystem per test. No shared state.
class TickSystemTest : public ::testing::Test {
protected:
    World      world_;
    TickSystem tickSys_;
};

// ── Registration ──────────────────────────────────────────────────────────────

/// RegisterTickable with Update group increments GetTickableCount.
TEST_F(TickSystemTest, RegisterTickable_UpdateGroup_CountIncrements) {
    TestTickable comp;
    Entity e = world_.CreateEntity();

    tickSys_.RegisterTickable(e, &comp, world_);

    EXPECT_EQ(tickSys_.GetTickableCount(), 1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::Update), 1u);
}

/// RegisterTickable with PrePhysics group goes into PrePhysics bucket.
TEST_F(TickSystemTest, RegisterTickable_PrePhysicsGroup_CorrectBucket) {
    PrePhysicsTickable comp;
    Entity e = world_.CreateEntity();

    tickSys_.RegisterTickable(e, &comp, world_);

    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PrePhysics), 1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::Update),     0u);
}

/// RegisterTickable with PostPhysics group goes into PostPhysics bucket.
TEST_F(TickSystemTest, RegisterTickable_PostPhysicsGroup_CorrectBucket) {
    PostPhysicsTickable comp;
    Entity e = world_.CreateEntity();

    tickSys_.RegisterTickable(e, &comp, world_);

    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PostPhysics), 1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PrePhysics),  0u);
}

/// RegisterTickable with PostUpdate group goes into PostUpdate bucket.
TEST_F(TickSystemTest, RegisterTickable_PostUpdateGroup_CorrectBucket) {
    PostUpdateTickable comp;
    Entity e = world_.CreateEntity();

    tickSys_.RegisterTickable(e, &comp, world_);

    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PostUpdate), 1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::Update),     0u);
}

/// Multiple tickables in different groups accumulate in GetTickableCount total.
TEST_F(TickSystemTest, RegisterTickable_MultipleGroups_TotalCountIsSum) {
    TestTickable       updateComp;
    PrePhysicsTickable preComp;
    PostUpdateTickable postComp;

    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();

    tickSys_.RegisterTickable(e1, &updateComp, world_);
    tickSys_.RegisterTickable(e2, &preComp,    world_);
    tickSys_.RegisterTickable(e3, &postComp,   world_);

    EXPECT_EQ(tickSys_.GetTickableCount(), 3u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::Update),     1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PrePhysics), 1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PostUpdate), 1u);
}

/// Duplicate registration of the same entity is ignored (count stays at 1).
TEST_F(TickSystemTest, RegisterTickable_DuplicateEntity_IgnoredCountStaysOne) {
    TestTickable comp;
    Entity e = world_.CreateEntity();

    tickSys_.RegisterTickable(e, &comp, world_);
    tickSys_.RegisterTickable(e, &comp, world_); // second registration — should be no-op

    EXPECT_EQ(tickSys_.GetTickableCount(), 1u)
        << "Duplicate registration must be silently ignored";
}

// ── Unregistration ────────────────────────────────────────────────────────────

/// UnregisterTickable removes the entry and decrements count.
TEST_F(TickSystemTest, UnregisterTickable_RegisteredEntity_CountDecrements) {
    TestTickable comp;
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);
    ASSERT_EQ(tickSys_.GetTickableCount(), 1u);

    tickSys_.UnregisterTickable(e);

    EXPECT_EQ(tickSys_.GetTickableCount(), 0u);
}

/// UnregisterTickable on an entity that was never registered is a no-op.
TEST_F(TickSystemTest, UnregisterTickable_NotRegistered_IsNoOp) {
    Entity e = world_.CreateEntity();

    EXPECT_NO_FATAL_FAILURE(tickSys_.UnregisterTickable(e));
    EXPECT_EQ(tickSys_.GetTickableCount(), 0u);
}

/// Unregister only removes the specified entity — others remain.
TEST_F(TickSystemTest, UnregisterTickable_OneOfTwo_OtherRemains) {
    TestTickable comp1, comp2;
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    tickSys_.RegisterTickable(e1, &comp1, world_);
    tickSys_.RegisterTickable(e2, &comp2, world_);
    ASSERT_EQ(tickSys_.GetTickableCount(), 2u);

    tickSys_.UnregisterTickable(e1);

    EXPECT_EQ(tickSys_.GetTickableCount(), 1u);

    // e2's tickable must still fire when ticked.
    tickSys_.Tick(TickGroup::Update, 0.016F, world_);
    EXPECT_EQ(comp2.tickCount, 1);
    EXPECT_EQ(comp1.tickCount, 0) << "Unregistered tickable must not receive OnTick";
}

// ── Tick dispatch — basic ─────────────────────────────────────────────────────

/// Tick dispatches OnTick to a registered enabled tickable with the correct dt.
TEST_F(TickSystemTest, Tick_EnabledTickable_DispatchesOnTickWithCorrectDt) {
    TestTickable comp;
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(comp.tickCount, 1);
    EXPECT_TRUE(NearlyEqual(comp.lastDt, 0.016F))
        << "OnTick must receive the exact deltaTime passed to Tick()";
}

/// Multiple frames accumulate tickCount correctly.
TEST_F(TickSystemTest, Tick_CalledMultipleTimes_TickCountAccumulates) {
    TestTickable comp;
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Tick(TickGroup::Update, 0.016F, world_);
    tickSys_.Tick(TickGroup::Update, 0.016F, world_);
    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(comp.tickCount, 3);
}

/// Tick with wrong group does not dispatch to Update-registered component.
TEST_F(TickSystemTest, Tick_WrongGroup_DoesNotDispatch) {
    TestTickable comp; // default group = Update
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Tick(TickGroup::PrePhysics,  0.016F, world_);
    tickSys_.Tick(TickGroup::PostPhysics, 0.016F, world_);
    tickSys_.Tick(TickGroup::PostUpdate,  0.016F, world_);

    EXPECT_EQ(comp.tickCount, 0)
        << "Tickable in Update group must not fire for other groups";
}

/// All tickables in the same group receive OnTick in a single Tick() call.
TEST_F(TickSystemTest, Tick_MultipleTickablesInSameGroup_AllReceiveOnTick) {
    constexpr int kCount = 5;
    std::vector<TestTickable>           comps(kCount);
    std::vector<Entity>                 entities;

    for (int i = 0; i < kCount; ++i) {
        entities.push_back(world_.CreateEntity());
        tickSys_.RegisterTickable(entities[i], &comps[i], world_);
    }

    tickSys_.Tick(TickGroup::Update, 0.02F, world_);

    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(comps[i].tickCount, 1) << "Tickable[" << i << "] must receive OnTick";
        EXPECT_TRUE(NearlyEqual(comps[i].lastDt, 0.02F))
            << "Tickable[" << i << "] must receive correct dt";
    }
}

// ── Tick dispatch — disabled tickables ───────────────────────────────────────

/// Disabled tickable (SetTickEnabled(false)) is skipped by Tick().
TEST_F(TickSystemTest, Tick_DisabledTickable_IsSkipped) {
    TestTickable comp;
    comp.SetTickEnabled(false);

    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(comp.tickCount, 0)
        << "Disabled tickable must not receive OnTick";
}

/// Re-enabling a tickable after disabling resumes dispatch.
TEST_F(TickSystemTest, Tick_ReEnabledTickable_ResumesDispatch) {
    TestTickable comp;
    comp.SetTickEnabled(false);

    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Tick(TickGroup::Update, 0.016F, world_);
    ASSERT_EQ(comp.tickCount, 0);

    comp.SetTickEnabled(true);
    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(comp.tickCount, 1)
        << "Re-enabled tickable must receive OnTick on next Tick()";
}

// ── Tick dispatch — destroyed entities ───────────────────────────────────────

/// Tick skips components whose entity has been destroyed.
TEST_F(TickSystemTest, Tick_DestroyedEntity_IsSkipped) {
    TestTickable comp;
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    world_.DestroyEntity(e);

    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(comp.tickCount, 0)
        << "Tickable owned by a destroyed entity must not receive OnTick";
}

/// Tick dispatches to valid entities even when a destroyed entity is in the same group.
TEST_F(TickSystemTest, Tick_MixedValidAndDestroyedEntities_OnlyValidReceiveTick) {
    TestTickable compA, compB;
    Entity eA = world_.CreateEntity();
    Entity eB = world_.CreateEntity();

    tickSys_.RegisterTickable(eA, &compA, world_);
    tickSys_.RegisterTickable(eB, &compB, world_);

    world_.DestroyEntity(eA); // destroy eA but leave eB alive

    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(compA.tickCount, 0) << "Destroyed entity tickable must be skipped";
    EXPECT_EQ(compB.tickCount, 1) << "Valid entity tickable must receive OnTick";
}

// ── Interval throttling ───────────────────────────────────────────────────────

/// Interval=0.5, three Tick(0.1) calls → no OnTick fires (< 0.5 accumulated).
TEST_F(TickSystemTest, Tick_IntervalThrottling_NoFireBeforeIntervalElapsed) {
    TestTickable comp;
    comp.SetTickInterval(0.5F);

    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Tick(TickGroup::Update, 0.1F, world_);
    tickSys_.Tick(TickGroup::Update, 0.1F, world_);
    tickSys_.Tick(TickGroup::Update, 0.1F, world_);

    EXPECT_EQ(comp.tickCount, 0)
        << "OnTick must not fire when accumulated time (0.3s) is below interval (0.5s)";
}

/// Interval=0.5, five Tick(0.1) calls → OnTick fires exactly once at 0.5s accumulated.
TEST_F(TickSystemTest, Tick_IntervalThrottling_FiresAtExactInterval) {
    TestTickable comp;
    comp.SetTickInterval(0.5F);

    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    // 4 × 0.1 = 0.4 — below threshold
    for (int i = 0; i < 4; ++i) {
        tickSys_.Tick(TickGroup::Update, 0.1F, world_);
    }
    EXPECT_EQ(comp.tickCount, 0);

    // 5th × 0.1 = 0.5 — exactly at threshold, must fire
    tickSys_.Tick(TickGroup::Update, 0.1F, world_);
    EXPECT_EQ(comp.tickCount, 1)
        << "OnTick must fire exactly once when accumulated time reaches interval";
}

/// Interval=0.5, single Tick(0.6) → OnTick fires with elapsed=0.6, overshoot=0.1 preserved.
TEST_F(TickSystemTest, Tick_IntervalOvershoot_PreservesRemainder) {
    TestTickable comp;
    comp.SetTickInterval(0.5F);

    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    // Single large step overshoots the interval.
    tickSys_.Tick(TickGroup::Update, 0.6F, world_);

    ASSERT_EQ(comp.tickCount, 1)
        << "OnTick must fire once when dt overshoots the interval";
    EXPECT_TRUE(NearlyEqual(comp.lastDt, 0.6F))
        << "OnTick must receive the actual elapsed time (0.6), not the interval";

    // Next Tick(0.1) should see 0.1 (overshoot) + 0.1 (new) = 0.2 — below 0.5, no fire.
    comp.tickCount = 0;
    tickSys_.Tick(TickGroup::Update, 0.1F, world_);
    EXPECT_EQ(comp.tickCount, 0)
        << "Overshoot (0.1) + new dt (0.1) = 0.2 is still below interval 0.5";

    // Tick(0.4) → accumulated = 0.2 + 0.4 = 0.6 ≥ 0.5 — fire again.
    tickSys_.Tick(TickGroup::Update, 0.4F, world_);
    EXPECT_EQ(comp.tickCount, 1)
        << "Second fire expected when accumulator exceeds interval again";
}

/// Interval=0.5, after firing the accumulator is reduced by the interval (not zeroed).
TEST_F(TickSystemTest, Tick_IntervalAccumulator_ReducedByIntervalOnFire) {
    TestTickable comp;
    comp.SetTickInterval(0.5F);

    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    // Fire with an overshoot of 0.2.
    tickSys_.Tick(TickGroup::Update, 0.7F, world_);
    ASSERT_EQ(comp.tickCount, 1);

    // timeSinceLastTick should now be 0.2 (0.7 - 0.5).
    // The GetTimeSinceLastTick accessor reflects this (managed by TickSystem friend).
    EXPECT_TRUE(NearlyEqual(comp.GetTimeSinceLastTick(), 0.2F))
        << "Accumulator must hold the overshoot remainder (0.7 - 0.5 = 0.2)";
}

/// Zero interval (default) bypasses throttling — OnTick fires every frame.
TEST_F(TickSystemTest, Tick_ZeroInterval_FiresEveryFrame) {
    TestTickable comp; // interval = 0 by default
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    constexpr int kFrames = 5;
    for (int i = 0; i < kFrames; ++i) {
        tickSys_.Tick(TickGroup::Update, 0.016F, world_);
    }

    EXPECT_EQ(comp.tickCount, kFrames)
        << "With interval=0, OnTick must fire on every frame";
}

// ── Clear ─────────────────────────────────────────────────────────────────────

/// Clear removes all tickables from all groups.
TEST_F(TickSystemTest, Clear_RemovesAllTickables) {
    TestTickable       comp1;
    PrePhysicsTickable comp2;

    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    tickSys_.RegisterTickable(e1, &comp1, world_);
    tickSys_.RegisterTickable(e2, &comp2, world_);
    ASSERT_EQ(tickSys_.GetTickableCount(), 2u);

    tickSys_.Clear();

    EXPECT_EQ(tickSys_.GetTickableCount(), 0u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::Update),     0u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PrePhysics), 0u);
}

/// After Clear, Tick does not dispatch to previously registered tickables.
TEST_F(TickSystemTest, Clear_ThenTick_NoDispatch) {
    TestTickable comp;
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    tickSys_.Clear();
    tickSys_.Tick(TickGroup::Update, 0.016F, world_);

    EXPECT_EQ(comp.tickCount, 0)
        << "OnTick must not fire after Clear()";
}

/// Clear on empty TickSystem is a no-op (no crash).
TEST_F(TickSystemTest, Clear_OnEmptySystem_IsNoOp) {
    EXPECT_NO_FATAL_FAILURE(tickSys_.Clear());
    EXPECT_EQ(tickSys_.GetTickableCount(), 0u);
}

// ── GetTickableCount ──────────────────────────────────────────────────────────

/// GetTickableCount(TickGroup::None) returns 0 (no storage for None group).
TEST_F(TickSystemTest, GetTickableCount_NoneGroup_ReturnsZero) {
    TestTickable comp;
    Entity e = world_.CreateEntity();
    tickSys_.RegisterTickable(e, &comp, world_);

    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::None), 0u)
        << "TickGroup::None has no storage bucket — count must always be 0";
}

/// GetTickableCount totals all four groups.
TEST_F(TickSystemTest, GetTickableCount_Total_SumsAllGroups) {
    TestTickable       c1;
    PrePhysicsTickable c2;
    PostPhysicsTickable c3;
    PostUpdateTickable c4;

    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    Entity e3 = world_.CreateEntity();
    Entity e4 = world_.CreateEntity();

    tickSys_.RegisterTickable(e1, &c1, world_);
    tickSys_.RegisterTickable(e2, &c2, world_);
    tickSys_.RegisterTickable(e3, &c3, world_);
    tickSys_.RegisterTickable(e4, &c4, world_);

    EXPECT_EQ(tickSys_.GetTickableCount(), 4u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::Update),      1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PrePhysics),  1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PostPhysics), 1u);
    EXPECT_EQ(tickSys_.GetTickableCount(TickGroup::PostUpdate),  1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TickSubsystem tests
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture: fresh TickSubsystem and World for each test.
class TickSubsystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        gDispatchLog.clear();
    }

    void TearDown() override {
        subsystem_.Shutdown();
        gDispatchLog.clear();
    }

    TickSubsystem subsystem_;
    World         world_;
};

// ── Metadata ──────────────────────────────────────────────────────────────────

/// GetName returns "Tick".
TEST_F(TickSubsystemTest, GetName_ReturnsTick) {
    EXPECT_STREQ(subsystem_.GetName(), "Tick");
}

/// GetInitPhase returns InitPhase::Start.
TEST_F(TickSubsystemTest, GetInitPhase_ReturnsStart) {
    EXPECT_EQ(subsystem_.GetInitPhase(), InitPhase::Start);
}

/// GetTickGroup returns TickGroup::Update.
TEST_F(TickSubsystemTest, GetTickGroup_ReturnsUpdate) {
    EXPECT_EQ(subsystem_.GetTickGroup(), TickGroup::Update);
}

/// GetDependencies returns a vector containing "World".
TEST_F(TickSubsystemTest, GetDependencies_ContainsWorld) {
    const auto deps = subsystem_.GetDependencies();
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_STREQ(deps[0], "World");
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

/// Init returns true.
TEST_F(TickSubsystemTest, Init_ReturnsTrue) {
    bool ok = subsystem_.Init();
    EXPECT_TRUE(ok);
}

/// GetTickSystem after construction returns a usable (empty) TickSystem.
TEST_F(TickSubsystemTest, GetTickSystem_AfterConstruction_CountIsZero) {
    EXPECT_EQ(subsystem_.GetTickSystem().GetTickableCount(), 0u);
}

/// Shutdown clears all registered tickables.
TEST_F(TickSubsystemTest, Shutdown_ClearsAllTickables) {
    subsystem_.Init();
    subsystem_.SetWorld(&world_);

    TestTickable comp;
    Entity e = world_.CreateEntity();
    subsystem_.GetTickSystem().RegisterTickable(e, &comp, world_);
    ASSERT_EQ(subsystem_.GetTickSystem().GetTickableCount(), 1u);

    subsystem_.Shutdown();

    EXPECT_EQ(subsystem_.GetTickSystem().GetTickableCount(), 0u)
        << "Shutdown must clear all registered tickables";
}

/// Shutdown before Init is a no-op (no crash).
TEST_F(TickSubsystemTest, Shutdown_BeforeInit_IsNoOp) {
    EXPECT_NO_FATAL_FAILURE(subsystem_.Shutdown());
    EXPECT_EQ(subsystem_.GetTickSystem().GetTickableCount(), 0u);
}

// ── Null world guard ──────────────────────────────────────────────────────────

/// Tick with null world pointer is a no-op — no crash, no dispatch.
TEST_F(TickSubsystemTest, Tick_NullWorld_IsNoOp) {
    subsystem_.Init();
    // SetWorld never called → m_world == nullptr.

    // Register a tickable via the internal TickSystem directly (no world needed for registration test).
    // Since we cannot register without a world, we just confirm Tick does not crash.
    EXPECT_NO_FATAL_FAILURE(subsystem_.Tick(0.016F))
        << "Tick with null world must be a no-op and not crash";
}

/// After SetWorld(nullptr), Tick is again a no-op.
TEST_F(TickSubsystemTest, Tick_AfterSetWorldNull_IsNoOp) {
    subsystem_.Init();
    subsystem_.SetWorld(&world_);
    subsystem_.SetWorld(nullptr); // explicitly clear

    EXPECT_NO_FATAL_FAILURE(subsystem_.Tick(0.016F));
}

// ── Group dispatch order ──────────────────────────────────────────────────────

/// Tick dispatches all four groups in order: PrePhysics→PostPhysics→Update→PostUpdate.
TEST_F(TickSubsystemTest, Tick_DispatchesAllGroupsInCorrectOrder) {
    subsystem_.Init();
    subsystem_.SetWorld(&world_);

    LoggingPrePhysicsTickable  preComp;
    LoggingPostPhysicsTickable postPhysComp;
    LoggingUpdateTickable      updateComp;
    LoggingPostUpdateTickable  postUpdateComp;

    Entity ePre       = world_.CreateEntity();
    Entity ePostPhys  = world_.CreateEntity();
    Entity eUpdate    = world_.CreateEntity();
    Entity ePostUpd   = world_.CreateEntity();

    auto& ts = subsystem_.GetTickSystem();
    ts.RegisterTickable(ePre,      &preComp,       world_);
    ts.RegisterTickable(ePostPhys, &postPhysComp,  world_);
    ts.RegisterTickable(eUpdate,   &updateComp,    world_);
    ts.RegisterTickable(ePostUpd,  &postUpdateComp, world_);

    subsystem_.Tick(0.016F);

    ASSERT_EQ(gDispatchLog.size(), 4u)
        << "All four groups must dispatch exactly one tick each";

    EXPECT_EQ(gDispatchLog[0], TickGroup::PrePhysics)
        << "First dispatched group must be PrePhysics";
    EXPECT_EQ(gDispatchLog[1], TickGroup::PostPhysics)
        << "Second dispatched group must be PostPhysics";
    EXPECT_EQ(gDispatchLog[2], TickGroup::Update)
        << "Third dispatched group must be Update";
    EXPECT_EQ(gDispatchLog[3], TickGroup::PostUpdate)
        << "Fourth dispatched group must be PostUpdate";
}

/// Multiple Tick calls in successive frames maintain correct group order each time.
TEST_F(TickSubsystemTest, Tick_CalledTwice_OrderCorrectBothTimes) {
    subsystem_.Init();
    subsystem_.SetWorld(&world_);

    LoggingPrePhysicsTickable  preComp;
    LoggingUpdateTickable      updateComp;

    Entity ePre    = world_.CreateEntity();
    Entity eUpdate = world_.CreateEntity();

    auto& ts = subsystem_.GetTickSystem();
    ts.RegisterTickable(ePre,    &preComp,    world_);
    ts.RegisterTickable(eUpdate, &updateComp, world_);

    subsystem_.Tick(0.016F);
    subsystem_.Tick(0.016F);

    // 2 ticks × 2 groups = 4 log entries in alternating PrePhysics, Update order.
    ASSERT_EQ(gDispatchLog.size(), 4u);
    EXPECT_EQ(gDispatchLog[0], TickGroup::PrePhysics);
    EXPECT_EQ(gDispatchLog[1], TickGroup::Update);
    EXPECT_EQ(gDispatchLog[2], TickGroup::PrePhysics);
    EXPECT_EQ(gDispatchLog[3], TickGroup::Update);
}

// ── GetTickSystem accessor ────────────────────────────────────────────────────

/// GetTickSystem const overload returns a const reference accessible from const subsystem.
TEST_F(TickSubsystemTest, GetTickSystem_ConstOverload_IsAccessible) {
    subsystem_.Init();
    subsystem_.SetWorld(&world_);

    TestTickable comp;
    Entity e = world_.CreateEntity();
    subsystem_.GetTickSystem().RegisterTickable(e, &comp, world_);

    const TickSubsystem& constSub = subsystem_;
    EXPECT_EQ(constSub.GetTickSystem().GetTickableCount(), 1u);
}

/// SetWorld and GetTickSystem integration: registered tickable fires via Tick.
TEST_F(TickSubsystemTest, SetWorld_Then_Tick_DispatchesToRegisteredTickable) {
    subsystem_.Init();
    subsystem_.SetWorld(&world_);

    TestTickable comp;
    Entity e = world_.CreateEntity();
    subsystem_.GetTickSystem().RegisterTickable(e, &comp, world_);

    subsystem_.Tick(0.033F);

    EXPECT_EQ(comp.tickCount, 1)
        << "Tickable registered via GetTickSystem must receive OnTick on Tick()";
    EXPECT_TRUE(NearlyEqual(comp.lastDt, 0.033F));
}

// ─────────────────────────────────────────────────────────────────────────────
// UZ_BEHAVIOR macro tests
// ─────────────────────────────────────────────────────────────────────────────

/// Test behavior class registered via UZ_BEHAVIOR macro.
/// Uses a simple float field to verify property registration and tick dispatch.
class TestRegisteredBehavior : public uz::TickableComponent {
    UZ_BEHAVIOR(TestRegisteredBehavior);

public:
    float accumulator = 0.F;

    void OnTick(float dt) override { accumulator += dt; }

    // Properties registered manually to avoid offsetof on non-standard-layout type.
    static void RegisterProperties(uz::ClassInfo& info) {
        info.AddProperty<float>("accumulator", 0, "Accumulated time");
    }
};

/// Non-behavior component registered via UZ_COMPONENT for contrast tests.
class TestNonBehaviorComponent {
    UZ_COMPONENT(TestNonBehaviorComponent);

    int value = 42;

    UZ_PROPERTIES_BEGIN(TestNonBehaviorComponent)
        UZ_PROP(int, value, "Test value")
    UZ_PROPERTIES_END()
};

/// UZ_BEHAVIOR registers the class in ClassRegistry.
TEST(BehaviorRegistration, UZ_BEHAVIOR_RegistersInClassRegistry) {
    const auto& reg = uz::ClassRegistry::Get();
    EXPECT_TRUE(reg.IsRegistered("TestRegisteredBehavior"))
        << "UZ_BEHAVIOR must auto-register the class in ClassRegistry";
}

/// UZ_BEHAVIOR sets is_behavior to true on the ClassInfo.
TEST(BehaviorRegistration, UZ_BEHAVIOR_IsBehaviorFlagIsTrue) {
    const auto& info = TestRegisteredBehavior::GetStaticClassInfo();
    EXPECT_TRUE(info.is_behavior)
        << "UZ_BEHAVIOR must set is_behavior = true";
}

/// UZ_BEHAVIOR factory creates a valid instance via placement new.
TEST(BehaviorRegistration, UZ_BEHAVIOR_FactoryCreatesInstance) {
    const auto& info = TestRegisteredBehavior::GetStaticClassInfo();
    ASSERT_NE(info.factory, nullptr)
        << "UZ_BEHAVIOR must register a placement-new factory";

    alignas(TestRegisteredBehavior) uint8_t buffer[sizeof(TestRegisteredBehavior)];
    auto* instance = static_cast<TestRegisteredBehavior*>(info.factory(buffer));
    ASSERT_NE(instance, nullptr);

    // Verify it behaves as a TickableComponent — call OnTick.
    instance->OnTick(0.5F);

    // Clean up placement-new object.
    instance->~TestRegisteredBehavior();
}

/// UZ_BEHAVIOR registers properties declared via UZ_PROPERTIES_BEGIN/END.
TEST(BehaviorRegistration, UZ_BEHAVIOR_PropertiesAreRegistered) {
    const auto& info = TestRegisteredBehavior::GetStaticClassInfo();
    EXPECT_EQ(info.PropertyCount(), 1u)
        << "TestRegisteredBehavior has one UZ_PROP (accumulator)";

    const auto* prop = info.FindProperty("accumulator");
    ASSERT_NE(prop, nullptr);
    EXPECT_EQ(prop->type, uz::PropertyType::Float);
    EXPECT_STREQ(prop->description, "Accumulated time");
}

/// ForEachBehavior includes classes registered with UZ_BEHAVIOR.
TEST(BehaviorRegistration, ForEachBehavior_IncludesRegisteredBehavior) {
    const auto& reg = uz::ClassRegistry::Get();
    bool found = false;
    reg.ForEachBehavior([&](std::string_view name, const uz::ClassInfo&) {
        if (name == "TestRegisteredBehavior") {
            found = true;
        }
    });
    EXPECT_TRUE(found)
        << "ForEachBehavior must iterate over UZ_BEHAVIOR-registered classes";
}

/// ForEachBehavior excludes classes registered with UZ_COMPONENT (is_behavior = false).
TEST(BehaviorRegistration, ForEachBehavior_ExcludesNonBehaviorComponents) {
    const auto& reg = uz::ClassRegistry::Get();

    // Verify the non-behavior component is registered but NOT a behavior.
    ASSERT_TRUE(reg.IsRegistered("TestNonBehaviorComponent"));
    EXPECT_FALSE(TestNonBehaviorComponent::GetStaticClassInfo().is_behavior);

    bool foundNonBehavior = false;
    reg.ForEachBehavior([&](std::string_view name, const uz::ClassInfo&) {
        if (name == "TestNonBehaviorComponent") {
            foundNonBehavior = true;
        }
    });
    EXPECT_FALSE(foundNonBehavior)
        << "ForEachBehavior must NOT include UZ_COMPONENT-registered classes";
}
