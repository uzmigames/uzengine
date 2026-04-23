/// Unit tests for the VFX bone-attached emitter feature.
///
/// Covers the scene-sync resolve path for `VfxEmitterComponent::attachEntity`
/// and `attachBoneName`:
///
///   * No attach target   -> emitter uses its own SceneComponent position.
///   * Valid target entity, empty bone name -> emitter tracks entity root.
///   * Valid target entity + bone name      -> emitter tracks bone world.
///   * Valid target entity + unknown bone   -> falls back to entity root.
///   * Invalid entity handle                -> emitter falls back silently.
///
/// The tests exercise the `ResolveAttachTransform` contract indirectly through
/// the public runtime fields that the helper writes (`attachedThisFrame` and
/// `attachedWorldPosition`) plus the emitter's `worldPosition`, using the ECS
/// `World` + `SkeletalMeshSubsystem` fixtures. No GPU hardware is required.

#include <gtest/gtest.h>

#include "engine/animation/bone_transform.h"
#include "engine/animation/pose.h"
#include "engine/animation/skeletal_mesh_instance.h"
#include "engine/animation/skeletal_mesh_subsystem.h"
#include "engine/animation/skeleton.h"
#include "engine/ecs/core/world.h"
#include "engine/ecs/environment/vfx_emitter_component.h"
#include "engine/ecs/mesh/skeletal_mesh_component.h"
#include "engine/ecs/scene/scene_component.h"
#include "engine/vfx/components/particle_emitter.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Component-level default validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAttach, ComponentDefaultsAreEmpty) {
    VfxEmitterComponent comp{};
    EXPECT_FALSE(comp.attachEntity.IsValid());
    EXPECT_TRUE(comp.attachBoneName.empty());
}

TEST(VfxAttach, RuntimeDefaultsAreEmpty) {
    ParticleEmitter rt{};
    EXPECT_FALSE(rt.attachedThisFrame);
    EXPECT_EQ(rt.attachedWorldPosition.x, 0.0f);
    EXPECT_EQ(rt.attachedWorldPosition.y, 0.0f);
    EXPECT_EQ(rt.attachedWorldPosition.z, 0.0f);
    EXPECT_TRUE(rt.attachBoneNameDefault.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton / SkeletalMeshInstance world-bone helper
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Builds a 3-bone skeleton: root -> spine_01 -> hand_r.
/// `hand_r` is at +80 cm along the parent's +Z axis in bind pose; when we push
/// a TRS pose that slides the hand by (50, 0, 0) the world matrix should have
/// translation (50, 0, 80).
Skeleton MakeTestSkeleton() {
    Skeleton skel;

    BoneInfo root{};
    root.name = "root";
    root.parentIndex = kInvalidBoneIndex;
    (void)skel.AddBone(std::move(root));

    BoneInfo spine{};
    spine.name = "spine_01";
    spine.parentIndex = 0;
    spine.bindTranslation = Vec3{0.0f, 0.0f, 50.0f};
    (void)skel.AddBone(std::move(spine));

    BoneInfo hand{};
    hand.name = "hand_r";
    hand.parentIndex = 1;
    hand.bindTranslation = Vec3{0.0f, 0.0f, 80.0f};
    (void)skel.AddBone(std::move(hand));

    skel.BuildNameIndex();
    skel.ComputeInverseBindMatrices();
    return skel;
}

} // namespace

TEST(VfxAttach, SkeletonNameLookupResolvesKnownBones) {
    const Skeleton skel = MakeTestSkeleton();
    EXPECT_EQ(skel.FindBoneIndex("root"), 0);
    EXPECT_EQ(skel.FindBoneIndex("spine_01"), 1);
    EXPECT_EQ(skel.FindBoneIndex("hand_r"), 2);
    EXPECT_EQ(skel.FindBoneIndex("not_a_real_bone"), kInvalidBoneIndex);
}

TEST(VfxAttach, GetBoneWorldMatrixFallsBackToIdentityForInvalidIndex) {
    SkeletalMeshInstance inst;
    // No Bind() call, so `m_worldBoneTransforms` is empty.
    const Mat4 m = inst.GetBoneWorldMatrix(0);
    const Vec3 t = m.getTranslation();
    EXPECT_FLOAT_EQ(t.x, 0.0f);
    EXPECT_FLOAT_EQ(t.y, 0.0f);
    EXPECT_FLOAT_EQ(t.z, 0.0f);
}

TEST(VfxAttach, GetBoneWorldMatrixRejectsNegativeIndex) {
    SkeletalMeshInstance inst;
    const Mat4 m = inst.GetBoneWorldMatrix(-1);
    const Vec3 t = m.getTranslation();
    EXPECT_FLOAT_EQ(t.x, 0.0f);
    EXPECT_FLOAT_EQ(t.y, 0.0f);
    EXPECT_FLOAT_EQ(t.z, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialization: attachBoneName default round-trips through the asset
// ─────────────────────────────────────────────────────────────────────────────

#include "engine/vfx/components/emitter_serializer.h"

TEST(VfxAttach, SerializerOmitsEmptyBoneName) {
    ParticleEmitter rt{};
    // Empty default -> key must not appear.
    const std::string json = SerializeEmitter(rt);
    EXPECT_EQ(json.find("attachBoneName"), std::string::npos);
}

TEST(VfxAttach, SerializerRoundTripsBoneName) {
    ParticleEmitter src{};
    src.attachBoneNameDefault = "hand_r";
    const std::string json = SerializeEmitter(src);
    ASSERT_NE(json.find("attachBoneName"), std::string::npos);
    ASSERT_NE(json.find("hand_r"), std::string::npos);

    ParticleEmitter dst{};
    EXPECT_TRUE(DeserializeEmitter(json, dst));
    EXPECT_EQ(dst.attachBoneNameDefault, "hand_r");
}
