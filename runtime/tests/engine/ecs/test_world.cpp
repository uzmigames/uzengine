/// Unit tests for uz::World and uz::WorldSubsystem — ECS container with
/// generational handles, component lifecycle, view queries, and subsystem
/// integration. No GPU, RHI, or audio hardware required.
///
/// Coverage targets (Phase 3.0b task 2.7):
///   - world.cpp              → 100%
///   - world.h                → 100%  (template impls inlined in header)
///   - entity.h               → 100%
///   - world_subsystem.cpp    → 100%
///
/// Test cases follow the spec in .rulebook/tasks/phase3b-3.0b-world-entity/tasks.md

#include "world.h"
#include "world_subsystem.h"

#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>
#include <chrono>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Test components — plain data structs, no virtual functions
// ─────────────────────────────────────────────────────────────────────────────

/// 3D position component.
struct Position { float x = 0.0f, y = 0.0f, z = 0.0f; };

/// 3D velocity component.
struct Velocity { float dx = 0.0f, dy = 0.0f, dz = 0.0f; };

/// Hit-point component.
struct Health { int hp = 100; };

/// Marker component. EnTT returns void for truly empty structs via
/// emplace_or_replace, so we use a single-byte sentinel to keep it non-empty
/// while still testing the "tag" usage pattern.
struct Tag { uint8_t sentinel = 1; };

