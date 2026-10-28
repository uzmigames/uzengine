/// Screen-space ReSTIR GI (Lumen-parity phase C, r.GI.ReSTIR): the three passes of
/// ssgi_restir_{initial,temporal,spatial}_cs.hlsl modelled on a discrete sample space and
/// enumerated exactly over restir_math.hlsli. A sample is a direction shared by every receiver
/// (the shift map is the identity in direction space); a receiver (domain) generates it with
/// the cosine density and scores it with the target lum(L) * cos/pi of its own replay, zero
/// outside its own support (below its hemisphere, or a replay that returns nothing). Unbiased
/// means: for every receiver, E[W * f(y)] equals the receiver's own gather mean for every basis
/// radiance — the estimate the plain gather converges to.
#include "gi/ssgi_renderer.h"
#include "../../../shaders/runtime/include/restir_math.hlsli"
#include "../../../shaders/runtime/ssgi/ssgi_sample_validity.hlsli"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <vector>

TEST(SsgiGatherValidity, ExcludesUnavailableBlackButCountsGenuinelyUnlitSamples) {
    // The exact GPU weight function is shared by spatial, temporal and upsample taps.
    const auto reconstruct = [](float blackSampleCount, bool enabled) {
        const float litWeight = uz::SsgiAvailableWeight(0.25f, 1.0f, enabled);
        const float blackWeight = uz::SsgiAvailableWeight(0.75f, blackSampleCount, enabled);
        return (4.0f * litWeight + 0.0f * blackWeight) / (litWeight + blackWeight);
    };
    EXPECT_FLOAT_EQ(reconstruct(0.0f, true), 4.0f);
    EXPECT_FLOAT_EQ(reconstruct(1.0f, true), 1.0f);
    EXPECT_FLOAT_EQ(reconstruct(0.0f, false), 1.0f);
    EXPECT_FLOAT_EQ(uz::SsgiAvailableWeight(1.0f, 0.0f, true), 0.0f);
    EXPECT_FLOAT_EQ(uz::SsgiAvailableWeight(1.0f, 64.0f, true), 1.0f);
}

