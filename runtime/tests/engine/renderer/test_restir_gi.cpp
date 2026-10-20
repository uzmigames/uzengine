#include "pathtracing/pipeline_path_tracer_pass.h"
#include "../../../shaders/runtime/include/restir_math.hlsli"
#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <vector>

namespace {
// Targets are the area-measure one-bounce integrand luminance (f cos L_o cos2/d^2 V)
// of three secondary vertices at a receiving surface and at a history source.
using Target = std::array<float, 3>;
constexpr unsigned kNull = 3;
struct Outcome {
    unsigned sample;
    float W;
    double probability;
};
using Distribution = std::vector<Outcome>;

// One BSDF continuation: each vertex with probability (1 - miss) / 3 and
// W = 1 / p_A, or the null outcome (environment/unsupported hit) with
// probability miss. M = 1 either way: the null outcome still owns its domain.
Distribution Canonical(const Target& target, double miss) {
    Distribution result;
    for (unsigned y = 0; y < 3; ++y)
        result.push_back({y, target[y] > 0.f ? float(3. / (1. - miss)) : 0.f, (1. - miss) / 3.});
    result.push_back({kNull, 0.f, miss});
    return result;
}

enum class Mode { Corrected, Biased, SkipWhenNull };

float At(const Target& t, unsigned sample) { return sample < 3 ? t[sample] : 0.f; }

// restir_gi_temporal_cs: canonical (M = 1) plus a capped history (M), both
// candidates evaluated at BOTH surfaces, generalized balance MIS.
Distribution Combine(const Distribution& canonical, const Target& target,
                     const Distribution& reused, const Target& source, unsigned M,
                     Mode mode = Mode::Corrected) {
    Distribution output;
    for (const auto& a : canonical) {
        for (const auto& b : reused) {
            const double probability = a.probability * b.probability;
            if (mode == Mode::SkipWhenNull && a.sample == kNull) {
                output.push_back({kNull, 0.f, probability});
                continue;
            }
            const float ta = At(target, a.sample), sa = At(source, a.sample);
            const float tb = At(target, b.sample), sb = At(source, b.sample);
            const float ma = mode == Mode::Biased ? 1.f / float(M + 1u)
                : uz::RestirMIS(ta, 1u, ta + float(M) * sa);
            const float mb = mode == Mode::Biased ? float(M) / float(M + 1u)
                : uz::RestirMIS(sb, M, tb + float(M) * sb);
            const float wa = uz::RestirReuseWeight(ta, a.W, ma);
            const float wb = uz::RestirReuseWeight(tb, b.W, mb);
            const float sum = wa + wb;
            if (sum == 0.f) {
                output.push_back({kNull, 0.f, probability});
                continue;
            }
            if (wa > 0.f)
                output.push_back({a.sample, uz::RestirReuseFinalize(sum, ta), probability * wa / sum});
            if (wb > 0.f)
                output.push_back({b.sample, uz::RestirReuseFinalize(sum, tb), probability * wb / sum});
        }
    }
    return output;
}

double Integral(const Distribution& distribution, const Target& integrand) {
    double result = 0.;
    for (const auto& outcome : distribution)
        result += outcome.probability * outcome.W * At(integrand, outcome.sample);
    return result;
}

double Variance(const Distribution& distribution, const Target& integrand) {
    double mean = 0., square = 0.;
    for (const auto& outcome : distribution) {
        const double estimate = outcome.W * At(integrand, outcome.sample);
        mean += outcome.probability * estimate;
        square += outcome.probability * estimate * estimate;
    }
    return square - mean * mean;
}
} // namespace

TEST(RestirGIStage1, ReservoirLayoutsPreserveRootBudget) {
    EXPECT_EQ(sizeof(uz::RestirGISample), 88u);
    EXPECT_EQ(offsetof(uz::RestirGISample, lightIndex), 12u);
    EXPECT_EQ(offsetof(uz::RestirGISample, lightValue), 76u);
    EXPECT_EQ(sizeof(uz::RestirGIReservoir), 160u);
    EXPECT_EQ(offsetof(uz::RestirGIReservoir, weightSum), 88u);
    EXPECT_EQ(offsetof(uz::RestirGIReservoir, M), 92u);
    EXPECT_EQ(offsetof(uz::RestirGIReservoir, W), 96u);
    EXPECT_EQ(offsetof(uz::RestirGIReservoir, surfacePosition), 100u);
    EXPECT_EQ(offsetof(uz::RestirGIReservoir, visibility), 156u);
    EXPECT_EQ(sizeof(uz::RestirGIRoot), 8u * sizeof(uint32_t));
    // The DI reservoir and the private DXR root are untouched.
    EXPECT_EQ(sizeof(uz::RestirReservoir), 116u);
    EXPECT_EQ(sizeof(uz::PipelinePathRoot), 24u * sizeof(uint32_t));
}

