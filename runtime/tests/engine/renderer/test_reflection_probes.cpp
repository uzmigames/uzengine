/// Unit tests for the reflection probes (phase3a-3.37.0-reflection-probes), CPU-only, no GPU device.
///
/// - `ReflectionProbeComponent`   — defaults (the parallax box off), the `reflection_probe`
///                                  serializer: `box_extents` read, defaulted when absent, written,
///                                  the round trip
/// - `ReflectionProbeGPU`         — the 48-byte record the deferred lighting reads through
///                                  PerFrameCB::reflectionProbeBufferSrvIndex, field by field
///                                  (scripts/rendering/test_cbuffer_layouts.py reflects the HLSL copy)
/// - `BuildCubemapFaceMatrices`   — every face looks down its DX cube axis under the capture's
///                                  Z-up -> Y-up rotation (world (x, y, z) -> DX (y, z, x)), with the
///                                  DX face's up on screen-up and its right on screen-right, reversed-Z;
///                                  the same rotation the lighting shader's `ProbeWorldToCubemap`
///                                  applies, so a captured direction is read back where it was drawn
/// - the parallax lookup          — a CPU mirror of the shader's `ReflectionProbeLookup`: the box
///                                  exit point seen from the probe, the plain direction without a box,
///                                  a receiver outside the box projected from its closest point
///
/// The capture, the bake and the per-pixel blend run on the GPU; scripts/rendering/
/// test_reflection_probes.py measures them on the demo scene (reflection_probes_demo.py).

#include "reflection_probe_renderer.h"
#include "reflection_probe_component.h"
#include "component_serializer.h"
#include "constant_buffers.h"
#include "world.h"
#include "mat4.h"
#include "vector3.h"
#include "vector4.h"

#include <json.hpp>
#include <entt/entt.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace uz;

namespace {

constexpr const char* kSerializerKey = "reflection_probe";

entt::entity ToEntt(Entity e) { return static_cast<entt::entity>(static_cast<uint32_t>(e.id)); }

/// The shader's `ProbeWorldToCubemap`: world (x, y, z) -> DX cube (y, z, x).
Vec3 ProbeWorldToCubemap(const Vec3& w) { return Vec3{w.y, w.z, w.x}; }

/// Its inverse: a DX cube direction back to the world.
Vec3 ProbeCubemapToWorld(const Vec3& d) { return Vec3{d.z, d.x, d.y}; }

/// Normalised device coordinates of a world point under a row-vector view-projection
/// (`mul(float4(p, 1), viewProj)` in HLSL).
struct Ndc { float x, y, z, w; };

Ndc Project(const Mat4& viewProj, const Vec3& p) {
    const Vec4 clip = viewProj * Vec4(p.x, p.y, p.z, 1.0f);
    return Ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w, clip.w};
}

/// CPU mirror of the shader's `ReflectionProbeLookup` (deferred_lighting_ps.hlsl).
Vec3 ParallaxLookup(const Vec3& probePos, const Vec3& boxHalf, const Vec3& worldPos, const Vec3& R) {
    if (boxHalf.x <= 0.0f || boxHalf.y <= 0.0f || boxHalf.z <= 0.0f) return R;
    const float p[3] = {probePos.x, probePos.y, probePos.z};
    const float h[3] = {boxHalf.x, boxHalf.y, boxHalf.z};
    const float w[3] = {worldPos.x, worldPos.y, worldPos.z};
    const float r[3] = {R.x, R.y, R.z};
    float o[3];
    float t = INFINITY;
    for (int i = 0; i < 3; ++i) {
        o[i] = std::clamp(w[i], p[i] - h[i], p[i] + h[i]);   // the closest point of the box
    }
    for (int i = 0; i < 3; ++i) {
        const float sgn   = r[i] <= 0.0f ? -1.0f : 1.0f;
        const float safeR = sgn * std::max(std::fabs(r[i]), 1e-5f);
        const float invR  = 1.0f / safeR;
        t = std::min(t, std::max((p[i] + h[i] - o[i]) * invR, (p[i] - h[i] - o[i]) * invR));
    }
    return Vec3{o[0] + r[0] * t - p[0], o[1] + r[1] * t - p[1], o[2] + r[2] * t - p[2]};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ReflectionProbeComponent
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReflectionProbeComponent, DefaultsHaveNoParallaxBox) {
    const ReflectionProbeComponent c{};
    EXPECT_FLOAT_EQ(c.influenceRadius, 300.0f);
    EXPECT_FLOAT_EQ(c.blendDistance, 100.0f);
    EXPECT_FLOAT_EQ(c.boxExtents.x, 0.0f);
    EXPECT_FLOAT_EQ(c.boxExtents.y, 0.0f);
    EXPECT_FLOAT_EQ(c.boxExtents.z, 0.0f);
    EXPECT_EQ(c.resolution, 128u);
    EXPECT_EQ(c.priority, 0);
    EXPECT_TRUE(c.enabled);
    EXPECT_TRUE(c.needsCapture);
    EXPECT_EQ(c.prefilteredSrvIndex, UINT32_MAX);
    EXPECT_EQ(c.irradianceSrvIndex, UINT32_MAX);
}

