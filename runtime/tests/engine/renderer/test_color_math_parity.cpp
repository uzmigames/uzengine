/// Unit tests for the SSR temporal pass's YCoCg colour conversion
/// (phase2_ssr-stochastic-refactor T.7).
///
/// All tests are pure CPU math — no GPU device, no RHI backend. The functions under test are
/// the shader's own text: `shaders/runtime/common/color_ycocg.hlsli` compiles as HLSL into
/// `postprocess/ssr_temporal_cs.hlsl` (RGBToYCoCg / YCoCgToRGB wrap it) and as C++ here, the
/// nrd_sdk_math.hlsli pattern, so what this file pins is what the GPU runs.
///
/// # Test Groups
/// - `SsrYCoCg` — RGB -> YCoCg -> RGB round trip (identity within 1e-5), known values of the
///   primaries / white / grey, bit-exactness on dyadic inputs, the unscaled-form coefficients.
///
/// # Reference
/// - Malvar & Sullivan 2003 "YCoCg-R: A Color Space with RGB Reversibility and Low Dynamic Range"
/// - Karis 2014 "High Quality Temporal Supersampling" — history clipping in YCoCg

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {
#include "../../../shaders/runtime/common/color_ycocg.hlsli"

struct Rgb { float r, g, b; };
struct YCoCg { float y, co, cg; };

YCoCg Forward(const Rgb& c) {
    return {SsrYCoCgLumaY(c.r, c.g, c.b), SsrYCoCgChromaCo(c.r, c.g, c.b), SsrYCoCgChromaCg(c.r, c.g, c.b)};
}

Rgb Inverse(const YCoCg& v) {
    return {SsrYCoCgToR(v.y, v.co, v.cg), SsrYCoCgToG(v.y, v.co, v.cg), SsrYCoCgToB(v.y, v.co, v.cg)};
}

Rgb RoundTrip(const Rgb& c) { return Inverse(Forward(c)); }
} // namespace

// RGB -> YCoCg -> RGB is the identity within 1e-5 over the unit cube (11^3 grid, corners included).
TEST(SsrYCoCg, RoundTripIsIdentityWithin1e5OnTheUnitCube) {
    for (int i = 0; i <= 10; ++i) {
        for (int j = 0; j <= 10; ++j) {
            for (int k = 0; k <= 10; ++k) {
                const Rgb c{i / 10.0f, j / 10.0f, k / 10.0f};
                const Rgb rt = RoundTrip(c);
                EXPECT_NEAR(rt.r, c.r, 1e-5f) << "rgb=(" << c.r << "," << c.g << "," << c.b << ")";
                EXPECT_NEAR(rt.g, c.g, 1e-5f) << "rgb=(" << c.r << "," << c.g << "," << c.b << ")";
                EXPECT_NEAR(rt.b, c.b, 1e-5f) << "rgb=(" << c.r << "," << c.g << "," << c.b << ")";
            }
        }
    }
}

// The temporal pass clips unbounded linear HDR radiance, and a clipped chroma can carry a
// negative channel before the shader's max(..., 0): the identity holds there too, to 1e-5 of the
// largest magnitude (float rounding scales with the value).
TEST(SsrYCoCg, RoundTripIsIdentityForHdrAndNegativeValues) {
    const Rgb colors[] = {
        {12.0f, 0.5f, 3.0f}, {1.0e4f, 0.0f, 0.0f}, {0.0f, 2500.0f, 60.0f},
        {-1.0f, 0.5f, 2.0f}, {0.001f, 0.002f, 0.003f}, {7.25f, 7.25f, 7.25f},
    };
    for (const Rgb& c : colors) {
        const float scale = std::max({1.0f, std::fabs(c.r), std::fabs(c.g), std::fabs(c.b)});
        const Rgb rt = RoundTrip(c);
        EXPECT_NEAR(rt.r, c.r, 1e-5f * scale) << "rgb=(" << c.r << "," << c.g << "," << c.b << ")";
        EXPECT_NEAR(rt.g, c.g, 1e-5f * scale) << "rgb=(" << c.r << "," << c.g << "," << c.b << ")";
        EXPECT_NEAR(rt.b, c.b, 1e-5f * scale) << "rgb=(" << c.r << "," << c.g << "," << c.b << ")";
    }
}

