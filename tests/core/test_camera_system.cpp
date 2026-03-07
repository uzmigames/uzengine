// test_camera_system.cpp — Google Test suite for the camera system.
//
// Covers: FOV lerp, shake noise, CameraShake::evaluate, shakes_accumulate,
// spring_arm_update, catmull_rom_eval, and .uzcam preset loading.

#include <gtest/gtest.h>

#include "engine/gameplay/camera_system.h"
#include "engine/gameplay/camera_preset_loader.h"

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// FOV Lerp
// ============================================================================

TEST(CameraSystem_FovLerp, ZeroDt_NoChange) {
    EXPECT_FLOAT_EQ(fov_lerp(90.0f, 45.0f, 8.0f, 0.0f), 90.0f);
}

TEST(CameraSystem_FovLerp, NegativeDt_NoChange) {
    EXPECT_FLOAT_EQ(fov_lerp(90.0f, 45.0f, 8.0f, -1.0f), 90.0f);
}

TEST(CameraSystem_FovLerp, ConvergesToTarget) {
    float fov = 90.0f;
    for (int i = 0; i < 100; ++i) {
        fov = fov_lerp(fov, 45.0f, 8.0f, 1.0f / 60.0f);
    }
    EXPECT_NEAR(fov, 45.0f, 0.5f);
}

TEST(CameraSystem_FovLerp, SameTarget_NoChange) {
    EXPECT_FLOAT_EQ(fov_lerp(90.0f, 90.0f, 8.0f, 0.016f), 90.0f);
}

TEST(CameraSystem_FovLerp, SingleStep_MovesTowardTarget) {
    float fov = fov_lerp(90.0f, 45.0f, 8.0f, 0.016f);
    EXPECT_GT(fov, 45.0f);
    EXPECT_LT(fov, 90.0f);
}

TEST(CameraSystem_FovLerp, HighSpeed_FastConvergence) {
    float fov = fov_lerp(90.0f, 45.0f, 100.0f, 1.0f);
    EXPECT_NEAR(fov, 45.0f, 0.01f);
}

// ============================================================================
// Shake Noise
// ============================================================================

TEST(CameraSystem_ShakeNoise, Deterministic) {
    float a = shake_noise(1.5f);
    float b = shake_noise(1.5f);
    EXPECT_FLOAT_EQ(a, b);
}

TEST(CameraSystem_ShakeNoise, RangeNeg1To1) {
    for (float t = -10.0f; t <= 10.0f; t += 0.1f) {
        float v = shake_noise(t);
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
    }
}

TEST(CameraSystem_ShakeNoise, IntegerPoints) {
    // At integer points, noise should be a hash value (still in [-1, 1]).
    float v = shake_noise(5.0f);
    EXPECT_GE(v, -1.0f);
    EXPECT_LE(v, 1.0f);
}

TEST(CameraSystem_ShakeNoise, SmoothBetweenIntegers) {
    // Should interpolate smoothly (no discontinuities).
    float prev = shake_noise(3.0f);
    for (float t = 3.01f; t <= 4.0f; t += 0.01f) {
        float curr = shake_noise(t);
        float diff = std::abs(curr - prev);
        EXPECT_LT(diff, 0.5f);  // No huge jumps.
        prev = curr;
    }
}

// ============================================================================
// CameraShake::evaluate
// ============================================================================

TEST(CameraSystem_CameraShake, EvaluateAtStart) {
    CameraShake shake;
    shake.magnitude = 10.0f;
    shake.frequency = 20.0f;
    shake.duration  = 1.0f;
    shake.elapsed   = 0.0f;

    Vec2 offset = shake.evaluate();
    // At t=0, envelope is 1.0, so magnitude should be full.
    EXPECT_LE(std::abs(offset.x), 10.0f);
    EXPECT_LE(std::abs(offset.y), 10.0f);
}

