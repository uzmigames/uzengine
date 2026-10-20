#include "../../../shaders/runtime/reflections/rt_reflection_contract.hlsli"
#include "passes/raytracing/rt_reflection_pass.h"
#include "raytracing/rt_manager.h"   // RTFrameNeedsTLAS / RTReflectionsAvailable (the Phase B capability gate)
#include "lighting/brdf_math.h"   // uz::brdf::D_GGX / SampleGGX_VNDF: the CPU mirrors of the shader's
#include "core/cvar.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace uz;
using namespace uz::rtreflection;

TEST(RTReflectionStage1, MirrorDirectionAndSurfaceBias) {
    const Vec3 normal(0, 0, 1);
    const Vec3 incident = Vec3(0, 4, -3).Normalized();
    const Vec3 direction = ReflectionDirection(incident, normal);
    EXPECT_NEAR(direction.x, 0.0f, 1e-6f);
    EXPECT_NEAR(direction.y, 0.8f, 1e-6f);
    EXPECT_NEAR(direction.z, 0.6f, 1e-6f);
    const Vec3 origin = Vec3(350, -84, 0) + normal * kReflectionSurfaceBias;
    EXPECT_FLOAT_EQ(origin.x, 350.0f);
    EXPECT_FLOAT_EQ(origin.z, 2.0f);
    EXPECT_FLOAT_EQ(kReflectionRayTMin, 1.0f);
}

TEST(RTReflectionStage1, GrazingRayStaysAboveSurface) {
    const Vec3 incident = Vec3(1, 0, -0.0001f).Normalized();
    const Vec3 direction = ReflectionDirection(incident, Vec3(0, 0, 1));
    EXPECT_GT(direction.z, 0.0f);
    EXPECT_NEAR(direction.Dot(direction), 1.0f, 1e-6f);
}

TEST(RTReflectionStage1, RoughnessCutoffIncludesBoundaryAndExcludesSky) {
    EXPECT_TRUE(ReflectionEligible(0.5f, 0.0f, 0.35f));
    EXPECT_TRUE(ReflectionEligible(0.5f, 0.35f, 0.35f));
    EXPECT_FALSE(ReflectionEligible(0.5f, std::nextafter(0.35f, 1.0f), 0.35f));
    EXPECT_FALSE(ReflectionEligible(0.0f, 0.02f, 0.35f));
}

TEST(RTReflectionStage1, CurrentScreenConfidenceSelectsFallback) {
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 1.0f));
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 0.002f));
    EXPECT_TRUE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 0.001f));
    EXPECT_TRUE(ReflectionNeedsRT(0.5f, 0.02f, 0.35f, 0.0f));
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.5f, 0.35f, 0.0f));
}

TEST(RTReflectionStage1, UncoveredAndMissingHitsSelectProbeOrSky) {
    EXPECT_TRUE(ReflectionHitUsable(true, 0.0f, 1.0f));
    EXPECT_FALSE(ReflectionHitUsable(false, 0.0f, 1.0f));
    EXPECT_FALSE(ReflectionHitUsable(true, 1e9f, 1.0f));
    EXPECT_FALSE(ReflectionHitUsable(true, 0.0f, 0.0f));
}

// The pass binding contract (rt_reflection_cs.hlsl b0 / b2): the root block fits the engine's
// 20-DWORD bindless budget and the fields sit where the shader's cbuffers read them.
TEST(RTReflectionStage1, RootConstantsFitTheBindlessBudget) {
    EXPECT_EQ(sizeof(RTReflectionRootConstants), kRTReflectionRootDwords * sizeof(uint32_t));
    EXPECT_LE(kRTReflectionRootDwords, 20u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, tlasSrvIdx), 16u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, outputHeight), 24u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, maxRoughness), 28u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, roughnessMaskScale), 40u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, materialTableSrv), 44u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, skyCubemapSrv), 48u);
}

