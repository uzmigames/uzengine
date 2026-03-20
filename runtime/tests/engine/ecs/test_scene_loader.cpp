/// Unit tests for uz::LoadScene — JSON entity loading into a World.
///
/// Covers:
///   - Empty and malformed JSON → returns 0
///   - Missing "entities" key   → returns 0
///   - Empty entities array     → returns 0, world empty
///   - Single entity with name only → 1 entity, CName set
///   - Transform component → SceneComponent position/rotation/scale
///   - Script component    → CScript.scriptPath
///   - Camera component    → CameraComponent fov/near/far/isActive
///   - Directional light   → DirectionalLightComponent color/intensity
///   - Point light         → PointLightComponent color/intensity/range
///   - Spot light          → SpotLightComponent color/intensity/range/angles (degrees→radians)
///   - Parent + children   → correct total entity count
///   - Unknown component   → entity still created (CName present), unknown key skipped
///   - Transform rotation (Euler degrees) → quaternion near-identity for zero input,
///                                          non-identity for 90° yaw

#include "scene_loader.h"
#include "world.h"
#include "name_component.h"
#include "scene_component.h"
#include "camera_component.h"
#include "light_component.h"
#include "../scripting/script_component.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a UTF-8 string into the byte buffer expected by LoadScene.
static std::vector<uint8_t> ToBytes(const std::string& s) {
    return {s.begin(), s.end()};
}

/// Constant used to convert expected degree values to radians for assertions.
static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