namespace {
constexpr unsigned kSamples = 3;
constexpr unsigned kNull = kSamples;   ///< exhausted trace: no sample, M still counts it
using Lum = std::array<float, kSamples>;
using Geometry = std::array<double, kSamples>;

/// One receiver: the cosine density of each direction at its surface (cos/pi, zero below the
/// hemisphere) and whether the direction's replay from there returns radiance at all (`visible`:
/// the direction is in the receiver's support). The density of the generating ray is G * V; the
/// rest of the mass is the null outcome (a ray that hit something black or exhausted its budget).
struct Domain {
    Geometry geometry;
    std::array<bool, kSamples> visible;
    double Density(unsigned y) const { return y < kSamples && visible[y] ? geometry[y] : 0.; }
    double Miss() const {
        double sum = 0.;
        for (unsigned y = 0; y < kSamples; ++y) sum += Density(y);
        return 1. - sum;
    }
};

/// lum(L) * G * V, the committed target (SsgiRestirVisibleTarget / SsgiRestirOwnTarget).
float Target(const Domain& d, const Lum& lum, unsigned y) {
    return y < kSamples ? lum[y] * float(d.Density(y)) : 0.f;
}
/// The same target with the visibility ignored at the evaluating domain (negative control).
float UnshadowedTarget(const Domain& d, const Lum& lum, unsigned y) {
    return y < kSamples ? lum[y] * float(d.geometry[y]) : 0.f;
}

struct Outcome {
    unsigned sample;
    float W;
    double probability;
};
using Distribution = std::vector<Outcome>;

Distribution Merge(const Distribution& in) {
    Distribution out;
    for (const auto& o : in) {
        bool merged = false;
        for (auto& e : out) {
            if (e.sample == o.sample && e.W == o.W) { e.probability += o.probability; merged = true; break; }
        }
        if (!merged) out.push_back(o);
    }
    return out;
}

/// ssgi_restir_initial_cs: N cosine rays streamed into one reservoir, candidate weight lum(L) on
/// the support (target / density, the geometry term common to both) and W = sum / (N p_hat).
/// Weighted reservoir sampling selects candidate i with probability w_i / sum w, so the tuple's
/// selection distribution is enumerated instead of its random stream.
Distribution Initial(const Domain& d, const Lum& lum, unsigned N) {
    Distribution out;
    std::vector<unsigned> tuple(N, 0u);
    std::function<void(unsigned, double)> recurse = [&](unsigned i, double probability) {
        if (i == N) {
            float sum = 0.f;
            for (unsigned y : tuple) sum += y < kSamples ? lum[y] : 0.f;   // weight 0 off the support
            if (sum <= 0.f) { out.push_back({kNull, 0.f, probability}); return; }
            for (unsigned y : tuple) {
                if (y == kNull) continue;
                const float weight = lum[y];
                out.push_back({y, uz::RestirWeight(sum, N, Target(d, lum, y)), probability * weight / sum});
            }
            return;
        }
        for (unsigned y = 0; y <= kNull; ++y) {
            const double p = y < kSamples ? d.Density(y) : d.Miss();
            if (p <= 0.) continue;
            tuple[i] = y;
            recurse(i + 1, probability * p);
        }
    };
    recurse(0, 1.);
    return Merge(out);
}

enum class Mode { Corrected, UniformMis, UnshadowedCrossTargets };

float CrossTarget(const Domain& d, const Lum& lum, unsigned y, Mode mode) {
    return mode == Mode::UnshadowedCrossTargets ? UnshadowedTarget(d, lum, y) : Target(d, lum, y);
}

/// ssgi_restir_temporal_cs: the canonical reservoir (M = N, even when every candidate missed)
/// and the capped history (M), targets at BOTH domains, generalized balance MIS
/// m_i = M_i p_i / sum_j M_j p_j, W = sum / p_hat(selected) at the receiver.
Distribution Temporal(const Distribution& canonical, const Domain& receiver, unsigned N,
                      const Distribution& history, const Domain& source, unsigned M,
                      const Lum& lum, Mode mode = Mode::Corrected) {
    Distribution out;
    for (const auto& a : canonical) {
        for (const auto& b : history) {
            const double probability = a.probability * b.probability;
            const float currentHere = Target(receiver, lum, a.sample);
            const float currentThere = CrossTarget(source, lum, a.sample, mode);
            const float oldHere = CrossTarget(receiver, lum, b.sample, mode);
            const float oldThere = Target(source, lum, b.sample);
            const float misCurrent = mode == Mode::UniformMis
                ? float(N) / float(N + M)
                : uz::RestirMIS(currentHere, N, float(N) * currentHere + float(M) * currentThere);
            const float misOld = mode == Mode::UniformMis
                ? float(M) / float(N + M)
                : uz::RestirMIS(oldThere, M, float(N) * oldHere + float(M) * oldThere);
            const float wa = uz::RestirReuseWeight(currentHere, a.W, misCurrent);
            const float wb = uz::RestirReuseWeight(oldHere, b.W, misOld);
            const float sum = wa + wb;
            if (sum <= 0.f) { out.push_back({kNull, 0.f, probability}); continue; }
            if (wa > 0.f) out.push_back({a.sample, uz::RestirReuseFinalize(sum, currentHere), probability * wa / sum});
            if (wb > 0.f) out.push_back({b.sample, uz::RestirReuseFinalize(sum, oldHere), probability * wb / sum});
        }
    }
    return Merge(out);
}

struct Candidate {
    const Distribution* reservoir;
    const Domain* domain;
    unsigned M;
};

float PairwiseMis(float numerator, float denominator) {
    return denominator > 0.f ? numerator / denominator : 0.f;
}

/// ssgi_restir_spatial_cs: candidate 0 is the pixel's own reservoir (M_c), candidates 1..K its
/// neighbours; each neighbour's direction is replayed at the receiver and the canonical direction
/// at each neighbour, pairwise MIS with M weighting:
///   m_k = M_k p_k(y_k) / (K M_k p_k(y_k) + M_c p_c(y_k)),
///   m_c = (1/K) sum_k M_c p_c(y_c) / (K M_k p_k(y_c) + M_c p_c(y_c)),
/// resolve at the receiver (candidate 0's domain).
Distribution Spatial(const std::vector<Candidate>& candidates, const Lum& lum, Mode mode = Mode::Corrected) {
    Distribution out;
    unsigned totalM = 0;
    for (const auto& c : candidates) totalM += c.M;
    const size_t K = candidates.size() - 1;
    const Domain& receiver = *candidates[0].domain;
    const float Mc = float(candidates[0].M);
    std::vector<const Outcome*> picked(candidates.size(), nullptr);
    std::function<void(size_t, double)> recurse = [&](size_t k, double probability) {
        if (k == candidates.size()) {
            std::vector<float> weights(candidates.size(), 0.f);
            std::vector<float> here(candidates.size(), 0.f);
            float sum = 0.f;
            const unsigned yc = picked[0]->sample;
            here[0] = Target(receiver, lum, yc);
            float misCanonical = 0.f;
            for (size_t i = 1; i <= K; ++i) {
                const float there = CrossTarget(*candidates[i].domain, lum, yc, mode);
                misCanonical += mode == Mode::UniformMis ? Mc / float(totalM)
                    : PairwiseMis(Mc * here[0], float(K) * float(candidates[i].M) * there + Mc * here[0]);
            }
            misCanonical /= float(K);
            weights[0] = uz::RestirReuseWeight(here[0], picked[0]->W, misCanonical);
            sum += weights[0];
            for (size_t i = 1; i <= K; ++i) {
                const unsigned y = picked[i]->sample;
                here[i] = CrossTarget(receiver, lum, y, mode);
                const float there = Target(*candidates[i].domain, lum, y);
                const float Mk = float(candidates[i].M);
                const float mis = mode == Mode::UniformMis ? Mk / float(totalM)
                    : PairwiseMis(Mk * there, float(K) * Mk * there + Mc * here[i]);
                weights[i] = uz::RestirReuseWeight(here[i], picked[i]->W, mis);
                sum += weights[i];
            }
            if (sum <= 0.f) { out.push_back({kNull, 0.f, probability}); return; }
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (weights[i] <= 0.f) continue;
                out.push_back({picked[i]->sample, uz::RestirReuseFinalize(sum, here[i]), probability * weights[i] / sum});
            }
            return;
        }
        for (const auto& o : *candidates[k].reservoir) {
            picked[k] = &o;
            recurse(k + 1, probability * o.probability);
        }
    };
    recurse(0, 1.);
    return Merge(out);
}

