// camera_component.cpp — Camera update implementations (RHI-agnostic).
//
// All math uses engine types (Mat4, Vec3). The DX12/Vulkan renderer
// converts to API-specific types at the boundary.

#include "camera_component.h"

#include "engine/renderer/scene/scene_params.h"
#include "engine/scene/scene.h"
#include "engine/scene/scene_node.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ============================================================================
// Helper — build a left-handed perspective projection matrix
// ============================================================================

static Mat4 mat4_perspective_lh(float fov_y_rad, float aspect, float near_z, float far_z) {
    const float h = 1.0f / std::tan(fov_y_rad * 0.5f);
    const float w = h / aspect;
    const float q = far_z / (far_z - near_z);

    Mat4 m{};
    m.m[0][0] = w;
    m.m[1][1] = h;
    m.m[2][2] = q;
    m.m[2][3] = 1.0f;   // Left-handed: +1
    m.m[3][2] = -near_z * q;
    m.m[3][3] = 0.0f;
    return m;
}

// ============================================================================
// Helper — build a left-handed look-at view matrix
// ============================================================================

static Mat4 mat4_look_at_lh(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 diff = {target.x - eye.x, target.y - eye.y, target.z - eye.z, 0.0f};
    float diffLen = vec3_len(diff);
    if (diffLen < kEpsilon) return Mat4::identity();
    Vec3 zaxis = diff * (1.0f / diffLen);
    Vec3 crossResult = vec3_cross(up, zaxis);
    float crossLen = vec3_len(crossResult);
    if (crossLen < kEpsilon) {
        Vec3 altUp = (std::abs(zaxis.y) > 0.99f) ? Vec3{1,0,0,0} : Vec3{0,1,0,0};
        crossResult = vec3_cross(altUp, zaxis);
        crossLen = vec3_len(crossResult);
        if (crossLen < kEpsilon) return Mat4::identity();
    }
    Vec3 xaxis = crossResult * (1.0f / crossLen);
    Vec3 yaxis = vec3_cross(zaxis, xaxis);

    // NOTE: This builds the matrix in DirectXMath's row-major memory layout
    // (axis vectors in rows, not columns). Since Mat4::operator* treats
    // m[col][row] as column-major, we must reverse the multiplication order:
    // use `proj * view` instead of `view * proj`.
    Mat4 m{};
    m.m[0][0] = xaxis.x;  m.m[1][0] = xaxis.y;  m.m[2][0] = xaxis.z;
    m.m[0][1] = yaxis.x;  m.m[1][1] = yaxis.y;  m.m[2][1] = yaxis.z;
    m.m[0][2] = zaxis.x;  m.m[1][2] = zaxis.y;  m.m[2][2] = zaxis.z;
    m.m[3][0] = -vec3_dot(xaxis, eye);
    m.m[3][1] = -vec3_dot(yaxis, eye);
    m.m[3][2] = -vec3_dot(zaxis, eye);
    m.m[3][3] = 1.0f;
    return m;
}

// ============================================================================
// Constants
// ============================================================================

static constexpr float kPiDiv4   = kPi / 4.0f;
static constexpr float kDeg2Rad  = kPi / 180.0f;

// ============================================================================
// Editor fly-camera
// ============================================================================

