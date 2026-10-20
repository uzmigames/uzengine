#include <gtest/gtest.h>

// Test the exact scalar implementation used by cloud_composite_ps.hlsl.
#include "../../../shaders/runtime/atmosphere/cloud_aerial_math.hlsli"

TEST(CloudAerialPerspective, TransparentCloudDoesNotAddAtmosphere) {
    EXPECT_FLOAT_EQ(CloudAtmosphereRadiance(0.0f, 0.3f, 8.0f, 1.0f), 0.0f);
}

TEST(CloudAerialPerspective, ZeroDistancePreservesCloudRadiance) {
    EXPECT_FLOAT_EQ(CloudAtmosphereRadiance(4.0f, 1.0f, 0.0f, 0.2f), 4.0f);
}

TEST(CloudAerialPerspective, OpaqueDistantCloudApproachesInscatter) {
    EXPECT_FLOAT_EQ(CloudAtmosphereRadiance(4.0f, 0.0f, 0.7f, 0.0f), 0.7f);
}

TEST(CloudAerialPerspective, InscatterIsPremultipliedByCloudOpacity) {
    EXPECT_FLOAT_EQ(CloudAtmosphereRadiance(4.0f, 0.5f, 0.8f, 0.25f), 2.6f);
}

TEST(CloudAerialPerspective, SegmentUsesStartOverEndNotInverse) {
    EXPECT_FLOAT_EQ(CloudSegmentTransmittance(0.25f, 0.5f), 0.5f);
}

TEST(CloudAerialPerspective, SegmentCompositionMatchesWholePath) {
    const float first = CloudSegmentTransmittance(0.125f, 0.25f);
    const float second = CloudSegmentTransmittance(0.25f, 0.5f);
    EXPECT_FLOAT_EQ(first * second, CloudSegmentTransmittance(0.125f, 0.5f));
}

TEST(CloudAerialPerspective, ZeroLengthSegmentIsIdentityEvenAtLutFloor) {
    EXPECT_FLOAT_EQ(CloudSegmentTransmittance(0.0f, 0.0f), 1.0f);
}
