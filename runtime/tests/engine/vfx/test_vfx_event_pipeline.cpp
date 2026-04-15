/// Unit tests for phase 7.3.0 - full event pipeline CPU dispatcher.
///
/// Covers:
///   - Receiver accepts only its configured source emitter.
///   - Kind mask filter honours each bit independently.
///   - SpawnBurst / InheritVelocity / SpawnAtPosition action semantics.
///   - Payload queue drained after Dispatch.
///   - Multiple receivers consuming the same payload.
///   - RemoveReceiver drops exactly the matching entries.
///   - VfxEventReceiverConfig::Accepts default mask catches every kind.
///
/// @ref phase7_7.3.0-vfx-event-pipeline-full

#include <gtest/gtest.h>

#include "engine/vfx/events/vfx_event_pipeline.h"

using namespace uz;

namespace {

VfxEventPayload MakeDeathPayload(uint32_t sourceHandle, uint64_t sourceID) {
    VfxEventPayload p;
    p.sourceEmitterHandle = sourceHandle;
    p.sourceParticleID    = sourceID;
    p.kind                = VfxEventKind::Death;
    p.position            = Vec3{1.0f, 2.0f, 3.0f};
    p.velocity            = Vec3{10.0f, 20.0f, 30.0f};
    p.color               = Vec4{0.5f, 0.5f, 0.5f, 1.0f};
    return p;
}

} // namespace

// ─── Receiver filter: source handle ────────────────────────────────────────

TEST(VfxEventPipeline, ReceiverOnlyConsumesOwnSource) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    cfg.burstCount          = 4u;
    pipe.AddReceiver(/*receiver=*/ 999u, cfg);

    pipe.SubmitPayload(MakeDeathPayload(100u, 7u));   // matching source
    pipe.SubmitPayload(MakeDeathPayload(200u, 8u));   // mismatching source

    const auto reqs = pipe.Dispatch();
    ASSERT_EQ(reqs.size(), 1u);
    EXPECT_EQ(reqs[0].receiverEmitterHandle, 999u);
    EXPECT_EQ(reqs[0].sourceParticleID, 7u);
    EXPECT_EQ(reqs[0].burstCount, 4u);
}

// ─── Receiver filter: kind mask ────────────────────────────────────────────

TEST(VfxEventPipeline, KindMaskFiltersIndependentBits) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    // Only Collision events.
    cfg.kindMask = (1u << static_cast<uint32_t>(VfxEventKind::Collision));
    pipe.AddReceiver(999u, cfg);

    VfxEventPayload death = MakeDeathPayload(100u, 1u);   // skipped
    VfxEventPayload coll  = MakeDeathPayload(100u, 2u);   // accepted
    coll.kind = VfxEventKind::Collision;

    pipe.SubmitPayload(death);
    pipe.SubmitPayload(coll);
    const auto reqs = pipe.Dispatch();
    ASSERT_EQ(reqs.size(), 1u);
    EXPECT_EQ(reqs[0].triggeringKind, VfxEventKind::Collision);
}

TEST(VfxEventPipeline, DefaultMaskAcceptsEveryKind) {
    VfxEventReceiverConfig cfg;  // default kindMask = 0xFFFFFFFF
    EXPECT_TRUE(cfg.Accepts(VfxEventKind::Spawn));
    EXPECT_TRUE(cfg.Accepts(VfxEventKind::Death));
    EXPECT_TRUE(cfg.Accepts(VfxEventKind::Collision));
    EXPECT_TRUE(cfg.Accepts(VfxEventKind::Custom));
}

// ─── Action semantics ──────────────────────────────────────────────────────

TEST(VfxEventPipeline, SpawnBurstActionZeroVelocity) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    cfg.action              = VfxEventReceiverAction::SpawnBurst;
    cfg.burstCount          = 12u;
    pipe.AddReceiver(999u, cfg);

    pipe.SubmitPayload(MakeDeathPayload(100u, 42u));
    const auto reqs = pipe.Dispatch();
    ASSERT_EQ(reqs.size(), 1u);
    EXPECT_FLOAT_EQ(reqs[0].inheritedVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(reqs[0].inheritedVelocity.y, 0.0f);
    EXPECT_EQ(reqs[0].burstCount, 12u);
}

