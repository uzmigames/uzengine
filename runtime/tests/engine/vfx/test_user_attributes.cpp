/// Unit tests for the VFX user-declared per-particle attribute feature.
///
/// Covers:
///   * `UserAttributeDef` default construction.
///   * Quota enforcement on `RegisterUserAttribute` (4 float, 2 vec4).
///   * Idempotent re-declaration of an existing name.
///   * Type-mismatch rejection when a name is re-declared with a different type.
///   * Slot assignment is monotonic starting from 0 for each type independently.
///   * `FindUserAttribute` lookup returns null when not present.
///   * Serializer round-trip through `emitter_serializer` JSON.
///
/// No GPU allocations are exercised here; the buffer allocation path is
/// validated indirectly through the integration demos + scene sync.

#include <gtest/gtest.h>

#include "engine/vfx/asset/vfx_graph.h"
#include "engine/vfx/components/emitter_serializer.h"
#include "engine/vfx/components/particle_emitter.h"

using namespace uz;

TEST(VfxUserAttrs, DefaultDefIsFloatZero) {
    UserAttributeDef d;
    EXPECT_EQ(d.type, UserAttributeType::Float);
    EXPECT_EQ(d.defaultFloat, 0.0f);
    EXPECT_EQ(d.slotIndex, UINT32_MAX);
    EXPECT_TRUE(d.name.empty());
}

TEST(VfxUserAttrs, RegisterAssignsSequentialSlotsByType) {
    ParticleEmitter e;
    UserAttributeDef f1; f1.name = "A"; f1.type = UserAttributeType::Float;
    UserAttributeDef f2; f2.name = "B"; f2.type = UserAttributeType::Float;
    UserAttributeDef v1; v1.name = "C"; v1.type = UserAttributeType::Vec4;
    UserAttributeDef v2; v2.name = "D"; v2.type = UserAttributeType::Vec4;

    EXPECT_EQ(e.RegisterUserAttribute(f1), 0u);
    EXPECT_EQ(e.RegisterUserAttribute(f2), 1u);
    EXPECT_EQ(e.RegisterUserAttribute(v1), 0u);
    EXPECT_EQ(e.RegisterUserAttribute(v2), 1u);
    ASSERT_EQ(e.userAttributes.size(), 4u);
}

TEST(VfxUserAttrs, RegisterReturnsExistingSlotOnReDeclaration) {
    ParticleEmitter e;
    UserAttributeDef a; a.name = "Mass"; a.type = UserAttributeType::Float;
    a.defaultFloat = 1.0f;
    const uint32_t slot = e.RegisterUserAttribute(a);
    ASSERT_NE(slot, UINT32_MAX);

    // Re-declaration with the same name + type leaves the slot assigned.
    UserAttributeDef clone = a;
    clone.defaultFloat = 999.0f; // ignored — original is preserved
    EXPECT_EQ(e.RegisterUserAttribute(clone), slot);
    ASSERT_EQ(e.userAttributes.size(), 1u);
    // The stored entry's default stays from the first registration.
    EXPECT_FLOAT_EQ(e.userAttributes[0].defaultFloat, 1.0f);
}

TEST(VfxUserAttrs, RegisterRejectsTypeMismatch) {
    ParticleEmitter e;
    UserAttributeDef a; a.name = "Mass"; a.type = UserAttributeType::Float;
    ASSERT_NE(e.RegisterUserAttribute(a), UINT32_MAX);

    UserAttributeDef bad; bad.name = "Mass"; bad.type = UserAttributeType::Vec4;
    EXPECT_EQ(e.RegisterUserAttribute(bad), UINT32_MAX);
    // Original entry untouched.
    ASSERT_EQ(e.userAttributes.size(), 1u);
    EXPECT_EQ(e.userAttributes[0].type, UserAttributeType::Float);
}

TEST(VfxUserAttrs, RegisterRejectsQuotaOverflowFloat) {
    ParticleEmitter e;
    for (uint32_t i = 0; i < kMaxUserFloatAttrs; ++i) {
        UserAttributeDef a;
        a.name = "F" + std::to_string(i);
        a.type = UserAttributeType::Float;
        EXPECT_NE(e.RegisterUserAttribute(a), UINT32_MAX);
    }
    UserAttributeDef overflow;
    overflow.name = "FExcess";
    overflow.type = UserAttributeType::Float;
    EXPECT_EQ(e.RegisterUserAttribute(overflow), UINT32_MAX);
}

TEST(VfxUserAttrs, RegisterRejectsQuotaOverflowVec4) {
    ParticleEmitter e;
    for (uint32_t i = 0; i < kMaxUserVec4Attrs; ++i) {
        UserAttributeDef a;
        a.name = "V" + std::to_string(i);
        a.type = UserAttributeType::Vec4;
        EXPECT_NE(e.RegisterUserAttribute(a), UINT32_MAX);
    }
    UserAttributeDef overflow;
    overflow.name = "VExcess";
    overflow.type = UserAttributeType::Vec4;
    EXPECT_EQ(e.RegisterUserAttribute(overflow), UINT32_MAX);
}

TEST(VfxUserAttrs, FindUserAttributeReturnsMatchOrNull) {
    ParticleEmitter e;
    UserAttributeDef a; a.name = "Drag"; a.type = UserAttributeType::Float;
    a.defaultFloat = 0.25f;
    e.RegisterUserAttribute(a);

    const UserAttributeDef* found = e.FindUserAttribute("Drag");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, UserAttributeType::Float);
    EXPECT_FLOAT_EQ(found->defaultFloat, 0.25f);
    EXPECT_EQ(e.FindUserAttribute("not_present"), nullptr);
}

TEST(VfxUserAttrs, SerializerRoundTrip) {
    ParticleEmitter src;
    UserAttributeDef f; f.name = "Mass"; f.type = UserAttributeType::Float;
    f.defaultFloat = 1.5f;
    UserAttributeDef v; v.name = "CustomColor"; v.type = UserAttributeType::Vec4;
    v.defaultVec4 = Vec4{0.2f, 0.4f, 0.8f, 1.0f};
    src.RegisterUserAttribute(f);
    src.RegisterUserAttribute(v);

    const std::string json = SerializeEmitter(src);
    ASSERT_NE(json.find("userAttributes"), std::string::npos);
    ASSERT_NE(json.find("Mass"), std::string::npos);
    ASSERT_NE(json.find("CustomColor"), std::string::npos);

    ParticleEmitter dst;
    ASSERT_TRUE(DeserializeEmitter(json, dst));
    ASSERT_EQ(dst.userAttributes.size(), 2u);

    const UserAttributeDef* m = dst.FindUserAttribute("Mass");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->type, UserAttributeType::Float);
    EXPECT_FLOAT_EQ(m->defaultFloat, 1.5f);

    const UserAttributeDef* c = dst.FindUserAttribute("CustomColor");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, UserAttributeType::Vec4);
    EXPECT_FLOAT_EQ(c->defaultVec4.x, 0.2f);
    EXPECT_FLOAT_EQ(c->defaultVec4.y, 0.4f);
    EXPECT_FLOAT_EQ(c->defaultVec4.z, 0.8f);
    EXPECT_FLOAT_EQ(c->defaultVec4.w, 1.0f);
}
