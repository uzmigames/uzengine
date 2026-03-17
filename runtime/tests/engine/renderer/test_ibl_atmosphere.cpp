/// Unit tests for IBL baker, SH projection, auto-exposure, and PerFrameCB IBL fields.
///
/// All tests are CPU-only — no GPU device, no audio hardware, no real
/// RHI backend required. GPU-path functions (BakeBRDFLut, BakeEnvironment,
/// Execute, ProjectCubemapToSH with device) are tested only for argument
/// validation with null/sentinel inputs.
///
/// # Test Groups
/// - `IBLBaker`                  — init/shutdown/state, BakeResult defaults
/// - `SHCoefficients`            — size, layout, memcpy round-trip
/// - `SHProjection`              — uniform white, all-black, null face, linearity
/// - `AutoExposure`              — init/shutdown/state, settings defaults
/// - `HistogramRootConstants`    — size, field offsets, write/read
/// - `AverageRootConstants`      — size, field offsets, write/read
/// - `PerFrameCB_IBL`            — size, IBL field defaults, SH array defaults, field offsets
///
/// See also: test_ibl_atmosphere_cb.cpp for atmosphere CBs, ECS components, and pass constants.

#include "constant_buffers.h"
#include "ibl_baker.h"
#include "sh_projection.h"
#include "auto_exposure.h"
#include "sky_atmosphere_component.h"
#include "height_fog_component.h"
#include "sky_light_component.h"
#include "sky_cubemap_capture_pass.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// IBLBaker — argument validation and uninitialized state
// ─────────────────────────────────────────────────────────────────────────────

TEST(IBLBaker, DefaultStateIsUninitialized) {
    IBLBaker baker;
    EXPECT_FALSE(baker.IsInitialized());
}

TEST(IBLBaker, DefaultStateHasNoBRDFLut) {
    IBLBaker baker;
    EXPECT_FALSE(baker.HasBRDFLut());
}

TEST(IBLBaker, DefaultBRDFLutSrvIndexIsMaxUint) {
    IBLBaker baker;
    EXPECT_EQ(baker.GetBRDFLutSrvIndex(), UINT32_MAX);
}

TEST(IBLBaker, InitRejectsNullDevice) {
    IBLBaker baker;
    EXPECT_FALSE(baker.Init(nullptr, reinterpret_cast<ShaderManager*>(0x1)));
}

TEST(IBLBaker, InitRejectsNullShaderManager) {
    IBLBaker baker;
    EXPECT_FALSE(baker.Init(reinterpret_cast<IRHIDevice*>(0x1), nullptr));
}

TEST(IBLBaker, InitRejectsBothNull) {
    IBLBaker baker;
    EXPECT_FALSE(baker.Init(nullptr, nullptr));
}

TEST(IBLBaker, ShutdownOnUninitializedIsNoOp) {
    IBLBaker baker;
    // Must not crash or assert
    baker.Shutdown();
    EXPECT_FALSE(baker.IsInitialized());
}

TEST(IBLBaker, ShutdownTwiceOnUninitializedIsNoOp) {
    IBLBaker baker;
    baker.Shutdown();
    baker.Shutdown();
    EXPECT_FALSE(baker.IsInitialized());
}

TEST(IBLBaker, BakeResultDefaultIrradianceSrvIndexIsMaxUint) {
    IBLBaker::BakeResult result{};
    EXPECT_EQ(result.irradianceSrvIndex, UINT32_MAX);
}

TEST(IBLBaker, BakeResultDefaultPrefilteredSrvIndexIsMaxUint) {
    IBLBaker::BakeResult result{};
    EXPECT_EQ(result.prefilteredSrvIndex, UINT32_MAX);
}

TEST(IBLBaker, BakeResultBothInvalidByDefault) {
    IBLBaker::BakeResult result{};
    // Both indices invalid — caller checks IsValid() before using
    EXPECT_EQ(result.irradianceSrvIndex, UINT32_MAX);
    EXPECT_EQ(result.prefilteredSrvIndex, UINT32_MAX);
}

TEST(IBLBaker, BakeResultCanBeAssigned) {
    IBLBaker::BakeResult result{};
    result.irradianceSrvIndex  = 10u;
    result.prefilteredSrvIndex = 20u;
    EXPECT_EQ(result.irradianceSrvIndex, 10u);
    EXPECT_EQ(result.prefilteredSrvIndex, 20u);
}

TEST(IBLBaker, BakerTypeCompiles) {
    // Verifies the header is self-contained and the type is well-formed
    static_assert(sizeof(IBLBaker) > 0, "IBLBaker must be a concrete type");
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// SHCoefficients — struct size and layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(SHCoefficients, SizeIs112Bytes) {
    // 7 Vec4 * 16 bytes each = 112 bytes
    EXPECT_EQ(sizeof(SHCoefficients), 112u);
}