/// E[W * f(y)] for the basis radiance e_basis at `receiver`: f(y) = [y == basis] * G_R(y) V_R(y).
double Estimate(const Distribution& distribution, const Domain& receiver, unsigned basis) {
    double result = 0.;
    for (const auto& o : distribution) {
        if (o.sample != basis) continue;
        result += o.probability * o.W * receiver.Density(basis);
    }
    return result;
}
/// What the plain gather converges to for that basis: its own cosine density at the sample.
double Exact(const Domain& receiver, unsigned basis) { return receiver.Density(basis); }

double Variance(const Distribution& distribution, const Domain& receiver, const Lum& lum) {
    double mean = 0., square = 0.;
    for (const auto& o : distribution) {
        const double estimate = o.sample < kSamples ? o.W * lum[o.sample] * receiver.Density(o.sample) : 0.;
        mean += o.probability * estimate;
        square += o.probability * estimate * estimate;
    }
    return square - mean * mean;
}

double TotalProbability(const Distribution& distribution) {
    double sum = 0.;
    for (const auto& o : distribution) sum += o.probability;
    return sum;
}

void ExpectExact(const Distribution& distribution, const Domain& receiver, double tolerance, const char* what) {
    // Selection probabilities are ratios of float reuse weights: the mass sums to 1 within 1e-7.
    EXPECT_NEAR(TotalProbability(distribution), 1., 1e-6) << what;
    for (unsigned y = 0; y < kSamples; ++y)
        EXPECT_NEAR(Estimate(distribution, receiver, y), Exact(receiver, y), tolerance) << what << " basis " << y;
}