TEST(VfxEventPipeline, InheritVelocityAppliesFactor) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle  = 100u;
    cfg.action               = VfxEventReceiverAction::InheritVelocity;
    cfg.velocityInheritance  = 0.25f;
    pipe.AddReceiver(999u, cfg);

    pipe.SubmitPayload(MakeDeathPayload(100u, 1u));
    const auto reqs = pipe.Dispatch();
    ASSERT_EQ(reqs.size(), 1u);
    EXPECT_NEAR(reqs[0].inheritedVelocity.x, 10.0f * 0.25f, 1e-3f);
    EXPECT_NEAR(reqs[0].inheritedVelocity.y, 20.0f * 0.25f, 1e-3f);
    EXPECT_NEAR(reqs[0].inheritedVelocity.z, 30.0f * 0.25f, 1e-3f);
}

TEST(VfxEventPipeline, SpawnAtPositionIgnoresVelocity) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    cfg.action              = VfxEventReceiverAction::SpawnAtPosition;
    cfg.velocityInheritance = 0.9f;  // should be ignored
    pipe.AddReceiver(999u, cfg);

    pipe.SubmitPayload(MakeDeathPayload(100u, 1u));
    const auto reqs = pipe.Dispatch();
    ASSERT_EQ(reqs.size(), 1u);
    EXPECT_FLOAT_EQ(reqs[0].inheritedVelocity.x, 0.0f);
    // Position still honoured.
    EXPECT_FLOAT_EQ(reqs[0].spawnPosition.x, 1.0f);
    EXPECT_FLOAT_EQ(reqs[0].spawnPosition.z, 3.0f);
}

// ─── Queue + dispatch lifecycle ────────────────────────────────────────────

TEST(VfxEventPipeline, DispatchDrainsPayloadQueue) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    pipe.AddReceiver(999u, cfg);

    pipe.SubmitPayload(MakeDeathPayload(100u, 1u));
    pipe.SubmitPayload(MakeDeathPayload(100u, 2u));
    EXPECT_EQ(pipe.PendingCount(), 2u);
    (void)pipe.Dispatch();
    EXPECT_EQ(pipe.PendingCount(), 0u);
}

TEST(VfxEventPipeline, MultipleReceiversConsumeSamePayload) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    cfg.burstCount          = 3u;
    pipe.AddReceiver(/*receiver=*/ 999u, cfg);
    cfg.burstCount          = 7u;
    pipe.AddReceiver(/*receiver=*/ 888u, cfg);

    pipe.SubmitPayload(MakeDeathPayload(100u, 1u));
    const auto reqs = pipe.Dispatch();
    ASSERT_EQ(reqs.size(), 2u);
    EXPECT_NE(reqs[0].receiverEmitterHandle, reqs[1].receiverEmitterHandle);
}

TEST(VfxEventPipeline, SubmitPayloadsBatchHelperWorks) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfg;
    cfg.sourceEmitterHandle = 100u;
    pipe.AddReceiver(999u, cfg);

    VfxEventPayload batch[3] = {
        MakeDeathPayload(100u, 1u),
        MakeDeathPayload(100u, 2u),
        MakeDeathPayload(100u, 3u),
    };
    pipe.SubmitPayloads(batch, 3);
    EXPECT_EQ(pipe.PendingCount(), 3u);
    const auto reqs = pipe.Dispatch();
    EXPECT_EQ(reqs.size(), 3u);
}

// ─── Receiver management ──────────────────────────────────────────────────

TEST(VfxEventPipeline, RemoveReceiverDropsMatchingEntries) {
    VfxEventPipeline pipe;
    VfxEventReceiverConfig cfgA; cfgA.sourceEmitterHandle = 100u;
    VfxEventReceiverConfig cfgB; cfgB.sourceEmitterHandle = 100u;
    pipe.AddReceiver(999u, cfgA);
    pipe.AddReceiver(888u, cfgB);
    pipe.AddReceiver(999u, cfgA);  // duplicate handle

    EXPECT_EQ(pipe.ReceiverCount(), 3u);
    pipe.RemoveReceiver(999u);
    EXPECT_EQ(pipe.ReceiverCount(), 1u);
}
