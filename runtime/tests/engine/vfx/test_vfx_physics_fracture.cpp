/// Unit tests for phase 8.24.0 — VFX physics-fracture event bridge.
///
/// Validates the callback → `VFXPendingEvent` translation and the
/// forwarding bridge that enqueues events on a VFXEventQueue.
///
/// @ref engine/vfx/events/vfx_physics_fracture.h
/// @ref docs/engine/25-vfx/41-physics-fracture-events.md

#include <gtest/gtest.h>

#include "engine/vfx/events/vfx_physics_fracture.h"

#include <cmath>

using namespace uz;

namespace {

VfxFractureEventInput MakeTwoPieceFracture() {
    VfxFractureEventInput in;
    in.bodyId       = 42u;
    in.impactNormal = Vec3{1.0f, 0.0f, 0.0f};
    in.impactSpeed  = 5.0f;
    in.authoredTint = Vec3{0.6f, 0.4f, 0.2f};
    VfxFracturePiece a;
    a.position       = Vec3{1, 2, 3};
    a.linearVelocity = Vec3{0, 0, 1};
    a.mass           = 3.0f;
    VfxFracturePiece b;
    b.position       = Vec3{4, 5, 6};
    b.linearVelocity = Vec3{0, 0, -2};
    b.mass           = 1.0f;
    in.pieces.push_back(a);
    in.pieces.push_back(b);
    return in;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Translation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxFractureTranslate, EmptyPiecesProducesEmptyEvent) {
    VfxFractureEventInput in;
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    EXPECT_EQ(evt.emissionCount, 0u);
    EXPECT_TRUE(evt.payloads.empty());
    EXPECT_EQ(evt.name, "fracture");
}

TEST(VfxFractureTranslate, EmissionCountMatchesPieceCount) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    EXPECT_EQ(evt.emissionCount, 2u);
    EXPECT_EQ(evt.payloads.size(), 2u);
}

TEST(VfxFractureTranslate, PositionCopiedPerPiece) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    ASSERT_EQ(evt.payloads.size(), 2u);
    EXPECT_FLOAT_EQ(evt.payloads[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(evt.payloads[1].position.x, 4.0f);
}

TEST(VfxFractureTranslate, VelocityCombinesImpactAndPieceVel) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    // Impact velocity = normal (1,0,0) · speed 5 = (5,0,0).
    // Piece 0 linearVel = (0,0,1) → total (5,0,1).
    EXPECT_FLOAT_EQ(evt.payloads[0].velocity.x, 5.0f);
    EXPECT_FLOAT_EQ(evt.payloads[0].velocity.z, 1.0f);
    // Piece 1 linearVel = (0,0,-2) → total (5,0,-2).
    EXPECT_FLOAT_EQ(evt.payloads[1].velocity.x,  5.0f);
    EXPECT_FLOAT_EQ(evt.payloads[1].velocity.z, -2.0f);
}

TEST(VfxFractureTranslate, TintCopiedToRGB) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    EXPECT_FLOAT_EQ(evt.payloads[0].color.x, 0.6f);
    EXPECT_FLOAT_EQ(evt.payloads[0].color.y, 0.4f);
    EXPECT_FLOAT_EQ(evt.payloads[0].color.z, 0.2f);
}

TEST(VfxFractureTranslate, AlphaIsMassFraction) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    // Piece 0 mass 3.0 / total 4.0 = 0.75
    // Piece 1 mass 1.0 / total 4.0 = 0.25
    EXPECT_NEAR(evt.payloads[0].color.w, 0.75f, 1e-5f);
    EXPECT_NEAR(evt.payloads[1].color.w, 0.25f, 1e-5f);
}

TEST(VfxFractureTranslate, ZeroMassPiecesYieldSafeAlpha) {
    // Two pieces with zero mass shouldn't divide by zero.
    VfxFractureEventInput in;
    in.impactNormal = Vec3{1, 0, 0};
    in.impactSpeed = 1.0f;
    VfxFracturePiece p0; p0.mass = 0.0f; p0.position = Vec3{0,0,0};
    VfxFracturePiece p1; p1.mass = 0.0f; p1.position = Vec3{1,0,0};
    in.pieces = {p0, p1};

    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    ASSERT_EQ(evt.payloads.size(), 2u);
    EXPECT_FLOAT_EQ(evt.payloads[0].color.w, 0.0f);
    EXPECT_FLOAT_EQ(evt.payloads[1].color.w, 0.0f);
}