TEST(ReflectionProbeSerializer, RegisteredUnderTheReflectionProbeKey) {
    EXPECT_TRUE(ComponentSerializerRegistry::Get().Has(kSerializerKey));
}

TEST(ReflectionProbeSerializer, DeserializeReadsBoxExtents) {
    World world;
    const Entity e = world.CreateEntity();
    // 256u: json::Uint reads an unsigned number, which is what the parser makes of a scene file's 256.
    const nlohmann::json j = {
        {"influence_radius", 650.0}, {"blend_distance", 200.0}, {"box_extents", {400.0, 400.0, 150.0}},
        {"resolution", 256u}, {"priority", 1}, {"enabled", true}, {"needs_capture", false},
    };
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<ReflectionProbeComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->influenceRadius, 650.0f);
    EXPECT_FLOAT_EQ(c->blendDistance, 200.0f);
    EXPECT_FLOAT_EQ(c->boxExtents.x, 400.0f);
    EXPECT_FLOAT_EQ(c->boxExtents.y, 400.0f);
    EXPECT_FLOAT_EQ(c->boxExtents.z, 150.0f);
    EXPECT_EQ(c->resolution, 256u);
    EXPECT_EQ(c->priority, 1);
    EXPECT_TRUE(c->enabled);
    EXPECT_FALSE(c->needsCapture);
    // Runtime SRV indices are never read from the scene.
    EXPECT_EQ(c->prefilteredSrvIndex, UINT32_MAX);
    EXPECT_EQ(c->irradianceSrvIndex, UINT32_MAX);
}

TEST(ReflectionProbeSerializer, MissingBoxExtentsKeepsTheInfiniteEnvironment) {
    World world;
    const Entity e = world.CreateEntity();
    const nlohmann::json j = {{"influence_radius", 2000.0}, {"resolution", 128}};   // the shipped scenes' form
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<ReflectionProbeComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->influenceRadius, 2000.0f);
    EXPECT_FLOAT_EQ(c->boxExtents.x, 0.0f);
    EXPECT_FLOAT_EQ(c->boxExtents.y, 0.0f);
    EXPECT_FLOAT_EQ(c->boxExtents.z, 0.0f);
    EXPECT_FLOAT_EQ(c->blendDistance, 100.0f);
    EXPECT_TRUE(c->enabled);
    EXPECT_TRUE(c->needsCapture);
}