// The rows of the shared b2 layout the shader reads through gi_trace_common.hlsli (rows 3-10 of
// RadianceCacheTraceConstants): a shift here would move every field the GDF sampling reads.
TEST(RTReflectionStage1, TraceConstantsRowsMatchTheSharedLayout) {
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gdfCenter0X), 48u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gdfExtent3), 108u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gdfClipmapSrv0), 112u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, numGdfClipmaps), 128u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, skyColorR), 132u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, lightDirX), 144u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, lightColorR), 160u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, groundAlbedo), 172u);
}

TEST(RTReflectionStage1, UnsetClipmapsStayAtTheSentinelAndTheLightingIsTheFrames) {
    PerFrameCB perFrame{};
    perFrame.lightDir          = Vec4(0.0f, 0.6f, 0.8f, 0.0f);
    perFrame.lightColor        = Vec4(3.0f, 2.7f, 2.4f, 0.0f);   // colour x intensity, as the lighting reads it
    perFrame.skyLightIntensity = 0.03f;
    perFrame.skyLightColorR    = 1.0f;
    perFrame.skyLightColorG    = 0.5f;
    perFrame.skyLightColorB    = 0.0f;

    // An uninitialised field: every clipmap at the sentinel, which gdf_surface.hlsli skips.
    GlobalDistanceField noField;
    RadianceCacheTraceConstants tc{};
    FillRTReflectionTraceConstants(tc, noField, perFrame);
    EXPECT_EQ(tc.numGdfClipmaps, 0u);
    EXPECT_EQ(tc.gdfClipmapSrv0, UINT32_MAX);
    EXPECT_EQ(tc.gdfClipmapSrv3, UINT32_MAX);
    EXPECT_FLOAT_EQ(tc.gdfExtent0, 0.0f);
    EXPECT_FLOAT_EQ(tc.lightDirY, 0.6f);
    EXPECT_FLOAT_EQ(tc.lightDirZ, 0.8f);
    EXPECT_FLOAT_EQ(tc.lightIntensity, 1.0f);
    EXPECT_FLOAT_EQ(tc.lightColorR, 3.0f);
    EXPECT_FLOAT_EQ(tc.lightColorB, 2.4f);
    EXPECT_FLOAT_EQ(tc.skyColorR, 0.03f);
    EXPECT_FLOAT_EQ(tc.skyColorG, 0.015f);
    EXPECT_FLOAT_EQ(tc.skyColorB, 0.0f);
    EXPECT_FLOAT_EQ(tc.groundAlbedo, 0.18f);

    SetRTReflectionClipmap(tc, 0, Vec3(350, -84, 12), 2500.0f, 24741u);
    SetRTReflectionClipmap(tc, 3, Vec3(-1, -2, -3), 160000.0f, 24753u);
    SetRTReflectionClipmap(tc, 4, Vec3(9, 9, 9), 1.0f, 7u);   // beyond the four rows: ignored
    EXPECT_FLOAT_EQ(tc.gdfCenter0X, 350.0f);
    EXPECT_FLOAT_EQ(tc.gdfCenter0Z, 12.0f);
    EXPECT_FLOAT_EQ(tc.gdfExtent0, 2500.0f);
    EXPECT_EQ(tc.gdfClipmapSrv0, 24741u);
    EXPECT_FLOAT_EQ(tc.gdfCenter3Y, -2.0f);
    EXPECT_EQ(tc.gdfClipmapSrv3, 24753u);
    EXPECT_EQ(tc.gdfClipmapSrv1, UINT32_MAX);
    EXPECT_FLOAT_EQ(tc.gdfExtent1, 0.0f);
    EXPECT_EQ(tc.gdfClipmapSrv2, UINT32_MAX);
    EXPECT_FLOAT_EQ(tc.gdfExtent2, 0.0f);
}

// ── Stage 2: glossy lobe, indirect at the hit, denoise route ─────────────────────────────────

