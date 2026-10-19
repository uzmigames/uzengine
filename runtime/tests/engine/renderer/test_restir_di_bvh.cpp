#include "../../../shaders/runtime/include/restir_math.hlsli"
#include <gtest/gtest.h>
#include <algorithm>

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
