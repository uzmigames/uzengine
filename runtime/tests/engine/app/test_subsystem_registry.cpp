/// Unit tests for SubsystemRegistry — dependency ordering, circular detection,
/// reverse shutdown, init-failure rollback, phase grouping, tick dispatch,
/// Find(), idempotent shutdown, and empty-registry edge cases.

#include "subsystem_registry.h"

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// MockSubsystem — configurable test double
// ─────────────────────────────────────────────────────────────────────────────

/// Global monotonically-increasing call counter shared across all instances.
/// Reset to 0 in each test's SetUp via MockSubsystem::ResetOrder().
static int g_callOrder = 0;

class MockSubsystem : public ISubsystem {
public:
    explicit MockSubsystem(const char*  name,
                           InitPhase    phase     = InitPhase::PreInit,
                           TickGroup    tickGroup = TickGroup::None,
                           bool         succeed   = true)
        : name_(name), phase_(phase), tickGroup_(tickGroup), shouldSucceed_(succeed) {}

    [[nodiscard]] const char*              GetName()         const override { return name_; }
    [[nodiscard]] InitPhase                GetInitPhase()    const override { return phase_; }
    [[nodiscard]] TickGroup                GetTickGroup()    const override { return tickGroup_; }
    [[nodiscard]] std::vector<const char*> GetDependencies() const override { return deps_; }

    [[nodiscard]] bool Init() override {
        initOrder_ = g_callOrder++;
        initialized_ = true;
        return shouldSucceed_;
    }

    void Shutdown() override {
        shutdownOrder_ = g_callOrder++;
        shutDown_ = true;
    }

    void Tick(float dt) override {
        ++tickCount_;
        lastDt_ = dt;
    }

    // ── Test helpers ────────────────────────────────────────────────────────

    bool     Initialized()    const { return initialized_; }
    bool     ShutDown()       const { return shutDown_; }
    int      InitOrder()      const { return initOrder_; }
    int      ShutdownOrder()  const { return shutdownOrder_; }
    int      TickCount()      const { return tickCount_; }
    float    LastDt()         const { return lastDt_; }

    void AddDependency(const char* dep) { deps_.push_back(dep); }

    static void ResetOrder() { g_callOrder = 0; }

private:
    const char*              name_;
    InitPhase                phase_;
    TickGroup                tickGroup_;
    bool                     shouldSucceed_;
    std::vector<const char*> deps_;

    bool  initialized_   = false;
    bool  shutDown_      = false;
    int   initOrder_     = -1;
    int   shutdownOrder_ = -1;
    int   tickCount_     = 0;
    float lastDt_        = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class SubsystemRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        MockSubsystem::ResetOrder();
    }

    SubsystemRegistry registry_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Empty registry
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, EmptyRegistry_InitAll_ReturnsTrue) {
    EXPECT_TRUE(registry_.InitAll());
}

TEST_F(SubsystemRegistryTest, EmptyRegistry_GetCount_IsZero) {
    EXPECT_EQ(registry_.GetCount(), 0u);
}

TEST_F(SubsystemRegistryTest, EmptyRegistry_ShutdownAll_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(registry_.ShutdownAll());
}