// The CPU mirror (rt_reflection_pass.h) and the shader contract agree on the flag bits and on the
// roughness cutoff rule: an unset r.RayTracing.Reflections.MaxRoughness keeps the SSR cutoff.
TEST(RTReflectionStage2, FlagsAndRoughnessCutoffMatchTheContract) {
    EXPECT_EQ(kRTReflectionFlagGlossy, kReflectionFlagGlossy);
    EXPECT_EQ(kRTReflectionFlagIndirect, kReflectionFlagIndirect);
    EXPECT_EQ(kRTReflectionFlagHitDistance, kReflectionFlagHitDistance);
    EXPECT_EQ(kReflectionFlagGlossy & kReflectionFlagIndirect, 0u);
    EXPECT_EQ(kReflectionFlagIndirect & kReflectionFlagHitDistance, 0u);
    EXPECT_FLOAT_EQ(RTReflectionMaxRoughness(0.0f, 0.35f), 0.35f);
    EXPECT_FLOAT_EQ(RTReflectionMaxRoughness(0.8f, 0.35f), 0.8f);
    EXPECT_FLOAT_EQ(ReflectionMaxRoughness(0.0f, 0.35f), 0.35f);
    EXPECT_FLOAT_EQ(ReflectionMaxRoughness(0.8f, 0.35f), 0.8f);
}

TEST(RTReflectionStage2, RootConstantsGrowToSixteenDwordsWithinTheBudget) {
    EXPECT_EQ(sizeof(RTReflectionRootConstants), 64u);
    EXPECT_EQ(kRTReflectionRootDwords, 16u);
    EXPECT_LE(kRTReflectionRootDwords, 20u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, probeShSrv), 52u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, hitDistUavIdx), 56u);
    EXPECT_EQ(offsetof(RTReflectionRootConstants, flags), 60u);
    const RTReflectionRootConstants rc{};
    EXPECT_EQ(rc.probeShSrv, UINT32_MAX);      // sentinel: no indirect term
    EXPECT_EQ(rc.hitDistUavIdx, UINT32_MAX);
    EXPECT_EQ(rc.flags, 0u);                   // the stage 1 pass
}