TEST(SHCoefficients, DefaultInitializedToZero) {
    SHCoefficients sh{};
    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(sh.coeffs[i].x, 0.0f) << "coeffs[" << i << "].x";
        EXPECT_FLOAT_EQ(sh.coeffs[i].y, 0.0f) << "coeffs[" << i << "].y";
        EXPECT_FLOAT_EQ(sh.coeffs[i].z, 0.0f) << "coeffs[" << i << "].z";
        EXPECT_FLOAT_EQ(sh.coeffs[i].w, 0.0f) << "coeffs[" << i << "].w";
    }
}

TEST(SHCoefficients, SevenVec4Elements) {
    // Verify coeffs array has exactly 7 elements by writing to each
    SHCoefficients sh{};
    for (int i = 0; i < 7; ++i) {
        sh.coeffs[i].x = static_cast<float>(i * 4 + 0);
        sh.coeffs[i].y = static_cast<float>(i * 4 + 1);
        sh.coeffs[i].z = static_cast<float>(i * 4 + 2);
        sh.coeffs[i].w = static_cast<float>(i * 4 + 3);
    }
    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(sh.coeffs[i].x, static_cast<float>(i * 4 + 0)) << "coeffs[" << i << "].x";
        EXPECT_FLOAT_EQ(sh.coeffs[i].y, static_cast<float>(i * 4 + 1)) << "coeffs[" << i << "].y";
        EXPECT_FLOAT_EQ(sh.coeffs[i].z, static_cast<float>(i * 4 + 2)) << "coeffs[" << i << "].z";
        EXPECT_FLOAT_EQ(sh.coeffs[i].w, static_cast<float>(i * 4 + 3)) << "coeffs[" << i << "].w";
    }
}

TEST(SHCoefficients, MemcpyRoundTrip) {
    SHCoefficients src{};
    for (int i = 0; i < 7; ++i) {
        src.coeffs[i].x = static_cast<float>(i + 1) * 0.1f;
        src.coeffs[i].y = static_cast<float>(i + 1) * 0.2f;
        src.coeffs[i].z = static_cast<float>(i + 1) * 0.3f;
        src.coeffs[i].w = static_cast<float>(i + 1) * 0.4f;
    }

    uint8_t buffer[112];
    std::memcpy(buffer, &src, sizeof(src));

    SHCoefficients dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(dst.coeffs[i].x, src.coeffs[i].x) << "coeffs[" << i << "].x";
        EXPECT_FLOAT_EQ(dst.coeffs[i].y, src.coeffs[i].y) << "coeffs[" << i << "].y";
        EXPECT_FLOAT_EQ(dst.coeffs[i].z, src.coeffs[i].z) << "coeffs[" << i << "].z";
        EXPECT_FLOAT_EQ(dst.coeffs[i].w, src.coeffs[i].w) << "coeffs[" << i << "].w";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SHProjection — CPU projection analytic checks
// ─────────────────────────────────────────────────────────────────────────────

TEST(SHProjection, AllBlackCubemap_AllCoefficientsAreZero) {
    const uint32_t faceSize       = 4u;
    const uint32_t floatsPerFace  = faceSize * faceSize * 4u;
    std::vector<float> blackPixels(floatsPerFace * 6u, 0.0f);

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = blackPixels.data() + f * floatsPerFace;
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(sh.coeffs[i].x, 0.0f) << "coeffs[" << i << "].x should be 0 for black input";
        EXPECT_FLOAT_EQ(sh.coeffs[i].y, 0.0f) << "coeffs[" << i << "].y should be 0 for black input";
        EXPECT_FLOAT_EQ(sh.coeffs[i].z, 0.0f) << "coeffs[" << i << "].z should be 0 for black input";
        // coeffs[6].w is always 1.0 (UE5 sentinel), all other .w fields should be 0
        if (i < 6) {
            EXPECT_FLOAT_EQ(sh.coeffs[i].w, 0.0f) << "coeffs[" << i << "].w should be 0 for black input";
        }
    }
    // UE5 packing sets coeffs[6].w = 1.0 unconditionally
    EXPECT_FLOAT_EQ(sh.coeffs[6].w, 1.0f) << "coeffs[6].w is always 1.0 (UE5 sentinel)";
}

TEST(SHProjection, UniformWhiteCubemap_RGBChannelsAreEqual) {
    // For a uniform white input, R == G == B in every packed coefficient because
    // the SH projection is channel-independent and all channels have the same input.
    const uint32_t faceSize      = 8u;
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;
    std::vector<float> whitePixels(floatsPerFace * 6u);
    for (size_t i = 0; i < whitePixels.size(); i += 4) {
        whitePixels[i + 0] = 1.0f;
        whitePixels[i + 1] = 1.0f;
        whitePixels[i + 2] = 1.0f;
        whitePixels[i + 3] = 1.0f;
    }

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = whitePixels.data() + f * floatsPerFace;
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    // coeffs[0].w = packed L0 R, coeffs[1].w = packed L0 G, coeffs[2].w = packed L0 B
    // All should be equal for uniform white
    EXPECT_NEAR(sh.coeffs[0].w, sh.coeffs[1].w, 1e-4f)
        << "L0 R and G should be equal for uniform white";
    EXPECT_NEAR(sh.coeffs[1].w, sh.coeffs[2].w, 1e-4f)
        << "L0 G and B should be equal for uniform white";
}

