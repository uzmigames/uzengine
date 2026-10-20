#include "pathtracing/pipeline_path_tracer_pass.h"
#include "../../../shaders/runtime/include/restir_math.hlsli"
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace {
using Target = std::array<float, 3>;
struct Outcome {
    unsigned sample;
    float W;
    double probability;
};
using Distribution = std::vector<Outcome>;

// Enumerate one uniform initial candidate, including the null outcome when
// its source target vanishes. No random trials or convergence tolerances.
Distribution Initial(const Target& target) {
    Distribution result;
    for (unsigned y = 0; y < 3; ++y)
        result.push_back({y, target[y] > 0.f ? 3.f : 0.f, 1. / 3.});
    return result;
}

Distribution Combine(const Distribution& canonical, const Target& target,
                     const Distribution& reused, const Target& source, unsigned M,
                     bool corrected = true) {
    Distribution output;
    for (const auto& a : canonical) {
        for (const auto& b : reused) {
            const float ma = corrected
                ? uz::RestirMIS(target[a.sample], 1u, target[a.sample] + M * source[a.sample])
                : 1.f / float(M + 1u);
            const float mb = corrected
                ? uz::RestirMIS(source[b.sample], M, target[b.sample] + M * source[b.sample])
                : float(M) / float(M + 1u);
            const float wa = uz::RestirReuseWeight(target[a.sample], a.W, ma);
            const float wb = uz::RestirReuseWeight(target[b.sample], b.W, mb);
            const float sum = wa + wb;
            const double probability = a.probability * b.probability;
            if (sum == 0.f) {
                output.push_back({0, 0.f, probability});
                continue;
            }
            if (wa > 0.f)
                output.push_back({a.sample, uz::RestirReuseFinalize(sum, target[a.sample]),
                                  probability * wa / sum});
            if (wb > 0.f)
                output.push_back({b.sample, uz::RestirReuseFinalize(sum, target[b.sample]),
                                  probability * wb / sum});
        }
    }
    return output;
}

double Integral(const Distribution& distribution, const Target& integrand) {
    double result = 0.;
    for (const auto& outcome : distribution)
        result += outcome.probability * outcome.W * integrand[outcome.sample];
    return result;
}
} // namespace

TEST(RestirDIStage2, ProvenanceLayoutPreservesRootBudget) {
    EXPECT_EQ(sizeof(uz::RestirReservoir), 116u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, visibility), 112u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, surfaceView), 80u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, surfaceRoughness), 92u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, surfaceAlbedo), 96u);
    EXPECT_EQ(offsetof(uz::RestirReservoir, surfaceMetallic), 108u);
    EXPECT_EQ(sizeof(uz::PipelinePathRoot), 24u * sizeof(uint32_t));
}

TEST(RestirDIStage2, MISPartitionsUnityAcrossDifferentSupports) {
    const std::array<Target, 5> domains{{{1, 2, 4}, {9, 0, 1}, {0, 3, 0}, {2, 0, 0}, {0, 0, 8}}};
    const std::array<unsigned, 5> count{1, 19, 3, 7, 20};
    for (unsigned y = 0; y < 3; ++y) {
        float denominator = 0.f;
        for (unsigned i = 0; i < domains.size(); ++i)
            denominator += count[i] * domains[i][y];
        float total = 0.f;
        for (unsigned i = 0; i < domains.size(); ++i) {
            const float mis = uz::RestirMIS(domains[i][y], count[i], denominator);
            total += mis;
            if (domains[i][y] == 0.f) EXPECT_FLOAT_EQ(mis, 0.f);
        }
        EXPECT_NEAR(total, 1.f, 1e-6f);
    }
    EXPECT_FLOAT_EQ(uz::RestirMIS(0.f, 19u, 0.f), 0.f);
    EXPECT_FLOAT_EQ(uz::RestirReuseFinalize(0.f, 0.f), 0.f);
}

TEST(RestirDIStage2, ExactEnumerationRemovesBiasedCombineDrift) {
    const Target target{1, 2, 4}, source{9, 0, 1};
    const auto canonical = Initial(target), reused = Initial(source);
    const auto corrected = Combine(canonical, target, reused, source, 1);
    const auto biased = Combine(canonical, target, reused, source, 1, false);
    EXPECT_NEAR(Integral(corrected, target), 7., 1e-6);
    EXPECT_NEAR(Integral(biased, target), 6., 1e-6); // -14.2857% support-loss bias.
    // Stronger than matching the target's integral: every basis function is
    // correct, so every integrable direct-light contribution is correct.
    for (unsigned y = 0; y < 3; ++y) {
        Target basis{};
        basis[y] = 1.f;
        EXPECT_NEAR(Integral(corrected, basis), 1., 1e-6);
    }
}

TEST(RestirDIStage2, RecursiveTemporalReuseRetainsContributionInvariant) {
    Target previous{9, 0, 1};
    Distribution history = Initial(previous);
    const std::array<Target, 4> targets{{{1, 2, 4}, {0, 5, 2}, {3, 1, 0}, {1, 2, 4}}};
    for (const auto& current : targets) {
        // Capping changes only the MIS preference, not the already-correct W.
        history = Combine(Initial(current), current, history, previous, 19u);
        previous = current;
        for (unsigned y = 0; y < 3; ++y) {
            Target basis{};
            basis[y] = 1.f;
            EXPECT_NEAR(Integral(history, basis), current[y] > 0.f ? 1. : 0., 2e-6);
        }
    }
}

TEST(RestirDIStage2, CorrelatedSpatialInputsRemainUnbiased) {
    const Target target{1, 2, 4};
    const auto input = Initial(target);
    double expectation = 0.;
    // Perfectly correlated copies, not a fictitious independent joint PDF.
    for (const auto& sample : input) {
        const float mis = uz::RestirMIS(target[sample.sample], 20u, 40.f * target[sample.sample]);
        const float weight = uz::RestirReuseWeight(target[sample.sample], sample.W, mis);
        expectation += sample.probability * target[sample.sample]
            * uz::RestirReuseFinalize(2.f * weight, target[sample.sample]);
    }
    EXPECT_NEAR(expectation, 7., 1e-6);
}