// The glossy lobe is opt-in and only for surfaces rougher than a mirror; its alpha is the deferred
// BRDF's conversion (roughness^2 floored at kMinGGXAlpha), so the sampled lobe is the lobe the
// lighting's split-sum integrates.
TEST(RTReflectionStage2, GlossyLobeGateAndAlphaFollowTheDeferredBrdf) {
    EXPECT_FALSE(ReflectionUsesGlossyLobe(0u, 0.5f));
    EXPECT_FALSE(ReflectionUsesGlossyLobe(kReflectionFlagIndirect, 0.5f));
    EXPECT_FALSE(ReflectionUsesGlossyLobe(kReflectionFlagGlossy, kReflectionMirrorRoughness));
    EXPECT_TRUE(ReflectionUsesGlossyLobe(kReflectionFlagGlossy, 0.05f));
    EXPECT_TRUE(ReflectionUsesGlossyLobe(kReflectionFlagGlossy | kReflectionFlagIndirect, 0.5f));
    EXPECT_FLOAT_EQ(kReflectionMirrorRoughness, 0.04f);
    EXPECT_FLOAT_EQ(ReflectionGlossyAlpha(0.5f), 0.25f);
    EXPECT_FLOAT_EQ(ReflectionGlossyAlpha(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(ReflectionGlossyAlpha(0.0f), kReflectionMinGGXAlpha);
    EXPECT_FLOAT_EQ(ReflectionGlossyAlpha(0.08f), kReflectionMinGGXAlpha);   // 0.08^2 = the floor
}

namespace {

// G1_SmithGGX of brdf.hlsli (alphaSq, cosTheta).
float SmithG1(float alphaSq, float cosTheta) {
    const float cos2 = cosTheta * cosTheta;
    const float tan2 = std::max(1.0f - cos2, 0.0f) / cos2;
    return 2.0f / (1.0f + std::sqrt(1.0f + alphaSq * tan2));
}

Vec3 ViewAtCosine(float NdotV) {
    return Vec3(std::sqrt(std::max(1.0f - NdotV * NdotV, 0.0f)), 0.0f, NdotV);
}

// The shader's sampler (sampling.hlsli SampleGGX_VNDF) through its CPU mirror, which uses the
// brdf header's own small vector type.
Vec3 SampleVndf(float alpha, const Vec3& wi, float ux, float uy) {
    const uz::brdf::Vec3 h = uz::brdf::SampleGGX_VNDF(alpha, uz::brdf::Vec3{wi.x, wi.y, wi.z}, ux, uy);
    return Vec3(h.x, h.y, h.z);
}

} // namespace

// The glossy ray's density is the path tracer's SampleGGX pdf: D(H) G1(V) / (4 N.V), zero outside
// the lobe's support.
TEST(RTReflectionStage2, VndfPdfIsThePathTracersVisibleNormalDensity) {
    const float alpha = 0.25f, NdotV = 0.8f, NdotH = 0.95f, VdotH = 0.9f;
    const float expected = uz::brdf::D_GGX(alpha, NdotH) * SmithG1(alpha * alpha, NdotV) / (4.0f * NdotV);
    EXPECT_NEAR(ReflectionVndfPdf(alpha, NdotH, NdotV, VdotH), expected, 1e-5f);
    EXPECT_GT(expected, 0.0f);
    EXPECT_EQ(ReflectionVndfPdf(alpha, 0.0f, NdotV, VdotH), 0.0f);     // microfacet below the surface
    EXPECT_EQ(ReflectionVndfPdf(alpha, NdotH, 0.0f, VdotH), 0.0f);     // view below the surface
    EXPECT_EQ(ReflectionVndfPdf(alpha, NdotH, NdotV, -0.2f), 0.0f);    // microfacet facing away
    // Normal incidence, H = N: the GGX peak 1 / (pi alpha^2), G1 = 1.
    EXPECT_NEAR(ReflectionVndfPdf(alpha, 1.0f, 1.0f, 1.0f), 1.0f / (kReflectionPi * alpha * alpha) / 4.0f, 1e-4f);
}

// A density integrates to one: the pdf over every reflected direction (the full sphere — a rough
// lobe at grazing incidence reflects part of itself below the surface) is 1 for every alpha and
// view angle. Midpoint rule on a (theta, phi) grid.
TEST(RTReflectionStage2, VndfPdfIntegratesToOneOverReflectedDirections) {
    const Vec3 N(0.0f, 0.0f, 1.0f);
    for (const float alpha : {0.0625f, 0.25f, 0.64f}) {
        for (const float NdotV : {1.0f, 0.5f, 0.2f}) {
            const Vec3 V = ViewAtCosine(NdotV);
            const int nTheta = 1000, nPhi = 1000;
            const double dTheta = kReflectionPi / nTheta, dPhi = 2.0 * kReflectionPi / nPhi;
            double sum = 0.0;
            for (int i = 0; i < nTheta; ++i) {
                const double theta = (i + 0.5) * dTheta;
                const double sinT = std::sin(theta), cosT = std::cos(theta);
                for (int j = 0; j < nPhi; ++j) {
                    const double phi = (j + 0.5) * dPhi;
                    const Vec3 L(static_cast<float>(sinT * std::cos(phi)), static_cast<float>(sinT * std::sin(phi)),
                                 static_cast<float>(cosT));
                    const Vec3 sumVL = V + L;
                    if (sumVL.LengthSq() < 1e-12f) continue;   // L = -V: H undefined, measure zero
                    const Vec3 H = sumVL.Normalized();
                    // d omega_L = 4 (V.H) d omega_H: the contract's pdf is already per reflected direction.
                    sum += static_cast<double>(ReflectionVndfPdf(alpha, H.Dot(N), NdotV, V.Dot(H))) * sinT * dTheta * dPhi;
                }
            }
            EXPECT_NEAR(sum, 1.0, 0.01) << "alpha " << alpha << " NdotV " << NdotV;
        }
    }
}

// The sampler the shader draws from (SampleGGX_VNDF, the path tracer's) and the contract's pdf
// describe the same lobe: with L = reflect(incident, H) built as the shader builds it, the
// estimator E[max(0, N.L) / pdf(L)] over a stratified sample grid is the cosine integral over the
// upper hemisphere, pi, at every roughness and view angle. A sampler/pdf mismatch would bias it.
// The GGX tail puts a sliver of every lobe below the horizon (one row of the grid at normal
// incidence for the narrow lobe), which the shader redraws; the tolerance covers the tail's
// weight variance at a million stratified samples.
TEST(RTReflectionStage2, VndfSamplerAndPdfAgreeOnTheCosineIntegral) {
    const Vec3 N(0.0f, 0.0f, 1.0f);
    for (const float alpha : {0.0625f, 0.25f, 0.64f}) {
        for (const float NdotV : {1.0f, 0.5f, 0.2f}) {
            const Vec3 V = ViewAtCosine(NdotV);
            const Vec3 incident = -V;
            const int n = 1024;
            double sum = 0.0;
            int belowHorizon = 0;
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    // Tangent space is world space here (N = +Z), as the shader's T/B/N frame.
                    const Vec3 H = SampleVndf(alpha, V, (i + 0.5f) / n, (j + 0.5f) / n);
                    const Vec3 L = ReflectionDirection(incident, H);
                    EXPECT_NEAR(L.LengthSq(), 1.0f, 1e-4f);
                    EXPECT_GT(V.Dot(H), 0.0f);                 // a visible microfacet, always
                    if (L.Dot(N) <= 0.0f) { ++belowHorizon; continue; }   // the shader redraws these
                    const float pdf = ReflectionVndfPdf(alpha, H.Dot(N), NdotV, V.Dot(H));
                    ASSERT_GT(pdf, 0.0f);
                    sum += static_cast<double>(L.Dot(N)) / pdf;
                }
            }
            EXPECT_NEAR(sum / (n * n), kReflectionPi, 0.04 * kReflectionPi) << "alpha " << alpha << " NdotV " << NdotV;
            // Normal incidence on a narrow lobe: only the tail (under 1 % of the samples) reflects
            // below the horizon.
            if (NdotV == 1.0f && alpha == 0.0625f) EXPECT_LE(belowHorizon, n * n / 100);
        }
    }
}

