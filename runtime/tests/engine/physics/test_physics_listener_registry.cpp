/// Unit tests for phase 10.10.0 — physics listener registry.
///
/// @ref engine/physics/physics_listener_registry.h

#include <gtest/gtest.h>

#include "engine/physics/physics_listener_registry.h"

using namespace uz;

namespace {

struct RecordingContactListener : IPhysicsContactListener {
    uint32_t added     = 0;
    uint32_t persisted = 0;
    uint32_t removed   = 0;
    uint64_t lastA     = 0;
    uint64_t lastB     = 0;
    PhysicsContactEvent lastEvent{};

    void OnContactAdded(const PhysicsContactEvent& e) override {
        ++added; lastEvent = e; lastA = e.bodyIdA; lastB = e.bodyIdB;
    }
    void OnContactPersisted(const PhysicsContactEvent& e) override {
        ++persisted; lastEvent = e;
    }
    void OnContactRemoved(uint64_t a, uint64_t b) override {
        ++removed; lastA = a; lastB = b;
    }
};

struct RecordingFractureListener : IPhysicsFractureListener {
    uint32_t fractures = 0;
    uint64_t lastBodyId = 0;

    void OnFracture(const VfxFractureEventInput& e) override {
        ++fractures;
        lastBodyId = e.bodyId;
    }
};

PhysicsContactEvent MakeContact(uint64_t a, uint64_t b) {
    PhysicsContactEvent e;
    e.bodyIdA = a; e.bodyIdB = b;
    e.position = Vec3{1, 2, 3};
    e.normal   = Vec3{0, 0, 1};
    e.impulseMagnitude = 7.5f;
    e.relativeVelocity = -3.2f;
    return e;
}

VfxFractureEventInput MakeFracture(uint64_t id) {
    VfxFractureEventInput e;
    e.bodyId = id;
    e.impactNormal = Vec3{1, 0, 0};
    e.impactSpeed  = 5.0f;
    VfxFracturePiece p; p.mass = 1.0f;
    e.pieces.push_back(p);
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Basic lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST(PhysicsListenerRegistry, StartsEmpty) {
    PhysicsListenerRegistry r;
    EXPECT_EQ(r.ContactListenerCount(),  0u);
    EXPECT_EQ(r.FractureListenerCount(), 0u);
}

TEST(PhysicsListenerRegistry, RegisterContactReturnsHandle) {
    PhysicsListenerRegistry r;
    RecordingContactListener l;
    const auto h = r.RegisterContactListener(&l);
    EXPECT_NE(h, kInvalidPhysicsListenerHandle);
    EXPECT_EQ(r.ContactListenerCount(), 1u);
}

TEST(PhysicsListenerRegistry, NullListenerRejectedWithInvalidHandle) {
    PhysicsListenerRegistry r;
    EXPECT_EQ(r.RegisterContactListener(nullptr),  kInvalidPhysicsListenerHandle);
    EXPECT_EQ(r.RegisterFractureListener(nullptr), kInvalidPhysicsListenerHandle);
    EXPECT_EQ(r.ContactListenerCount(), 0u);
}

TEST(PhysicsListenerRegistry, UnregisterRemovesSlot) {
    PhysicsListenerRegistry r;
    RecordingContactListener l;
    const auto h = r.RegisterContactListener(&l);
    r.UnregisterContactListener(h);
    EXPECT_EQ(r.ContactListenerCount(), 0u);
}

TEST(PhysicsListenerRegistry, UnregisterInvalidHandleIsNoOp) {
    PhysicsListenerRegistry r;
    r.UnregisterContactListener(kInvalidPhysicsListenerHandle); // no-op
    r.UnregisterContactListener(9999); // never-registered handle
    EXPECT_EQ(r.ContactListenerCount(), 0u);
}

TEST(PhysicsListenerRegistry, ClearRemovesAll) {
    PhysicsListenerRegistry r;
    RecordingContactListener  cl;
    RecordingFractureListener fl;
    r.RegisterContactListener(&cl);
    r.RegisterFractureListener(&fl);
    r.Clear();
    EXPECT_EQ(r.ContactListenerCount(),  0u);
    EXPECT_EQ(r.FractureListenerCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Contact dispatch
// ─────────────────────────────────────────────────────────────────────────────

TEST(PhysicsListenerRegistry, DispatchContactAddedReachesEverySubscriber) {
    PhysicsListenerRegistry r;
    RecordingContactListener a, b, c;
    r.RegisterContactListener(&a);
    r.RegisterContactListener(&b);
    r.RegisterContactListener(&c);
    r.DispatchContactAdded(MakeContact(10, 20));
    EXPECT_EQ(a.added, 1u);
    EXPECT_EQ(b.added, 1u);
    EXPECT_EQ(c.added, 1u);
    EXPECT_EQ(a.lastEvent.bodyIdA, 10u);
    EXPECT_EQ(a.lastEvent.bodyIdB, 20u);
}

TEST(PhysicsListenerRegistry, DispatchPersistedAndRemovedRoutedCorrectly) {
    PhysicsListenerRegistry r;
    RecordingContactListener l;
    r.RegisterContactListener(&l);
    r.DispatchContactPersisted(MakeContact(1, 2));
    r.DispatchContactRemoved(1, 2);
    EXPECT_EQ(l.added,     0u);
    EXPECT_EQ(l.persisted, 1u);
    EXPECT_EQ(l.removed,   1u);
    EXPECT_EQ(l.lastA, 1u);
    EXPECT_EQ(l.lastB, 2u);
}

TEST(PhysicsListenerRegistry, UnregisteredListenerReceivesNoEvents) {
    PhysicsListenerRegistry r;
    RecordingContactListener kept, removed;
    r.RegisterContactListener(&kept);
    const auto h = r.RegisterContactListener(&removed);
    r.UnregisterContactListener(h);
    r.DispatchContactAdded(MakeContact(1, 2));
    EXPECT_EQ(kept.added,    1u);
    EXPECT_EQ(removed.added, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fracture dispatch
// ─────────────────────────────────────────────────────────────────────────────

TEST(PhysicsListenerRegistry, FractureDispatchRoutesToAll) {
    PhysicsListenerRegistry r;
    RecordingFractureListener a, b;
    r.RegisterFractureListener(&a);
    r.RegisterFractureListener(&b);
    r.DispatchFracture(MakeFracture(42));
    EXPECT_EQ(a.fractures, 1u);
    EXPECT_EQ(b.fractures, 1u);
    EXPECT_EQ(a.lastBodyId, 42u);
}

TEST(PhysicsListenerRegistry, FractureAndContactIndependent) {
    PhysicsListenerRegistry r;
    RecordingContactListener   c;
    RecordingFractureListener  f;
    r.RegisterContactListener(&c);
    r.RegisterFractureListener(&f);

    r.DispatchContactAdded(MakeContact(1, 2));
    EXPECT_EQ(c.added, 1u);
    EXPECT_EQ(f.fractures, 0u); // contact shouldn't leak into fracture

    r.DispatchFracture(MakeFracture(7));
    EXPECT_EQ(c.added, 1u);     // unchanged
    EXPECT_EQ(f.fractures, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-entrancy safety
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct ReentrantListener : IPhysicsContactListener {
    PhysicsListenerRegistry* registry = nullptr;
    uint32_t added = 0;
    void OnContactAdded(const PhysicsContactEvent&) override {
        ++added;
        // Remove self during dispatch — must not invalidate the snapshot
        // iterator in `DispatchContactAdded`.
        if (registry && selfHandle != kInvalidPhysicsListenerHandle) {
            registry->UnregisterContactListener(selfHandle);
            selfHandle = kInvalidPhysicsListenerHandle;
        }
    }
    PhysicsListenerHandle selfHandle = kInvalidPhysicsListenerHandle;
};

} // namespace

TEST(PhysicsListenerRegistry, SelfUnregisterDuringDispatchIsSafe) {
    PhysicsListenerRegistry r;
    ReentrantListener rl;
    rl.registry   = &r;
    rl.selfHandle = r.RegisterContactListener(&rl);
    r.DispatchContactAdded(MakeContact(1, 2));
    EXPECT_EQ(rl.added, 1u);
    EXPECT_EQ(r.ContactListenerCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Handle uniqueness
// ─────────────────────────────────────────────────────────────────────────────

TEST(PhysicsListenerRegistry, HandlesAreUniqueAcrossRegistrations) {
    PhysicsListenerRegistry r;
    RecordingContactListener a, b;
    RecordingFractureListener c;
    const auto h1 = r.RegisterContactListener(&a);
    const auto h2 = r.RegisterContactListener(&b);
    const auto h3 = r.RegisterFractureListener(&c);
    EXPECT_NE(h1, h2);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h2, h3);
}
