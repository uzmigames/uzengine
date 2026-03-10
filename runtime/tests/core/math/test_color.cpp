#include <gtest/gtest.h>
#include "uz_math.h"
#include "uz_color.h"

using namespace uz;

TEST(LinearColorTest, DefaultIsBlackOpaque) {
    LinearColor c;
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(LinearColorTest, NamedColors) {
    LinearColor white = LinearColor::White();
    EXPECT_FLOAT_EQ(white.r, 1.0f);
    EXPECT_FLOAT_EQ(white.g, 1.0f);
    EXPECT_FLOAT_EQ(white.b, 1.0f);

    LinearColor red = LinearColor::Red();
    EXPECT_FLOAT_EQ(red.r, 1.0f);
    EXPECT_FLOAT_EQ(red.g, 0.0f);
    EXPECT_FLOAT_EQ(red.b, 0.0f);
}

TEST(LinearColorTest, Arithmetic) {
    LinearColor a(0.2f, 0.3f, 0.4f);
    LinearColor b(0.1f, 0.2f, 0.3f);
    LinearColor sum = a + b;
    EXPECT_NEAR(sum.r, 0.3f, 1e-5f);
    EXPECT_NEAR(sum.g, 0.5f, 1e-5f);
    EXPECT_NEAR(sum.b, 0.7f, 1e-5f);

    LinearColor scaled = a * 2.0f;
    EXPECT_NEAR(scaled.r, 0.4f, 1e-5f);
}

TEST(LinearColorTest, Lerp) {
    LinearColor a = LinearColor::Black();
    LinearColor b = LinearColor::White();
    LinearColor mid = LinearColor::Lerp(a, b, 0.5f);
    EXPECT_NEAR(mid.r, 0.5f, 1e-5f);
    EXPECT_NEAR(mid.g, 0.5f, 1e-5f);
    EXPECT_NEAR(mid.b, 0.5f, 1e-5f);
}

TEST(sRGBColorTest, DefaultIsBlackOpaque) {
    sRGBColor c;
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 255);
}

TEST(sRGBColorTest, FromHex) {
    sRGBColor c = sRGBColor::FromHex(0xFF8040FF);
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.g, 128);
    EXPECT_EQ(c.b, 64);
    EXPECT_EQ(c.a, 255);
}

TEST(sRGBColorTest, ToHex) {
    sRGBColor c(255, 128, 64, 255);
    EXPECT_EQ(c.ToHex(), 0xFF8040FF);
}

TEST(ColorConversionTest, SRGBToLinearRoundTrip) {
    sRGBColor original(128, 64, 200, 255);
    LinearColor linear = original.ToLinear();
    sRGBColor back = linear.ToSRGB();
    EXPECT_NEAR(back.r, original.r, 1);
    EXPECT_NEAR(back.g, original.g, 1);
    EXPECT_NEAR(back.b, original.b, 1);
    EXPECT_EQ(back.a, original.a);
}

TEST(ColorConversionTest, LinearBlackIsZero) {
    sRGBColor black(0, 0, 0, 255);
    LinearColor linear = black.ToLinear();
    EXPECT_FLOAT_EQ(linear.r, 0.0f);
    EXPECT_FLOAT_EQ(linear.g, 0.0f);
    EXPECT_FLOAT_EQ(linear.b, 0.0f);
}

TEST(ColorConversionTest, LinearWhiteIsOne) {
    sRGBColor white(255, 255, 255, 255);
    LinearColor linear = white.ToLinear();
    EXPECT_NEAR(linear.r, 1.0f, 1e-3f);
    EXPECT_NEAR(linear.g, 1.0f, 1e-3f);
    EXPECT_NEAR(linear.b, 1.0f, 1e-3f);
}

TEST(ColorConversionTest, SRGBMidGrayIsNonLinear) {
    sRGBColor mid(128, 128, 128, 255);
    LinearColor linear = mid.ToLinear();
    // sRGB mid gray (128) should be roughly 0.216 in linear
    EXPECT_GT(linear.r, 0.2f);
    EXPECT_LT(linear.r, 0.25f);
}