const Lum kLum{1.f, 2.f, 4.f};
// The receiver sees all three samples; the source cannot see sample 1 (its support differs) and
// weighs the others differently (a different surface, so different cosines and distances).
const Domain kReceiver{{0.3, 0.3, 0.2}, {true, true, true}};
const Domain kSource{{0.5, 0.1, 0.3}, {true, false, true}};
} // namespace

TEST(SsgiRestirGI, ReservoirAndRootLayoutsMatchTheShaders) {
    EXPECT_EQ(sizeof(uz::SSGIRestirReservoir), 64u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, sampleDirection), 0u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, flags), 12u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, sampleRadiance), 16u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, sampleDistance), 28u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, surfacePosition), 32u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, surfaceNormal), 44u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, weightSum), 48u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, M), 52u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, W), 56u);
    EXPECT_EQ(offsetof(uz::SSGIRestirReservoir, surfaceGeometricNormal), 60u);
    // 16 of the compute root signature's 24 root DWORDs; the shared PerFrameCB does not grow.
    EXPECT_EQ(sizeof(uz::SSGIRestirRootConstants), 16u * sizeof(uint32_t));
    EXPECT_LE(sizeof(uz::SSGIRestirRootConstants), 24u * sizeof(uint32_t));
}

TEST(SsgiRestirGI, InitialCandidatesReproduceThePlainGatherMean) {
    // N cosine rays into one reservoir: every basis radiance integrates to the receiver's own
    // cosine density, which is what the plain gather's mean of N rays converges to. The
    // luminance of a single estimate is sum_i lum(L_i) / N whichever candidate is kept.
    for (unsigned N : {1u, 2u, 3u}) {
        const auto initial = Initial(kReceiver, kLum, N);
        ExpectExact(initial, kReceiver, 1e-6, "initial");
    }
    const auto source = Initial(kSource, kLum, 2);
    ExpectExact(source, kSource, 1e-6, "initial at the source");
    EXPECT_NEAR(Estimate(source, kSource, 1), 0., 1e-12);   // invisible from the source: no mass
}

TEST(SsgiRestirGI, TemporalCombineStaysUnbiasedAcrossSupports) {
    // The history comes from a surface that cannot see sample 1; the generalized balance MIS
    // gives sample 1 to the canonical reservoir alone and shares 0 and 2, so every basis at the
    // receiver stays exact for any history M (the cap only moves the MIS preference).
    const unsigned N = 2;
    const auto canonical = Initial(kReceiver, kLum, N);
    const auto history = Initial(kSource, kLum, N);
    for (unsigned M : {N, 5u * N, 20u * N}) {
        const auto combined = Temporal(canonical, kReceiver, N, history, kSource, M, kLum);
        ExpectExact(combined, kReceiver, 1e-5, "temporal");
    }
}

TEST(SsgiRestirGI, NullCanonicalStillOwnsItsDomain) {
    // A receiver whose rays mostly miss: the canonical reservoir keeps M = N, so the history's
    // MIS share is never scaled by the hit rate (the shader's `candidates` is r.M, not the hits).
    const Domain sparse{{0.05, 0.05, 0.05}, {true, true, true}};
    const unsigned N = 2;
    const auto canonical = Initial(sparse, kLum, N);
    const auto history = Initial(kSource, kLum, N);
    const auto combined = Temporal(canonical, sparse, N, history, kSource, 10u * N, kLum);
    ExpectExact(combined, sparse, 1e-5, "sparse temporal");
}

TEST(SsgiRestirGI, BiasedCombinesDriftAsNegativeControls) {
    const unsigned N = 2;
    const auto canonical = Initial(kReceiver, kLum, N);
    const auto history = Initial(kSource, kLum, N);
    // Uniform M/(N+M) weights ignore that sample 1 is reachable from the receiver only: part of
    // its mass is handed to a reservoir that can never produce it, and it is under-estimated.
    const auto uniform = Temporal(canonical, kReceiver, N, history, kSource, N, kLum, Mode::UniformMis);
    EXPECT_LT(Estimate(uniform, kReceiver, 1), Exact(kReceiver, 1) - 1e-3);
    // Cross-domain targets without visibility credit the source with a sample it cannot see: the
    // MIS weights no longer partition unity on the receiver's support.
    const auto unshadowed = Temporal(canonical, kReceiver, N, history, kSource, N, kLum, Mode::UnshadowedCrossTargets);
    double drift = 0.;
    for (unsigned y = 0; y < kSamples; ++y) drift += std::abs(Estimate(unshadowed, kReceiver, y) - Exact(kReceiver, y));
    EXPECT_GT(drift, 1e-3);
}

