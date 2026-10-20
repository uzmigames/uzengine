#include "../../../shaders/runtime/include/restir_math.hlsli"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>

TEST(RestirDIStage3, BVHSourceDensityPreservesUnbiasedRISWeight) {
    // Exact enumeration of two candidates from a nonuniform light hierarchy.
    // Contributions/targets are constant over each triangle, but deliberately
    // differ: neither the source density nor the RIS target is the integrand.
    const float selection[] = {.1f, .3f, .6f};
    const float area[] = {2.f, 3.f, 4.f};
    const float target[] = {1.f, 4.f, 2.f};
    const float contribution[] = {2.f, 3.f, 7.f};
    double estimate = 0.0, missingPdfControl = 0.0;
    for (unsigned a = 0; a < 3; ++a) {
        for (unsigned b = 0; b < 3; ++b) {
            const float sourcePdfA = selection[a] / area[a];
            const float sourcePdfB = selection[b] / area[b];
            const float wa = uz::RestirCandidateWeight(target[a], sourcePdfA);
            const float wb = uz::RestirCandidateWeight(target[b], sourcePdfB);
            float sum = 0.f;
            EXPECT_TRUE(uz::RestirStream(sum, wa, .5f));
            const float probabilityB = wb / (wa + wb);
            EXPECT_TRUE(uz::RestirStream(sum, wb, .5f * probabilityB));
            float otherSum = wa;
            EXPECT_FALSE(uz::RestirStream(otherSum, wb, .5f * (1.f + probabilityB)));
            const double sequenceProbability = double(selection[a]) * selection[b];
            const double conditional =
                (1.0 - probabilityB) * uz::RestirWeight(sum, 2u, target[a]) * contribution[a]
                + probabilityB * uz::RestirWeight(sum, 2u, target[b]) * contribution[b];
            estimate += sequenceProbability * conditional;

            // Negative control: omit the BVH source density from the weights.
            const float biasedSum = target[a] + target[b];
            const float biasedProbabilityB = target[b] / biasedSum;
            missingPdfControl += sequenceProbability *
                ((1.0 - biasedProbabilityB) * uz::RestirWeight(biasedSum, 2u, target[a]) * contribution[a]
                 + biasedProbabilityB * uz::RestirWeight(biasedSum, 2u, target[b]) * contribution[b]);
        }
    }
    EXPECT_NEAR(estimate, 41.0, 1e-5); // Integral = sum(area * contribution).
    EXPECT_NEAR(missingPdfControl, 5.3, 1e-5);
    EXPECT_GT(estimate - missingPdfControl, 35.0);

    // Nonidentical stratified proposals use their equally weighted mixture
    // (the original BVH density), not a second factor of 32 in the RIS weight.
    // Integrate each stratum/light intersection exactly, including boundaries.
    const double cdf[] = {0.0, .1, .4, 1.0};
    double stratifiedEstimate = 0.0;
    for (unsigned stratum = 0; stratum < 32; ++stratum) {
        for (unsigned light = 0; light < 3; ++light) {
            const double overlap = std::max(0.0,
                std::min(double(stratum + 1) / 32.0, cdf[light + 1])
                - std::max(double(stratum) / 32.0, cdf[light]));
            const float weight = uz::RestirCandidateWeight(target[light], selection[light] / area[light]);
            stratifiedEstimate += overlap * weight * contribution[light] / target[light];
        }
    }
    EXPECT_NEAR(stratifiedEstimate, 41.0, 1e-5);
}