// At the alpha floor the glossy ray is the mirror ray: every sample lies within the lobe's tail
// around the mirror direction and their mean is the mirror direction.
TEST(RTReflectionStage2, GlossyRayCollapsesToTheMirrorAtTheAlphaFloor) {
    const Vec3 N(0.0f, 0.0f, 1.0f);
    const Vec3 V = ViewAtCosine(0.7f);
    const Vec3 incident = -V;
    const Vec3 mirror = ReflectionDirection(incident, N);
    const float alpha = ReflectionGlossyAlpha(0.0f);
    Vec3 mean(0.0f, 0.0f, 0.0f);
    float minCos = 1.0f;
    const int n = 64;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const Vec3 H = SampleVndf(alpha, V, (i + 0.5f) / n, (j + 0.5f) / n);
            const Vec3 L = ReflectionDirection(incident, H);
            minCos = std::min(minCos, L.Dot(mirror));
            mean += L;
        }
    }
    EXPECT_GT(minCos, std::cos(10.0f * kReflectionPi / 180.0f));                  // tail within 10 degrees
    EXPECT_GT(mean.Normalized().Dot(mirror), std::cos(0.5f * kReflectionPi / 180.0f));   // mean within 0.5 degree
}

// The indirect term at the hit: a reflected surface in the sun's shadow (visibility 0) is black in
// stage 1 and lit by the probe term alone in stage 2; the radiance cache stores E / pi, so the
// term adds to the Lambert sun term without another 1 / pi; a lit surface receives both.
TEST(RTReflectionStage2, ShadowedHitIsLitByTheProbeTermAlone) {
    const Vec3 albedo(0.6f, 0.5f, 0.4f);
    const Vec3 sunRadiance(3.0f, 2.7f, 2.4f);       // colour x intensity x N.L x visibility
    const Vec3 probeOverPi(0.2f, 0.1f, 0.05f);      // EvalSH_L2 of the probe SH at the hit
    const Vec3 none(0.0f, 0.0f, 0.0f);

    const Vec3 stage1 = ReflectionHitRadiance(albedo, sunRadiance, none);
    EXPECT_NEAR(stage1.x, 0.6f * 3.0f / kReflectionPi, 1e-6f);
    EXPECT_NEAR(stage1.z, 0.4f * 2.4f / kReflectionPi, 1e-6f);

    const Vec3 shadowedStage1 = ReflectionHitRadiance(albedo, none, none);
    EXPECT_EQ(shadowedStage1.x, 0.0f);
    EXPECT_EQ(shadowedStage1.y, 0.0f);
    EXPECT_EQ(shadowedStage1.z, 0.0f);

    const Vec3 shadowedStage2 = ReflectionHitRadiance(albedo, none, probeOverPi);
    EXPECT_NEAR(shadowedStage2.x, 0.6f * 0.2f, 1e-6f);
    EXPECT_NEAR(shadowedStage2.y, 0.5f * 0.1f, 1e-6f);
    EXPECT_GT(shadowedStage2.z, 0.0f);

    const Vec3 both = ReflectionHitRadiance(albedo, sunRadiance, probeOverPi);
    EXPECT_NEAR(both.x, stage1.x + shadowedStage2.x, 1e-6f);
    EXPECT_NEAR(both.y, stage1.y + shadowedStage2.y, 1e-6f);
}