TEST(ReflectionProbeSerializer, RoundTripKeepsBoxExtents) {
    World world;
    const Entity src = world.CreateEntity();
    auto& c = world.GetRegistry().emplace<ReflectionProbeComponent>(ToEntt(src));
    c.influenceRadius = 900.0f;
    c.blendDistance = 300.0f;
    c.boxExtents = Vec3{250.0f, 125.0f, 75.0f};
    c.resolution = 64;
    c.priority = -2;
    c.enabled = false;
    c.needsCapture = false;

    nlohmann::json j;
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Serialize(kSerializerKey, world, src, &j));
    ASSERT_TRUE(j.contains("box_extents"));
    ASSERT_TRUE(j["box_extents"].is_array());
    ASSERT_EQ(j["box_extents"].size(), 3u);
    EXPECT_FLOAT_EQ(j["box_extents"][0].get<float>(), 250.0f);
    EXPECT_FLOAT_EQ(j["box_extents"][1].get<float>(), 125.0f);
    EXPECT_FLOAT_EQ(j["box_extents"][2].get<float>(), 75.0f);
    EXPECT_FALSE(j.contains("prefiltered_srv_index"));

    const Entity dst = world.CreateEntity();
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, dst, &j));
    const auto* d = world.GetRegistry().try_get<ReflectionProbeComponent>(ToEntt(dst));
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->influenceRadius, 900.0f);
    EXPECT_FLOAT_EQ(d->blendDistance, 300.0f);
    EXPECT_FLOAT_EQ(d->boxExtents.x, 250.0f);
    EXPECT_FLOAT_EQ(d->boxExtents.y, 125.0f);
    EXPECT_FLOAT_EQ(d->boxExtents.z, 75.0f);
    EXPECT_EQ(d->resolution, 64u);
    EXPECT_EQ(d->priority, -2);
    EXPECT_FALSE(d->enabled);
    EXPECT_FALSE(d->needsCapture);
}

// ─────────────────────────────────────────────────────────────────────────────
// ReflectionProbeGPU — the structured-buffer record the shader reads
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReflectionProbeGPU, Is48BytesInThreeFloat4Rows) {
    EXPECT_EQ(sizeof(ReflectionProbeGPU), 48u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, posX), 0u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, posY), 4u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, posZ), 8u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, influenceRadius), 12u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, blendDistance), 16u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, boxHalfX), 20u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, boxHalfY), 24u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, boxHalfZ), 28u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, irradianceSrvIndex), 32u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, prefilteredSrvIndex), 36u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, prefilteredMipLevels), 40u);
    EXPECT_EQ(offsetof(ReflectionProbeGPU, _pad0), 44u);
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildCubemapFaceMatrices — the capture's face layout
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// DX cube face conventions (look, up, right) in DX space: what a TextureCube sampler expects
/// of slice `face` (D3D11 3.4.1 Cube Map Face Orientation).
struct DxFace { Vec3 look, up, right; };

const DxFace kDxFaces[kCubeFaceCount] = {
    {{ 1, 0, 0}, {0,  1, 0}, { 0, 0, -1}},   // +X
    {{-1, 0, 0}, {0,  1, 0}, { 0, 0,  1}},   // -X
    {{ 0, 1, 0}, {0,  0, -1}, { 1, 0,  0}},  // +Y
    {{ 0,-1, 0}, {0,  0, 1}, { 1, 0,  0}},   // -Y
    {{ 0, 0, 1}, {0,  1, 0}, { 1, 0,  0}},   // +Z
    {{ 0, 0,-1}, {0,  1, 0}, {-1, 0,  0}},   // -Z
};

} // namespace

TEST(CubemapFaceMatrices, EachFaceLooksDownItsDxAxisUnderTheCaptureRotation) {
    const Vec3 probe{120.0f, -340.0f, 150.0f};
    const CubemapFaceMatrices faces = BuildCubemapFaceMatrices(probe, 10.0f, 100000.0f);
    for (uint32_t f = 0; f < kCubeFaceCount; ++f) {
        const Vec3 look  = ProbeCubemapToWorld(kDxFaces[f].look);
        const Vec3 up    = ProbeCubemapToWorld(kDxFaces[f].up);
        const Vec3 right = ProbeCubemapToWorld(kDxFaces[f].right);
        const Mat4& vp = faces.viewProj[f];

        // The face's axis lands on the face centre, in front of the camera.
        const Ndc centre = Project(vp, Vec3{probe.x + look.x * 500.0f, probe.y + look.y * 500.0f, probe.z + look.z * 500.0f});
        EXPECT_GT(centre.w, 0.0f) << "face " << f;
        EXPECT_NEAR(centre.x, 0.0f, 1e-4f) << "face " << f;
        EXPECT_NEAR(centre.y, 0.0f, 1e-4f) << "face " << f;

        // The DX face's up is screen-up, its right is screen-right (a 90-degree FOV: 500 cm ahead,
        // 250 cm aside is NDC 0.5).
        const Ndc top = Project(vp, Vec3{probe.x + look.x * 500.0f + up.x * 250.0f,
                                         probe.y + look.y * 500.0f + up.y * 250.0f,
                                         probe.z + look.z * 500.0f + up.z * 250.0f});
        EXPECT_NEAR(top.x, 0.0f, 1e-4f) << "face " << f;
        EXPECT_NEAR(top.y, 0.5f, 1e-4f) << "face " << f;
        const Ndc side = Project(vp, Vec3{probe.x + look.x * 500.0f + right.x * 250.0f,
                                          probe.y + look.y * 500.0f + right.y * 250.0f,
                                          probe.z + look.z * 500.0f + right.z * 250.0f});
        EXPECT_NEAR(side.x, 0.5f, 1e-4f) << "face " << f;
        EXPECT_NEAR(side.y, 0.0f, 1e-4f) << "face " << f;

        // The shader's lookup rotation sends the world axis back to this face's DX axis.
        const Vec3 d = ProbeWorldToCubemap(look);
        EXPECT_FLOAT_EQ(d.x, kDxFaces[f].look.x) << "face " << f;
        EXPECT_FLOAT_EQ(d.y, kDxFaces[f].look.y) << "face " << f;
        EXPECT_FLOAT_EQ(d.z, kDxFaces[f].look.z) << "face " << f;
    }
}