TEST(RestirDIStage3, VisibilityReusePreservesSupportAndRejectsOccludedHistory) {
    const float pdf[] = {.2f, .5f, .3f};
    const float unshadowed[2][3] = {{1.f, 8.f, 3.f}, {9.f, 2.f, 4.f}};
    // Different source supports model jitter across a shadow boundary. The
    // history cap is an MIS preference, never another division applied to W.
    const float visibility[2][3] = {{1.f, 0.f, 1.f}, {0.f, 1.f, 1.f}};
    std::array<double, 3> basis{}, inconsistentControl{};
    for (unsigned a = 0; a < 3; ++a) {
        for (unsigned b = 0; b < 3; ++b) {
            const unsigned selected[] = {a, b};
            float target[2][3];
            for (unsigned i = 0; i < 2; ++i)
                for (unsigned y = 0; y < 3; ++y)
                    target[i][y] = uz::RestirVisibilityTarget(unshadowed[i][y], visibility[i][y]);
            float weights[2], wrongWeights[2];
            float sum = 0.f, wrongSum = 0.f;
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned y = selected[i], M = i == 0 ? 1u : 19u;
                // Initial unshadowed RIS followed by selected-sample visibility.
                // The null result keeps its domain and M in both denominators.
                const float initialSum = uz::RestirCandidateWeight(unshadowed[i][y], pdf[y]);
                const float W = uz::RestirWeight(initialSum, 1u, unshadowed[i][y]) * visibility[i][y];
                weights[i] = uz::RestirReuseWeight(target[0][y], W,
                    uz::RestirMIS(target[i][y], M, target[0][y] + 19.f * target[1][y]));
                wrongWeights[i] = uz::RestirReuseWeight(target[0][y], W,
                    uz::RestirMIS(unshadowed[i][y], M, unshadowed[0][y] + 19.f * unshadowed[1][y]));
                uz::RestirStream(sum, weights[i], .5f);
                uz::RestirStream(wrongSum, wrongWeights[i], .5f);
            }
            const double sequenceProbability = double(pdf[a]) * pdf[b];
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned y = selected[i];
                if (weights[i] > 0.f)
                    basis[y] += sequenceProbability * weights[i] / sum
                        * uz::RestirReuseFinalize(sum, target[0][y]);
                if (wrongWeights[i] > 0.f)
                    inconsistentControl[y] += sequenceProbability * wrongWeights[i] / wrongSum
                        * uz::RestirReuseFinalize(wrongSum, target[0][y]);
            }
        }
    }
    EXPECT_NEAR(basis[0], 1.0, 1e-6);
    EXPECT_DOUBLE_EQ(basis[1], 0.0);
    EXPECT_NEAR(basis[2], 1.0, 1e-6);
    // Rejecting occlusion but retaining the old unshadowed MIS denominator
    // loses almost all light 0: its historical domain cannot sample it.
    EXPECT_NEAR(inconsistentControl[0], 1.0 / 172.0, 1e-6);
    EXPECT_NEAR(inconsistentControl[2], 1.0, 1e-6);
}

TEST(RestirDIStage3, VisibilityReuseReducesVarianceAgainstUnshadowedControl) {
    const float pdf[] = {.2f, .5f, .3f};
    const float target[] = {1.f, 8.f, 3.f};
    const float visibility[] = {1.f, 0.f, 0.f};
    double means[2]{}, moments[2]{};
    // Enumerate all source draws and final reservoir selections. Mode 0 is
    // the pre-fix estimator: visibility only in final shading, never reuse.
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (unsigned a = 0; a < 3; ++a) {
            for (unsigned b = 0; b < 3; ++b) {
                const unsigned selected[] = {a, b};
                float weights[2], targets[2], sum = 0.f;
                for (unsigned i = 0; i < 2; ++i) {
                    const unsigned y = selected[i];
                    targets[i] = uz::RestirVisibilityTarget(target[y], mode ? visibility[y] : 1.f);
                    float W = uz::RestirWeight(uz::RestirCandidateWeight(target[y], pdf[y]), 1u, target[y]);
                    if (mode) W *= visibility[y];
                    weights[i] = uz::RestirReuseWeight(targets[i], W,
                        uz::RestirMIS(targets[i], 1u, 2.f * targets[i]));
                    uz::RestirStream(sum, weights[i], .5f);
                }
                for (unsigned i = 0; i < 2; ++i) {
                    if (weights[i] == 0.f) continue;
                    const unsigned y = selected[i];
                    const double probability = double(pdf[a]) * pdf[b] * weights[i] / sum;
                    const double estimate = uz::RestirReuseFinalize(sum, targets[i]) * target[y] * visibility[y];
                    means[mode] += probability * estimate;
                    moments[mode] += probability * estimate * estimate;
                }
            }
        }
    }
    EXPECT_NEAR(means[0], 1.0, 1e-6);
    EXPECT_NEAR(means[1], 1.0, 1e-6);
    EXPECT_NEAR(moments[1] - means[1] * means[1], 2.0, 1e-6);
    EXPECT_GT(moments[0] - means[0] * means[0], 6.0);
    EXPECT_LT(moments[1], moments[0]);
}