TEST(RestirGIStage1, CanonicalWeightReproducesPlainEstimator) {
    // W = 1/p_A: every integrable one-bounce contribution is exact with no reuse,
    // and the null outcome contributes nothing.
    const Target target{1, 2, 4};
    const auto canonical = Canonical(target, 0.25);
    EXPECT_NEAR(Integral(canonical, target), 7., 1e-6);
    for (unsigned y = 0; y < 3; ++y) {
        Target basis{};
        basis[y] = 1.f;
        EXPECT_NEAR(Integral(canonical, basis), 1., 1e-6);
    }
}

TEST(RestirGIStage1, TemporalCombineStaysUnbiasedWithNullCanonical) {
    // Different receiving/source supports and a 25% continuation miss rate:
    // the corrected combine integrates every basis function on the receiving
    // support exactly, so every integrable contribution is exact.
    const Target target{1, 2, 4}, source{9, 0, 1};
    const auto canonical = Canonical(target, 0.25), reused = Canonical(source, 0.4);
    for (unsigned M : {1u, 19u}) {
        const auto corrected = Combine(canonical, target, reused, source, M);
        EXPECT_NEAR(Integral(corrected, target), 7., 1e-5);
        for (unsigned y = 0; y < 3; ++y) {
            Target basis{};
            basis[y] = 1.f;
            EXPECT_NEAR(Integral(corrected, basis), 1., 1e-5);
        }
    }
}

TEST(RestirGIStage1, BiasedCombinesDriftAsNegativeControls) {
    const Target target{1, 2, 4}, source{9, 0, 1};
    const auto canonical = Canonical(target, 0.25), reused = Canonical(source, 0.4);
    // Uniform 1/(M+1) weights ignore the support mismatch: vertex 1 is only
    // reachable from the receiver, so half of its mass is lost.
    const auto biased = Combine(canonical, target, reused, source, 1, Mode::Biased);
    EXPECT_NEAR(Integral(biased, target), 6., 1e-5);
    // Treating a missed continuation as an absent domain (no reuse) scales the
    // history terms by the hit rate: basis 0 loses miss * m_history.
    const auto skipped = Combine(canonical, target, reused, source, 1, Mode::SkipWhenNull);
    Target basis0{1, 0, 0};
    const double history = 9. / (1. + 9.);
    EXPECT_NEAR(Integral(skipped, basis0), 1. - 0.25 * history, 1e-5);
    EXPECT_LT(Integral(skipped, target), 7. - 1e-3);
}

TEST(RestirGIStage1, RecursiveHistoryRetainsContributionInvariant) {
    Target previous{9, 0, 1};
    Distribution history = Canonical(previous, 0.3);
    const std::array<Target, 4> targets{{{1, 2, 4}, {0, 5, 2}, {3, 1, 0}, {1, 2, 4}}};
    for (const auto& current : targets) {
        // Capping changes only the MIS preference, never the already-correct W.
        history = Combine(Canonical(current, 0.3), current, history, previous, 19u);
        previous = current;
        for (unsigned y = 0; y < 3; ++y) {
            Target basis{};
            basis[y] = 1.f;
            EXPECT_NEAR(Integral(history, basis), current[y] > 0.f ? 1. : 0., 2e-5);
        }
    }
}

TEST(RestirGIStage1, TemporalReuseLowersExactVarianceAtEqualMean) {
    const Target target{1, 2, 4};
    const auto canonical = Canonical(target, 0.);
    const auto combined = Combine(canonical, target, Canonical(target, 0.), target, 1u);
    EXPECT_NEAR(Integral(canonical, target), 7., 1e-6);
    EXPECT_NEAR(Integral(combined, target), 7., 1e-6);
    // Two equally weighted independent candidates halve the estimator variance.
    EXPECT_NEAR(Variance(combined, target), 0.5 * Variance(canonical, target), 1e-5);
    EXPECT_GT(Variance(canonical, target), 0.);
}
