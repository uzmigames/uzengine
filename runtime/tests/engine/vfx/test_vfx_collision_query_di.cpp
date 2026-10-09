/// Unit tests for phase 7.14.0 - collision query DI CPU resolver.
///
/// Covers:
///   - Default-constructed resolver has no sink and yields misses.
///   - Missing sink path still produces one result per request.
///   - Custom sink invoked once per request and its result passed through.
///   - Resolver echoes request.particleID onto the result regardless of sink.
///   - SubmitRequests batch helper works + defensive null/zero handling.
///   - Resolve drains the pending queue.
///   - VfxRaycastMiss helper produces hit=0 with echoed particle ID.
///
/// @ref phase7_7.14.0-vfx-collision-query-di

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_collision_query_di.h"

using namespace uz;

namespace {

/// Fabricates a simple hit at (1, 2, 3) with material ID 7 — irrespective
/// of the request. Used to verify per-request routing.
VfxRaycastResult FakeSink(const VfxRaycastRequest& req) {
    VfxRaycastResult r;
    r.hit         = 1u;
    r.hitMaterialID = 7u;
    r.hitDistance = req.maxDist * 0.5f;   // halfway down the ray
    r.hitPosition = Vec3{1.0f, 2.0f, 3.0f};
    r.hitNormal   = Vec3{0.0f, 0.0f, 1.0f};
    // Intentionally leave particleID at 0 so we can verify the resolver
    // always re-stamps it from the request.
    return r;
}

VfxRaycastRequest MakeRequest(uint64_t particleID, float maxDist = 100.0f) {
    VfxRaycastRequest r;
    r.particleID = particleID;
    r.origin     = Vec3{0.0f, 0.0f, 0.0f};
    r.direction  = Vec3{0.0f, 0.0f, -1.0f};
    r.maxDist    = maxDist;
    return r;
}

} // namespace

// ─── Default state / miss fallback ─────────────────────────────────────────

TEST(VfxCollisionQueryResolver, DefaultHasNoSink) {
    VfxCollisionQueryResolver r;
    EXPECT_FALSE(r.HasSink());
    EXPECT_EQ(r.PendingCount(), 0u);
}

TEST(VfxCollisionQueryResolver, MissingSinkProducesMissPerRequest) {
    VfxCollisionQueryResolver r;
    r.SubmitRequest(MakeRequest(10u));
    r.SubmitRequest(MakeRequest(20u));
    const auto results = r.Resolve();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].hit, 0u);
    EXPECT_EQ(results[1].hit, 0u);
    EXPECT_EQ(results[0].particleID, 10u);  // ID echoed
    EXPECT_EQ(results[1].particleID, 20u);
}

// ─── Custom sink ───────────────────────────────────────────────────────────

TEST(VfxCollisionQueryResolver, SinkInvokedOncePerRequest) {
    int calls = 0;
    VfxCollisionQueryResolver r;
    r.SetPhysicsSink([&](const VfxRaycastRequest& req) {
        ++calls;
        return FakeSink(req);
    });
    r.SubmitRequest(MakeRequest(1u));
    r.SubmitRequest(MakeRequest(2u));
    r.SubmitRequest(MakeRequest(3u));
    const auto results = r.Resolve();
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(results.size(), 3u);
}

TEST(VfxCollisionQueryResolver, SinkResultPassesThroughAttributes) {
    VfxCollisionQueryResolver r;
    r.SetPhysicsSink(&FakeSink);
    r.SubmitRequest(MakeRequest(42u, /*maxDist=*/ 200.0f));
    const auto results = r.Resolve();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].hit, 1u);
    EXPECT_EQ(results[0].hitMaterialID, 7u);
    EXPECT_FLOAT_EQ(results[0].hitDistance, 100.0f);  // 200 * 0.5
    EXPECT_FLOAT_EQ(results[0].hitPosition.x, 1.0f);
    EXPECT_FLOAT_EQ(results[0].hitNormal.z, 1.0f);
}

TEST(VfxCollisionQueryResolver, AlwaysEchoesParticleIDEvenIfSinkDrops) {
    // FakeSink returns particleID=0 deliberately; resolver must overwrite
    // it with the request's ID so per-particle routing stays correct.
    VfxCollisionQueryResolver r;
    r.SetPhysicsSink(&FakeSink);
    r.SubmitRequest(MakeRequest(99u));
    const auto results = r.Resolve();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].particleID, 99u);
}

// ─── Queue management ──────────────────────────────────────────────────────

TEST(VfxCollisionQueryResolver, ResolveDrainsQueue) {
    VfxCollisionQueryResolver r;
    r.SubmitRequest(MakeRequest(1u));
    EXPECT_EQ(r.PendingCount(), 1u);
    (void)r.Resolve();
    EXPECT_EQ(r.PendingCount(), 0u);
}

TEST(VfxCollisionQueryResolver, SubmitRequestsBatchHelperWorks) {
    VfxCollisionQueryResolver r;
    VfxRaycastRequest batch[3] = {
        MakeRequest(1u),
        MakeRequest(2u),
        MakeRequest(3u),
    };
    r.SubmitRequests(batch, 3u);
    EXPECT_EQ(r.PendingCount(), 3u);
}

TEST(VfxCollisionQueryResolver, SubmitRequestsHandlesNullAndZeroCount) {
    VfxCollisionQueryResolver r;
    VfxRaycastRequest dummy = MakeRequest(1u);
    r.SubmitRequests(nullptr, 5u);    // must be a no-op, not UB
    r.SubmitRequests(&dummy,  0u);
    EXPECT_EQ(r.PendingCount(), 0u);
}

// ─── VfxRaycastMiss helper ─────────────────────────────────────────────────

TEST(VfxRaycastMiss, ProducesHitZeroWithEchoedID) {
    const auto miss = VfxRaycastMiss(/*particleID=*/ 123u);
    EXPECT_EQ(miss.hit, 0u);
    EXPECT_EQ(miss.particleID, 123u);
    EXPECT_FLOAT_EQ(miss.hitDistance, 0.0f);
}

// ─── Sink swap mid-session ─────────────────────────────────────────────────

TEST(VfxCollisionQueryResolver, SinkCanBeReplaced) {
    VfxCollisionQueryResolver r;
    r.SetPhysicsSink(&FakeSink);
    EXPECT_TRUE(r.HasSink());
    r.SetPhysicsSink({});  // clear
    EXPECT_FALSE(r.HasSink());
}
