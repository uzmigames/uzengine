/// Unit tests for the point / spot light shadow pipeline (phase3a-3.30.0, CPU-only, no GPU device).
///
/// - `LocalLightShadowData` / `ShadowDataCB` — the 96-byte entry the shader reads as six float4
///   (`localShadowRaw[]`, shadow.hlsli) and the CB offsets the HLSL copy declares
/// - `LocalLightShadowAtlas` — sequential row-major tile allocation, the UV rects, the refusal when
///   all 16 tiles are taken, the per-frame `Reset`, the default layout `Init` publishes
/// - `ExtractLightingFromECS` — `castShadows` selection, the importance ranking
///   (`intensity * radius^2 / distance^2`), the 16-entry budget split between spot lights (one tile
///   each, allocated first) and point lights (six consecutive tiles after the spots), the per-light
///   parameters the shadow pass receives and the `shadowIndex` the LightingCB carries
/// - the cube-face convention — the six face cameras frame_constants.cpp builds (forward / up table,
///   90-degree FOV, aspect 1) and the major-axis face selection of deferred_lighting_ps.hlsl agree:
///   every direction lands inside the frustum of the face selected for it and both faces that share
///   a seam contain the seam, so a receiver is never outside every tile (lit by default) and never
///   read from a face that does not cover it
/// - the spot tile — the perspective frame_constants.cpp builds (FOV = 2 * outer angle, aspect 1)
///   puts the cone boundary exactly on the tile edge, including with the vertical-direction up
///   fallback
///
/// The face table, the face selection and the spot/face camera setup are mirrored here from
/// frame_constants.cpp / deferred_lighting_ps.hlsl (they are not exported); the atlas, the light
/// extraction and the Camera are the production code.

#include "light_renderer.h"
#include "local_light_shadow_atlas.h"
#include "light_component.h"
#include "constant_buffers.h"
#include "shadow_data_params.h"
#include "camera.h"
#include "mat4.h"
#include "quaternion.h"
#include "vector3.h"
#include "vector4.h"
#include "world.h"
#include "mock_rhi_device.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace uz;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Rig: a World with lights, extracted through a CB ring on the mock device
// ─────────────────────────────────────────────────────────────────────────────

struct ExtractionRig {
    uz::test::MockRHIDevice device;
    CBRingAllocator ring;
    World world;

    ExtractionRig() {
        // LightingCB is 43264 bytes; the mock maps a 64 KB scratch, one allocation per BeginFrame.
        EXPECT_TRUE(ring.Init(&device, 1, 64 * 1024));
    }

    PointLightComponent& AddPoint(const Vec3& pos, float intensity, float range, bool castShadows) {
        auto& pl = world.AddComponent<PointLightComponent>(world.CreateEntity());
        pl.SetPosition(pos);
        pl.intensity = intensity;
        pl.range = range;
        pl.castShadows = castShadows;
        return pl;
    }

    SpotLightComponent& AddSpot(const Vec3& pos, float intensity, float range, bool castShadows) {
        auto& sl = world.AddComponent<SpotLightComponent>(world.CreateEntity());
        sl.SetPosition(pos);
        sl.intensity = intensity;
        sl.range = range;
        sl.castShadows = castShadows;
        return sl;
    }

    LightFrameData Extract(const Vec3& cameraPos) {
        ring.BeginFrame(0);
        return ExtractLightingFromECS(&world, ring, cameraPos);
    }

    static const LightingCB& CB(const LightFrameData& data) {
        return *static_cast<const LightingCB*>(data.lightingCBAlloc.cpuPtr);
    }
};

bool SamePosition(const Vec3& a, const Vec3& b) {
    return (a - b).Length() < 1e-3f;
}

/// shadowIndex of the point light at `pos` in the uploaded LightingCB.
uint32_t PointShadowIndexAt(const LightingCB& cb, const Vec3& pos) {
    for (uint32_t i = 0; i < cb.pointLightCount; ++i) {
        if (SamePosition(cb.pointLights[i].position, pos)) return cb.pointLights[i].shadowIndex;
    }
    ADD_FAILURE() << "no point light at (" << pos.x << ", " << pos.y << ", " << pos.z << ")";
    return 0xDEADBEEFu;
}