// ─────────────────────────────────────────────────────────────────────────────
// WorldTest fixture — fresh World per test, no shared state
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture that owns a fresh World for every test case.
/// SetUp/TearDown ensure no registry state leaks between tests.
class WorldTest : public ::testing::Test {
protected:
    World world_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 2.1 Entity CRUD — create, destroy, recreate
// ─────────────────────────────────────────────────────────────────────────────

/// Test 1 — CreateEntity returns a handle that IsValid() in the world.
TEST_F(WorldTest, CreateEntity_IsValid_AfterCreation) {
    Entity e = world_.CreateEntity();

    EXPECT_TRUE(e.IsValid())       << "Entity handle must have non-sentinel id";
    EXPECT_TRUE(world_.IsValid(e)) << "World must recognise newly created entity as alive";
    EXPECT_NE(e.id, UINT32_MAX)    << "id must not be the null sentinel UINT32_MAX";
    EXPECT_GT(e.generation, 0u)    << "generation must be at least 1 after first create";
}

/// Test 2 — CreateMultipleEntities: 10 entities are all valid and have unique IDs.
TEST_F(WorldTest, CreateMultipleEntities_AllValid_AllUniqueIds) {
    constexpr int kCount = 10;
    std::vector<Entity> entities;
    entities.reserve(kCount);

    for (int i = 0; i < kCount; ++i) {
        entities.push_back(world_.CreateEntity());
    }

    std::unordered_set<uint32_t> ids;
    for (const auto& e : entities) {
        EXPECT_TRUE(world_.IsValid(e)) << "Entity " << e.id << " must be alive";
        EXPECT_TRUE(ids.insert(e.id).second)
            << "Entity id=" << e.id << " is duplicated — ids must be unique";
    }
    EXPECT_EQ(ids.size(), static_cast<size_t>(kCount));
}

/// Test 3 — DestroyEntity: after destruction world.IsValid() returns false.
TEST_F(WorldTest, DestroyEntity_IsValid_ReturnsFalseAfterDestroy) {
    Entity e = world_.CreateEntity();
    ASSERT_TRUE(world_.IsValid(e));

    world_.DestroyEntity(e);

    EXPECT_FALSE(world_.IsValid(e))
        << "Destroyed entity must no longer be valid in the world";
}

/// Test 4 — RecreateEntity: a new entity may reuse the slot but must carry a
/// different generation, making the old handle stale.
TEST_F(WorldTest, RecreateEntity_NewGenerationMakesOldHandleStale) {
    Entity first = world_.CreateEntity();
    uint32_t firstGen = first.generation;

    world_.DestroyEntity(first);
    Entity second = world_.CreateEntity(); // EnTT likely reuses slot 0

    // The old handle must be stale regardless of slot reuse
    EXPECT_FALSE(world_.IsValid(first))
        << "Old handle must be stale after slot is recycled";
    EXPECT_TRUE(world_.IsValid(second))
        << "New handle for recycled slot must be valid";

    // If EnTT reused the same slot, generation must be higher
    if (second.id == first.id) {
        EXPECT_GT(second.generation, firstGen)
            << "Recycled slot must have higher generation than the destroyed entity";
    }
}

/// Test 5 — EntityCount: create 5, destroy 2, expect count == 3.
TEST_F(WorldTest, EntityCount_CreateFiveDestroyTwo_CountIsThree) {
    std::vector<Entity> entities;
    for (int i = 0; i < 5; ++i) {
        entities.push_back(world_.CreateEntity());
    }
    ASSERT_EQ(world_.EntityCount(), 5u);

    world_.DestroyEntity(entities[1]);
    world_.DestroyEntity(entities[3]);

    EXPECT_EQ(world_.EntityCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.2 Component lifecycle — add, get, remove, has
// ─────────────────────────────────────────────────────────────────────────────

/// Test 6 — AddComponent: stored component has the exact values passed.
TEST_F(WorldTest, AddComponent_Position_StoresCorrectValues) {
    Entity e = world_.CreateEntity();

    world_.AddComponent<Position>(e, Position{1.0f, 2.0f, 3.0f});

    const Position& pos = world_.GetComponent<Position>(e);
    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

/// Test 7 — AddMultipleComponents: all three components present and accessible.
TEST_F(WorldTest, AddMultipleComponents_AllPresent_AllRetrievable) {
    Entity e = world_.CreateEntity();

    world_.AddComponent<Position>(e, Position{10.0f, 20.0f, 30.0f});
    world_.AddComponent<Velocity>(e, Velocity{1.0f, 2.0f, 3.0f});
    world_.AddComponent<Health>(e, Health{50});

    EXPECT_TRUE(world_.HasComponent<Position>(e));
    EXPECT_TRUE(world_.HasComponent<Velocity>(e));
    EXPECT_TRUE(world_.HasComponent<Health>(e));

    EXPECT_FLOAT_EQ(world_.GetComponent<Position>(e).x,  10.0f);
    EXPECT_FLOAT_EQ(world_.GetComponent<Velocity>(e).dx,  1.0f);
    EXPECT_EQ(world_.GetComponent<Health>(e).hp, 50);
}

/// Test 8 — RemoveComponent: HasComponent returns false after removal.
TEST_F(WorldTest, RemoveComponent_HasComponent_ReturnsFalseAfterRemoval) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e);

    ASSERT_TRUE(world_.HasComponent<Position>(e));
    world_.RemoveComponent<Position>(e);

    EXPECT_FALSE(world_.HasComponent<Position>(e));
}

/// Test 9 — RemoveComponent on missing component: no crash (logs warning, no-op).
TEST_F(WorldTest, RemoveComponent_MissingComponent_IsNoOp) {
    Entity e = world_.CreateEntity();
    // Never added Position — RemoveComponent must log a warning and return cleanly

    EXPECT_NO_FATAL_FAILURE(world_.RemoveComponent<Position>(e))
        << "RemoveComponent on a component that was never added must not crash";
    EXPECT_FALSE(world_.HasComponent<Position>(e));
}

/// Test 10 — HasComponent: true for added components, false for absent ones.
TEST_F(WorldTest, HasComponent_TrueForAdded_FalseForAbsent) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e);

    EXPECT_TRUE(world_.HasComponent<Position>(e));
    EXPECT_FALSE(world_.HasComponent<Velocity>(e))
        << "Velocity was never added — HasComponent must return false";
    EXPECT_FALSE(world_.HasComponent<Health>(e))
        << "Health was never added — HasComponent must return false";
}

/// Test 11 — GetComponent mutation: change via reference persists on next get.
TEST_F(WorldTest, GetComponentMutation_ChangeViaMutableRef_PersistsOnNextGet) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e, Position{0.0f, 0.0f, 0.0f});

    world_.GetComponent<Position>(e).x = 99.0f;

    EXPECT_FLOAT_EQ(world_.GetComponent<Position>(e).x, 99.0f)
        << "Mutation via GetComponent reference must persist";
}