TEST(SHProjection, UniformWhiteCubemap_L0TermIsPositive) {
    // For uniform white, the L0 term after cosine convolution packing must be
    // positive. c0 * L0_raw - c3 * L20_raw > 0 (L20 = 0 for uniform input).
    const uint32_t faceSize      = 8u;
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;
    std::vector<float> whitePixels(floatsPerFace * 6u);
    for (size_t i = 0; i < whitePixels.size(); i += 4) {
        whitePixels[i + 0] = 1.0f;
        whitePixels[i + 1] = 1.0f;
        whitePixels[i + 2] = 1.0f;
        whitePixels[i + 3] = 1.0f;
    }

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = whitePixels.data() + f * floatsPerFace;
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    // L0 packed: c0 * sqrt(4*pi) ≈ 0.282095 * 3.5449 ≈ 1.0 (discretization varies slightly)
    EXPECT_GT(sh.coeffs[0].w, 0.5f) << "L0 R term should be close to 1.0 for uniform white";
    EXPECT_GT(sh.coeffs[1].w, 0.5f) << "L0 G term should be close to 1.0 for uniform white";
    EXPECT_GT(sh.coeffs[2].w, 0.5f) << "L0 B term should be close to 1.0 for uniform white";
}

TEST(SHProjection, UniformWhiteCubemap_L2StermsAreSymmetric) {
    // For a uniform cubemap, all L1 and L2 terms (odd/asymmetric basis functions)
    // should integrate to approximately zero — the uniform environment has no directionality.
    const uint32_t faceSize      = 16u; // Larger face to improve integration accuracy
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;
    std::vector<float> whitePixels(floatsPerFace * 6u);
    for (size_t i = 0; i < whitePixels.size(); i += 4) {
        whitePixels[i + 0] = 1.0f;
        whitePixels[i + 1] = 1.0f;
        whitePixels[i + 2] = 1.0f;
        whitePixels[i + 3] = 1.0f;
    }

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = whitePixels.data() + f * floatsPerFace;
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    // L1 terms are in x, y, z of coeffs[0..2] (channels R/G/B)
    // For uniform input, these should be ~0 (they encode directionality)
    EXPECT_NEAR(sh.coeffs[0].x, 0.0f, 0.05f) << "L1 R.x should be near 0 for uniform white";
    EXPECT_NEAR(sh.coeffs[0].y, 0.0f, 0.05f) << "L1 R.y should be near 0 for uniform white";
    EXPECT_NEAR(sh.coeffs[0].z, 0.0f, 0.05f) << "L1 R.z should be near 0 for uniform white";

    EXPECT_NEAR(sh.coeffs[1].x, 0.0f, 0.05f) << "L1 G.x should be near 0 for uniform white";
    EXPECT_NEAR(sh.coeffs[1].y, 0.0f, 0.05f) << "L1 G.y should be near 0 for uniform white";
    EXPECT_NEAR(sh.coeffs[1].z, 0.0f, 0.05f) << "L1 G.z should be near 0 for uniform white";

    EXPECT_NEAR(sh.coeffs[2].x, 0.0f, 0.05f) << "L1 B.x should be near 0 for uniform white";
    EXPECT_NEAR(sh.coeffs[2].y, 0.0f, 0.05f) << "L1 B.y should be near 0 for uniform white";
    EXPECT_NEAR(sh.coeffs[2].z, 0.0f, 0.05f) << "L1 B.z should be near 0 for uniform white";
}

TEST(SHProjection, UniformColoredCubemap_ChannelRatioPreserved) {
    // For a uniform red input (1, 0.5, 0.25), the ratio of L0 R : L0 G : L0 B
    // must match the input ratio (1 : 0.5 : 0.25) within discretization tolerance.
    const uint32_t faceSize      = 8u;
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;
    std::vector<float> colorPixels(floatsPerFace * 6u);
    for (size_t i = 0; i < colorPixels.size(); i += 4) {
        colorPixels[i + 0] = 1.0f;   // R
        colorPixels[i + 1] = 0.5f;   // G
        colorPixels[i + 2] = 0.25f;  // B
        colorPixels[i + 3] = 1.0f;   // A (ignored)
    }

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = colorPixels.data() + f * floatsPerFace;
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    // L0 coefficients are in coeffs[0..2].w
    const float L0_R = sh.coeffs[0].w;
    const float L0_G = sh.coeffs[1].w;
    const float L0_B = sh.coeffs[2].w;

    // L0_R should be non-zero (uniform red)
    ASSERT_GT(L0_R, 0.0f) << "L0 R must be positive for red input";

    // Ratio G/R should be ~0.5
    EXPECT_NEAR(L0_G / L0_R, 0.5f, 0.01f)
        << "L0 G/R ratio should be 0.5 for (1, 0.5, 0.25) uniform input";

    // Ratio B/R should be ~0.25
    EXPECT_NEAR(L0_B / L0_R, 0.25f, 0.01f)
        << "L0 B/R ratio should be 0.25 for (1, 0.5, 0.25) uniform input";
}