TEST_F(SubsystemRegistryTest, EmptyRegistry_Find_ReturnsNullptr) {
    EXPECT_EQ(registry_.Find("Missing"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, Register_SingleSubsystem_CountIsOne) {
    registry_.Register(std::make_unique<MockSubsystem>("A"));
    EXPECT_EQ(registry_.GetCount(), 1u);
}

TEST_F(SubsystemRegistryTest, Register_MultipleSubsystems_CountIsCorrect) {
    registry_.Register(std::make_unique<MockSubsystem>("A"));
    registry_.Register(std::make_unique<MockSubsystem>("B"));
    registry_.Register(std::make_unique<MockSubsystem>("C"));
    EXPECT_EQ(registry_.GetCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dependency ordering: B depends on A → A must init before B
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, DependencyOrdering_BDependsOnA_AInitsFirst) {
    auto* a = new MockSubsystem("A");
    auto* b = new MockSubsystem("B");
    b->AddDependency("A");

    registry_.Register(std::unique_ptr<ISubsystem>(b)); // B registered first
    registry_.Register(std::unique_ptr<ISubsystem>(a));

    ASSERT_TRUE(registry_.InitAll());
    EXPECT_LT(a->InitOrder(), b->InitOrder())
        << "A must initialize before B (B depends on A)";
}

TEST_F(SubsystemRegistryTest, DependencyOrdering_ChainABC_InitsInOrder) {
    auto* a = new MockSubsystem("A");
    auto* b = new MockSubsystem("B");
    auto* c = new MockSubsystem("C");
    b->AddDependency("A");
    c->AddDependency("B");

    registry_.Register(std::unique_ptr<ISubsystem>(c));
    registry_.Register(std::unique_ptr<ISubsystem>(b));
    registry_.Register(std::unique_ptr<ISubsystem>(a));

    ASSERT_TRUE(registry_.InitAll());
    EXPECT_LT(a->InitOrder(), b->InitOrder());
    EXPECT_LT(b->InitOrder(), c->InitOrder());
}

// ─────────────────────────────────────────────────────────────────────────────
// Circular dependency detection
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, CircularDependency_DirectCycle_ReturnsFalse) {
    auto* a = new MockSubsystem("A");
    auto* b = new MockSubsystem("B");
    a->AddDependency("B");
    b->AddDependency("A");

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    registry_.Register(std::unique_ptr<ISubsystem>(b));

    EXPECT_FALSE(registry_.InitAll());
}

TEST_F(SubsystemRegistryTest, CircularDependency_IndirectCycle_ReturnsFalse) {
    auto* a = new MockSubsystem("A");
    auto* b = new MockSubsystem("B");
    auto* c = new MockSubsystem("C");
    a->AddDependency("C");
    b->AddDependency("A");
    c->AddDependency("B"); // A→C→B→A

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    registry_.Register(std::unique_ptr<ISubsystem>(b));
    registry_.Register(std::unique_ptr<ISubsystem>(c));

    EXPECT_FALSE(registry_.InitAll());
}

TEST_F(SubsystemRegistryTest, CircularDependency_NoSubsystemIsInitialized) {
    auto* a = new MockSubsystem("A");
    auto* b = new MockSubsystem("B");
    a->AddDependency("B");
    b->AddDependency("A");

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    registry_.Register(std::unique_ptr<ISubsystem>(b));

    registry_.InitAll();

    EXPECT_FALSE(a->Initialized());
    EXPECT_FALSE(b->Initialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Missing dependency
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, MissingDependency_ReturnsFalse) {
    auto* a = new MockSubsystem("A");
    a->AddDependency("NonExistent");

    registry_.Register(std::unique_ptr<ISubsystem>(a));

    EXPECT_FALSE(registry_.InitAll());
}

TEST_F(SubsystemRegistryTest, MissingDependency_SubsystemNotInitialized) {
    auto* a = new MockSubsystem("A");
    a->AddDependency("Ghost");

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    registry_.InitAll();

    EXPECT_FALSE(a->Initialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Reverse shutdown: A, B, C inited → C, B, A shut down
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, ShutdownAll_ReverseInitOrder_ABCInitsCABShuts) {
    auto* a = new MockSubsystem("A");
    auto* b = new MockSubsystem("B");
    auto* c = new MockSubsystem("C");
    b->AddDependency("A");
    c->AddDependency("B");

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    registry_.Register(std::unique_ptr<ISubsystem>(b));
    registry_.Register(std::unique_ptr<ISubsystem>(c));

    ASSERT_TRUE(registry_.InitAll());
    registry_.ShutdownAll();

    // All must have shut down
    EXPECT_TRUE(a->ShutDown());
    EXPECT_TRUE(b->ShutDown());
    EXPECT_TRUE(c->ShutDown());

    // Shutdown order must be reverse of init order
    EXPECT_GT(a->ShutdownOrder(), b->ShutdownOrder())
        << "A shuts down after B (B was inited after A)";
    EXPECT_GT(b->ShutdownOrder(), c->ShutdownOrder())
        << "B shuts down after C (C was inited after B)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Init failure rollback: C fails → B and A get Shutdown called
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, InitFailure_PriorSubsystemsAreRolledBack) {
    auto* a = new MockSubsystem("A", InitPhase::PreInit, TickGroup::None, true);
    auto* b = new MockSubsystem("B", InitPhase::PreInit, TickGroup::None, true);
    auto* c = new MockSubsystem("C", InitPhase::PreInit, TickGroup::None, false); // fails

    b->AddDependency("A");
    c->AddDependency("B");

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    registry_.Register(std::unique_ptr<ISubsystem>(b));
    registry_.Register(std::unique_ptr<ISubsystem>(c));

    EXPECT_FALSE(registry_.InitAll());

    // A and B were initialized before C failed — they must be shut down
    EXPECT_TRUE(a->ShutDown()) << "A must be rolled back after C fails";
    EXPECT_TRUE(b->ShutDown()) << "B must be rolled back after C fails";
    EXPECT_FALSE(c->ShutDown()) << "C never completed Init, should not be shut down in rollback";
}

TEST_F(SubsystemRegistryTest, InitFailure_InitOrderIsCleared) {
    auto* a = new MockSubsystem("A", InitPhase::PreInit, TickGroup::None, false);

    registry_.Register(std::unique_ptr<ISubsystem>(a));
    EXPECT_FALSE(registry_.InitAll());

    EXPECT_TRUE(registry_.GetInitOrder().empty())
        << "initOrder_ must be cleared after a failed init";
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase ordering: PreInit before Init before Start
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, PhaseOrdering_PreInitBeforeInit) {
    auto* pre  = new MockSubsystem("Pre",  InitPhase::PreInit);
    auto* init = new MockSubsystem("Init", InitPhase::Init);

    registry_.Register(std::unique_ptr<ISubsystem>(init)); // registered later
    registry_.Register(std::unique_ptr<ISubsystem>(pre));

    ASSERT_TRUE(registry_.InitAll());
    EXPECT_LT(pre->InitOrder(), init->InitOrder())
        << "PreInit subsystem must init before Init subsystem";
}

TEST_F(SubsystemRegistryTest, PhaseOrdering_InitBeforeStart) {
    auto* init  = new MockSubsystem("Init",  InitPhase::Init);
    auto* start = new MockSubsystem("Start", InitPhase::Start);

    registry_.Register(std::unique_ptr<ISubsystem>(start));
    registry_.Register(std::unique_ptr<ISubsystem>(init));

    ASSERT_TRUE(registry_.InitAll());
    EXPECT_LT(init->InitOrder(), start->InitOrder());
}

TEST_F(SubsystemRegistryTest, PhaseOrdering_AllThreePhases_CorrectOrder) {
    auto* pre   = new MockSubsystem("Pre",   InitPhase::PreInit);
    auto* init  = new MockSubsystem("Init",  InitPhase::Init);
    auto* start = new MockSubsystem("Start", InitPhase::Start);

    registry_.Register(std::unique_ptr<ISubsystem>(start));
    registry_.Register(std::unique_ptr<ISubsystem>(init));
    registry_.Register(std::unique_ptr<ISubsystem>(pre));

    ASSERT_TRUE(registry_.InitAll());
    EXPECT_LT(pre->InitOrder(),  init->InitOrder());
    EXPECT_LT(init->InitOrder(), start->InitOrder());
}

// ─────────────────────────────────────────────────────────────────────────────
// RunTickGroup — only ticks the matching group
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, RunTickGroup_OnlyTicksMatchingGroup) {
    auto* update     = new MockSubsystem("Update",     InitPhase::PreInit, TickGroup::Update);
    auto* postUpdate = new MockSubsystem("PostUpdate", InitPhase::PreInit, TickGroup::PostUpdate);
    auto* none       = new MockSubsystem("None",       InitPhase::PreInit, TickGroup::None);

    registry_.Register(std::unique_ptr<ISubsystem>(update));
    registry_.Register(std::unique_ptr<ISubsystem>(postUpdate));
    registry_.Register(std::unique_ptr<ISubsystem>(none));

    ASSERT_TRUE(registry_.InitAll());
    registry_.RunTickGroup(TickGroup::Update, 0.016f);

    EXPECT_EQ(update->TickCount(),     1) << "Update subsystem must be ticked";
    EXPECT_EQ(postUpdate->TickCount(), 0) << "PostUpdate must NOT be ticked when group=Update";
    EXPECT_EQ(none->TickCount(),       0) << "None group must never be ticked";
}

TEST_F(SubsystemRegistryTest, RunTickGroup_PassesDeltaTimeCorrectly) {
    auto* sub = new MockSubsystem("S", InitPhase::PreInit, TickGroup::Update);

    registry_.Register(std::unique_ptr<ISubsystem>(sub));
    ASSERT_TRUE(registry_.InitAll());

    registry_.RunTickGroup(TickGroup::Update, 0.033f);

    EXPECT_FLOAT_EQ(sub->LastDt(), 0.033f);
}

TEST_F(SubsystemRegistryTest, RunTickGroup_PrePhysicsGroup_OnlyTicksPrePhysics) {
    auto* pre  = new MockSubsystem("PrePhys", InitPhase::PreInit, TickGroup::PrePhysics);
    auto* post = new MockSubsystem("PostPhys", InitPhase::PreInit, TickGroup::PostPhysics);

    registry_.Register(std::unique_ptr<ISubsystem>(pre));
    registry_.Register(std::unique_ptr<ISubsystem>(post));

    ASSERT_TRUE(registry_.InitAll());
    registry_.RunTickGroup(TickGroup::PrePhysics, 0.016f);

    EXPECT_EQ(pre->TickCount(),  1);
    EXPECT_EQ(post->TickCount(), 0);
}

TEST_F(SubsystemRegistryTest, RunTickGroup_MultipleCalls_AccumulatesTickCount) {
    auto* sub = new MockSubsystem("S", InitPhase::PreInit, TickGroup::Update);

    registry_.Register(std::unique_ptr<ISubsystem>(sub));
    ASSERT_TRUE(registry_.InitAll());

    registry_.RunTickGroup(TickGroup::Update, 0.016f);
    registry_.RunTickGroup(TickGroup::Update, 0.016f);
    registry_.RunTickGroup(TickGroup::Update, 0.016f);

    EXPECT_EQ(sub->TickCount(), 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Find
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, Find_RegisteredName_ReturnsSubsystem) {
    registry_.Register(std::make_unique<MockSubsystem>("Target"));

    ISubsystem* found = registry_.Find("Target");

    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->GetName(), "Target");
}

TEST_F(SubsystemRegistryTest, Find_UnknownName_ReturnsNullptr) {
    registry_.Register(std::make_unique<MockSubsystem>("A"));

    EXPECT_EQ(registry_.Find("NotRegistered"), nullptr);
}

TEST_F(SubsystemRegistryTest, Find_EmptyRegistry_ReturnsNullptr) {
    EXPECT_EQ(registry_.Find("Anything"), nullptr);
}

TEST_F(SubsystemRegistryTest, Find_WorksBeforeAndAfterInit) {
    registry_.Register(std::make_unique<MockSubsystem>("X"));

    EXPECT_NE(registry_.Find("X"), nullptr) << "Find must work before InitAll";

    ASSERT_TRUE(registry_.InitAll());

    EXPECT_NE(registry_.Find("X"), nullptr) << "Find must work after InitAll";
}

// ─────────────────────────────────────────────────────────────────────────────
// Idempotent shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, ShutdownAll_CalledTwice_DoesNotCrash) {
    auto* a = new MockSubsystem("A");
    registry_.Register(std::unique_ptr<ISubsystem>(a));

    ASSERT_TRUE(registry_.InitAll());

    registry_.ShutdownAll();
    EXPECT_NO_FATAL_FAILURE(registry_.ShutdownAll()) << "Second ShutdownAll must be a no-op";
}

TEST_F(SubsystemRegistryTest, ShutdownAll_CalledTwice_SubsystemShutdownOnlyOnce) {
    auto* a = new MockSubsystem("A");
    registry_.Register(std::unique_ptr<ISubsystem>(a));

    ASSERT_TRUE(registry_.InitAll());

    registry_.ShutdownAll();
    const int shutdownAfterFirst = a->ShutdownOrder();
    EXPECT_GE(shutdownAfterFirst, 0) << "Shutdown must have been called at least once";

    // The second call must be a no-op — shutdownOrder_ must not change.
    MockSubsystem::ResetOrder();
    registry_.ShutdownAll(); // second call — must be no-op

    EXPECT_EQ(a->ShutdownOrder(), shutdownAfterFirst)
        << "shutdownOrder_ must not change on second ShutdownAll (no-op)";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetInitOrder — populated after InitAll
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, GetInitOrder_BeforeInit_IsEmpty) {
    registry_.Register(std::make_unique<MockSubsystem>("A"));
    EXPECT_TRUE(registry_.GetInitOrder().empty());
}

TEST_F(SubsystemRegistryTest, GetInitOrder_AfterSuccessfulInit_HasAllSubsystems) {
    registry_.Register(std::make_unique<MockSubsystem>("A"));
    registry_.Register(std::make_unique<MockSubsystem>("B"));
    registry_.Register(std::make_unique<MockSubsystem>("C"));

    ASSERT_TRUE(registry_.InitAll());

    EXPECT_EQ(registry_.GetInitOrder().size(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Duplicate name — asserts/crashes
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, Register_DuplicateName_Asserts) {
    registry_.Register(std::make_unique<MockSubsystem>("A"));
    EXPECT_DEATH(registry_.Register(std::make_unique<MockSubsystem>("A")),
                 ".*") // UZ_CHECK fires an assertion
        << "Registering a duplicate subsystem name must assert";
}

// ─────────────────────────────────────────────────────────────────────────────
// Single subsystem — happy path end-to-end
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SubsystemRegistryTest, SingleSubsystem_FullLifecycle_InitAndShutdown) {
    auto* a = new MockSubsystem("A");
    registry_.Register(std::unique_ptr<ISubsystem>(a));

    ASSERT_TRUE(registry_.InitAll());
    EXPECT_TRUE(a->Initialized());
    EXPECT_FALSE(a->ShutDown());

    registry_.ShutdownAll();
    EXPECT_TRUE(a->ShutDown());
}