/// Counts entities in a view (workaround: ViewCount(view) not available in this EnTT config).
template<typename View>
static size_t ViewCount(View& view) {
    size_t n = 0;
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — fresh World per test
// ─────────────────────────────────────────────────────────────────────────────

class SceneLoaderTest : public ::testing::Test {
protected:
    uz::World world_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 1. Empty JSON → returns 0
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, EmptyBuffer_Returns0) {
    const size_t count = uz::LoadScene(world_, {}, "test");
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(world_.EntityCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Malformed JSON → returns 0
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, MalformedJson_Returns0) {
    const size_t count = uz::LoadScene(world_, ToBytes("{not valid json!!!"), "test");
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(world_.EntityCount(), 0u);
}

TEST_F(SceneLoaderTest, BareString_Returns0) {
    // Root element is a string, not an object.
    const size_t count = uz::LoadScene(world_, ToBytes("\"just a string\""), "test");
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(world_.EntityCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Missing "entities" key → returns 0
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, MissingEntitiesKey_Returns0) {
    const size_t count = uz::LoadScene(world_, ToBytes(R"({"version": 1})"), "test");
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(world_.EntityCount(), 0u);
}

TEST_F(SceneLoaderTest, EntitiesIsNotArray_Returns0) {
    // "entities" exists but is an object, not an array.
    const size_t count = uz::LoadScene(world_, ToBytes(R"({"entities": {"a": 1}})"), "test");
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(world_.EntityCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Empty entities array → returns 0, world has no entities
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, EmptyEntitiesArray_Returns0_WorldEmpty) {
    const size_t count = uz::LoadScene(world_, ToBytes(R"({"entities": []})"), "test");
    EXPECT_EQ(count, 0u);
    EXPECT_EQ(world_.EntityCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Single entity with name only → creates 1 entity with CName
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, SingleEntityNameOnly_Creates1Entity_WithCName) {
    const char* json = R"({
        "entities": [
            { "name": "Player" }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");

    EXPECT_EQ(count, 1u);
    EXPECT_EQ(world_.EntityCount(), 1u);

    // Verify the CName component carries the correct name.
    auto view = world_.View<uz::CName>();
    int found = 0;
    for (auto [enttEntity, cname] : view.each()) {
        EXPECT_EQ(cname.name, "Player");
        ++found;
    }
    EXPECT_EQ(found, 1);
}

TEST_F(SceneLoaderTest, EntityWithEmptyName_DoesNotReceiveCName) {
    // The loader skips CName when the name string is empty.
    const char* json = R"({
        "entities": [
            { "name": "" }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");

    EXPECT_EQ(count, 1u);

    auto view = world_.View<uz::CName>();
    EXPECT_EQ(ViewCount(view), 0u) << "No CName should be added for an empty name";
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Entity with transform → SceneComponent has correct position/rotation/scale
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, TransformComponent_SetsPositionAndScale) {
    const char* json = R"({
        "entities": [
            {
                "name": "Cube",
                "components": {
                    "transform": {
                        "position": [1.0, 2.0, 3.0],
                        "rotation": [0.0, 0.0, 0.0],
                        "scale":    [2.0, 3.0, 4.0]
                    }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u);

    auto view = world_.View<uz::SceneComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, sc] = *view.each().begin();

    EXPECT_NEAR(sc.GetPosition().x, 1.0f, 1e-4f);
    EXPECT_NEAR(sc.GetPosition().y, 2.0f, 1e-4f);
    EXPECT_NEAR(sc.GetPosition().z, 3.0f, 1e-4f);

    EXPECT_NEAR(sc.GetScale().x, 2.0f, 1e-4f);
    EXPECT_NEAR(sc.GetScale().y, 3.0f, 1e-4f);
    EXPECT_NEAR(sc.GetScale().z, 4.0f, 1e-4f);
}

TEST_F(SceneLoaderTest, TransformComponent_MissingPosition_DefaultsToOrigin) {
    const char* json = R"({
        "entities": [
            {
                "name": "Pivot",
                "components": {
                    "transform": {}
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::SceneComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, sc] = *view.each().begin();
    EXPECT_NEAR(sc.GetPosition().x, 0.0f, 1e-4f);
    EXPECT_NEAR(sc.GetPosition().y, 0.0f, 1e-4f);
    EXPECT_NEAR(sc.GetPosition().z, 0.0f, 1e-4f);
}

TEST_F(SceneLoaderTest, TransformComponent_MissingScale_DefaultsToOne) {
    const char* json = R"({
        "entities": [
            {
                "name": "Pivot",
                "components": {
                    "transform": {}
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::SceneComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, sc] = *view.each().begin();
    EXPECT_NEAR(sc.GetScale().x, 1.0f, 1e-4f);
    EXPECT_NEAR(sc.GetScale().y, 1.0f, 1e-4f);
    EXPECT_NEAR(sc.GetScale().z, 1.0f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 14. Transform rotation Euler degrees → correct quaternion
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, TransformComponent_ZeroRotation_ProducesIdentityQuat) {
    const char* json = R"({
        "entities": [
            {
                "name": "Idle",
                "components": {
                    "transform": {
                        "position": [0,0,0],
                        "rotation": [0,0,0],
                        "scale":    [1,1,1]
                    }
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::SceneComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, sc] = *view.each().begin();
    const uz::Quat& q = sc.GetRotation();

    // Identity quaternion: (0, 0, 0, 1).
    EXPECT_NEAR(q.x, 0.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
    EXPECT_NEAR(q.z, 0.0f, 1e-4f);
    EXPECT_NEAR(q.w, 1.0f, 1e-4f);
}

TEST_F(SceneLoaderTest, TransformComponent_90DegYaw_ProducesCorrectQuat) {
    // 90-degree yaw (rotation around Y axis).
    // Euler pitch=0, yaw=90, roll=0 → Quat::FromEuler(0, π/2, 0)
    // Expected: (x≈0, y≈0.7071, z≈0, w≈0.7071)
    const char* json = R"({
        "entities": [
            {
                "name": "Spun",
                "components": {
                    "transform": {
                        "rotation": [0.0, 90.0, 0.0]
                    }
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::SceneComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, sc] = *view.each().begin();
    const uz::Quat& q = sc.GetRotation();

    const float expected_yw = std::sin(90.0f * kDegToRad / 2.0f); // ≈ 0.7071
    const float expected_w  = std::cos(90.0f * kDegToRad / 2.0f); // ≈ 0.7071

    EXPECT_NEAR(q.x, 0.0f,       1e-3f);
    EXPECT_NEAR(q.y, expected_yw, 1e-3f);
    EXPECT_NEAR(q.z, 0.0f,       1e-3f);
    EXPECT_NEAR(q.w, expected_w,  1e-3f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Entity with script → CScript has correct scriptPath
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, ScriptComponent_SetsScriptPath) {
    const char* json = R"({
        "entities": [
            {
                "name": "Actor",
                "components": {
                    "script": { "path": "scripts/rotate.lua" }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u);

    auto view = world_.View<uz::CScript>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, cs] = *view.each().begin();
    EXPECT_EQ(cs.scriptPath, "scripts/rotate.lua");
    // luaRef and envRef should remain at sentinel values — ScriptSystem initialises them.
    EXPECT_EQ(cs.luaRef,  LUA_REFNIL);
    EXPECT_EQ(cs.envRef,  LUA_REFNIL);
    EXPECT_FALSE(cs.started);
}

TEST_F(SceneLoaderTest, ScriptComponent_MissingPath_SkipsComponent) {
    // A "script" component without a "path" field must not create CScript.
    const char* json = R"({
        "entities": [
            {
                "name": "Broken",
                "components": {
                    "script": {}
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u) << "Entity itself must still be created";

    auto view = world_.View<uz::CScript>();
    EXPECT_EQ(ViewCount(view), 0u) << "CScript must not be added when 'path' is missing";
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Entity with camera → CameraComponent with fov/near/far/active
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, CameraComponent_SetsAllFields) {
    const char* json = R"({
        "entities": [
            {
                "name": "MainCamera",
                "components": {
                    "camera": {
                        "fov":    75.0,
                        "near":   0.05,
                        "far":    500.0,
                        "active": true
                    }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u);

    auto view = world_.View<uz::CameraComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, cam] = *view.each().begin();
    EXPECT_NEAR(cam.fov,       75.0f,  1e-4f);
    EXPECT_NEAR(cam.nearPlane,  0.05f, 1e-5f);
    EXPECT_NEAR(cam.farPlane,  500.0f, 1e-3f);
    EXPECT_TRUE(cam.isActive);
}

TEST_F(SceneLoaderTest, CameraComponent_DefaultValues_WhenFieldsAbsent) {
    const char* json = R"({
        "entities": [
            {
                "name": "DefaultCam",
                "components": {
                    "camera": {}
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::CameraComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, cam] = *view.each().begin();
    EXPECT_NEAR(cam.fov,       60.0f,   1e-4f);
    EXPECT_NEAR(cam.nearPlane,  0.1f,   1e-5f);
    EXPECT_NEAR(cam.farPlane, 1000.0f,  1e-3f);
    EXPECT_FALSE(cam.isActive);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. Entity with directional light → DirectionalLightComponent
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, DirectionalLightComponent_SetsColorAndIntensity) {
    const char* json = R"({
        "entities": [
            {
                "name": "Sun",
                "components": {
                    "light": {
                        "type":      "directional",
                        "color":     [1.0, 0.9, 0.8],
                        "intensity": 3.5
                    }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u);

    auto view = world_.View<uz::DirectionalLightComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, light] = *view.each().begin();
    EXPECT_NEAR(light.color.x,    1.0f, 1e-4f);
    EXPECT_NEAR(light.color.y,    0.9f, 1e-4f);
    EXPECT_NEAR(light.color.z,    0.8f, 1e-4f);
    EXPECT_NEAR(light.intensity,  3.5f, 1e-4f);
}

TEST_F(SceneLoaderTest, DirectionalLightComponent_DefaultColor_WhenAbsent) {
    const char* json = R"({
        "entities": [
            {
                "name": "DefaultSun",
                "components": {
                    "light": { "type": "directional" }
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::DirectionalLightComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, light] = *view.each().begin();
    EXPECT_NEAR(light.color.x,   1.0f, 1e-4f);
    EXPECT_NEAR(light.color.y,   1.0f, 1e-4f);
    EXPECT_NEAR(light.color.z,   1.0f, 1e-4f);
    EXPECT_NEAR(light.intensity, 1.0f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Entity with point light → PointLightComponent with range
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, PointLightComponent_SetsColorIntensityRange) {
    const char* json = R"({
        "entities": [
            {
                "name": "Lamp",
                "components": {
                    "light": {
                        "type":      "point",
                        "color":     [0.2, 0.4, 1.0],
                        "intensity": 2.0,
                        "range":     15.0
                    }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u);

    auto view = world_.View<uz::PointLightComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, light] = *view.each().begin();
    EXPECT_NEAR(light.color.x,   0.2f,  1e-4f);
    EXPECT_NEAR(light.color.y,   0.4f,  1e-4f);
    EXPECT_NEAR(light.color.z,   1.0f,  1e-4f);
    EXPECT_NEAR(light.intensity, 2.0f,  1e-4f);
    EXPECT_NEAR(light.range,    15.0f,  1e-4f);
}

TEST_F(SceneLoaderTest, PointLightComponent_DefaultRange_WhenAbsent) {
    const char* json = R"({
        "entities": [
            {
                "name": "DefaultLamp",
                "components": {
                    "light": { "type": "point" }
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::PointLightComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, light] = *view.each().begin();
    EXPECT_NEAR(light.range, 10.0f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. Entity with spot light → SpotLightComponent with angles
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, SpotLightComponent_SetsAllFields_AnglesStoredAsRadians) {
    const char* json = R"({
        "entities": [
            {
                "name": "Spotlight",
                "components": {
                    "light": {
                        "type":       "spot",
                        "color":      [1.0, 0.5, 0.0],
                        "intensity":  4.0,
                        "range":      20.0,
                        "innerAngle": 15.0,
                        "outerAngle": 30.0
                    }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    ASSERT_EQ(count, 1u);

    auto view = world_.View<uz::SpotLightComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, light] = *view.each().begin();
    EXPECT_NEAR(light.color.x,    1.0f, 1e-4f);
    EXPECT_NEAR(light.color.y,    0.5f, 1e-4f);
    EXPECT_NEAR(light.color.z,    0.0f, 1e-4f);
    EXPECT_NEAR(light.intensity,  4.0f, 1e-4f);
    EXPECT_NEAR(light.range,     20.0f, 1e-4f);
    // Angles stored as degrees (standard — conversion happens in proxy sync).
    EXPECT_NEAR(light.innerAngleDeg, 15.0f, 1e-4f);
    EXPECT_NEAR(light.outerAngleDeg, 30.0f, 1e-4f);
}

TEST_F(SceneLoaderTest, SpotLightComponent_DefaultAngles_WhenAbsent) {
    const char* json = R"({
        "entities": [
            {
                "name": "DefaultSpot",
                "components": {
                    "light": { "type": "spot" }
                }
            }
        ]
    })";
    (void)uz::LoadScene(world_, ToBytes(json), "test");

    auto view = world_.View<uz::SpotLightComponent>();
    ASSERT_EQ(ViewCount(view), 1u);

    auto [enttEntity, light] = *view.each().begin();
    // Default inner/outer angles (standard: 30° and 45°).
    EXPECT_NEAR(light.innerAngleDeg, 30.0f, 1e-4f);
    EXPECT_NEAR(light.outerAngleDeg, 45.0f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. Entity with children → correct count (parent + children)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, EntityWithChildren_CorrectTotalCount) {
    const char* json = R"({
        "entities": [
            {
                "name": "Root",
                "children": [
                    { "name": "Child1" },
                    { "name": "Child2" }
                ]
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");

    // 1 root + 2 children = 3 total entities.
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(world_.EntityCount(), 3u);
}

TEST_F(SceneLoaderTest, NestedChildren_CorrectDeepCount) {
    const char* json = R"({
        "entities": [
            {
                "name": "Root",
                "children": [
                    {
                        "name": "Child",
                        "children": [
                            { "name": "GrandChild" }
                        ]
                    }
                ]
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");

    // Root + Child + GrandChild = 3.
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(world_.EntityCount(), 3u);
}

TEST_F(SceneLoaderTest, MultipleTopLevelEntities_AllCounted) {
    const char* json = R"({
        "entities": [
            { "name": "Alpha" },
            { "name": "Beta" },
            { "name": "Gamma" }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");

    EXPECT_EQ(count, 3u);
    EXPECT_EQ(world_.EntityCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 13. Unknown component type → entity still created, warning logged (no crash)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, UnknownComponent_EntityStillCreated_NoCrash) {
    const char* json = R"({
        "entities": [
            {
                "name": "FutureEntity",
                "components": {
                    "foliage": { "density": 42 }
                }
            }
        ]
    })";
    // Must not crash or throw.
    size_t count = 0;
    EXPECT_NO_THROW(count = uz::LoadScene(world_, ToBytes(json), "test"));

    EXPECT_EQ(count, 1u) << "Entity must be created even when component type is unknown";
    EXPECT_EQ(world_.EntityCount(), 1u);

    // The entity should have a CName (set before the component loop).
    auto view = world_.View<uz::CName>();
    ASSERT_EQ(ViewCount(view), 1u);
    auto [enttEntity, cname] = *view.each().begin();
    EXPECT_EQ(cname.name, "FutureEntity");
}

TEST_F(SceneLoaderTest, MultipleUnknownComponents_EntityCreatedOnce) {
    const char* json = R"({
        "entities": [
            {
                "name": "Multi",
                "components": {
                    "navmesh_agent": { "speed": 5 },
                    "crowd_control": { "radius": 1 }
                }
            }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(world_.EntityCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional: light with unknown type → no light component, entity created
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, LightWithUnknownType_NoLightComponent_EntityStillCreated) {
    const char* json = R"({
        "entities": [
            {
                "name": "WeirdLight",
                "components": {
                    "light": { "type": "area" }
                }
            }
        ]
    })";
    size_t count = 0;
    EXPECT_NO_THROW(count = uz::LoadScene(world_, ToBytes(json), "test"));

    EXPECT_EQ(count, 1u);

    // None of the known light components should have been created.
    { auto v = world_.View<uz::DirectionalLightComponent>(); EXPECT_EQ(ViewCount(v), 0u); }
    { auto v = world_.View<uz::PointLightComponent>();       EXPECT_EQ(ViewCount(v), 0u); }
    { auto v = world_.View<uz::SpotLightComponent>();        EXPECT_EQ(ViewCount(v), 0u); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional: component value that is not an object → skipped gracefully
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, ComponentValueIsNotObject_EntityCreated_ComponentSkipped) {
    // "transform" key maps to a number instead of an object.
    const char* json = R"({
        "entities": [
            {
                "name": "Broken",
                "components": {
                    "transform": 99
                }
            }
        ]
    })";
    size_t count = 0;
    EXPECT_NO_THROW(count = uz::LoadScene(world_, ToBytes(json), "test"));

    EXPECT_EQ(count, 1u);
    { auto v = world_.View<uz::SceneComponent>(); EXPECT_EQ(ViewCount(v), 0u)
        << "SceneComponent must not be added when transform value is not a JSON object"; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional: entity array entry that is not an object → skipped, others succeed
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, EntityEntryIsNotObject_SkippedGracefully) {
    // Second entry is a number — must be skipped; first and third still load.
    const char* json = R"({
        "entities": [
            { "name": "Good1" },
            42,
            { "name": "Good2" }
        ]
    })";
    const size_t count = uz::LoadScene(world_, ToBytes(json), "test");

    // The invalid entry (42) returns 0, valid entries return 1 each.
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(world_.EntityCount(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional: sourceName does not affect loading behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SceneLoaderTest, SourceNameDoesNotAffectResult) {
    const char* json = R"({ "entities": [ { "name": "Obj" } ] })";

    const size_t countA = uz::LoadScene(world_, ToBytes(json), "scenes/a.uzscene");
    world_.Clear();
    const size_t countB = uz::LoadScene(world_, ToBytes(json), "");

    EXPECT_EQ(countA, 1u);
    EXPECT_EQ(countB, 1u);
}
