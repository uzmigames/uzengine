// TAA jitter must be a pure NDC translation of the view-projection: every vertex
// moves by exactly (jitterX, jitterY) render pixels, independent of its depth
// and world position. Regression for phase0_sponza-followup-07: the offset used
// to be added to VP[2][0]/[2][1] of the combined view-projection, which scales
// with the vertex's WORLD z over its clip w — near geometry shook by several
// pixels per frame while far geometry barely moved.

#include "frame/taa_jitter.h"
#include "mat4.h"
#include "vector3.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr float kRenderW = 1920.0f;
constexpr float kRenderH = 1080.0f;
constexpr float kPi      = 3.14159265358979323846f;

struct Ndc { float x, y, z; };

Ndc Project(const uz::Mat4& vp, const uz::Vec3& p) {
    const uz::Vec4 c = vp * uz::Vec4{p.x, p.y, p.z, 1.0f};
    return {c.x / c.w, c.y / c.w, c.z / c.w};
}

// Sponza close-up pose from the report: z-up world, camera at 5.8 m height, a wall
// ~40 cm in front, the far wall of the atrium ~15 m away, floor at z = 0.
uz::Mat4 SponzaCloseUpViewProj() {
    const uz::Vec3 eye{-223.3f, -599.6f, 576.6f};
    const uz::Vec3 target{-418.6f, -1577.7f, 648.7f};
    const uz::Mat4 view = uz::Mat4::LookAt(eye, target, uz::Vec3{0.0f, 0.0f, 1.0f});
    const uz::Mat4 proj = uz::Mat4::Perspective(58.51f * kPi / 180.0f, kRenderW / kRenderH, 5.0f, 10000.0f);
    return view * proj; // row-vector convention: world -> view -> clip
}

uz::Vec3 ViewDir() {
    const uz::Vec3 d{-418.6f + 223.3f, -1577.7f + 599.6f, 648.7f - 576.6f};
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return {d.x / len, d.y / len, d.z / len};
}

uz::Vec3 Along(float distance, float dx, float dz) {
    const uz::Vec3 eye{-223.3f, -599.6f, 576.6f};
    const uz::Vec3 d = ViewDir();
    return {eye.x + d.x * distance + dx, eye.y + d.y * distance, eye.z + d.z * distance + dz};
}

} // namespace

TEST(TaaJitter, TranslatesEveryVertexByExactlyTheJitterInPixels) {
    const uz::Mat4 vp = SponzaCloseUpViewProj();
    const float jx = 0.375f, jy = -0.278f; // Halton(2,3) samples 7 and 6
    const uz::Mat4 jittered = uz::ApplyTaaJitter(vp, jx, jy, kRenderW, kRenderH);

    const uz::Vec3 points[] = {
        Along(40.0f, 0.0f, 0.0f),        // near wall, straight ahead
        Along(40.0f, 15.0f, -20.0f),     // near wall, off-centre
        Along(300.0f, -80.0f, 60.0f),    // mid distance
        Along(1500.0f, 200.0f, 100.0f),  // far wall
        {-223.3f - 50.0f, -1500.0f, 0.0f},    // floor (world z = 0) far away
        {-223.3f + 120.0f, -900.0f, 1000.0f}, // ceiling
    };
    for (const auto& p : points) {
        const Ndc a = Project(vp, p);
        const Ndc b = Project(jittered, p);
        const float dxPx = (b.x - a.x) * 0.5f * kRenderW;
        const float dyPx = (b.y - a.y) * -0.5f * kRenderH; // pixel y grows downward
        EXPECT_NEAR(dxPx, jx, 2e-3f) << "world (" << p.x << ", " << p.y << ", " << p.z << ")";
        EXPECT_NEAR(dyPx, jy, 2e-3f) << "world (" << p.x << ", " << p.y << ", " << p.z << ")";
        EXPECT_NEAR(b.z, a.z, 1e-6f) << "depth must not change";
    }
}

TEST(TaaJitter, ZeroJitterIsIdentity) {
    const uz::Mat4 vp = SponzaCloseUpViewProj();
    const uz::Mat4 j = uz::ApplyTaaJitter(vp, 0.0f, 0.0f, kRenderW, kRenderH);
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
            EXPECT_FLOAT_EQ(j.m.m[i][k], vp.m.m[i][k]);
}

// Documents the defect: the legacy formula shifts the near wall by an order of
// magnitude more than the requested jitter and the far wall by less.
TEST(TaaJitter, LegacyRowTwoOffsetWasDepthAndWorldZDependent) {
    const uz::Mat4 vp = SponzaCloseUpViewProj();
    const float jx = 0.375f;
    uz::Mat4 legacy = vp;
    legacy.m.m[2][0] += jx * 2.0f / kRenderW;

    const uz::Vec3 nearWall = Along(40.0f, 0.0f, 0.0f);
    const uz::Vec3 farWall  = Along(1500.0f, 0.0f, 0.0f);
    const float nearShiftPx = (Project(legacy, nearWall).x - Project(vp, nearWall).x) * 0.5f * kRenderW;
    const float farShiftPx  = (Project(legacy, farWall).x  - Project(vp, farWall).x)  * 0.5f * kRenderW;

    EXPECT_GT(std::fabs(nearShiftPx), 3.0f) << "legacy near-wall shift was several pixels, not sub-pixel";
    EXPECT_LT(std::fabs(farShiftPx), std::fabs(jx)) << "legacy far-wall shift was smaller than the jitter";
    EXPECT_GT(std::fabs(nearShiftPx) / std::fabs(farShiftPx), 10.0f);
}