CameraState camera_update_fly(SceneParams* params, float aspect, float dt) {
    CameraState state{};
    dt = std::min(dt, 0.1f);

    if (!params) {
        const Vec3 eye = {0.0f, 15.0f, -20.0f, 0.0f};
        const Vec3 at  = {0.0f, 0.0f,   0.0f, 0.0f};
        const Vec3 up  = {0.0f, 1.0f,   0.0f, 0.0f};
        Mat4 view = mat4_look_at_lh(eye, at, up);
        Mat4 proj = mat4_perspective_lh(kPiDiv4, aspect, 0.1f, 500.0f);
        state.view_proj = proj * view;  // reversed: row-major data in col-major multiply
        state.eye_pos   = eye;
        state.forward   = {0.0f, 0.0f, 1.0f, 0.0f};
        return state;
    }

    auto& ci = params->cameraInput;
    auto& sp = *params;

    // Scroll wheel → adjust camera speed.
    const float scroll = ci.scrollDelta.exchange(0.0f, std::memory_order_relaxed);
    if (scroll != 0.0f) {
        sp.camSpeed *= std::pow(1.1f, scroll);
        sp.camSpeed  = std::clamp(sp.camSpeed, 0.1f, 500.0f);
    }

    // Mouse look (only when RMB held).
    const float mdx = ci.mouseDeltaX.exchange(0.0f, std::memory_order_relaxed);
    const float mdy = ci.mouseDeltaY.exchange(0.0f, std::memory_order_relaxed);
    const bool  rmb = ci.rightMouseDown.load(std::memory_order_relaxed);

    if (rmb) {
        sp.camYaw   += mdx * sp.camSensitivity;
        sp.camPitch -= mdy * sp.camSensitivity;

        constexpr float kMaxPitch = 89.0f * kDeg2Rad;
        sp.camPitch = std::clamp(sp.camPitch, -kMaxPitch, kMaxPitch);
    }

    // Forward / right / up vectors from yaw + pitch.
    const float cosYaw   = std::cos(sp.camYaw);
    const float sinYaw   = std::sin(sp.camYaw);
    const float cosPitch = std::cos(sp.camPitch);
    const float sinPitch = std::sin(sp.camPitch);

    const Vec3 forward = {sinYaw * cosPitch, sinPitch, cosYaw * cosPitch, 0.0f};
    const Vec3 worldUp = {0.0f, 1.0f, 0.0f, 0.0f};
    Vec3 rightCross = vec3_cross(worldUp, forward);
    float rightLen = vec3_len(rightCross);
    const Vec3 right = (rightLen > kEpsilon)
        ? rightCross * (1.0f / rightLen)
        : Vec3{1.0f, 0.0f, 0.0f, 0.0f};

    // WASD + EQ movement (only when RMB held).
    if (rmb) {
        const float fwd    = ci.moveForward.load(std::memory_order_relaxed)
                           - ci.moveBack.load(std::memory_order_relaxed);
        const float strafe = ci.moveRight.load(std::memory_order_relaxed)
                           - ci.moveLeft.load(std::memory_order_relaxed);
        const float vert   = ci.moveUp.load(std::memory_order_relaxed)
                           - ci.moveDown.load(std::memory_order_relaxed);

        const float boost = ci.shiftDown.load(std::memory_order_relaxed) ? 3.0f : 1.0f;
        const float speed = sp.camSpeed * boost * dt;

        sp.camPosX += forward.x * fwd * speed + right.x * strafe * speed;
        sp.camPosY += forward.y * fwd * speed + worldUp.y * vert * speed;
        sp.camPosZ += forward.z * fwd * speed + right.z * strafe * speed;
    }

    // Build view/projection matrices.
    const Vec3 eye = {sp.camPosX, sp.camPosY, sp.camPosZ, 0.0f};
    const Vec3 at  = {eye.x + forward.x, eye.y + forward.y, eye.z + forward.z, 0.0f};
    Mat4 view = mat4_look_at_lh(eye, at, worldUp);
    Mat4 proj = mat4_perspective_lh(kPiDiv4, aspect, 0.1f, 500.0f);

    state.view_proj = proj * view;  // reversed: row-major data in col-major multiply
    state.eye_pos   = eye;
    state.forward   = forward;

    return state;
}

// ============================================================================
// Scene-driven camera
// ============================================================================

CameraState camera_update_from_scene(const Scene* scene, float aspect) {
    CameraState state{};

    auto make_default = [&]() {
        const Vec3 eye = {0.0f, 5.0f, -10.0f, 0.0f};
        const Vec3 at  = {0.0f, 0.0f,  0.0f, 0.0f};
        const Vec3 up  = {0.0f, 1.0f,  0.0f, 0.0f};
        state.view_proj = mat4_perspective_lh(kPiDiv4, aspect, 0.1f, 500.0f)
                        * mat4_look_at_lh(eye, at, up);  // reversed: row-major data
        state.eye_pos   = eye;
        state.forward   = {0.0f, 0.0f, 1.0f, 0.0f};
    };

    if (!scene) { make_default(); return state; }

    // Find first active Camera node.
    NodeId cam_id = kNodeIdInvalid;
    for (uint32_t i = 1; i < scene->next_id; ++i) {
        const SceneNode& node = scene->nodes[i];
        if (node.alive && node.type == NodeType::Camera &&
            (node.flags & NODE_FLAG_ACTIVE)) {
            cam_id = i;
            break;
        }
    }

    if (cam_id == kNodeIdInvalid) { make_default(); return state; }

    const SceneNode& camNode = scene->nodes[cam_id];

    // Read CameraData from ComponentStore.
    float fov_y = 45.0f, near_p = 0.1f, far_p = 500.0f;
    const CameraData* cd = component_get<CameraData>(&scene->components, cam_id);
    if (cd) {
        fov_y  = cd->fov_y;
        near_p = cd->near_plane;
        far_p  = cd->far_plane;
    }

    // Extract from world transform.
    const Mat4& wt = camNode.world_transform;
    const Vec3 eye = {wt.m[3][0], wt.m[3][1], wt.m[3][2], 0.0f};
    Vec3 fwdRaw = {wt.m[2][0], wt.m[2][1], wt.m[2][2], 0.0f};
    float fwdLen = vec3_len(fwdRaw);
    const Vec3 fwd = (fwdLen > kEpsilon) ? fwdRaw * (1.0f / fwdLen) : Vec3{0,0,1,0};
    Vec3 upRaw = {wt.m[1][0], wt.m[1][1], wt.m[1][2], 0.0f};
    float upLen = vec3_len(upRaw);
    const Vec3 up = (upLen > kEpsilon) ? upRaw * (1.0f / upLen) : Vec3{0,1,0,0};
    const Vec3 at  = {eye.x + fwd.x, eye.y + fwd.y, eye.z + fwd.z, 0.0f};

    Mat4 view = mat4_look_at_lh(eye, at, up);
    Mat4 proj = mat4_perspective_lh(fov_y * kDeg2Rad, aspect, near_p, far_p);

    state.view_proj = proj * view;  // reversed: row-major data in col-major multiply
    state.eye_pos   = eye;
    state.forward   = fwd;

    return state;
}

} // namespace uz