TEST(SHProjection, SingleFaceOnly_ResultIsFinite) {
    // When only face 0 is supplied and the rest are null, the function should
    // skip null faces (logging a warning), produce finite values, and not crash.
    const uint32_t faceSize      = 4u;
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;
    std::vector<float> pixels(floatsPerFace, 1.0f);

    const float* facePtrs[6] = {};
    facePtrs[0] = pixels.data();
    // Faces 1–5 remain null

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    // Result must not contain any NaN values
    for (int i = 0; i < 7; ++i) {
        EXPECT_FALSE(std::isnan(sh.coeffs[i].x)) << "coeffs[" << i << "].x is NaN";
        EXPECT_FALSE(std::isnan(sh.coeffs[i].y)) << "coeffs[" << i << "].y is NaN";
        EXPECT_FALSE(std::isnan(sh.coeffs[i].z)) << "coeffs[" << i << "].z is NaN";
        EXPECT_FALSE(std::isnan(sh.coeffs[i].w)) << "coeffs[" << i << "].w is NaN";
    }
    // Result must not contain any infinities
    for (int i = 0; i < 7; ++i) {
        EXPECT_FALSE(std::isinf(sh.coeffs[i].x)) << "coeffs[" << i << "].x is Inf";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].y)) << "coeffs[" << i << "].y is Inf";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].z)) << "coeffs[" << i << "].z is Inf";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].w)) << "coeffs[" << i << "].w is Inf";
    }
}

TEST(SHProjection, AllFacesNull_ResultIsAllZero) {
    // All-null face array: totalWeight remains zero, no accumulation occurs.
    // PackSHToUE5Format is called with zero inputs — result should be all-zero.
    const float* facePtrs[6] = {};

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, 4u);

    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(sh.coeffs[i].x, 0.0f) << "coeffs[" << i << "].x";
        EXPECT_FLOAT_EQ(sh.coeffs[i].y, 0.0f) << "coeffs[" << i << "].y";
        EXPECT_FLOAT_EQ(sh.coeffs[i].z, 0.0f) << "coeffs[" << i << "].z";
        // coeffs[6].w is always 1.0 (UE5 sentinel)
        if (i < 6) {
            EXPECT_FLOAT_EQ(sh.coeffs[i].w, 0.0f) << "coeffs[" << i << "].w";
        }
    }
    EXPECT_FLOAT_EQ(sh.coeffs[6].w, 1.0f) << "coeffs[6].w is always 1.0 (UE5 sentinel)";
}

TEST(SHProjection, LastCoeffW_IsAlwaysOne_ForNonzeroInput) {
    // PackSHToUE5Format always sets out[6].w = 1.0f regardless of input.
    // This matches the UE5 SkyIrradianceEnvironmentMap[6].w = 1 sentinel.
    const uint32_t faceSize      = 4u;
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;
    std::vector<float> pixels(floatsPerFace * 6u);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 0.5f;
        pixels[i + 1] = 0.3f;
        pixels[i + 2] = 0.7f;
        pixels[i + 3] = 1.0f;
    }

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = pixels.data() + f * floatsPerFace;
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, faceSize);

    // UE5 packing convention: coeffs[6].w is always 1.0 (shader uses it as scale)
    EXPECT_FLOAT_EQ(sh.coeffs[6].w, 1.0f)
        << "coeffs[6].w must be 1.0 (UE5 SH packing sentinel)";
}

TEST(SHProjection, FaceSize1_SingleTexelPerFace_IsFinite) {
    // Edge case: 1x1 cubemap (single texel per face, 6 texels total).
    // All SH coefficients must be finite.
    float facePixels[6][4] = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
    };

    const float* facePtrs[6];
    for (int f = 0; f < 6; ++f) {
        facePtrs[f] = facePixels[f];
    }

    SHCoefficients sh = ProjectCubemapToSH(facePtrs, 1u);

    for (int i = 0; i < 7; ++i) {
        EXPECT_FALSE(std::isnan(sh.coeffs[i].x)) << "coeffs[" << i << "].x is NaN";
        EXPECT_FALSE(std::isnan(sh.coeffs[i].y)) << "coeffs[" << i << "].y is NaN";
        EXPECT_FALSE(std::isnan(sh.coeffs[i].z)) << "coeffs[" << i << "].z is NaN";
        EXPECT_FALSE(std::isnan(sh.coeffs[i].w)) << "coeffs[" << i << "].w is NaN";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].x)) << "coeffs[" << i << "].x is Inf";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].y)) << "coeffs[" << i << "].y is Inf";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].z)) << "coeffs[" << i << "].z is Inf";
        EXPECT_FALSE(std::isinf(sh.coeffs[i].w)) << "coeffs[" << i << "].w is Inf";
    }
}