TEST(CameraSystem_CameraShake, EvaluateAtEnd_Decayed) {
    CameraShake shake;
    shake.magnitude = 10.0f;
    shake.frequency = 20.0f;
    shake.duration  = 1.0f;
    shake.elapsed   = 1.0f;  // At end.

    Vec2 offset = shake.evaluate();
    // Envelope is 0 at end.
    EXPECT_NEAR(offset.x, 0.0f, 0.001f);
    EXPECT_NEAR(offset.y, 0.0f, 0.001f);
}

TEST(CameraSystem_CameraShake, Expired) {
    CameraShake shake;
    shake.duration = 0.5f;
    shake.elapsed  = 0.3f;
    EXPECT_FALSE(shake.expired());

    shake.elapsed = 0.5f;
    EXPECT_TRUE(shake.expired());

    shake.elapsed = 1.0f;
    EXPECT_TRUE(shake.expired());
}

TEST(CameraSystem_CameraShake, ZeroDuration_NoOutput) {
    CameraShake shake;
    shake.duration = 0.0f;
    shake.elapsed  = 0.0f;
    Vec2 offset = shake.evaluate();
    EXPECT_FLOAT_EQ(offset.x, 0.0f);
    EXPECT_FLOAT_EQ(offset.y, 0.0f);
}

TEST(CameraSystem_CameraShake, MidDuration_PartialDecay) {
    CameraShake shake;
    shake.magnitude = 10.0f;
    shake.frequency = 20.0f;
    shake.duration  = 1.0f;
    shake.elapsed   = 0.5f;  // Halfway.

    Vec2 offset = shake.evaluate();
    // Envelope at 0.5 = (1 - 0.5)^2 = 0.25, so max ~2.5.
    EXPECT_LE(std::abs(offset.x), 10.0f * 0.25f + 0.01f);
    EXPECT_LE(std::abs(offset.y), 10.0f * 0.25f + 0.01f);
}

// ============================================================================
// shakes_accumulate
// ============================================================================

TEST(CameraSystem_ShakesAccumulate, EmptyVector) {
    std::vector<CameraShake> shakes;
    Vec2 total = shakes_accumulate(shakes, 0.016f);
    EXPECT_FLOAT_EQ(total.x, 0.0f);
    EXPECT_FLOAT_EQ(total.y, 0.0f);
    EXPECT_TRUE(shakes.empty());
}

TEST(CameraSystem_ShakesAccumulate, AdvancesElapsed) {
    std::vector<CameraShake> shakes;
    CameraShake s;
    s.magnitude = 5.0f;
    s.frequency = 10.0f;
    s.duration  = 1.0f;
    s.elapsed   = 0.0f;
    shakes.push_back(s);

    shakes_accumulate(shakes, 0.1f);
    EXPECT_EQ(shakes.size(), 1u);
    EXPECT_NEAR(shakes[0].elapsed, 0.1f, 0.001f);
}

TEST(CameraSystem_ShakesAccumulate, RemovesExpired) {
    std::vector<CameraShake> shakes;
    CameraShake s;
    s.duration = 0.5f;
    s.elapsed  = 0.49f;
    shakes.push_back(s);

    shakes_accumulate(shakes, 0.02f);  // 0.49 + 0.02 = 0.51 > 0.5.
    EXPECT_TRUE(shakes.empty());
}

TEST(CameraSystem_ShakesAccumulate, MultipleShakesAccumulate) {
    std::vector<CameraShake> shakes;
    CameraShake s1;
    s1.magnitude = 5.0f; s1.frequency = 10.0f; s1.duration = 1.0f; s1.elapsed = 0.0f;
    CameraShake s2;
    s2.magnitude = 3.0f; s2.frequency = 15.0f; s2.duration = 0.5f; s2.elapsed = 0.0f;
    shakes.push_back(s1);
    shakes.push_back(s2);

    Vec2 total = shakes_accumulate(shakes, 0.016f);
    // Both should still be active.
    EXPECT_EQ(shakes.size(), 2u);
    // Total is the sum of both shakes (non-zero for most seeds).
}