// Probes are weighted by the squared cosine between the receiver normal and the direction to the
// probe (the deferred lighting's and the SDF reflections' rule): one behind the tangent plane
// contributes nothing, one coincident with the receiver fully.
TEST(RTReflectionStage2, ProbeFacingWeightIsTheLightingsSquaredCosine) {
    const Vec3 n(0.0f, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(ReflectionProbeFacingWeight(n, Vec3(0.0f, 0.0f, 100.0f)), 1.0f);
    EXPECT_FLOAT_EQ(ReflectionProbeFacingWeight(n, Vec3(0.0f, 0.0f, -100.0f)), 0.0f);
    EXPECT_NEAR(ReflectionProbeFacingWeight(n, Vec3(100.0f, 0.0f, 100.0f)), 0.5f, 1e-6f);
    EXPECT_FLOAT_EQ(ReflectionProbeFacingWeight(n, Vec3(100.0f, 0.0f, 0.0f)), 0.0f);
    EXPECT_FLOAT_EQ(ReflectionProbeFacingWeight(n, Vec3(0.0f, 0.0f, 0.0f)), 1.0f);
}

// The probe grid the indirect read indexes the SH buffer with and the frame index of the glossy
// sample rotation ride on rows 1-2 of the shared b2 layout, where gi_trace_common.hlsli reads them;
// without a grid the rows stay zero (probesPerAxis 0 = no indirect term in the shader).
TEST(RTReflectionStage2, ProbeGridAndFrameIndexFillTheSharedRows) {
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, gridOriginX), 0u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, probeSpacing), 12u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, probesPerAxis), 16u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, totalProbes), 20u);
    EXPECT_EQ(offsetof(RadianceCacheTraceConstants, frameIndex), 28u);

    PerFrameCB perFrame{};
    perFrame.lightColor = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
    GlobalDistanceField noField;
    RadianceCacheTraceConstants tc{};
    FillRTReflectionTraceConstants(tc, noField, perFrame, 95u);
    EXPECT_EQ(tc.frameIndex, 95u);
    EXPECT_EQ(tc.probesPerAxis, 0u);
    EXPECT_FLOAT_EQ(tc.probeSpacing, 0.0f);

    SetRTReflectionProbeGrid(tc, Vec3(-1650.0f, -2000.0f, -1850.0f), 4000.0f / 15.0f, 16u);
    EXPECT_FLOAT_EQ(tc.gridOriginX, -1650.0f);
    EXPECT_FLOAT_EQ(tc.gridOriginY, -2000.0f);
    EXPECT_FLOAT_EQ(tc.gridOriginZ, -1850.0f);
    EXPECT_FLOAT_EQ(tc.probeSpacing, 4000.0f / 15.0f);
    EXPECT_EQ(tc.probesPerAxis, 16u);
    EXPECT_EQ(tc.totalProbes, 4096u);
    EXPECT_EQ(tc.frameIndex, 95u);               // untouched by the grid fill
    EXPECT_FLOAT_EQ(tc.lightColorR, 1.0f);       // and so are the sun rows
    EXPECT_FLOAT_EQ(tc.groundAlbedo, 0.18f);
}