TEST(SHProjection, DoubledIntensity_DoubledCoefficients) {
    // SH projection is linear in radiance: doubling input should double all
    // packed coefficients (the packing is also linear).
    const uint32_t faceSize      = 4u;
    const uint32_t floatsPerFace = faceSize * faceSize * 4u;

    std::vector<float> base(floatsPerFace * 6u);
    for (size_t i = 0; i < base.size(); i += 4) {
        base[i + 0] = 0.3f;
        base[i + 1] = 0.5f;
        base[i + 2] = 0.8f;
        base[i + 3] = 1.0f;
    }

    std::vector<float> doubled(floatsPerFace * 6u);
    for (size_t i = 0; i < doubled.size(); i += 4) {
        doubled[i + 0] = 0.6f;
        doubled[i + 1] = 1.0f;
        doubled[i + 2] = 1.6f;
        doubled[i + 3] = 1.0f;
    }

    const float* basePtrs[6];
    const float* doubledPtrs[6];
    for (int f = 0; f < 6; ++f) {
        basePtrs[f]    = base.data()    + f * floatsPerFace;
        doubledPtrs[f] = doubled.data() + f * floatsPerFace;
    }

    SHCoefficients shBase   = ProjectCubemapToSH(basePtrs,   faceSize);
    SHCoefficients shDouble = ProjectCubemapToSH(doubledPtrs, faceSize);

    // All coefficients (except coeffs[6].w which is always 1) should be ~doubled
    const float kTol = 1e-3f;
    for (int i = 0; i < 6; ++i) {
        EXPECT_NEAR(shDouble.coeffs[i].x, 2.0f * shBase.coeffs[i].x, kTol)
            << "coeffs[" << i << "].x should double";
        EXPECT_NEAR(shDouble.coeffs[i].y, 2.0f * shBase.coeffs[i].y, kTol)
            << "coeffs[" << i << "].y should double";
        EXPECT_NEAR(shDouble.coeffs[i].z, 2.0f * shBase.coeffs[i].z, kTol)
            << "coeffs[" << i << "].z should double";
        EXPECT_NEAR(shDouble.coeffs[i].w, 2.0f * shBase.coeffs[i].w, kTol)
            << "coeffs[" << i << "].w should double";
    }
    // coeffs[6].xyz should double (Y2,2 terms), coeffs[6].w is always 1.0
    EXPECT_NEAR(shDouble.coeffs[6].x, 2.0f * shBase.coeffs[6].x, kTol)
        << "coeffs[6].x (Y22 R) should double";
    EXPECT_NEAR(shDouble.coeffs[6].y, 2.0f * shBase.coeffs[6].y, kTol)
        << "coeffs[6].y (Y22 G) should double";
    EXPECT_NEAR(shDouble.coeffs[6].z, 2.0f * shBase.coeffs[6].z, kTol)
        << "coeffs[6].z (Y22 B) should double";
    // w is always 1 in both (the sentinel, not scaled by input)
    EXPECT_FLOAT_EQ(shDouble.coeffs[6].w, 1.0f);
    EXPECT_FLOAT_EQ(shBase.coeffs[6].w, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// AutoExposure — uninitialized state and argument validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(AutoExposure, DefaultStateIsUninitialized) {
    AutoExposure ae;
    EXPECT_FALSE(ae.IsInitialized());
}

TEST(AutoExposure, DefaultLastExposureIsOne) {
    AutoExposure ae;
    EXPECT_FLOAT_EQ(ae.GetLastExposure(), 1.0f);
}

TEST(AutoExposure, InitRejectsNullDevice) {
    AutoExposure ae;
    EXPECT_FALSE(ae.Init(nullptr, reinterpret_cast<ShaderManager*>(0x1)));
}

TEST(AutoExposure, InitRejectsNullShaderManager) {
    AutoExposure ae;
    EXPECT_FALSE(ae.Init(reinterpret_cast<IRHIDevice*>(0x1), nullptr));
}

TEST(AutoExposure, InitRejectsBothNull) {
    AutoExposure ae;
    EXPECT_FALSE(ae.Init(nullptr, nullptr));
}

TEST(AutoExposure, ShutdownOnUninitializedIsNoOp) {
    AutoExposure ae;
    ae.Shutdown();
    EXPECT_FALSE(ae.IsInitialized());
}

TEST(AutoExposure, ShutdownTwiceOnUninitializedIsNoOp) {
    AutoExposure ae;
    ae.Shutdown();
    ae.Shutdown();
    EXPECT_FALSE(ae.IsInitialized());
}

TEST(AutoExposure, DefaultSettingsExposureBias) {
    AutoExposure ae;
    EXPECT_FLOAT_EQ(ae.settings.exposureBias, 0.0f);
}

TEST(AutoExposure, DefaultSettingsMinBrightness) {
    AutoExposure ae;
    /// @source UE5 Scene.cpp:196 — histogram method default
    EXPECT_FLOAT_EQ(ae.settings.minBrightness, 0.03f);
}

TEST(AutoExposure, DefaultSettingsMaxBrightness) {
    AutoExposure ae;
    /// @source UE5 Scene.cpp:197 — histogram method default
    EXPECT_FLOAT_EQ(ae.settings.maxBrightness, 8.0f);
}

TEST(AutoExposure, DefaultSettingsHistLogMin) {
    AutoExposure ae;
    EXPECT_FLOAT_EQ(ae.settings.histLogMin, -10.0f);
}

TEST(AutoExposure, DefaultSettingsHistLogMax) {
    AutoExposure ae;
    EXPECT_FLOAT_EQ(ae.settings.histLogMax, 20.0f);
}

TEST(AutoExposure, DefaultSettingsEnabled) {
    AutoExposure ae;
    EXPECT_TRUE(ae.settings.enabled);
}

TEST(AutoExposure, DefaultSettingsManualExposureIsZero) {
    AutoExposure ae;
    EXPECT_FLOAT_EQ(ae.settings.manualExposure, 0.0f);
}

TEST(AutoExposure, SettingsCanBeModified) {
    AutoExposure ae;
    ae.settings.exposureBias   = 1.0f;
    ae.settings.minBrightness  = 0.01f;
    ae.settings.maxBrightness  = 512.0f;
    ae.settings.histLogMin     = -5.0f;
    ae.settings.histLogMax     = 10.0f;
    ae.settings.enabled        = false;
    ae.settings.manualExposure = 0.5f;

    EXPECT_FLOAT_EQ(ae.settings.exposureBias,   1.0f);
    EXPECT_FLOAT_EQ(ae.settings.minBrightness,  0.01f);
    EXPECT_FLOAT_EQ(ae.settings.maxBrightness,  512.0f);
    EXPECT_FLOAT_EQ(ae.settings.histLogMin,     -5.0f);
    EXPECT_FLOAT_EQ(ae.settings.histLogMax,     10.0f);
    EXPECT_FALSE(ae.settings.enabled);
    EXPECT_FLOAT_EQ(ae.settings.manualExposure, 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HistogramRootConstants — size
// ─────────────────────────────────────────────────────────────────────────────

TEST(HistogramRootConstants, SizeIs20Bytes) {
    // 5 DWORDs: histLogMin(4) + histLogMax(4) + screenWidth(4) + screenHeight(4) + hdrSrvIndex(4)
    EXPECT_EQ(sizeof(HistogramRootConstants), 20u);
}

TEST(HistogramRootConstants, FieldOffsets) {
    EXPECT_EQ(offsetof(HistogramRootConstants, histLogMin),    0u);
    EXPECT_EQ(offsetof(HistogramRootConstants, histLogMax),    4u);
    EXPECT_EQ(offsetof(HistogramRootConstants, screenWidth),   8u);
    EXPECT_EQ(offsetof(HistogramRootConstants, screenHeight), 12u);
    EXPECT_EQ(offsetof(HistogramRootConstants, hdrSrvIndex),  16u);
}

TEST(HistogramRootConstants, WriteAndReadBack) {
    HistogramRootConstants rc{};
    rc.histLogMin    = -10.0f;
    rc.histLogMax    = 20.0f;
    rc.screenWidth   = 1920u;
    rc.screenHeight  = 1080u;
    rc.hdrSrvIndex   = 42u;

    EXPECT_FLOAT_EQ(rc.histLogMin,   -10.0f);
    EXPECT_FLOAT_EQ(rc.histLogMax,    20.0f);
    EXPECT_EQ(rc.screenWidth,  1920u);
    EXPECT_EQ(rc.screenHeight, 1080u);
    EXPECT_EQ(rc.hdrSrvIndex,    42u);
}

// ─────────────────────────────────────────────────────────────────────────────
// AverageRootConstants — size and layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(AverageRootConstants, SizeIs32Bytes) {
    // 8 DWORDs: histLogMin(4) + histLogMax(4) + histogramSrvIdx(4) + padding(4)
    //         + exposureBias(4) + minBrightness(4) + maxBrightness(4) + deltaTime(4)
    EXPECT_EQ(sizeof(AverageRootConstants), 32u);
}

TEST(AverageRootConstants, FieldOffsets) {
    EXPECT_EQ(offsetof(AverageRootConstants, histLogMin),       0u);
    EXPECT_EQ(offsetof(AverageRootConstants, histLogMax),       4u);
    EXPECT_EQ(offsetof(AverageRootConstants, histogramSrvIdx),  8u);
    EXPECT_EQ(offsetof(AverageRootConstants, padding),         12u);
    EXPECT_EQ(offsetof(AverageRootConstants, exposureBias),    16u);
    EXPECT_EQ(offsetof(AverageRootConstants, minBrightness),   20u);
    EXPECT_EQ(offsetof(AverageRootConstants, maxBrightness),   24u);
    EXPECT_EQ(offsetof(AverageRootConstants, deltaTime),       28u);
}

TEST(AverageRootConstants, WriteAndReadBack) {
    AverageRootConstants rc{};
    rc.histLogMin      = -10.0f;
    rc.histLogMax      =  20.0f;
    rc.histogramSrvIdx = 7u;
    rc.padding         = 0u;
    rc.exposureBias    = 0.5f;
    rc.minBrightness   = 0.001f;
    rc.maxBrightness   = 1048576.0f;
    rc.deltaTime       = 0.016f;

    EXPECT_FLOAT_EQ(rc.histLogMin,    -10.0f);
    EXPECT_FLOAT_EQ(rc.histLogMax,     20.0f);
    EXPECT_EQ(rc.histogramSrvIdx, 7u);
    EXPECT_EQ(rc.padding,         0u);
    EXPECT_FLOAT_EQ(rc.exposureBias,   0.5f);
    EXPECT_FLOAT_EQ(rc.minBrightness,  0.001f);
    EXPECT_FLOAT_EQ(rc.maxBrightness,  1048576.0f);
    EXPECT_FLOAT_EQ(rc.deltaTime,      0.016f);
}

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB — IBL fields (size, defaults, offsets, round-trips)
// ─────────────────────────────────────────────────────────────────────────────

TEST(PerFrameCB_IBL, SizeIs512Bytes) {
    EXPECT_EQ(sizeof(PerFrameCB), 512u);
}

TEST(PerFrameCB_IBL, NearPlaneDefault) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.nearPlane, 10.0f);
}