TEST(CubemapFaceMatrices, ReversedZProjectionNearIsOneFarIsZero) {
    const Vec3 probe{0.0f, 0.0f, 150.0f};
    const CubemapFaceMatrices faces = BuildCubemapFaceMatrices(probe, 10.0f, 100000.0f);
    const Vec3 look = ProbeCubemapToWorld(kDxFaces[0].look);   // world +Y
    const Ndc near = Project(faces.viewProj[0], Vec3{probe.x + look.x * 10.0f, probe.y + look.y * 10.0f, probe.z + look.z * 10.0f});
    const Ndc mid  = Project(faces.viewProj[0], Vec3{probe.x + look.x * 1000.0f, probe.y + look.y * 1000.0f, probe.z + look.z * 1000.0f});
    const Ndc far  = Project(faces.viewProj[0], Vec3{probe.x + look.x * 100000.0f, probe.y + look.y * 100000.0f, probe.z + look.z * 100000.0f});
    EXPECT_NEAR(near.z, 1.0f, 1e-4f);
    EXPECT_NEAR(far.z, 0.0f, 1e-4f);
    EXPECT_GT(mid.z, far.z);
    EXPECT_LT(mid.z, near.z);
}

// -- BuildProbeShadowCascades: the capture's sun shadow map (item 1.10) -----------------------

TEST(ProbeShadowCascades, RadiiCoverTheProbeRegionAndItsSurroundings) {
    const Vec3 probe{0.0f, 0.0f, 150.0f};
    const Vec3 sun{0.3f, -0.4f, 0.866f};
    // Sphere larger than the box: cascade 0 is the influence radius.
    auto c = BuildProbeShadowCascades(probe, 600.0f, Vec3{300.0f, 300.0f, 150.0f}, sun, kCaptureShadowResolution);
    EXPECT_FLOAT_EQ(c[0].radius, 600.0f);
    EXPECT_FLOAT_EQ(c[1].radius, 600.0f * kCaptureShadowFarScale);
    // Box corner farther than the sphere: cascade 0 grows to it.
    const Vec3 bigBox{400.0f, 400.0f, 150.0f};
    c = BuildProbeShadowCascades(probe, 300.0f, bigBox, sun, kCaptureShadowResolution);
    EXPECT_NEAR(c[0].radius, bigBox.Length(), 1e-3f);
    // No box (a component <= 0), tiny sphere: the 1 m floor; cascade 1 capped at the frame's CSM range.
    c = BuildProbeShadowCascades(probe, 10.0f, Vec3{0.0f, 0.0f, 0.0f}, sun, kCaptureShadowResolution);
    EXPECT_FLOAT_EQ(c[0].radius, 100.0f);
    c = BuildProbeShadowCascades(probe, 20000.0f, Vec3{0.0f, 0.0f, 0.0f}, sun, kCaptureShadowResolution);
    EXPECT_FLOAT_EQ(c[1].radius, kCaptureShadowFarMax);
    // Texel size and the frame's bias rules: world-scaled depth bias, 2 cm normal bias floored at 1.5 texels.
    c = BuildProbeShadowCascades(probe, 512.0f, Vec3{0.0f, 0.0f, 0.0f}, sun, 1024);
    EXPECT_FLOAT_EQ(c[0].texelSize, 1.0f);
    EXPECT_FLOAT_EQ(c[0].depthRange, 9.0f * 512.0f);
    EXPECT_NEAR(c[0].depthBias, (10.0f / (9.0f * 512.0f)) * 1.0f, 1e-7f);
    EXPECT_FLOAT_EQ(c[0].normalBias, 2.0f);
    EXPECT_FLOAT_EQ(c[1].normalBias, 1.5f * c[1].texelSize);
}