/// shadowIndex of the spot light at `pos` in the uploaded LightingCB.
uint32_t SpotShadowIndexAt(const LightingCB& cb, const Vec3& pos) {
    for (uint32_t i = 0; i < cb.spotLightCount; ++i) {
        if (SamePosition(cb.spotLights[i].position, pos)) return cb.spotLights[i].shadowIndex;
    }
    ADD_FAILURE() << "no spot light at (" << pos.x << ", " << pos.y << ", " << pos.z << ")";
    return 0xDEADBEEFu;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cube faces and the spot tile, as frame_constants.cpp / deferred_lighting_ps.hlsl define them
// ─────────────────────────────────────────────────────────────────────────────

struct CubeFace {
    Vec3 forward;
    Vec3 up;
};

/// Mirrors `kCubeFaces` in frame_constants.cpp: the atlas order of a point light's six tiles.
constexpr CubeFace kCubeFaces[kPointLightShadowFaces] = {
    {{ 1.f,  0.f,  0.f}, { 0.f,  0.f,  1.f}},  // +X
    {{-1.f,  0.f,  0.f}, { 0.f,  0.f,  1.f}},  // -X
    {{ 0.f,  1.f,  0.f}, { 0.f,  0.f,  1.f}},  // +Y
    {{ 0.f, -1.f,  0.f}, { 0.f,  0.f,  1.f}},  // -Y
    {{ 0.f,  0.f,  1.f}, { 0.f, -1.f,  0.f}},  // +Z
    {{ 0.f,  0.f, -1.f}, { 0.f,  1.f,  0.f}},  // -Z
};

/// Mirrors the major-axis face selection of `ShadePointLight` / `LocalShadowTapUV`
/// (deferred_lighting_ps.hlsl) for the light -> pixel direction `d`.
uint32_t SelectCubeFace(const Vec3& d) {
    const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    if (ax >= ay && ax >= az) return d.x > 0.0f ? 0u : 1u;
    if (ay >= az) return d.y > 0.0f ? 2u : 3u;
    return d.z > 0.0f ? 4u : 5u;
}

/// The face camera frame_constants.cpp builds: lookAt along the face, 90-degree FOV, aspect 1.
Mat4 FaceViewProj(const Vec3& lightPos, uint32_t face, float nearZ, float farZ) {
    Camera cam;
    cam.SetPosition(lightPos);
    cam.LookAt(lightPos + kCubeFaces[face].forward, kCubeFaces[face].up);
    cam.SetHorizontalFOV(90.0f, 1.0f, nearZ, farZ);
    return cam.GetViewProjection();
}

/// The spot camera frame_constants.cpp builds: FOV = 2 * outer angle, aspect 1, Z up unless the
/// direction is (nearly) vertical, then +Y.
Mat4 SpotViewProj(const Vec3& pos, const Vec3& dir, float outerAngleRad, float nearZ, float farZ, Vec3* usedUp) {
    Vec3 up = Vec3::Up();
    if (std::fabs(dir.Dot(up)) > 0.99f) up = Vec3{0.f, 1.f, 0.f};
    if (usedUp) *usedUp = up;
    Camera cam;
    cam.SetPosition(pos);
    cam.LookAt(pos + dir, up);
    cam.SetHorizontalFOV(2.0f * outerAngleRad / kDegToRad, 1.0f, nearZ, farZ);
    return cam.GetViewProjection();
}

struct Ndc {
    float x, y, w;
};

/// `mul(float4(p, 1), vp)` as the shader does it (row vector), perspective-divided.
Ndc Project(const Mat4& vp, const Vec3& p) {
    const Vec4 clip = vp * Vec4(p, 1.0f);
    return {clip.x / clip.w, clip.y / clip.w, clip.w};
}

bool InsideTile(const Ndc& n, float eps = 1e-4f) {
    return n.w > 0.0f && std::fabs(n.x) <= 1.0f + eps && std::fabs(n.y) <= 1.0f + eps;
}

/// Fibonacci sphere: `count` directions spread over the whole sphere.
std::vector<Vec3> SphereDirections(int count) {
    std::vector<Vec3> out;
    out.reserve(static_cast<size_t>(count));
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phi = golden * static_cast<float>(i);
        out.push_back(Vec3{r * std::cos(phi), r * std::sin(phi), z});
    }
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(LocalLightShadows, ShadowEntryLayoutMatchesTheShaderFloat4View) {
    // shadow.hlsli reads `float4 localShadowRaw[96]`: entry e = float4 6e..6e+5 ->
    // [0..3] viewProj rows, [4] (atlasOffsetX, atlasOffsetY, atlasScale, bias), [5] (light xyz, radius).
    EXPECT_EQ(sizeof(LocalLightShadowData), 96u);
    EXPECT_EQ(offsetof(LocalLightShadowData, viewProj),     0u);
    EXPECT_EQ(offsetof(LocalLightShadowData, atlasOffsetX), 64u);
    EXPECT_EQ(offsetof(LocalLightShadowData, atlasOffsetY), 68u);
    EXPECT_EQ(offsetof(LocalLightShadowData, atlasScale),   72u);
    EXPECT_EQ(offsetof(LocalLightShadowData, bias),         76u);
    EXPECT_EQ(offsetof(LocalLightShadowData, lightPosX),    80u);
    EXPECT_EQ(offsetof(LocalLightShadowData, lightPosY),    84u);
    EXPECT_EQ(offsetof(LocalLightShadowData, lightPosZ),    88u);
    EXPECT_EQ(offsetof(LocalLightShadowData, radius),       92u);

    // ShadowDataCB: 768-byte CSM section, then the 16 entries (16 * 96 = 1536) -> 2304.
    EXPECT_EQ(kMaxLocalLightShadows, 16u);
    EXPECT_EQ(offsetof(ShadowDataCB, localShadowAtlasIndex),   696u);
    EXPECT_EQ(offsetof(ShadowDataCB, localShadowCount),        700u);
    EXPECT_EQ(offsetof(ShadowDataCB, localShadowNormalOffset), 704u);
    EXPECT_EQ(offsetof(ShadowDataCB, localSoftPCSS),           752u);
    EXPECT_EQ(offsetof(ShadowDataCB, localPcssSourceRadius),   756u);
    EXPECT_EQ(offsetof(ShadowDataCB, localShadows),            768u);
    EXPECT_EQ(sizeof(ShadowDataCB), 2304u);
    EXPECT_EQ(96u * 16u, 1536u);

    // The binder copy carries the same scalar section and entries.
    EXPECT_EQ(offsetof(ShadowDataParams, localShadows), offsetof(ShadowDataCB, localShadows));
    EXPECT_EQ(offsetof(ShadowDataParams, localShadowCount), offsetof(ShadowDataCB, localShadowCount));

    // A point light owns six consecutive entries; the frame can hold two of them (16 / 6).
    EXPECT_EQ(kPointLightShadowFaces, 6u);
    LightFrameData frame{};
    EXPECT_EQ(sizeof(frame.pointShadows) / sizeof(frame.pointShadows[0]), 2u);
    EXPECT_EQ(sizeof(frame.spotShadows) / sizeof(frame.spotShadows[0]), 16u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas allocation
// ─────────────────────────────────────────────────────────────────────────────

TEST(LocalLightShadowAtlasTest, AllocatesSixteenTilesRowMajorThenRefuses) {
    LocalLightShadowAtlas atlas; // the default layout: 4096 / 1024 -> 4 columns, 16 tiles
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 0u);
    EXPECT_EQ(kMaxLocalShadowTiles, 16u);
    EXPECT_EQ(kLocalShadowTileCols, 4u);

    for (uint32_t i = 0; i < kMaxLocalShadowTiles; ++i) {
        ShadowTileInfo tile{};
        ASSERT_TRUE(atlas.AllocateTile(tile)) << "tile " << i;
        const uint32_t col = i % kLocalShadowTileCols;
        const uint32_t row = i / kLocalShadowTileCols;
        EXPECT_EQ(tile.viewportX, col * kLocalShadowTileSize) << "tile " << i;
        EXPECT_EQ(tile.viewportY, row * kLocalShadowTileSize) << "tile " << i;
        EXPECT_EQ(tile.viewportSize, kLocalShadowTileSize);
        EXPECT_FLOAT_EQ(tile.uvOffsetX, 0.25f * static_cast<float>(col));
        EXPECT_FLOAT_EQ(tile.uvOffsetY, 0.25f * static_cast<float>(row));
        EXPECT_FLOAT_EQ(tile.uvScale, 0.25f);
        EXPECT_EQ(atlas.GetAllocatedTileCount(), i + 1);
    }

    ShadowTileInfo extra{};
    EXPECT_FALSE(atlas.AllocateTile(extra));
    EXPECT_EQ(atlas.GetAllocatedTileCount(), kMaxLocalShadowTiles);

    atlas.Reset();
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 0u);
    ShadowTileInfo first{};
    ASSERT_TRUE(atlas.AllocateTile(first));
    EXPECT_EQ(first.viewportX, 0u);
    EXPECT_EQ(first.viewportY, 0u);
    EXPECT_FLOAT_EQ(first.uvOffsetX, 0.0f);
    EXPECT_FLOAT_EQ(first.uvOffsetY, 0.0f);
}

TEST(LocalLightShadowAtlasTest, TilesDoNotOverlapAndCoverTheAtlas) {
    LocalLightShadowAtlas atlas;
    std::vector<ShadowTileInfo> tiles(kMaxLocalShadowTiles);
    for (auto& t : tiles) ASSERT_TRUE(atlas.AllocateTile(t));
    uint64_t covered = 0;
    for (size_t i = 0; i < tiles.size(); ++i) {
        covered += static_cast<uint64_t>(tiles[i].viewportSize) * tiles[i].viewportSize;
        for (size_t j = i + 1; j < tiles.size(); ++j) {
            const bool disjointX = tiles[i].viewportX + tiles[i].viewportSize <= tiles[j].viewportX
                                || tiles[j].viewportX + tiles[j].viewportSize <= tiles[i].viewportX;
            const bool disjointY = tiles[i].viewportY + tiles[i].viewportSize <= tiles[j].viewportY
                                || tiles[j].viewportY + tiles[j].viewportSize <= tiles[i].viewportY;
            EXPECT_TRUE(disjointX || disjointY) << "tiles " << i << " and " << j << " overlap";
        }
        // The UV rect is the viewport rect in atlas units.
        EXPECT_FLOAT_EQ(tiles[i].uvOffsetX * static_cast<float>(kLocalShadowAtlasSize), static_cast<float>(tiles[i].viewportX));
        EXPECT_FLOAT_EQ(tiles[i].uvOffsetY * static_cast<float>(kLocalShadowAtlasSize), static_cast<float>(tiles[i].viewportY));
        EXPECT_FLOAT_EQ(tiles[i].uvScale * static_cast<float>(kLocalShadowAtlasSize), static_cast<float>(tiles[i].viewportSize));
    }
    EXPECT_EQ(covered, static_cast<uint64_t>(kLocalShadowAtlasSize) * kLocalShadowAtlasSize);
}

TEST(LocalLightShadowAtlasTest, InitPublishesTheDefaultLayoutAndOwnsOneDepthTexture) {
    uz::test::MockRHIDevice device;
    LocalLightShadowAtlas atlas;
    ASSERT_TRUE(atlas.Init(&device));
    EXPECT_TRUE(atlas.IsValid());
    EXPECT_EQ(device.texturesCreated.load(), 1u);
    EXPECT_EQ(atlas.GetAtlasSize(), kLocalShadowAtlasSize);
    EXPECT_EQ(atlas.GetTileSize(), kLocalShadowTileSize);
    EXPECT_EQ(atlas.GetColumns(), kLocalShadowTileCols);
    EXPECT_EQ(atlas.GetMaxTiles(), kMaxLocalShadowTiles);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 0u);
    atlas.Shutdown(&device);
    EXPECT_FALSE(atlas.IsValid());
    EXPECT_EQ(device.texturesDestroyed.load(), 1u);
    EXPECT_EQ(atlas.GetAllocatedTileCount(), 0u);
}