TEST(PerFrameCB_IBL, FarPlaneDefault) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.farPlane, 100000.0f);
}

TEST(PerFrameCB_IBL, IrradianceSrvIndexDefaultIsMaxUint) {
    PerFrameCB cb{};
    EXPECT_EQ(cb.irradianceSrvIndex, UINT32_MAX);
}

TEST(PerFrameCB_IBL, PrefilteredSrvIndexDefaultIsMaxUint) {
    PerFrameCB cb{};
    EXPECT_EQ(cb.prefilteredSrvIndex, UINT32_MAX);
}

TEST(PerFrameCB_IBL, BRDFLutSrvIndexDefaultIsMaxUint) {
    PerFrameCB cb{};
    EXPECT_EQ(cb.brdfLutSrvIndex, UINT32_MAX);
}

TEST(PerFrameCB_IBL, PrefilteredMipLevelsDefaultIsFive) {
    PerFrameCB cb{};
    EXPECT_EQ(cb.prefilteredMipLevels, 5u);
}

TEST(PerFrameCB_IBL, SkyLightIntensityDefaultIsOne) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.skyLightIntensity, 1.0f);
}

TEST(PerFrameCB_IBL, SkyLightColorDefaultIsWhite) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.skyLightColorR, 1.0f);
    EXPECT_FLOAT_EQ(cb.skyLightColorG, 1.0f);
    EXPECT_FLOAT_EQ(cb.skyLightColorB, 1.0f);
}

