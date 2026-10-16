/// Tier-1 CPU parity for the helpers actually shipped in path_tracer_ps.hlsl.
/// Fixed seeds make statistical regressions reproducible without a GPU or RHI.
#include "pt_rng_mirror.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>

using namespace uz::pt_mirror;

namespace {
constexpr std::size_t kSamples = 1'000'000;
}

/// Sequence recorded by running the C++ mirror from seed 1 with vendored Zig.
TEST(PathTracerParity, WangHashKnownSequence) {
    constexpr std::array<std::uint32_t, 5> expected{
        663891101u, 1738326990u, 801461103u, 3205955024u, 2068514225u};
    std::uint32_t state = 1u;
    for (const auto value : expected) {
        EXPECT_EQ(WangHash(state), value);
        EXPECT_EQ(state, value);
    }
}

/// Rand01 preserves the shader's uint-to-float conversion and state advance.
TEST(PathTracerParity, Rand01KnownSequence) {
    constexpr std::array<std::uint32_t, 5> expected{
        663891101u, 1738326990u, 801461103u, 3205955024u, 2068514225u};
    std::uint32_t state = 1u;
    for (const auto value : expected) {
        EXPECT_FLOAT_EQ(Rand01(state), static_cast<float>(value) / 4294967296.0f);
        EXPECT_EQ(state, value);
    }
}

/// A uniform sphere has zero mean and unit length at every sampled point.
TEST(PathTracerParity, RandUnitVectorIsUniformAndUnitLength) {
    std::uint32_t state = 1u;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    for (std::size_t i = 0; i < kSamples; ++i) {
        const Vec3 dir = RandUnitVector(state);
        const double length = std::sqrt(
            double(dir.x) * dir.x + double(dir.y) * dir.y + double(dir.z) * dir.z);
        ASSERT_NEAR(length, 1.0, 1e-4) << "sample " << i;
        sumX += dir.x;
        sumY += dir.y;
        sumZ += dir.z;
    }
    const double meanLength = std::sqrt(
        sumX * sumX + sumY * sumY + sumZ * sumZ) / kSamples;
    EXPECT_LT(meanLength, 3.0 / std::sqrt(double(kSamples)));
}

/// Include terminated paths as zero; survivor-only averaging would be biased.
/// Unequal channels also pin max-channel selection, not just scalar roulette.
TEST(PathTracerParity, RussianRouletteIsUnbiased) {
    constexpr Vec3 input{0.3f, 0.15f, 0.075f};
    std::uint32_t state = 1u;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    for (std::size_t i = 0; i < kSamples; ++i) {
        Vec3 throughput = input;
        if (RussianRoulette(throughput, state)) {
            sumX += throughput.x;
            sumY += throughput.y;
            sumZ += throughput.z;
        }
    }
    EXPECT_NEAR(sumX / kSamples, input.x, input.x * 0.01);
    EXPECT_NEAR(sumY / kSamples, input.y, input.y * 0.01);
    EXPECT_NEAR(sumZ / kSamples, input.z, input.z * 0.01);
}

/// Check the shader expression normalize(N + RandUnitVector), N=(0,0,1).
/// The continuous construction gives a cosine lobe (E[cos]=2/3); finite RNG
/// and floating-point normalization approximate it. This checks hemisphere
/// support and lobe moments, not pointwise CPU/GPU PDF or intrinsic equality.
TEST(PathTracerParity, NormalizedSumLobeIsCenteredInHemisphere) {
    std::uint32_t state = 1u;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumZ = 0.0;
    for (std::size_t i = 0; i < kSamples; ++i) {
        Vec3 dir = RandUnitVector(state);
        dir.z += 1.0f;
        const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        ASSERT_GT(length, 0.0f) << "sample " << i;
        dir.x /= length;
        dir.y /= length;
        dir.z /= length;
        ASSERT_GE(dir.z, 0.0f) << "sample " << i;
        sumX += dir.x;
        sumY += dir.y;
        sumZ += dir.z;
    }
    const double meanX = sumX / kSamples;
    const double meanY = sumY / kSamples;
    const double meanZ = sumZ / kSamples;
    EXPECT_LT(std::hypot(meanX, meanY), 3.0 / std::sqrt(double(kSamples)));
    EXPECT_NEAR(meanZ, 2.0 / 3.0, 0.01);
    EXPECT_NEAR(std::sqrt(meanX * meanX + meanY * meanY + meanZ * meanZ),
                2.0 / 3.0, 0.01);
}