TEST(LocalLightShadowAtlasTest, InitWithoutADeviceFails) {
    LocalLightShadowAtlas atlas;
    EXPECT_FALSE(atlas.Init(nullptr));
    EXPECT_FALSE(atlas.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Light extraction: selection, importance, budget
// ─────────────────────────────────────────────────────────────────────────────

TEST(LocalLightShadowExtraction, LightsWithoutCastShadowsGetNoTile) {
    ExtractionRig rig;
    rig.AddPoint({100.f, 0.f, 50.f}, 10.f, 500.f, false);
    rig.AddPoint({-100.f, 0.f, 50.f}, 10.f, 500.f, false);
    rig.AddSpot({0.f, 100.f, 200.f}, 10.f, 500.f, false);

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    const LightingCB& cb = ExtractionRig::CB(data);
    EXPECT_EQ(cb.pointLightCount, 2u);
    EXPECT_EQ(cb.spotLightCount, 1u);
    EXPECT_EQ(data.pointShadowCount, 0u);
    EXPECT_EQ(data.spotShadowCount, 0u);
    for (uint32_t i = 0; i < cb.pointLightCount; ++i) EXPECT_EQ(cb.pointLights[i].shadowIndex, UINT32_MAX);
    for (uint32_t i = 0; i < cb.spotLightCount; ++i) EXPECT_EQ(cb.spotLights[i].shadowIndex, UINT32_MAX);
}

TEST(LocalLightShadowExtraction, PointShadowsRankByImportanceAndTrimToTwo) {
    // importance = |color * intensity| * range^2 / distance^2, camera at the origin:
    //   near       (100, 0, 0)  I 10,  r 500 -> 433
    //   mid        (300, 0, 0)  I 10,  r 500 -> 48
    //   far-bright (1000, 0, 0) I 100, r 500 -> 43   (brighter, but too far: loses)
    ExtractionRig rig;
    const Vec3 farBright{1000.f, 0.f, 0.f}, near{100.f, 0.f, 0.f}, mid{300.f, 0.f, 0.f};
    rig.AddPoint(farBright, 100.f, 500.f, true);
    rig.AddPoint(near, 10.f, 500.f, true);
    rig.AddPoint(mid, 10.f, 500.f, true);

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    const LightingCB& cb = ExtractionRig::CB(data);
    EXPECT_EQ(cb.pointLightCount, 3u);
    ASSERT_EQ(data.pointShadowCount, 2u); // 16 tiles / 6 faces
    EXPECT_EQ(data.spotShadowCount, 0u);
    EXPECT_TRUE(SamePosition(data.pointShadows[0].position, near));
    EXPECT_TRUE(SamePosition(data.pointShadows[1].position, mid));
    EXPECT_EQ(PointShadowIndexAt(cb, near), 0u);
    EXPECT_EQ(PointShadowIndexAt(cb, mid), 6u);
    EXPECT_EQ(PointShadowIndexAt(cb, farBright), UINT32_MAX);
    // The winners' lightIndex points back at their LightingCB slot.
    for (uint32_t i = 0; i < data.pointShadowCount; ++i) {
        const uint32_t li = data.pointShadows[i].lightIndex;
        ASSERT_LT(li, cb.pointLightCount);
        EXPECT_TRUE(SamePosition(cb.pointLights[li].position, data.pointShadows[i].position));
        EXPECT_EQ(cb.pointLights[li].shadowIndex, i * kPointLightShadowFaces);
    }
}

TEST(LocalLightShadowExtraction, CameraDistanceDecidesTheWinners) {
    ExtractionRig rig;
    const Vec3 lx{400.f, 0.f, 0.f}, ly{0.f, 400.f, 0.f}, lz{0.f, 0.f, 400.f};
    rig.AddPoint(lx, 10.f, 500.f, true);
    rig.AddPoint(ly, 10.f, 500.f, true);
    rig.AddPoint(lz, 10.f, 500.f, true);

    // Camera near +X: distances 112 / 461 / 503 -> X, Y shadowed, Z not.
    {
        const LightFrameData data = rig.Extract(Vec3{300.f, 50.f, 0.f});
        const LightingCB& cb = ExtractionRig::CB(data);
        ASSERT_EQ(data.pointShadowCount, 2u);
        EXPECT_EQ(PointShadowIndexAt(cb, lx), 0u);
        EXPECT_EQ(PointShadowIndexAt(cb, ly), 6u);
        EXPECT_EQ(PointShadowIndexAt(cb, lz), UINT32_MAX);
    }
    // Camera near +Z: distances 102 (Z) / 484 (X) / 500 (Y) -> Z, X shadowed, Y not.
    {
        const LightFrameData data = rig.Extract(Vec3{20.f, 0.f, 300.f});
        const LightingCB& cb = ExtractionRig::CB(data);
        ASSERT_EQ(data.pointShadowCount, 2u);
        EXPECT_EQ(PointShadowIndexAt(cb, lz), 0u);
        EXPECT_EQ(PointShadowIndexAt(cb, lx), 6u);
        EXPECT_EQ(PointShadowIndexAt(cb, ly), UINT32_MAX);
    }
}

TEST(LocalLightShadowExtraction, SpotShadowsTakeOneTileEachInImportanceOrder) {
    // camera at the origin: a (1000, 0, 0) I 100 -> 43; b (100, 0, 0) I 10 -> 433; c (2000, 0, 0) I 10 -> 1
    ExtractionRig rig;
    const Vec3 a{1000.f, 0.f, 0.f}, b{100.f, 0.f, 0.f}, c{2000.f, 0.f, 0.f};
    rig.AddSpot(a, 100.f, 500.f, true);
    rig.AddSpot(b, 10.f, 500.f, true);
    rig.AddSpot(c, 10.f, 500.f, true);

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    const LightingCB& cb = ExtractionRig::CB(data);
    EXPECT_EQ(cb.spotLightCount, 3u);
    ASSERT_EQ(data.spotShadowCount, 3u);
    EXPECT_TRUE(SamePosition(data.spotShadows[0].position, b));
    EXPECT_TRUE(SamePosition(data.spotShadows[1].position, a));
    EXPECT_TRUE(SamePosition(data.spotShadows[2].position, c));
    EXPECT_EQ(SpotShadowIndexAt(cb, b), 0u);
    EXPECT_EQ(SpotShadowIndexAt(cb, a), 1u);
    EXPECT_EQ(SpotShadowIndexAt(cb, c), 2u);
    for (uint32_t i = 0; i < data.spotShadowCount; ++i) {
        const uint32_t li = data.spotShadows[i].lightIndex;
        ASSERT_LT(li, cb.spotLightCount);
        EXPECT_EQ(cb.spotLights[li].shadowIndex, i);
    }
}

TEST(LocalLightShadowExtraction, PointTilesStartAfterTheSpotTiles) {
    ExtractionRig rig;
    const Vec3 spot{0.f, 0.f, 300.f}, pa{100.f, 0.f, 100.f}, pb{-100.f, 0.f, 100.f};
    rig.AddSpot(spot, 10.f, 500.f, true);
    rig.AddPoint(pa, 10.f, 500.f, true);
    rig.AddPoint(pb, 5.f, 500.f, true);

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    const LightingCB& cb = ExtractionRig::CB(data);
    ASSERT_EQ(data.spotShadowCount, 1u);
    ASSERT_EQ(data.pointShadowCount, 2u);
    EXPECT_EQ(SpotShadowIndexAt(cb, spot), 0u);
    EXPECT_EQ(PointShadowIndexAt(cb, pa), 1u);  // 1 + 6 * 0
    EXPECT_EQ(PointShadowIndexAt(cb, pb), 7u);  // 1 + 6 * 1
    // 1 + 2 * 6 = 13 entries, as the demo scene logs ("13 tiles").
    EXPECT_EQ(data.spotShadowCount + data.pointShadowCount * kPointLightShadowFaces, 13u);
}

TEST(LocalLightShadowExtraction, SpotsConsumeThePointBudget) {
    // 5 spots leave 11 entries: one point light (6), the second is unshadowed.
    {
        ExtractionRig rig;
        for (int i = 0; i < 5; ++i) rig.AddSpot({0.f, static_cast<float>(100 * (i + 1)), 300.f}, 10.f, 500.f, true);
        const Vec3 pa{100.f, 0.f, 100.f}, pb{-100.f, 0.f, 100.f};
        rig.AddPoint(pa, 10.f, 500.f, true);
        rig.AddPoint(pb, 5.f, 500.f, true);

        const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
        const LightingCB& cb = ExtractionRig::CB(data);
        EXPECT_EQ(data.spotShadowCount, 5u);
        ASSERT_EQ(data.pointShadowCount, 1u);
        EXPECT_EQ(PointShadowIndexAt(cb, pa), 5u);
        EXPECT_EQ(PointShadowIndexAt(cb, pb), UINT32_MAX);
    }
    // 11 spots leave 5 entries: no point light fits.
    {
        ExtractionRig rig;
        for (int i = 0; i < 11; ++i) rig.AddSpot({0.f, static_cast<float>(100 * (i + 1)), 300.f}, 10.f, 500.f, true);
        const Vec3 pa{100.f, 0.f, 100.f};
        rig.AddPoint(pa, 10.f, 500.f, true);

        const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
        const LightingCB& cb = ExtractionRig::CB(data);
        EXPECT_EQ(data.spotShadowCount, 11u);
        EXPECT_EQ(data.pointShadowCount, 0u);
        EXPECT_EQ(PointShadowIndexAt(cb, pa), UINT32_MAX);
    }
}

TEST(LocalLightShadowExtraction, SpotShadowsAreCappedAtSixteen) {
    ExtractionRig rig;
    // Twenty shadow-casting spots; the four farthest (least important) lose their tile.
    for (int i = 0; i < 20; ++i) rig.AddSpot({static_cast<float>(100 * (i + 1)), 0.f, 300.f}, 10.f, 500.f, true);

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    const LightingCB& cb = ExtractionRig::CB(data);
    EXPECT_EQ(cb.spotLightCount, 20u);
    ASSERT_EQ(data.spotShadowCount, kMaxLocalLightShadows);
    for (int i = 0; i < 20; ++i) {
        const Vec3 pos{static_cast<float>(100 * (i + 1)), 0.f, 300.f};
        if (i < 16) EXPECT_EQ(SpotShadowIndexAt(cb, pos), static_cast<uint32_t>(i)) << "spot " << i;
        else        EXPECT_EQ(SpotShadowIndexAt(cb, pos), UINT32_MAX) << "spot " << i;
    }
}

TEST(LocalLightShadowExtraction, ShadowInfoCarriesThePerLightParameters) {
    ExtractionRig rig;
    const Vec3 ppos{120.f, -40.f, 260.f};
    auto& pl = rig.AddPoint(ppos, 10.f, 1234.f, true);
    pl.pointShadowBias = 1.75f;

    const Vec3 spos{-300.f, 80.f, 410.f};
    auto& sl = rig.AddSpot(spos, 10.f, 987.f, true);
    sl.spotShadowBias = 0.25f;
    sl.innerAngleDeg = 30.0f;
    sl.outerAngleDeg = 40.0f;
    sl.SetRotation(Quat::FromAxisAngle(Vec3{0.f, 1.f, 0.f}, 0.7f));
    const Vec3 expectedDir = (sl.GetRotation() * Vec3::Forward()).Normalized();

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    ASSERT_EQ(data.pointShadowCount, 1u);
    ASSERT_EQ(data.spotShadowCount, 1u);

    const PointLightShadowInfo& p = data.pointShadows[0];
    EXPECT_TRUE(SamePosition(p.position, ppos));
    EXPECT_FLOAT_EQ(p.radius, 1234.f);
    EXPECT_FLOAT_EQ(p.bias, 1.75f);

    const SpotLightShadowInfo& s = data.spotShadows[0];
    EXPECT_TRUE(SamePosition(s.position, spos));
    EXPECT_FLOAT_EQ(s.radius, 987.f);
    EXPECT_FLOAT_EQ(s.bias, 0.25f);
    EXPECT_NEAR(s.outerAngle, 40.0f * kDegToRad, 1e-5f);
    EXPECT_NEAR(s.direction.Dot(expectedDir), 1.0f, 1e-5f);
    EXPECT_NEAR(s.direction.Length(), 1.0f, 1e-5f);
}

TEST(LocalLightShadowExtraction, SpotOuterAngleIsClampedLikeTheConeAttenuation) {
    // The shadow frustum uses the same clamp as the cone term: outer in (inner + 0.001, 89 deg + 0.001].
    ExtractionRig rig;
    auto& wide = rig.AddSpot({0.f, 0.f, 300.f}, 10.f, 500.f, true);
    wide.innerAngleDeg = 30.0f;
    wide.outerAngleDeg = 100.0f;
    auto& inverted = rig.AddSpot({0.f, 500.f, 300.f}, 10.f, 500.f, true);
    inverted.innerAngleDeg = 50.0f;
    inverted.outerAngleDeg = 20.0f;

    const LightFrameData data = rig.Extract(Vec3{0.f, 0.f, 0.f});
    ASSERT_EQ(data.spotShadowCount, 2u);
    for (uint32_t i = 0; i < 2; ++i) {
        const SpotLightShadowInfo& s = data.spotShadows[i];
        if (SamePosition(s.position, Vec3{0.f, 0.f, 300.f}))
            EXPECT_NEAR(s.outerAngle, 89.0f * kDegToRad + 0.001f, 1e-5f);
        else
            EXPECT_NEAR(s.outerAngle, 50.0f * kDegToRad + 0.001f, 1e-5f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cube-face convention: CPU face cameras vs the shader's face selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(LocalLightShadowCubeFaces, FaceTableMatchesTheMajorAxisSelection) {
    for (uint32_t face = 0; face < kPointLightShadowFaces; ++face) {
        EXPECT_EQ(SelectCubeFace(kCubeFaces[face].forward), face) << "face " << face;
        EXPECT_NEAR(kCubeFaces[face].forward.Length(), 1.0f, 1e-6f);
        EXPECT_NEAR(kCubeFaces[face].up.Length(), 1.0f, 1e-6f);
        EXPECT_NEAR(kCubeFaces[face].forward.Dot(kCubeFaces[face].up), 0.0f, 1e-6f) << "up collinear on face " << face;
    }
}

TEST(LocalLightShadowCubeFaces, EveryDirectionProjectsInsideItsSelectedFace) {
    const Vec3 lightPos{37.f, -120.f, 250.f};
    const float nearZ = 1.0f, farZ = 1500.0f; // r.Shadow.Local.NearPlane default, a typical range
    Mat4 faceVP[kPointLightShadowFaces];
    for (uint32_t f = 0; f < kPointLightShadowFaces; ++f) faceVP[f] = FaceViewProj(lightPos, f, nearZ, farZ);

    std::vector<Vec3> dirs = SphereDirections(4096);
    // Axis, edge and corner directions, plus near-seam directions on both sides of each seam.
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z)
                if (x || y || z) dirs.push_back(Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}.Normalized());
    const float d = 1e-4f;
    for (float s : {1.0f, -1.0f}) {
        dirs.push_back(Vec3{1.0f, s * (1.0f - d), 0.3f}.Normalized());
        dirs.push_back(Vec3{1.0f - d, s, 0.3f}.Normalized());
        dirs.push_back(Vec3{0.3f, 1.0f, s * (1.0f - d)}.Normalized());
        dirs.push_back(Vec3{0.3f, 1.0f - d, s}.Normalized());
        dirs.push_back(Vec3{s * (1.0f - d), 0.3f, 1.0f}.Normalized());
        dirs.push_back(Vec3{s, 0.3f, 1.0f - d}.Normalized());
    }

    size_t checked = 0;
    for (const Vec3& dir : dirs) {
        const uint32_t face = SelectCubeFace(dir);
        ASSERT_LT(face, kPointLightShadowFaces);
        for (float dist : {5.0f, 250.0f, 1400.0f}) {
            const Ndc n = Project(faceVP[face], lightPos + dir * dist);
            EXPECT_TRUE(InsideTile(n)) << "dir (" << dir.x << ", " << dir.y << ", " << dir.z << ") at " << dist
                                        << " cm, face " << face << ": ndc (" << n.x << ", " << n.y << "), w " << n.w;
            ++checked;
        }
    }
    EXPECT_GT(checked, 12000u);
}

TEST(LocalLightShadowCubeFaces, SeamsAreCoveredByBothNeighbouringFaces) {
    const Vec3 lightPos{0.f, 0.f, 300.f};
    Mat4 faceVP[kPointLightShadowFaces];
    for (uint32_t f = 0; f < kPointLightShadowFaces; ++f) faceVP[f] = FaceViewProj(lightPos, f, 1.0f, 1500.0f);

    // A direction with two equal major components lies on the seam between the two faces;
    // both face cameras must contain it (the shader may select either side of the seam, and the
    // PCSS taps cross it): neither tile has a gap at its edge.
    struct Seam { Vec3 dir; uint32_t faceA, faceB; };
    const Seam seams[] = {
        {Vec3{ 1.f,  1.f,  0.2f}, 0u, 2u}, {Vec3{ 1.f, -1.f,  0.2f}, 0u, 3u},
        {Vec3{-1.f,  1.f, -0.2f}, 1u, 2u}, {Vec3{-1.f, -1.f, -0.2f}, 1u, 3u},
        {Vec3{ 1.f,  0.2f,  1.f}, 0u, 4u}, {Vec3{ 1.f,  0.2f, -1.f}, 0u, 5u},
        {Vec3{-1.f,  0.2f,  1.f}, 1u, 4u}, {Vec3{-1.f,  0.2f, -1.f}, 1u, 5u},
        {Vec3{ 0.2f,  1.f,  1.f}, 2u, 4u}, {Vec3{ 0.2f,  1.f, -1.f}, 2u, 5u},
        {Vec3{ 0.2f, -1.f,  1.f}, 3u, 4u}, {Vec3{ 0.2f, -1.f, -1.f}, 3u, 5u},
    };
    for (const Seam& s : seams) {
        const Vec3 dir = s.dir.Normalized();
        const uint32_t selected = SelectCubeFace(dir);
        EXPECT_TRUE(selected == s.faceA || selected == s.faceB) << "seam selects face " << selected;
        for (float t = -0.9f; t <= 0.9f; t += 0.3f) {
            // Slide along the seam: scale the minor component.
            Vec3 along = s.dir;
            if (s.faceA < 2 && s.faceB < 4) along.z = t;          // X/Y seam: minor is z
            else if (s.faceA < 2) along.y = t;                    // X/Z seam: minor is y
            else along.x = t;                                     // Y/Z seam: minor is x
            const Vec3 p = lightPos + along.Normalized() * 400.0f;
            const Ndc a = Project(faceVP[s.faceA], p);
            const Ndc b = Project(faceVP[s.faceB], p);
            EXPECT_TRUE(InsideTile(a, 2e-4f)) << "face " << s.faceA << " misses its seam at t " << t << ": (" << a.x << ", " << a.y << ")";
            EXPECT_TRUE(InsideTile(b, 2e-4f)) << "face " << s.faceB << " misses its seam at t " << t << ": (" << b.x << ", " << b.y << ")";
            // The seam is the tile edge of both faces: one NDC coordinate is at +-1.
            EXPECT_NEAR(std::max(std::fabs(a.x), std::fabs(a.y)), 1.0f, 2e-4f);
            EXPECT_NEAR(std::max(std::fabs(b.x), std::fabs(b.y)), 1.0f, 2e-4f);
        }
    }
}

TEST(LocalLightShadowCubeFaces, OppositeFacesNeverShareAReceiver) {
    // A receiver in front of face F is behind (w <= 0) or outside the opposite face.
    const Vec3 lightPos{0.f, 0.f, 0.f};
    Mat4 faceVP[kPointLightShadowFaces];
    for (uint32_t f = 0; f < kPointLightShadowFaces; ++f) faceVP[f] = FaceViewProj(lightPos, f, 1.0f, 1000.0f);
    for (const Vec3& dir : SphereDirections(512)) {
        const uint32_t face = SelectCubeFace(dir);
        const uint32_t opposite = face ^ 1u; // +X/-X, +Y/-Y, +Z/-Z are adjacent pairs in the table
        const Ndc n = Project(faceVP[opposite], lightPos + dir * 200.0f);
        EXPECT_FALSE(InsideTile(n)) << "direction (" << dir.x << ", " << dir.y << ", " << dir.z
                                     << ") is inside the opposite face " << opposite;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Spot tile: the cone boundary is the tile edge
// ─────────────────────────────────────────────────────────────────────────────

namespace {

void CheckSpotTile(const Vec3& pos, const Vec3& dirIn, float outerDeg) {
    const Vec3 dir = dirIn.Normalized();
    const float outer = outerDeg * kDegToRad;
    Vec3 up{};
    const Mat4 vp = SpotViewProj(pos, dir, outer, 1.0f, 1500.0f, &up);
    // Image-plane axes: the component of `up` orthogonal to the direction, and the horizontal one.
    const Vec3 v = (up - dir * up.Dot(dir)).Normalized();
    const Vec3 h = dir.Cross(v).Normalized();

    for (float dist : {50.0f, 600.0f}) {
        // The cone boundary along the vertical / horizontal image axes lands on the tile edge.
        const Ndc top    = Project(vp, pos + (dir * std::cos(outer) + v * std::sin(outer)) * dist);
        const Ndc bottom = Project(vp, pos + (dir * std::cos(outer) - v * std::sin(outer)) * dist);
        const Ndc left   = Project(vp, pos + (dir * std::cos(outer) - h * std::sin(outer)) * dist);
        const Ndc right  = Project(vp, pos + (dir * std::cos(outer) + h * std::sin(outer)) * dist);
        EXPECT_NEAR(std::fabs(top.y), 1.0f, 1e-4f);    EXPECT_NEAR(top.x, 0.0f, 1e-4f);
        EXPECT_NEAR(std::fabs(bottom.y), 1.0f, 1e-4f); EXPECT_NEAR(bottom.x, 0.0f, 1e-4f);
        EXPECT_NEAR(std::fabs(left.x), 1.0f, 1e-4f);   EXPECT_NEAR(left.y, 0.0f, 1e-4f);
        EXPECT_NEAR(std::fabs(right.x), 1.0f, 1e-4f);  EXPECT_NEAR(right.y, 0.0f, 1e-4f);
        EXPECT_GT(top.w, 0.0f);
        // Opposite boundaries map to opposite edges.
        EXPECT_LT(top.y * bottom.y, 0.0f);
        EXPECT_LT(left.x * right.x, 0.0f);

        // Half the cone angle is strictly inside; 1.2x the cone angle is outside (lit by default).
        const Ndc inside  = Project(vp, pos + (dir * std::cos(0.5f * outer) + v * std::sin(0.5f * outer)) * dist);
        const Ndc outside = Project(vp, pos + (dir * std::cos(1.2f * outer) + v * std::sin(1.2f * outer)) * dist);
        EXPECT_NEAR(std::fabs(inside.y), std::tan(0.5f * outer) / std::tan(outer), 1e-4f);
        EXPECT_TRUE(InsideTile(inside));
        EXPECT_FALSE(InsideTile(outside));

        // The axis itself is the tile centre (1e-4 NDC = 0.05 texel of a 1024 tile; LookAt's
        // quaternion round-trip leaves ~1.5e-5).
        const Ndc centre = Project(vp, pos + dir * dist);
        EXPECT_NEAR(centre.x, 0.0f, 1e-4f);
        EXPECT_NEAR(centre.y, 0.0f, 1e-4f);
    }
}

} // namespace

TEST(LocalLightShadowSpotTile, ConeBoundaryIsTheTileEdge) {
    CheckSpotTile({0.f, 0.f, 400.f}, {1.0f, 0.3f, -0.8f}, 35.0f);
    CheckSpotTile({120.f, -50.f, 250.f}, {0.0f, 1.0f, -0.15f}, 45.0f);
    CheckSpotTile({0.f, 0.f, 300.f}, {1.0f, 0.0f, 0.0f}, 20.0f);
}

TEST(LocalLightShadowSpotTile, VerticalDirectionUsesTheUpFallback) {
    // Straight down / up: Z up would be collinear; the +Y fallback keeps the frame finite and the
    // boundary on the tile edge.
    Vec3 usedUp{};
    (void)SpotViewProj({0.f, 0.f, 400.f}, {0.f, 0.f, -1.f}, 45.0f * kDegToRad, 1.0f, 1500.0f, &usedUp);
    EXPECT_FLOAT_EQ(usedUp.x, 0.0f); EXPECT_FLOAT_EQ(usedUp.y, 1.0f); EXPECT_FLOAT_EQ(usedUp.z, 0.0f);
    CheckSpotTile({0.f, 0.f, 400.f}, {0.f, 0.f, -1.f}, 45.0f);
    CheckSpotTile({0.f, 0.f, 50.f}, {0.f, 0.f, 1.f}, 30.0f);
    // Nearly vertical (within the 0.99 threshold) also falls back; just outside it keeps Z up.
    (void)SpotViewProj({0.f, 0.f, 400.f}, Vec3{0.05f, 0.f, -1.f}.Normalized(), 45.0f * kDegToRad, 1.0f, 1500.0f, &usedUp);
    EXPECT_FLOAT_EQ(usedUp.y, 1.0f);
    (void)SpotViewProj({0.f, 0.f, 400.f}, Vec3{0.2f, 0.f, -1.f}.Normalized(), 45.0f * kDegToRad, 1.0f, 1500.0f, &usedUp);
    EXPECT_FLOAT_EQ(usedUp.z, 1.0f);
    CheckSpotTile({0.f, 0.f, 400.f}, {0.05f, 0.f, -1.f}, 45.0f);
}