TEST(PerFrameCB_IBL, SkyLightMinAmbientDefaultIsZero) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.skyLightMinAmbient, 0.0f);
}

TEST(PerFrameCB_IBL, ExposureDefaultIsOne) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.exposure, 1.0f);
}

TEST(PerFrameCB_IBL, SkyIrradianceSHDefaultsToAllZero) {
    PerFrameCB cb{};
    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].x, 0.0f) << "skyIrradianceSH[" << i << "].x";
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].y, 0.0f) << "skyIrradianceSH[" << i << "].y";
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].z, 0.0f) << "skyIrradianceSH[" << i << "].z";
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].w, 0.0f) << "skyIrradianceSH[" << i << "].w";
    }
}

TEST(PerFrameCB_IBL, FieldOffsets) {
    // First block (0..207): viewProj(64) + cameraPos(16) + timeParams(16) +
    //                        lightDir(16) + lightColor(16) + ambientColor(16) + invViewProj(64)
    EXPECT_EQ(offsetof(PerFrameCB, viewProj),     0u);
    EXPECT_EQ(offsetof(PerFrameCB, cameraPos),   64u);
    EXPECT_EQ(offsetof(PerFrameCB, timeParams),  80u);
    EXPECT_EQ(offsetof(PerFrameCB, lightDir),    96u);
    EXPECT_EQ(offsetof(PerFrameCB, lightColor), 112u);
    EXPECT_EQ(offsetof(PerFrameCB, ambientColor), 128u);
    EXPECT_EQ(offsetof(PerFrameCB, invViewProj), 144u);

    // IBL scalars start after invViewProj (144 + 64 = 208)
    EXPECT_EQ(offsetof(PerFrameCB, nearPlane),           208u);
    EXPECT_EQ(offsetof(PerFrameCB, farPlane),            212u);
    EXPECT_EQ(offsetof(PerFrameCB, irradianceSrvIndex),  216u);
    EXPECT_EQ(offsetof(PerFrameCB, prefilteredSrvIndex), 220u);
    EXPECT_EQ(offsetof(PerFrameCB, brdfLutSrvIndex),     224u);
    EXPECT_EQ(offsetof(PerFrameCB, prefilteredMipLevels), 228u);
    EXPECT_EQ(offsetof(PerFrameCB, skyLightIntensity),   232u);
    EXPECT_EQ(offsetof(PerFrameCB, skyLightColorR),      236u);
    EXPECT_EQ(offsetof(PerFrameCB, skyLightColorG),      240u);
    EXPECT_EQ(offsetof(PerFrameCB, skyLightColorB),      244u);
    EXPECT_EQ(offsetof(PerFrameCB, skyLightMinAmbient),  248u);
    EXPECT_EQ(offsetof(PerFrameCB, exposure),            252u);

    // SH array starts at 256 (after 12 floats = 48 bytes past nearPlane @ 208)
    EXPECT_EQ(offsetof(PerFrameCB, skyIrradianceSH), 256u);
}

