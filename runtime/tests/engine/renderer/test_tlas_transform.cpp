#include "raytracing/tlas_manager.h"
#include <gtest/gtest.h>

TEST(TLASInstanceTransform, TranslationIsInDXRLastColumn) {
    float transform[12]{};
    uz::TLASManager::WriteInstanceTransform(transform, uz::Mat4::Translation(17.f, -23.f, 509.f));
    EXPECT_FLOAT_EQ(transform[3], 17.f);
    EXPECT_FLOAT_EQ(transform[7], -23.f);
    EXPECT_FLOAT_EQ(transform[11], 509.f);
    EXPECT_FLOAT_EQ(transform[0], 1.f);
    EXPECT_FLOAT_EQ(transform[5], 1.f);
    EXPECT_FLOAT_EQ(transform[10], 1.f);
}

TEST(TLASInstanceTransform, RotatedScaledPointsMatchEngineWorldTransform) {
    const auto world = uz::Mat4::Scaling(2.f, 3.f, 4.f)
        * uz::Mat4::RotationZ(0.7f) * uz::Mat4::Translation(17.f, -23.f, 509.f);
    float transform[12]{};
    uz::TLASManager::WriteInstanceTransform(transform, world);
    const uz::Vec3 point(5.f, -7.f, 11.f);
    const auto expected = world * point;
    EXPECT_NEAR(transform[0] * point.x + transform[1] * point.y
        + transform[2] * point.z + transform[3], expected.x, 1e-5f);
    EXPECT_NEAR(transform[4] * point.x + transform[5] * point.y
        + transform[6] * point.z + transform[7], expected.y, 1e-5f);
    EXPECT_NEAR(transform[8] * point.x + transform[9] * point.y
        + transform[10] * point.z + transform[11], expected.z, 1e-5f);
}