TEST(CameraSystem_ShakesAccumulate, PartialExpiry) {
    std::vector<CameraShake> shakes;
    CameraShake s1;
    s1.magnitude = 5.0f; s1.duration = 0.1f; s1.elapsed = 0.09f;  // About to expire.
    CameraShake s2;
    s2.magnitude = 3.0f; s2.duration = 10.0f; s2.elapsed = 0.0f;  // Long-lived.
    shakes.push_back(s1);
    shakes.push_back(s2);

    shakes_accumulate(shakes, 0.02f);
    EXPECT_EQ(shakes.size(), 1u);  // s1 expired, s2 remains.
}

// ============================================================================
// Spring Arm Update
// ============================================================================

TEST(CameraSystem_SpringArm, ZeroDt_NoMovement) {
    SpringArmData data;
    SpringArmState state;
    state.arm_pos = {0, 0, 0, 0};
    state.arm_rot = {0, 0, 0, 1};

    Vec3 out_pos, out_rot_unused;
    Quat out_rot;
    spring_arm_update(data, state, {5, 0, 0, 0}, {0, 0, 0, 1}, 0.0f,
                      out_pos, out_rot);
    EXPECT_FLOAT_EQ(out_pos.x, 0.0f);
    EXPECT_FLOAT_EQ(out_pos.y, 0.0f);
    EXPECT_FLOAT_EQ(out_pos.z, 0.0f);
}

TEST(CameraSystem_SpringArm, BasicMovement) {
    SpringArmData data;
    data.arm_length      = 5.0f;
    data.socket_offset_z = 1.0f;
    data.lag_speed       = 1000.0f;  // Very fast — essentially snap.

    SpringArmState state;
    state.arm_pos = {0, 0, 0, 0};
    state.arm_rot = {0, 0, 0, 1};

    Vec3 pivot = {0, 0, 0, 0};
    Quat desired_rot = {0, 0, 0, 1};  // Identity.

    Vec3 out_pos;
    Quat out_rot;
    spring_arm_update(data, state, pivot, desired_rot, 1.0f, out_pos, out_rot);

    // With identity rotation, arm extends along -Z.
    EXPECT_NEAR(out_pos.z, -5.0f, 0.5f);
    EXPECT_NEAR(out_pos.y, 1.0f, 0.5f);  // socket offset
}

TEST(CameraSystem_SpringArm, LagSmoothing) {
    SpringArmData data;
    data.arm_length = 5.0f;
    data.lag_speed  = 2.0f;  // Slow — should not snap.

    SpringArmState state;
    state.arm_pos = {0, 0, 0, 0};
    state.arm_rot = {0, 0, 0, 1};

    Vec3 pivot = {10, 0, 0, 0};  // Far away.
    Vec3 out_pos;
    Quat out_rot;
    spring_arm_update(data, state, pivot, {0, 0, 0, 1}, 0.016f, out_pos, out_rot);

    // With low lag_speed and small dt, arm_pos should NOT snap to pivot.
    EXPECT_LT(state.arm_pos.x, 10.0f);
    EXPECT_GT(state.arm_pos.x, 0.0f);
}

// ============================================================================
// Catmull-Rom Spline
// ============================================================================

TEST(CameraSystem_CatmullRom, EmptyPoints) {
    std::vector<Vec3> pts;
    Vec3 result = catmull_rom_eval(pts, 0.0f);
    EXPECT_FLOAT_EQ(result.x, 0.0f);
}

TEST(CameraSystem_CatmullRom, SinglePoint) {
    std::vector<Vec3> pts = {{3, 4, 5, 0}};
    Vec3 result = catmull_rom_eval(pts, 0.0f);
    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 4.0f);
    EXPECT_FLOAT_EQ(result.z, 5.0f);
}

TEST(CameraSystem_CatmullRom, TwoPoints_AtEnds) {
    std::vector<Vec3> pts = {{0, 0, 0, 0}, {10, 0, 0, 0}};
    Vec3 r0 = catmull_rom_eval(pts, 0.0f);
    Vec3 r1 = catmull_rom_eval(pts, 1.0f);
    EXPECT_NEAR(r0.x, 0.0f, 0.01f);
    EXPECT_NEAR(r1.x, 10.0f, 0.01f);
}