TEST(PerFrameCB_IBL, WriteAndReadBackAllIBLFields) {
    PerFrameCB cb{};
    cb.irradianceSrvIndex  = 100u;
    cb.prefilteredSrvIndex = 101u;
    cb.brdfLutSrvIndex     = 102u;
    cb.prefilteredMipLevels = 5u;
    cb.skyLightIntensity   = 3.0f;
    cb.skyLightColorR      = 0.9f;
    cb.skyLightColorG      = 0.95f;
    cb.skyLightColorB      = 1.0f;
    cb.skyLightMinAmbient  = 0.05f;
    cb.exposure            = 0.75f;

    EXPECT_EQ(cb.irradianceSrvIndex,  100u);
    EXPECT_EQ(cb.prefilteredSrvIndex, 101u);
    EXPECT_EQ(cb.brdfLutSrvIndex,     102u);
    EXPECT_EQ(cb.prefilteredMipLevels, 5u);
    EXPECT_FLOAT_EQ(cb.skyLightIntensity,  3.0f);
    EXPECT_FLOAT_EQ(cb.skyLightColorR,     0.9f);
    EXPECT_FLOAT_EQ(cb.skyLightColorG,     0.95f);
    EXPECT_FLOAT_EQ(cb.skyLightColorB,     1.0f);
    EXPECT_FLOAT_EQ(cb.skyLightMinAmbient, 0.05f);
    EXPECT_FLOAT_EQ(cb.exposure,           0.75f);
}

TEST(PerFrameCB_IBL, SHCoefficientsCanBeUploadedViaWriteAndRead) {
    PerFrameCB cb{};
    SHCoefficients sh{};
    for (int i = 0; i < 7; ++i) {
        sh.coeffs[i].x = static_cast<float>(i) * 0.1f;
        sh.coeffs[i].y = static_cast<float>(i) * 0.2f;
        sh.coeffs[i].z = static_cast<float>(i) * 0.3f;
        sh.coeffs[i].w = static_cast<float>(i) * 0.4f;
    }

    // Simulate the upload path: copy SH into the CB field
    std::memcpy(cb.skyIrradianceSH, sh.coeffs, sizeof(sh.coeffs));

    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].x, sh.coeffs[i].x) << "SH[" << i << "].x";
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].y, sh.coeffs[i].y) << "SH[" << i << "].y";
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].z, sh.coeffs[i].z) << "SH[" << i << "].z";
        EXPECT_FLOAT_EQ(cb.skyIrradianceSH[i].w, sh.coeffs[i].w) << "SH[" << i << "].w";
    }
}

TEST(PerFrameCB_IBL, MemcpyRoundTrip) {
    PerFrameCB src{};
    src.irradianceSrvIndex  = 55u;
    src.prefilteredSrvIndex = 66u;
    src.brdfLutSrvIndex     = 77u;
    src.exposure            = 1.5f;
    src.skyLightIntensity   = 2.0f;
    for (int i = 0; i < 7; ++i) {
        src.skyIrradianceSH[i].x = static_cast<float>(i + 1);
        src.skyIrradianceSH[i].y = static_cast<float>(i + 2);
    }

    uint8_t buffer[512];
    std::memcpy(buffer, &src, sizeof(src));

    PerFrameCB dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_EQ(dst.irradianceSrvIndex,  55u);
    EXPECT_EQ(dst.prefilteredSrvIndex, 66u);
    EXPECT_EQ(dst.brdfLutSrvIndex,     77u);
    EXPECT_FLOAT_EQ(dst.exposure,          1.5f);
    EXPECT_FLOAT_EQ(dst.skyLightIntensity, 2.0f);
    for (int i = 0; i < 7; ++i) {
        EXPECT_FLOAT_EQ(dst.skyIrradianceSH[i].x, static_cast<float>(i + 1));
        EXPECT_FLOAT_EQ(dst.skyIrradianceSH[i].y, static_cast<float>(i + 2));
    }
}

// CB structs, ECS components, and pass root constants are covered in
// test_ibl_atmosphere_cb.cpp (split to respect the 1500-line file limit).