// ── Stage 3: the pass without the screen trace (SSR off) ─────────────────────────────────────

// The CPU mirror and the contract agree on the stage 3 flag, which overlaps no stage 2 bit and
// rides in the existing `flags` DWORD (no new root constant); a default root block is still the
// stage 1/2 pass (flag clear, the screen trace read), so the SSR-on route is untouched, and the
// trace SRV's sentinel is the "unread" value the standalone route binds.
TEST(RTReflectionStage3, NoScreenTraceFlagMatchesTheContractAndOverlapsNoOtherBit) {
    EXPECT_EQ(kRTReflectionFlagNoScreenTrace, kReflectionFlagNoScreenTrace);
    EXPECT_EQ(kReflectionFlagNoScreenTrace
                  & (kReflectionFlagGlossy | kReflectionFlagIndirect | kReflectionFlagHitDistance), 0u);
    EXPECT_FALSE(ReflectionOwnsOutput(0u));
    EXPECT_FALSE(ReflectionOwnsOutput(kReflectionFlagGlossy | kReflectionFlagIndirect | kReflectionFlagHitDistance));
    EXPECT_TRUE(ReflectionOwnsOutput(kReflectionFlagNoScreenTrace));
    EXPECT_TRUE(ReflectionOwnsOutput(kReflectionFlagNoScreenTrace | kReflectionFlagGlossy | kReflectionFlagHitDistance));
    // The stage 2 opt-ins are unaffected by the stage 3 bit.
    EXPECT_TRUE(ReflectionUsesGlossyLobe(kReflectionFlagNoScreenTrace | kReflectionFlagGlossy, 0.5f));
    EXPECT_FALSE(ReflectionUsesGlossyLobe(kReflectionFlagNoScreenTrace, 0.5f));

    const RTReflectionRootConstants rc{};
    EXPECT_FALSE(ReflectionOwnsOutput(rc.flags));
    EXPECT_EQ(rc.ssrTraceSrvIdx, UINT32_MAX);
    EXPECT_EQ(sizeof(RTReflectionRootConstants), 64u);
    EXPECT_EQ(kRTReflectionRootDwords, 16u);
    EXPECT_LE(kRTReflectionRootDwords, 20u);
}

// With no screen trace the confidence the shader feeds ReflectionNeedsRT is zero everywhere, so
// the ray selection reduces to the eligibility test: every eligible texel is traced and no
// ineligible one (the sky, a surface rougher than the cutoff) — the "no SSR = always need the
// ray" rule of the fallback ladder with its screen layer removed.
TEST(RTReflectionStage3, ZeroScreenConfidenceTracesExactlyTheEligibleTexels) {
    const float maxRoughness = 0.35f;
    const float depths[]      = {0.0f, 0.00005f, 0.0001f, 0.001f, 0.5f, 1.0f};
    const float roughnesses[] = {0.0f, 0.02f, kReflectionMirrorRoughness, 0.35f, 0.36f, 0.9f};
    int traced = 0;
    for (const float z : depths) {
        for (const float r : roughnesses) {
            EXPECT_EQ(ReflectionNeedsRT(z, r, maxRoughness, 0.0f), ReflectionEligible(z, r, maxRoughness))
                << "deviceZ " << z << " roughness " << r;
            traced += ReflectionNeedsRT(z, r, maxRoughness, 0.0f) ? 1 : 0;
        }
    }
    EXPECT_EQ(traced, 3 * 4);   // three depths above the sky threshold x four roughnesses within the cutoff
    // The same texel with a confident screen hit stays with the screen trace (stages 1 and 2).
    EXPECT_FALSE(ReflectionNeedsRT(0.5f, 0.02f, maxRoughness, 1.0f));
    EXPECT_TRUE(ReflectionNeedsRT(0.5f, 0.02f, maxRoughness, 0.0f));
}

// ── Phase B: the ray-traced reflections as the real-time default ────────────────────────

namespace {

float RegisteredDefault(const char* name) {
    const CVar* cvar = CVarRegistry::Get().Find(name);
    EXPECT_NE(cvar, nullptr) << name << " is not registered";
    return cvar ? cvar->defaultValue : -1.0f;
}

} // namespace