TEST(CameraSystem_CatmullRom, TwoPoints_Midpoint) {
    std::vector<Vec3> pts = {{0, 0, 0, 0}, {10, 0, 0, 0}};
    Vec3 mid = catmull_rom_eval(pts, 0.5f);
    EXPECT_NEAR(mid.x, 5.0f, 0.5f);
}

TEST(CameraSystem_CatmullRom, FourPoints_Smooth) {
    std::vector<Vec3> pts = {
        {0, 0, 0, 0}, {1, 2, 0, 0}, {3, 1, 0, 0}, {4, 0, 0, 0}
    };
    Vec3 r = catmull_rom_eval(pts, 1.5f);
    // Should be somewhere between points 1 and 2.
    EXPECT_GT(r.x, 1.0f);
    EXPECT_LT(r.x, 3.0f);
}

TEST(CameraSystem_CatmullRom, ClampsBeyondRange) {
    std::vector<Vec3> pts = {{0, 0, 0, 0}, {10, 0, 0, 0}};
    Vec3 r = catmull_rom_eval(pts, 5.0f);  // Beyond range.
    EXPECT_NEAR(r.x, 10.0f, 0.01f);
}

TEST(CameraSystem_CatmullRom, NegativeT_Clamped) {
    std::vector<Vec3> pts = {{0, 0, 0, 0}, {10, 0, 0, 0}};
    Vec3 r = catmull_rom_eval(pts, -1.0f);
    EXPECT_NEAR(r.x, 0.0f, 0.01f);
}

// ============================================================================
// .uzcam Preset Loader
// ============================================================================

class CameraPresetTest : public ::testing::Test {
protected:
    std::string temp_path;
    CameraSystemState state;

    void SetUp() override {
        temp_path = "test_camera_preset.uzcam";
        state = CameraSystemState{};
    }

    void TearDown() override {
        std::remove(temp_path.c_str());
    }

    void write_file(const std::string& content) {
        std::ofstream out(temp_path);
        out << content;
    }
};

TEST_F(CameraPresetTest, FileNotFound) {
    std::string error;
    EXPECT_FALSE(load_camera_preset("nonexistent.uzcam", state, error));
    EXPECT_FALSE(error.empty());
}

TEST_F(CameraPresetTest, BasicMode) {
    write_file("[mode]\ntype = third_person\n");
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_EQ(state.mode, CameraMode::ThirdPerson);
}

TEST_F(CameraPresetTest, FovSection) {
    write_file("[fov]\ndefault = 75.0\nads = 40.0\nsprint = 110.0\ninterp_speed = 12.0\n");
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_FLOAT_EQ(state.fov_settings.default_fov, 75.0f);
    EXPECT_FLOAT_EQ(state.fov_settings.ads_fov, 40.0f);
    EXPECT_FLOAT_EQ(state.fov_settings.sprint_fov, 110.0f);
    EXPECT_FLOAT_EQ(state.fov_settings.interp_speed, 12.0f);
    EXPECT_FLOAT_EQ(state.current_fov, 75.0f);  // Initialized from default.
}

TEST_F(CameraPresetTest, SpringArmSection) {
    write_file("[spring_arm]\narm_length = 8.0\nlag_speed = 15.0\nprobe_radius = 0.2\n");
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_FLOAT_EQ(state.spring_arm_data.arm_length, 8.0f);
    EXPECT_FLOAT_EQ(state.spring_arm_data.lag_speed, 15.0f);
    EXPECT_FLOAT_EQ(state.spring_arm_data.probe_radius, 0.2f);
}

TEST_F(CameraPresetTest, DoFSection) {
    write_file("[dof]\nenabled = true\nfocus_dist = 10.0\nfar_blur = 0.5\n");
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_TRUE(state.dof.enabled);
    EXPECT_FLOAT_EQ(state.dof.focus_dist, 10.0f);
    EXPECT_FLOAT_EQ(state.dof.far_blur, 0.5f);
}