// Known values: white is pure luma 1, greys have no chroma, and the primaries land on the
// published columns of the unscaled matrix (Malvar & Sullivan 2003).
TEST(SsrYCoCg, KnownValuesOfPrimariesWhiteAndGrey) {
    const YCoCg white = Forward({1.0f, 1.0f, 1.0f});
    EXPECT_FLOAT_EQ(white.y, 1.0f);
    EXPECT_FLOAT_EQ(white.co, 0.0f);
    EXPECT_FLOAT_EQ(white.cg, 0.0f);

    const YCoCg grey = Forward({0.18f, 0.18f, 0.18f});
    EXPECT_NEAR(grey.y, 0.18f, 1e-6f);
    EXPECT_NEAR(grey.co, 0.0f, 1e-7f);
    EXPECT_NEAR(grey.cg, 0.0f, 1e-7f);

    const YCoCg black = Forward({0.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(black.y, 0.0f);
    EXPECT_FLOAT_EQ(black.co, 0.0f);
    EXPECT_FLOAT_EQ(black.cg, 0.0f);

    const YCoCg red = Forward({1.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(red.y, 0.25f);
    EXPECT_FLOAT_EQ(red.co, 0.5f);
    EXPECT_FLOAT_EQ(red.cg, -0.25f);

    const YCoCg green = Forward({0.0f, 1.0f, 0.0f});
    EXPECT_FLOAT_EQ(green.y, 0.5f);
    EXPECT_FLOAT_EQ(green.co, 0.0f);
    EXPECT_FLOAT_EQ(green.cg, 0.5f);

    const YCoCg blue = Forward({0.0f, 0.0f, 1.0f});
    EXPECT_FLOAT_EQ(blue.y, 0.25f);
    EXPECT_FLOAT_EQ(blue.co, -0.5f);
    EXPECT_FLOAT_EQ(blue.cg, -0.25f);
}

// Every coefficient is a power of two, so dyadic inputs (integers, quarter steps) round-trip
// bit-exactly in float — the property that lets the shader clip in YCoCg without a drift floor.
TEST(SsrYCoCg, DyadicInputsRoundTripExactly) {
    const YCoCg v = Forward({3.0f, 5.0f, 7.0f});
    EXPECT_FLOAT_EQ(v.y, 5.0f);    // 0.75 + 2.5 + 1.75
    EXPECT_FLOAT_EQ(v.co, -2.0f);  // 1.5 - 3.5
    EXPECT_FLOAT_EQ(v.cg, 0.0f);   // -0.75 + 2.5 - 1.75

    const Rgb colors[] = {{3.0f, 5.0f, 7.0f}, {0.25f, 0.75f, 2.5f}, {1024.0f, 512.0f, 256.0f}, {0.5f, 0.0f, 0.125f}};
    for (const Rgb& c : colors) {
        const Rgb rt = RoundTrip(c);
        EXPECT_FLOAT_EQ(rt.r, c.r);
        EXPECT_FLOAT_EQ(rt.g, c.g);
        EXPECT_FLOAT_EQ(rt.b, c.b);
    }
}

// The unscaled Karis form: luma weights sum to 1 (white maps to Y = 1, not 4 as in the 4x-scaled
// TAA variant), chroma weights sum to 0 (greys have no chroma), and the transform is linear.
TEST(SsrYCoCg, UnscaledKarisForm) {
    EXPECT_FLOAT_EQ(SsrYCoCgLumaY(1.0f, 1.0f, 1.0f), 1.0f);
    for (int i = 0; i <= 8; ++i) {
        const float g = i / 8.0f;
        EXPECT_FLOAT_EQ(SsrYCoCgLumaY(g, g, g), g);
        EXPECT_FLOAT_EQ(SsrYCoCgChromaCo(g, g, g), 0.0f);
        EXPECT_FLOAT_EQ(SsrYCoCgChromaCg(g, g, g), 0.0f);
    }
    const Rgb a{0.3f, 0.6f, 0.9f};
    const YCoCg va = Forward(a);
    const YCoCg v2a = Forward({2.0f * a.r, 2.0f * a.g, 2.0f * a.b});
    EXPECT_NEAR(v2a.y, 2.0f * va.y, 1e-6f);
    EXPECT_NEAR(v2a.co, 2.0f * va.co, 1e-6f);
    EXPECT_NEAR(v2a.cg, 2.0f * va.cg, 1e-6f);
}