TEST(ProbeShadowCascades, ProbeProjectsToTheTileCentreAtStandardZDepth) {
    const Vec3 probe{120.0f, -340.0f, 150.0f};
    const Vec3 sun = Vec3{0.3f, -0.4f, 0.866f}.Normalized();
    const auto c = BuildProbeShadowCascades(probe, 500.0f, Vec3{0.0f, 0.0f, 0.0f}, sun, kCaptureShadowResolution);
    for (uint32_t i = 0; i < kCaptureShadowCascades; ++i) {
        // The probe sits at the tile centre; the eye is 8 radii toward the sun, so its standard-Z
        // depth is 8 / 9 of the volume.
        const Ndc centre = Project(c[i].viewProj, probe);
        EXPECT_NEAR(centre.x, 0.0f, 1e-4f) << "cascade " << i;
        EXPECT_NEAR(centre.y, 0.0f, 1e-4f) << "cascade " << i;
        EXPECT_NEAR(centre.z, 8.0f / 9.0f, 1e-4f) << "cascade " << i;
        // A point one radius toward the sun is nearer the light (smaller standard-Z); one radius
        // sideways (perpendicular to the sun) reaches the tile edge |ndc| = 1.
        const float r = c[i].radius;
        const Ndc toSun = Project(c[i].viewProj, Vec3{probe.x + sun.x * r, probe.y + sun.y * r, probe.z + sun.z * r});
        EXPECT_NEAR(toSun.z, 7.0f / 9.0f, 1e-4f) << "cascade " << i;
        const Vec3 side = sun.Cross(Vec3{0.0f, 0.0f, 1.0f}).Normalized();
        const Ndc edge = Project(c[i].viewProj, Vec3{probe.x + side.x * r, probe.y + side.y * r, probe.z + side.z * r});
        EXPECT_NEAR(std::max(std::abs(edge.x), std::abs(edge.y)), 1.0f, 1e-3f) << "cascade " << i;
        EXPECT_NEAR(edge.z, 8.0f / 9.0f, 1e-4f) << "cascade " << i;
    }
    // A sun straight overhead still yields a usable (non-degenerate) view.
    const auto up = BuildProbeShadowCascades(probe, 500.0f, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, kCaptureShadowResolution);
    const Ndc centre = Project(up[0].viewProj, probe);
    EXPECT_NEAR(centre.z, 8.0f / 9.0f, 1e-4f);
    EXPECT_TRUE(std::isfinite(centre.x) && std::isfinite(centre.y));
}

// ─────────────────────────────────────────────────────────────────────────────
// Parallax lookup (CPU mirror of ReflectionProbeLookup)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReflectionProbeParallax, NoBoxIsThePlainDirection) {
    const Vec3 R{0.6f, 0.0f, 0.8f};
    const Vec3 d = ParallaxLookup(Vec3{0, 0, 150}, Vec3{0, 0, 0}, Vec3{-250, 120, 70}, R);
    EXPECT_FLOAT_EQ(d.x, R.x);
    EXPECT_FLOAT_EQ(d.y, R.y);
    EXPECT_FLOAT_EQ(d.z, R.z);
}