/// Test 12 — GetComponent const overload: accessible via const World reference.
TEST_F(WorldTest, GetComponentConst_ViaConstWorldRef_ReturnsCorrectValue) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Health>(e, Health{75});

    const World& constWorld = world_;
    const Health& h = constWorld.GetComponent<Health>(e);

    EXPECT_EQ(h.hp, 75);
}

/// Test 13 — AddComponent emplace-replace: re-adding overwrites existing value.
TEST_F(WorldTest, AddComponent_Twice_SecondValueOverwritesFirst) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e, Position{1.0f, 1.0f, 1.0f});
    world_.AddComponent<Position>(e, Position{9.0f, 8.0f, 7.0f}); // replace

    const Position& pos = world_.GetComponent<Position>(e);
    EXPECT_FLOAT_EQ(pos.x, 9.0f);
    EXPECT_FLOAT_EQ(pos.y, 8.0f);
    EXPECT_FLOAT_EQ(pos.z, 7.0f);
}

/// Test 14 — Tag (sentinel) component: add, has, remove all function correctly.
TEST_F(WorldTest, AddComponent_TagComponent_WorksCorrectly) {
    Entity e = world_.CreateEntity();

    world_.AddComponent<Tag>(e);
    EXPECT_TRUE(world_.HasComponent<Tag>(e));

    world_.RemoveComponent<Tag>(e);
    EXPECT_FALSE(world_.HasComponent<Tag>(e));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.3 Generational handle stale detection
// ─────────────────────────────────────────────────────────────────────────────

/// Test 15 — StaleHandleDetection: IsValid returns false for destroyed entity.
TEST_F(WorldTest, StaleHandle_IsValid_ReturnsFalseAfterDestroy) {
    Entity e = world_.CreateEntity();
    world_.DestroyEntity(e);

    EXPECT_FALSE(world_.IsValid(e))
        << "Stale handle must fail world IsValid check";
}

/// Test 16 — StaleHandle: Entity handle local IsValid only checks the sentinel,
/// not liveness — documents the contract difference between handle and world check.
TEST_F(WorldTest, StaleHandle_HandleLocalIsValid_DoesNotCheckLiveness) {
    Entity e = world_.CreateEntity();
    world_.DestroyEntity(e);

    // Handle-level IsValid only checks id != UINT32_MAX — not world liveness.
    EXPECT_TRUE(e.IsValid())
        << "Handle IsValid only checks id sentinel, not world liveness";
    // World-level check correctly detects the stale generation.
    EXPECT_FALSE(world_.IsValid(e))
        << "World::IsValid correctly detects the stale generation";
}

/// Test 17 — StaleHandle after Clear: all pre-clear handles become stale.
TEST_F(WorldTest, StaleHandle_AfterClear_PreClearHandlesAreInvalid) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    world_.Clear();

    EXPECT_FALSE(world_.IsValid(e1))
        << "Handle from before Clear must be stale after Clear";
    EXPECT_FALSE(world_.IsValid(e2))
        << "Handle from before Clear must be stale after Clear";
}

/// Test 18 — StaleHandle AddComponent asserts: UZ_CHECK fires (death test).
TEST_F(WorldTest, StaleHandle_AddComponent_DeathTest) {
    Entity e = world_.CreateEntity();
    world_.DestroyEntity(e);

    EXPECT_DEATH({ world_.AddComponent<Position>(e); }, ".*")
        << "AddComponent with stale handle must trigger UZ_CHECK abort";
}

/// Test 19 — StaleHandle GetComponent asserts.
TEST_F(WorldTest, StaleHandle_GetComponent_DeathTest) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e);
    world_.DestroyEntity(e);

    EXPECT_DEATH({ (void)world_.GetComponent<Position>(e); }, ".*")
        << "GetComponent with stale handle must trigger UZ_CHECK abort";
}

/// Test 20 — StaleHandle HasComponent asserts.
TEST_F(WorldTest, StaleHandle_HasComponent_DeathTest) {
    Entity e = world_.CreateEntity();
    world_.DestroyEntity(e);

    EXPECT_DEATH({ (void)world_.HasComponent<Position>(e); }, ".*")
        << "HasComponent with stale handle must trigger UZ_CHECK abort";
}

