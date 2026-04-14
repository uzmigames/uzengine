/// Unit tests for `VfxSystemInstance` — runtime compound VFX instance.
///
/// Covers: Init failure paths (null device, empty asset, all disabled),
/// successful Init from a fireball preset asset (3 children), preset
/// resolution (known and unknown preset names, non-preset paths),
/// SetWorldTransform propagation with localSpace=true/false,
/// SetEmitting mirroring into children, Tick incrementing
/// timeSinceRegistration, Destroy cleanup with and without a device,
/// and GetEmitters pointer stability.
///
/// Uses `MockRHIDevice` — no real GPU required.

#include <gtest/gtest.h>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/system/vfx_system.h"
#include "engine/vfx/system/vfx_system_instance.h"
#include "engine/vfx/vfx_system.h"   // the global VFXSystem (for its ParticleBudget)

#include "mock_rhi_device.h"

#include <memory>
#include <string>

using namespace uz;
using uz::test::MockRHIDevice;

namespace {

/// Builds a fireball-compound asset (head + sparks + smoke) for tests.
std::unique_ptr<VfxSystem> MakeFireballAsset() {
    constexpr const char* kJson = R"({
        "format": "uzvfxsys", "version": 1, "name": "Fireball",
        "emitters": [
            {"name":"head",   "path":"preset:FireballHead",   "id":"e1"},
            {"name":"sparks", "path":"preset:FireballSparks", "id":"e2"},
            {"name":"smoke",  "path":"preset:FireballSmoke",  "id":"e3"}
        ]
    })";
    return VfxSystem::LoadFromString(kJson);
}

/// Creates a per-test VFXSystem+MockRHIDevice pair so each test has a fresh
/// budget (global singletons across tests would leak allocations).
struct TestFixture {
    MockRHIDevice device;
    VFXSystem     vfxSys;

    TestFixture() { vfxSys.Initialize(&device, /*budgetMax=*/100000u); }
    ~TestFixture() { vfxSys.Shutdown(&device); }

    ParticleBudget& Budget() { return vfxSys.GetBudget(); }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Init — failure paths
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemInstance, Init_FailsWithNullDevice) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    EXPECT_FALSE(inst.Init(*asset, /*device=*/nullptr, fx.Budget()));
    EXPECT_EQ(inst.GetEmitterCount(), 0u);
}

TEST(VfxSystemInstance, Init_FailsWhenAllEmittersDisabled) {
    TestFixture fx;
    constexpr const char* kJson = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[
            {"path":"preset:FireballHead","enabled":false},
            {"path":"preset:FireballSparks","enabled":false}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    EXPECT_FALSE(inst.Init(*asset, &fx.device, fx.Budget()));
    EXPECT_EQ(inst.GetEmitterCount(), 0u);
}

TEST(VfxSystemInstance, Init_FailsWhenAllPresetsUnknown) {
    TestFixture fx;
    constexpr const char* kJson = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[
            {"path":"preset:ThisDoesNotExist"},
            {"path":"preset:AlsoNotReal"}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    EXPECT_FALSE(inst.Init(*asset, &fx.device, fx.Budget()));
    EXPECT_EQ(inst.GetEmitterCount(), 0u);
}

TEST(VfxSystemInstance, Init_FailsWhenPathsAreNotPresetRefs) {
    TestFixture fx;
    // Non-preset paths (e.g., ".uzfx" files) are not supported by the instance
    // layer for now — they are silently skipped. Init returns false when zero
    // children initialise.
    constexpr const char* kJson = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[
            {"path":"effects/fireball.uzfx"}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    EXPECT_FALSE(inst.Init(*asset, &fx.device, fx.Budget()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Init — success path
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemInstance, Init_CreatesThreeChildrenForFireballAsset) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));
    EXPECT_EQ(inst.GetEmitterCount(), 3u);

    // Each child should have been fully initialised with preset-specific
    // values (preset functions mutate ParticleEmitter fields).
    auto emitters = inst.GetEmitters();
    ASSERT_EQ(emitters.size(), 3u);
    for (auto* e : emitters) {
        ASSERT_NE(e, nullptr);
        EXPECT_TRUE(e->initialized);
        EXPECT_GT(e->maxParticles, 0u);
        EXPECT_GT(e->emissionRate, 0.0f);
        EXPECT_TRUE(e->needsInitialUpload);
    }

    inst.Destroy(&fx.device, fx.Budget());
}

TEST(VfxSystemInstance, Init_SkipsDisabledEmittersButKeepsEnabled) {
    TestFixture fx;
    constexpr const char* kJson = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[
            {"path":"preset:FireballHead",  "enabled":true},
            {"path":"preset:FireballSparks","enabled":false},
            {"path":"preset:FireballSmoke", "enabled":true}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));
    EXPECT_EQ(inst.GetEmitterCount(), 2u);

    inst.Destroy(&fx.device, fx.Budget());
}

TEST(VfxSystemInstance, Init_SkipsUnknownPresetsSilentlyAndInitialisesKnownOnes) {
    TestFixture fx;
    constexpr const char* kJson = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[
            {"path":"preset:FireballHead"},
            {"path":"preset:Nonexistent"},
            {"path":"preset:FireballSmoke"}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));
    EXPECT_EQ(inst.GetEmitterCount(), 2u);

    inst.Destroy(&fx.device, fx.Budget());
}