TEST(ReflectionProbeParallax, ReceiverNearAWallReflectsThatWallNotTheProbesView) {
    // The demo room: probe at the centre, half-extents (400, 400, 150). A receiver 90 cm from the
    // west wall (x = -400) reflecting straight west hits the wall at x = -400 in front of itself.
    const Vec3 probe{0, 0, 150}, half{400, 400, 150}, P{-310, 120, 70};
    const Vec3 d = ParallaxLookup(probe, half, P, Vec3{-1, 0, 0});
    // The hit is (-400, 120, 70); seen from the probe that is (-400, 120, -80).
    EXPECT_NEAR(d.x, -400.0f, 1e-3f);
    EXPECT_NEAR(d.y, 120.0f, 1e-3f);
    EXPECT_NEAR(d.z, -80.0f, 1e-3f);
    // Without the correction the lookup would be the plain -X: the wall's centre from the probe.
    EXPECT_GT(std::fabs(d.y / d.x), 0.25f);
}

TEST(ReflectionProbeParallax, DiagonalRayExitsTheNearestSlab) {
    // From the centre of a 2 x 2 x 2 box, a ray towards (+x, +y) at 2:1 leaves through x = +1 first.
    const Vec3 d = ParallaxLookup(Vec3{0, 0, 0}, Vec3{1, 1, 1}, Vec3{0, 0, 0}, Vec3{0.8944272f, 0.4472136f, 0.0f});
    EXPECT_NEAR(d.x, 1.0f, 1e-4f);
    EXPECT_NEAR(d.y, 0.5f, 1e-4f);
    EXPECT_NEAR(d.z, 0.0f, 1e-4f);
    // An axis-aligned ray (two zero components) is bounded by its own slab only.
    const Vec3 up = ParallaxLookup(Vec3{0, 0, 0}, Vec3{1, 1, 1}, Vec3{0.3f, -0.2f, 0.0f}, Vec3{0, 0, 1});
    EXPECT_NEAR(up.x, 0.3f, 1e-4f);
    EXPECT_NEAR(up.y, -0.2f, 1e-4f);
    EXPECT_NEAR(up.z, 1.0f, 1e-4f);
}

TEST(ReflectionProbeParallax, ReceiverOutsideTheBoxProjectsFromItsClosestPoint) {
    // A receiver beyond the +x face reflecting back towards the box: projected from (1, 0.5, 0), the
    // closest point of the box, the ray leaves through x = -1.
    const Vec3 d = ParallaxLookup(Vec3{0, 0, 0}, Vec3{1, 1, 1}, Vec3{5, 0.5f, 0}, Vec3{-1, 0, 0});
    EXPECT_NEAR(d.x, -1.0f, 1e-4f);
    EXPECT_NEAR(d.y, 0.5f, 1e-4f);
    EXPECT_NEAR(d.z, 0.0f, 1e-4f);
    // Continuous across the face: a receiver just inside it looks up almost the same direction.
    const Vec3 in = ParallaxLookup(Vec3{0, 0, 0}, Vec3{1, 1, 1}, Vec3{0.999f, 0.5f, 0}, Vec3{-1, 0, 0});
    EXPECT_NEAR(in.x, d.x, 1e-3f);
    EXPECT_NEAR(in.y, d.y, 1e-3f);
    // Reflecting away from the box from outside: the ray leaves at the closest point (t = 0).
    const Vec3 away = ParallaxLookup(Vec3{0, 0, 0}, Vec3{1, 1, 1}, Vec3{5, 0, 0}, Vec3{1, 0, 0});
    EXPECT_NEAR(away.x, 1.0f, 1e-4f);
    EXPECT_NEAR(away.y, 0.0f, 1e-4f);
    EXPECT_NEAR(away.z, 0.0f, 1e-4f);
}

TEST(ReflectionProbeParallax, AFlatBoxIsNoProxy) {
    // A half-extent of 0 cannot bound a room: the plain direction, as with no box.
    const Vec3 R{0.6f, 0.0f, 0.8f};
    const Vec3 d = ParallaxLookup(Vec3{0, 0, 150}, Vec3{400, 400, 0}, Vec3{-250, 120, 70}, R);
    EXPECT_FLOAT_EQ(d.x, R.x);
    EXPECT_FLOAT_EQ(d.y, R.y);
    EXPECT_FLOAT_EQ(d.z, R.z);
}