/// Test 21 — StaleHandle RemoveComponent asserts.
TEST_F(WorldTest, StaleHandle_RemoveComponent_DeathTest) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e);
    world_.DestroyEntity(e);

    EXPECT_DEATH({ world_.RemoveComponent<Position>(e); }, ".*")
        << "RemoveComponent with stale handle must trigger UZ_CHECK abort";
}

/// Test 22 — DestroyEntity with already-destroyed handle asserts (double-free guard).
TEST_F(WorldTest, DestroyEntity_Twice_DeathTest) {
    Entity e = world_.CreateEntity();
    world_.DestroyEntity(e);

    EXPECT_DEATH({ world_.DestroyEntity(e); }, ".*")
        << "Double-destroying an entity must trigger UZ_CHECK abort";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.4 Entity iteration — view queries
// ─────────────────────────────────────────────────────────────────────────────

/// Test 23 — ViewSingleComponent: exactly 3 of 5 entities have Position.
TEST_F(WorldTest, View_SingleComponent_ReturnsOnlyEntitiesWithThatComponent) {
    std::vector<Entity> entities;
    for (int i = 0; i < 5; ++i) {
        entities.push_back(world_.CreateEntity());
    }
    world_.AddComponent<Position>(entities[0], Position{1, 0, 0});
    world_.AddComponent<Position>(entities[2], Position{2, 0, 0});
    world_.AddComponent<Position>(entities[4], Position{3, 0, 0});
    // entities[1] and entities[3] have no Position

    int count = 0;
    for ([[maybe_unused]] auto&& item : world_.View<Position>().each()) {
        ++count;
    }

    EXPECT_EQ(count, 3) << "View<Position> must iterate exactly 3 entities";
}

/// Test 24 — ViewMultipleComponents: subset intersection.
/// 5 entities have Position, only 2 also have Velocity.
TEST_F(WorldTest, View_MultipleComponents_ReturnsIntersection) {
    std::vector<Entity> entities;
    for (int i = 0; i < 5; ++i) {
        entities.push_back(world_.CreateEntity());
    }
    // All 5 get Position
    for (auto& e : entities) {
        world_.AddComponent<Position>(e);
    }
    // Only 2 also get Velocity
    world_.AddComponent<Velocity>(entities[1], Velocity{1, 0, 0});
    world_.AddComponent<Velocity>(entities[3], Velocity{0, 1, 0});

    int count = 0;
    for ([[maybe_unused]] auto&& item : world_.View<Position, Velocity>().each()) {
        ++count;
    }

    EXPECT_EQ(count, 2)
        << "View<Position,Velocity> must iterate only entities with both components";
}

/// Test 25 — EmptyView: no entities with Position — view range is empty.
TEST_F(WorldTest, View_NoMatchingEntities_EmptyRange) {
    for (int i = 0; i < 3; ++i) {
        [[maybe_unused]] auto e = world_.CreateEntity(); // no Position added
    }

    int count = 0;
    for ([[maybe_unused]] auto&& item : world_.View<Position>().each()) {
        ++count;
    }

    EXPECT_EQ(count, 0) << "View<Position> on world with no Position components must be empty";
}

/// Test 26 — EmptyWorld view: world has zero entities — view is empty.
TEST_F(WorldTest, View_EmptyWorld_EmptyRange) {
    int count = 0;
    for ([[maybe_unused]] auto&& item : world_.View<Position>().each()) {
        ++count;
    }
    EXPECT_EQ(count, 0);
}

/// Test 27 — View const overload: accessible from const World reference.
TEST_F(WorldTest, View_Const_Overload_IteratesCorrectly) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Health>(e, Health{42});

    const World& constWorld = world_;
    int count = 0;
    for ([[maybe_unused]] auto&& item : constWorld.View<Health>().each()) {
        ++count;
    }

    EXPECT_EQ(count, 1);
}

/// Test 28 — View after destroy: destroyed entity no longer appears in view.
TEST_F(WorldTest, View_AfterEntityDestroyed_DoesNotAppear) {
    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();
    world_.AddComponent<Health>(e1, Health{10});
    world_.AddComponent<Health>(e2, Health{20});

    world_.DestroyEntity(e1);

    int count = 0;
    for ([[maybe_unused]] auto&& item : world_.View<Health>().each()) {
        ++count;
    }

    EXPECT_EQ(count, 1)
        << "Destroyed entity must not appear in subsequent views";
}