TEST(SsgiRestirGI, SpatialCombineStaysUnbiasedWithTwoNeighbours) {
    // The pixel's reservoir plus two neighbours of different supports and M, pairwise MIS over
    // the replays at the receiver and at each neighbour: exact at the receiver.
    const Domain neighbourA{{0.4, 0.2, 0.1}, {true, true, false}};
    const unsigned N = 2;
    const auto own = Initial(kReceiver, kLum, N);
    const auto a = Initial(neighbourA, kLum, N);
    const auto b = Temporal(Initial(kSource, kLum, N), kSource, N, Initial(kSource, kLum, N), kSource, 3u * N, kLum);
    const std::vector<Candidate> candidates{{&own, &kReceiver, N}, {&a, &neighbourA, N}, {&b, &kSource, 4u * N}};
    const auto resolved = Spatial(candidates, kLum);
    ExpectExact(resolved, kReceiver, 1e-5, "spatial");
    // A single neighbour (K = 1) is the plain two-candidate balance heuristic.
    const std::vector<Candidate> one{{&own, &kReceiver, N}, {&b, &kSource, 4u * N}};
    ExpectExact(Spatial(one, kLum), kReceiver, 1e-5, "spatial K=1");
    // The same neighbours scored as if every direction were in every support (negative control).
    const auto unshadowed = Spatial(candidates, kLum, Mode::UnshadowedCrossTargets);
    double drift = 0.;
    for (unsigned y = 0; y < kSamples; ++y) drift += std::abs(Estimate(unshadowed, kReceiver, y) - Exact(kReceiver, y));
    EXPECT_GT(drift, 1e-3);
    const auto uniform = Spatial(candidates, kLum, Mode::UniformMis);
    drift = 0.;
    for (unsigned y = 0; y < kSamples; ++y) drift += std::abs(Estimate(uniform, kReceiver, y) - Exact(kReceiver, y));
    EXPECT_GT(drift, 1e-3);
}

TEST(SsgiRestirGI, RecursiveHistoryKeepsEveryBasisExact) {
    // Frame after frame the output becomes the next history (reprojected to a surface whose
    // support changes), capped at 4N: every basis stays exact at every frame's receiver.
    const unsigned N = 2;
    const std::array<Domain, 4> receivers{{{{0.3, 0.3, 0.2}, {true, true, true}},
                                           {{0.1, 0.5, 0.2}, {true, true, false}},
                                           {{0.4, 0.1, 0.3}, {false, true, true}},
                                           {{0.3, 0.3, 0.2}, {true, true, true}}}};
    Distribution history = Initial(kSource, kLum, N);
    const Domain* previous = &kSource;
    for (const auto& receiver : receivers) {
        history = Temporal(Initial(receiver, kLum, N), receiver, N, history, *previous, 4u * N, kLum);
        previous = &receiver;
        ExpectExact(history, receiver, 2e-5, "recursive temporal");
    }
}

TEST(SsgiRestirGI, ReuseLowersVarianceAtEqualMean) {
    // Two independent reservoirs of the same receiver: the combined estimator keeps the mean and
    // has strictly lower variance than either, the point of the reuse.
    const unsigned N = 2;
    const auto canonical = Initial(kReceiver, kLum, N);
    const auto combined = Temporal(canonical, kReceiver, N, Initial(kReceiver, kLum, N), kReceiver, N, kLum);
    ExpectExact(combined, kReceiver, 1e-6, "same-domain temporal");
    EXPECT_GT(Variance(canonical, kReceiver, kLum), 0.);
    EXPECT_LT(Variance(combined, kReceiver, kLum), 0.75 * Variance(canonical, kReceiver, kLum));
}