// The registered defaults: the reflections (and their probe term, the hit shading of the SDF
// reflections they replace) are on; every other ray-traced feature, the master switch and the
// stage 2 opt-ins that cost passes stay off, and the DXR-unavailable simulation is off.
TEST(RTReflectionPhaseB, ReflectionsDefaultOnEveryOtherRayTracedFeatureDefaultOff) {
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.Reflections"), 1.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.Reflections.Indirect"), 1.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.GI"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.Shadows"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.AO"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.TestPass"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.Reflections.Glossy"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.Reflections.Denoise"), 0.0f);
    EXPECT_FLOAT_EQ(RegisteredDefault("r.RayTracing.SimulateUnsupported"), 0.0f);
}

// The TLAS is built for the reflections alone (the default: master switch off), for the master
// switch alone (DDGI / the path tracer), and never without DXR or with both switches off (the
// pre-Phase-B frame: no build, an empty TLAS).
TEST(RTReflectionPhaseB, TlasIsBuiltForTheReflectionsAloneAndNeverWithoutDxr) {
    EXPECT_TRUE(RTFrameNeedsTLAS(true, false, true));     // the default
    EXPECT_TRUE(RTFrameNeedsTLAS(true, true, false));     // master switch alone
    EXPECT_TRUE(RTFrameNeedsTLAS(true, true, true));
    EXPECT_FALSE(RTFrameNeedsTLAS(true, false, false));   // r.RayTracing.Reflections 0: the old frame
    EXPECT_FALSE(RTFrameNeedsTLAS(false, true, true));    // no DXR: nothing, whatever the switches
    EXPECT_FALSE(RTFrameNeedsTLAS(false, false, true));
}

// The pass runs only with DXR, the switch and a non-empty TLAS; each missing fact is the fallback.
TEST(RTReflectionPhaseB, PassAvailableOnlyWithDxrTheSwitchAndANonEmptyTlas) {
    EXPECT_TRUE(RTReflectionsAvailable(true, true, 1u));
    EXPECT_TRUE(RTReflectionsAvailable(true, true, 4096u));
    EXPECT_FALSE(RTReflectionsAvailable(false, true, 1u));   // no DXR (RTManager not initialised)
    EXPECT_FALSE(RTReflectionsAvailable(true, false, 1u));   // switch off
    EXPECT_FALSE(RTReflectionsAvailable(true, true, 0u));    // TLAS not built / nothing to trace
    EXPECT_FALSE(RTReflectionsAvailable(false, false, 0u));
}

// The route selection: with the rays available the screen trace keeps its holes filled and SSR
// off hands the buffer to the rays (the default route, r.SSR defaulting off); without them every
// combination is the route of the frame before Phase B — the screen trace alone, or no
// reflection layer — and a warming-up SSR (on, no trace yet, not switched off) registers nothing
// in either case.
TEST(RTReflectionPhaseB, FallbackRouteIsTheFrameBeforePhaseB) {
    using Route = RTReflectionRoute;
    // Rays available.
    EXPECT_EQ(SelectRTReflectionRoute(true, false, true), Route::ScreenTraceAndRays);
    EXPECT_EQ(SelectRTReflectionRoute(false, true, true), Route::RaysOnly);
    EXPECT_EQ(SelectRTReflectionRoute(false, false, true), Route::None);   // SSR warming up
    // Rays unavailable (no DXR, switch off, empty TLAS): never a rays route.
    EXPECT_EQ(SelectRTReflectionRoute(true, false, false), Route::ScreenTraceOnly);
    EXPECT_EQ(SelectRTReflectionRoute(false, true, false), Route::None);
    EXPECT_EQ(SelectRTReflectionRoute(false, false, false), Route::None);
    for (const bool screenTrace : {false, true}) {
        for (const bool ssrOff : {false, true}) {
            const Route route = SelectRTReflectionRoute(screenTrace, ssrOff, false);
            EXPECT_NE(route, Route::RaysOnly);
            EXPECT_NE(route, Route::ScreenTraceAndRays);
            EXPECT_EQ(route == Route::ScreenTraceOnly, screenTrace);
        }
    }
}