/// Test 29 — View component values: values iterated match what was added.
TEST_F(WorldTest, View_ComponentValues_MatchAddedData) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e, Position{7.0f, 8.0f, 9.0f});

    for (auto [enttE, pos] : world_.View<Position>().each()) {
        (void)enttE;
        EXPECT_FLOAT_EQ(pos.x, 7.0f);
        EXPECT_FLOAT_EQ(pos.y, 8.0f);
        EXPECT_FLOAT_EQ(pos.z, 9.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.6 World shutdown / Clear
// ─────────────────────────────────────────────────────────────────────────────

/// Test 30 — Clear: EntityCount returns 0 after Clear.
TEST_F(WorldTest, Clear_EntityCount_IsZeroAfterClear) {
    for (int i = 0; i < 10; ++i) {
        [[maybe_unused]] auto e = world_.CreateEntity();
    }
    ASSERT_EQ(world_.EntityCount(), 10u);

    world_.Clear();

    EXPECT_EQ(world_.EntityCount(), 0u) << "EntityCount must be 0 after Clear";
}

/// Test 31 — ClearAndRecreate: new entities created after Clear are valid.
TEST_F(WorldTest, Clear_ThenCreateEntity_NewEntityIsValid) {
    for (int i = 0; i < 5; ++i) {
        [[maybe_unused]] auto e = world_.CreateEntity();
    }
    world_.Clear();

    Entity fresh = world_.CreateEntity();

    EXPECT_TRUE(world_.IsValid(fresh))
        << "New entity created after Clear must be valid";
    EXPECT_EQ(world_.EntityCount(), 1u);
}

/// Test 32 — Clear: components added before clear are gone.
TEST_F(WorldTest, Clear_ComponentsFromBeforeClear_AreGone) {
    Entity e = world_.CreateEntity();
    world_.AddComponent<Position>(e);
    world_.Clear();

    // Recreate one entity to be able to call View (old handle is stale)
    [[maybe_unused]] auto fresh = world_.CreateEntity();

    int count = 0;
    for ([[maybe_unused]] auto&& item : world_.View<Position>().each()) {
        ++count;
    }

    EXPECT_EQ(count, 0)
        << "No Position components should survive a Clear";
}

/// Test 33 — Clear on empty world: no crash, count stays 0.
TEST_F(WorldTest, Clear_OnEmptyWorld_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE(world_.Clear());
    EXPECT_EQ(world_.EntityCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity handle — value semantics and sentinel
// ─────────────────────────────────────────────────────────────────────────────

/// Test 34 — Entity::Invalid sentinel: id==UINT32_MAX, generation==0, IsValid==false.
TEST_F(WorldTest, EntityInvalid_Sentinel_CorrectValues) {
    Entity inv = Entity::Invalid();

    EXPECT_EQ(inv.id, UINT32_MAX);
    EXPECT_EQ(inv.generation, 0u);
    EXPECT_FALSE(inv.IsValid()) << "Entity::Invalid() must report IsValid()==false";
    EXPECT_FALSE(world_.IsValid(inv))
        << "World must also reject the null sentinel";
}

/// Test 35 — Entity::Invalid: default-constructed Entity equals Invalid().
TEST_F(WorldTest, EntityInvalid_DefaultConstructed_EqualsInvalid) {
    Entity defaultE;
    EXPECT_EQ(defaultE, Entity::Invalid());
    EXPECT_FALSE(defaultE.IsValid());
}

/// Test 36a — EntityEquality: same id+gen are equal.
TEST_F(WorldTest, EntityEquality_SameIdAndGen_Equal) {
    Entity a{42u, 3u};
    Entity b{42u, 3u};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

/// Test 36b — EntityEquality: different generation are not equal.
TEST_F(WorldTest, EntityEquality_DifferentGeneration_NotEqual) {
    Entity a{42u, 1u};
    Entity b{42u, 2u};
    EXPECT_NE(a, b);
    EXPECT_FALSE(a == b);
}

/// Test 36c — EntityEquality: different id are not equal.
TEST_F(WorldTest, EntityEquality_DifferentId_NotEqual) {
    Entity a{1u, 1u};
    Entity b{2u, 1u};
    EXPECT_NE(a, b);
}

/// Test 37 — Entity hash: can be used in unordered_set (hash specialization works).
TEST_F(WorldTest, EntityHash_CanBeUsedInUnorderedSet) {
    std::unordered_set<Entity> entitySet;

    Entity e1 = world_.CreateEntity();
    Entity e2 = world_.CreateEntity();

    entitySet.insert(e1);
    entitySet.insert(e2);

    EXPECT_EQ(entitySet.size(), 2u);
    EXPECT_EQ(entitySet.count(e1), 1u);
    EXPECT_EQ(entitySet.count(e2), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetRegistry — direct registry access
// ─────────────────────────────────────────────────────────────────────────────

/// Test 38 — GetRegistry mutable: returns usable registry reference.
TEST_F(WorldTest, GetRegistry_Mutable_IsUsable) {
    Entity e = world_.CreateEntity();
    entt::registry& reg = world_.GetRegistry();

    // Only querying (not creating/destroying) via registry to avoid breaking generation tracking.
    EXPECT_TRUE(reg.valid(static_cast<entt::entity>(e.id)));
}

/// Test 39 — GetRegistry const: accessible from const World.
TEST_F(WorldTest, GetRegistry_Const_IsAccessible) {
    Entity e = world_.CreateEntity();
    const World& cw = world_;
    const entt::registry& reg = cw.GetRegistry();

    EXPECT_TRUE(reg.valid(static_cast<entt::entity>(e.id)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.5 Bulk create — 100K entities (sanity check, not strict timing)
// ─────────────────────────────────────────────────────────────────────────────

/// Test 40 — BulkCreate: 100K entities, EntityCount matches, no crashes.
TEST_F(WorldTest, BulkCreate_100K_EntityCount_IsCorrect) {
    constexpr size_t kCount = 100'000;

    for (size_t i = 0; i < kCount; ++i) {
        [[maybe_unused]] auto e = world_.CreateEntity();
    }

    EXPECT_EQ(world_.EntityCount(), kCount)
        << "EntityCount must equal 100K after bulk creation";
}

/// Test 41 — BulkCreate + Clear: destroy all 100K, count returns to 0.
TEST_F(WorldTest, BulkCreate_ThenClear_CountReturnsToZero) {
    constexpr size_t kCount = 100'000;
    for (size_t i = 0; i < kCount; ++i) {
        [[maybe_unused]] auto e = world_.CreateEntity();
    }
    ASSERT_EQ(world_.EntityCount(), kCount);

    world_.Clear();

    EXPECT_EQ(world_.EntityCount(), 0u);
}

/// Test 42 — BulkCreate performance: 100K creates complete within 5 seconds
/// (soft guard against catastrophic regression — not a hard timing benchmark).
TEST_F(WorldTest, BulkCreate_100K_CompletesWithinReasonableTime) {
    constexpr size_t kCount = 100'000;

    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < kCount; ++i) {
        [[maybe_unused]] auto e = world_.CreateEntity();
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    EXPECT_LT(ms, 5000)
        << "100K entity creation took " << ms << "ms — unexpectedly slow";

    world_.Clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// WorldSubsystem — 2.8 subsystem lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Fixture for WorldSubsystem tests.
class WorldSubsystemTest : public ::testing::Test {
protected:
    WorldSubsystem subsystem_;
};

/// Test 43 — GetName returns "World".
TEST_F(WorldSubsystemTest, GetName_ReturnsWorld) {
    EXPECT_STREQ(subsystem_.GetName(), "World");
}

/// Test 44 — GetInitPhase returns InitPhase::Start.
TEST_F(WorldSubsystemTest, GetInitPhase_ReturnsStart) {
    EXPECT_EQ(subsystem_.GetInitPhase(), InitPhase::Start);
}

/// Test 45 — GetTickGroup returns TickGroup::None.
TEST_F(WorldSubsystemTest, GetTickGroup_ReturnsNone) {
    EXPECT_EQ(subsystem_.GetTickGroup(), TickGroup::None);
}

/// Test 46 — GetDependencies returns empty vector.
TEST_F(WorldSubsystemTest, GetDependencies_ReturnsEmpty) {
    EXPECT_TRUE(subsystem_.GetDependencies().empty());
}

/// Test 47 — GetWorld before Init returns nullptr.
TEST_F(WorldSubsystemTest, GetWorld_BeforeInit_ReturnsNullptr) {
    EXPECT_EQ(subsystem_.GetWorld(), nullptr)
        << "GetWorld must return nullptr before Init() is called";
}

/// Test 48 — Init returns true, GetWorld returns non-null.
TEST_F(WorldSubsystemTest, Init_ReturnsTrue_GetWorldIsNotNull) {
    bool ok = subsystem_.Init();

    EXPECT_TRUE(ok) << "WorldSubsystem::Init must always return true";
    EXPECT_NE(subsystem_.GetWorld(), nullptr)
        << "GetWorld must return non-null pointer after successful Init";

    subsystem_.Shutdown();
}

/// Test 49 — WorldSubsystemInit: world returned by GetWorld is fully operational.
TEST_F(WorldSubsystemTest, Init_GetWorld_IsFullyOperational) {
    ASSERT_TRUE(subsystem_.Init());

    World* world = subsystem_.GetWorld();
    ASSERT_NE(world, nullptr);

    Entity e = world->CreateEntity();
    EXPECT_TRUE(world->IsValid(e));
    EXPECT_EQ(world->EntityCount(), 1u);

    subsystem_.Shutdown();
}

/// Test 50 — WorldSubsystemShutdown: after Shutdown, GetWorld returns nullptr.
TEST_F(WorldSubsystemTest, Shutdown_GetWorld_ReturnsNullptrAfterShutdown) {
    ASSERT_TRUE(subsystem_.Init());
    ASSERT_NE(subsystem_.GetWorld(), nullptr);

    subsystem_.Shutdown();

    EXPECT_EQ(subsystem_.GetWorld(), nullptr)
        << "GetWorld must return nullptr after Shutdown";
}

/// Test 51 — WorldSubsystemShutdown: entities created before shutdown are cleaned up.
TEST_F(WorldSubsystemTest, Shutdown_EntitiesCreatedBeforeShutdown_CleanedUp) {
    ASSERT_TRUE(subsystem_.Init());

    World* world = subsystem_.GetWorld();
    for (int i = 0; i < 20; ++i) {
        [[maybe_unused]] auto e = world->CreateEntity();
    }
    ASSERT_EQ(world->EntityCount(), 20u);

    // Shutdown clears the world and destroys the unique_ptr — no crash, no leaks
    EXPECT_NO_FATAL_FAILURE(subsystem_.Shutdown());
    EXPECT_EQ(subsystem_.GetWorld(), nullptr);
}

/// Test 52 — Shutdown before Init: idempotent — no crash.
TEST_F(WorldSubsystemTest, Shutdown_BeforeInit_IsNoOp) {
    EXPECT_NO_FATAL_FAILURE(subsystem_.Shutdown())
        << "Shutdown before Init must be a no-op (world_ is null)";
    EXPECT_EQ(subsystem_.GetWorld(), nullptr);
}

/// Test 53 — Const GetWorld: accessible via const subsystem reference.
TEST_F(WorldSubsystemTest, GetWorld_Const_ReturnsCorrectPointer) {
    ASSERT_TRUE(subsystem_.Init());

    const WorldSubsystem& constSub = subsystem_;
    EXPECT_NE(constSub.GetWorld(), nullptr);

    subsystem_.Shutdown();
}

/// Test 54 — Tick is a no-op: calling Tick does not crash, world is unchanged.
TEST_F(WorldSubsystemTest, Tick_IsNoOp_WorldUnchanged) {
    ASSERT_TRUE(subsystem_.Init());

    World* world = subsystem_.GetWorld();
    [[maybe_unused]] auto e = world->CreateEntity();
    ASSERT_EQ(world->EntityCount(), 1u);

    subsystem_.Tick(0.016f); // default no-op from ISubsystem

    EXPECT_EQ(subsystem_.GetWorld()->EntityCount(), 1u)
        << "Tick must not modify the world entity count";

    subsystem_.Shutdown();
}