TEST_F(CameraPresetTest, ShakePresets) {
    write_file(
        "[shake preset=land]\nmagnitude = 3.0\nfrequency = 15.0\nduration = 0.4\n\n"
        "[shake preset=explosion]\nmagnitude = 8.0\nfrequency = 10.0\nduration = 1.2\n"
    );
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_EQ(state.shake_presets.size(), 2u);
    EXPECT_STREQ(state.shake_presets[0].name, "land");
    EXPECT_FLOAT_EQ(state.shake_presets[0].magnitude, 3.0f);
    EXPECT_STREQ(state.shake_presets[1].name, "explosion");
    EXPECT_FLOAT_EQ(state.shake_presets[1].duration, 1.2f);
}

TEST_F(CameraPresetTest, CommentsAndEmptyLines) {
    write_file("; This is a comment\n\n# Another comment\n[mode]\ntype = orbit\n");
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_EQ(state.mode, CameraMode::Orbit);
}

TEST_F(CameraPresetTest, FullPresetFile) {
    write_file(
        "[mode]\ntype = third_person\n\n"
        "[spring_arm]\narm_length = 4.0\nsocket_offset_z = 0.8\n"
        "lag_speed = 10.0\nrotation_lag_speed = 8.0\n"
        "collision_test = true\nprobe_radius = 0.12\n\n"
        "[fov]\ndefault = 90.0\nads = 45.0\nsprint = 100.0\ninterp_speed = 8.0\n\n"
        "[dof]\nenabled = true\nfocus_dist = 5.0\nfocus_range = 2.0\n"
        "near_blur = 0.0\nfar_blur = 0.8\nbokeh_radius = 6.0\n\n"
        "[shake preset=gunshot]\nmagnitude = 1.5\nfrequency = 25.0\nduration = 0.2\n"
    );
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_EQ(state.mode, CameraMode::ThirdPerson);
    EXPECT_FLOAT_EQ(state.spring_arm_data.arm_length, 4.0f);
    EXPECT_FLOAT_EQ(state.fov_settings.default_fov, 90.0f);
    EXPECT_TRUE(state.dof.enabled);
    EXPECT_EQ(state.shake_presets.size(), 1u);
    EXPECT_STREQ(state.shake_presets[0].name, "gunshot");
}

TEST_F(CameraPresetTest, MalformedSection_Fails) {
    write_file("[bad section header\ntype = orbit\n");
    std::string error;
    EXPECT_FALSE(load_camera_preset(temp_path, state, error));
    EXPECT_FALSE(error.empty());
}

TEST_F(CameraPresetTest, UnknownSection_Ignored) {
    write_file("[unknown_section]\nfoo = bar\n[mode]\ntype = cinematic\n");
    std::string error;
    EXPECT_TRUE(load_camera_preset(temp_path, state, error));
    EXPECT_EQ(state.mode, CameraMode::Cinematic);
}

TEST_F(CameraPresetTest, AllModes) {
    for (const auto& [str, expected] : std::vector<std::pair<std::string, CameraMode>>{
        {"third_person", CameraMode::ThirdPerson},
        {"first_person", CameraMode::FirstPerson},
        {"orbit", CameraMode::Orbit},
        {"cinematic", CameraMode::Cinematic},
    }) {
        state = CameraSystemState{};
        write_file("[mode]\ntype = " + str + "\n");
        std::string error;
        EXPECT_TRUE(load_camera_preset(temp_path, state, error)) << str;
        EXPECT_EQ(state.mode, expected) << str;
    }
}

// ============================================================================
// CameraSystemState default initialization
// ============================================================================

TEST(CameraSystem_State, DefaultValues) {
    CameraSystemState state;
    EXPECT_EQ(state.mode, CameraMode::Orbit);
    EXPECT_FLOAT_EQ(state.current_fov, 90.0f);
    EXPECT_FALSE(state.is_aiming);
    EXPECT_FALSE(state.is_sprinting);
    EXPECT_TRUE(state.active_shakes.empty());
    EXPECT_FALSE(state.dof.enabled);
    EXPECT_TRUE(state.shake_presets.empty());
}