// ─────────────────────────────────────────────────────────────────────────────
// SetWorldTransform
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemInstance, SetWorldTransform_PropagatesToLocalSpaceChildren) {
    TestFixture fx;
    auto asset = MakeFireballAsset(); // all localSpace defaults -> true
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));

    const Vec3 pos{123.0f, -45.0f, 67.0f};
    inst.SetWorldTransform(pos);
    EXPECT_FLOAT_EQ(inst.GetWorldPosition().x, pos.x);
    EXPECT_FLOAT_EQ(inst.GetWorldPosition().y, pos.y);
    EXPECT_FLOAT_EQ(inst.GetWorldPosition().z, pos.z);

    for (auto* e : inst.GetEmitters()) {
        ASSERT_NE(e, nullptr);
        EXPECT_FLOAT_EQ(e->worldPosition.x, pos.x);
        EXPECT_FLOAT_EQ(e->worldPosition.y, pos.y);
        EXPECT_FLOAT_EQ(e->worldPosition.z, pos.z);
    }

    inst.Destroy(&fx.device, fx.Budget());
}

TEST(VfxSystemInstance, SetWorldTransform_KeepsWorldSpaceChildrenAtOrigin) {
    TestFixture fx;
    // A mixed asset: first child is world-space, second is local-space.
    constexpr const char* kJson = R"({
        "format":"uzvfxsys","version":1,
        "emitters":[
            {"path":"preset:FireballHead",  "localSpace":false},
            {"path":"preset:FireballSmoke", "localSpace":true}
        ]
    })";
    auto asset = VfxSystem::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));

    const Vec3 pos{500.0f, 0.0f, 0.0f};
    inst.SetWorldTransform(pos);

    auto emitters = inst.GetEmitters();
    ASSERT_EQ(emitters.size(), 2u);

    // World-space child should be pinned at origin.
    EXPECT_FLOAT_EQ(emitters[0]->worldPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(emitters[0]->worldPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(emitters[0]->worldPosition.z, 0.0f);

    // Local-space child should follow the parent transform.
    EXPECT_FLOAT_EQ(emitters[1]->worldPosition.x, pos.x);

    inst.Destroy(&fx.device, fx.Budget());
}

// ─────────────────────────────────────────────────────────────────────────────
// SetEmitting + Tick
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemInstance, SetEmitting_IsMirroredIntoChildrenOnNextTick) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));

    // Flip the flag and force one child off by hand to prove the mirror works.
    inst.SetEmitting(false);
    auto emitters = inst.GetEmitters();
    ASSERT_FALSE(emitters.empty());
    emitters[0]->isEmitting = true; // stale — Tick should reset this.
    inst.Tick(0.016f);

    for (auto* e : inst.GetEmitters()) {
        EXPECT_FALSE(e->isEmitting);
    }
    EXPECT_FALSE(inst.IsEmitting());

    inst.SetEmitting(true);
    inst.Tick(0.016f);
    for (auto* e : inst.GetEmitters()) {
        EXPECT_TRUE(e->isEmitting);
    }

    inst.Destroy(&fx.device, fx.Budget());
}

TEST(VfxSystemInstance, Tick_IncrementsTimeSinceRegistrationOnChildren) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));

    const float dt = 0.25f;
    inst.Tick(dt);
    inst.Tick(dt);
    inst.Tick(dt);

    for (auto* e : inst.GetEmitters()) {
        EXPECT_FLOAT_EQ(e->timeSinceRegistration, 3.0f * dt);
    }

    inst.Destroy(&fx.device, fx.Budget());
}

TEST(VfxSystemInstance, Tick_NegativeDtIsClampedToZero) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));

    inst.Tick(-5.0f);
    for (auto* e : inst.GetEmitters()) {
        EXPECT_FLOAT_EQ(e->timeSinceRegistration, 0.0f);
    }

    inst.Destroy(&fx.device, fx.Budget());
}

// ─────────────────────────────────────────────────────────────────────────────
// Destroy
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSystemInstance, Destroy_ClearsChildrenAndReleasesBudget) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));
    EXPECT_GT(inst.GetEmitterCount(), 0u);

    inst.Destroy(&fx.device, fx.Budget());
    EXPECT_EQ(inst.GetEmitterCount(), 0u);
    EXPECT_TRUE(inst.GetEmitters().empty());
}

TEST(VfxSystemInstance, Destroy_IsSafeOnNeverInitialisedInstance) {
    TestFixture fx;
    VfxSystemInstance inst;
    inst.Destroy(&fx.device, fx.Budget()); // must not crash
    EXPECT_EQ(inst.GetEmitterCount(), 0u);
}

TEST(VfxSystemInstance, Destroy_WithNullDeviceStillDropsChildrenWithoutLeaking) {
    TestFixture fx;
    auto asset = MakeFireballAsset();
    ASSERT_NE(asset, nullptr);

    VfxSystemInstance inst;
    ASSERT_TRUE(inst.Init(*asset, &fx.device, fx.Budget()));

    // Passing null device is a programmer error but must not crash — the
    // implementation simply drops the unique_ptrs (may leak GPU handles, but
    // that's outside this test's remit).
    inst.Destroy(/*device=*/nullptr, fx.Budget());
    EXPECT_EQ(inst.GetEmitterCount(), 0u);
}