TEST(VfxFractureTranslate, NegativeImpactSpeedClampedToZero) {
    VfxFractureEventInput in;
    in.impactNormal = Vec3{1, 0, 0};
    in.impactSpeed = -5.0f; // pathological
    VfxFracturePiece p; p.mass = 1.0f; p.position = Vec3{0,0,0};
    in.pieces = {p};

    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    ASSERT_EQ(evt.payloads.size(), 1u);
    // Impact velocity becomes zero because speed clamped to 0.
    EXPECT_FLOAT_EQ(evt.payloads[0].velocity.x, 0.0f);
}

TEST(VfxFractureTranslate, ZeroNormalProducesZeroImpactVelocity) {
    VfxFractureEventInput in;
    in.impactNormal = Vec3{0, 0, 0}; // degenerate
    in.impactSpeed = 10.0f;
    VfxFracturePiece p; p.mass = 1.0f;
    p.linearVelocity = Vec3{1, 2, 3};
    in.pieces = {p};

    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    // Impact = (0,0,0); piece velocity passes through.
    EXPECT_FLOAT_EQ(evt.payloads[0].velocity.x, 1.0f);
    EXPECT_FLOAT_EQ(evt.payloads[0].velocity.y, 2.0f);
    EXPECT_FLOAT_EQ(evt.payloads[0].velocity.z, 3.0f);
}

TEST(VfxFractureTranslate, NonUnitNormalNormalised) {
    VfxFractureEventInput in;
    in.impactNormal = Vec3{0, 3, 4}; // length 5
    in.impactSpeed  = 10.0f;          // target impact velocity magnitude = 10
    VfxFracturePiece p; p.mass = 1.0f;
    in.pieces = {p};

    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    const auto& v = evt.payloads[0].velocity;
    const float mag = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    EXPECT_NEAR(mag, 10.0f, 1e-4f);
    // Direction unchanged: (0, 0.6, 0.8) × 10 = (0, 6, 8).
    EXPECT_NEAR(v.y, 6.0f, 1e-4f);
    EXPECT_NEAR(v.z, 8.0f, 1e-4f);
}

TEST(VfxFractureTranslate, EventNameForwarded) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "crate_smash");
    EXPECT_EQ(evt.name, "crate_smash");
}

TEST(VfxFractureTranslate, AgeIsZero) {
    const auto in = MakeTwoPieceFracture();
    const auto evt = VfxTranslateFractureEvent(in, "fracture");
    for (const auto& p : evt.payloads) {
        EXPECT_FLOAT_EQ(p.age, 0.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bridge forwarding
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxFractureBridge, EnqueuesOnTargetQueue) {
    VFXEventQueue q;
    VfxPhysicsFractureBridge bridge(&q, "fracture");
    ASSERT_TRUE(bridge.OnFracture(MakeTwoPieceFracture()));
    EXPECT_EQ(bridge.ForwardedCount(), 1u);
    auto drained = q.Drain();
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0].name, "fracture");
    EXPECT_EQ(drained[0].payloads.size(), 2u);
}

TEST(VfxFractureBridge, NullTargetRejectsForward) {
    VfxPhysicsFractureBridge bridge(nullptr, "fracture");
    EXPECT_FALSE(bridge.OnFracture(MakeTwoPieceFracture()));
    EXPECT_EQ(bridge.ForwardedCount(), 0u);
}

TEST(VfxFractureBridge, EmptyInputDoesNotEnqueue) {
    VFXEventQueue q;
    VfxPhysicsFractureBridge bridge(&q, "fracture");
    VfxFractureEventInput empty;
    EXPECT_FALSE(bridge.OnFracture(empty));
    EXPECT_FALSE(q.HasPending());
}

TEST(VfxFractureBridge, SetTargetRebinds) {
    VFXEventQueue qA, qB;
    VfxPhysicsFractureBridge bridge(&qA, "fracture");
    bridge.OnFracture(MakeTwoPieceFracture());
    bridge.SetTarget(&qB);
    bridge.OnFracture(MakeTwoPieceFracture());
    EXPECT_EQ(qA.GetPendingCount(), 1u);
    EXPECT_EQ(qB.GetPendingCount(), 1u);
}

TEST(VfxFractureBridge, DefaultEventNameIsFracture) {
    VFXEventQueue q;
    VfxPhysicsFractureBridge bridge(&q);
    EXPECT_EQ(bridge.EventName(), "fracture");
}
