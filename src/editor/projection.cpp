// ---------------------------------------------------------------------------
// projection.cpp -- Camera projection math helpers (uz:: engine math).
//
// RULE RHI-2: No DirectXMath above rhi.h — uses uz::Mat4 / uz::Vec3.
// ---------------------------------------------------------------------------

#include "projection.h"

#include <cmath>

uz::Mat4 buildLookAtLH(uz::Vec3 eye, uz::Vec3 target, uz::Vec3 up) {
    using namespace uz;
    const Vec3 diff = target - eye;
    const float diffLen = vec3_len(diff);
    if (diffLen < kEpsilon) return Mat4::identity();
    const Vec3 f = diff * (1.0f / diffLen);
    Vec3 crossUpF = vec3_cross(up, f);
    float crossLen = vec3_len(crossUpF);
    if (crossLen < kEpsilon) {
        Vec3 altUp = (std::abs(f.y) > 0.99f) ? Vec3{1,0,0,0} : Vec3{0,1,0,0};
        crossUpF = vec3_cross(altUp, f);
        crossLen = vec3_len(crossUpF);
        if (crossLen < kEpsilon) return Mat4::identity();
    }
    const Vec3 r = crossUpF * (1.0f / crossLen);
    const Vec3 u = vec3_cross(f, r);
    Mat4 m = {};
    m.m[0][0]=r.x;  m.m[0][1]=u.x;  m.m[0][2]=f.x;  m.m[0][3]=0;
    m.m[1][0]=r.y;  m.m[1][1]=u.y;  m.m[1][2]=f.y;  m.m[1][3]=0;
    m.m[2][0]=r.z;  m.m[2][1]=u.z;  m.m[2][2]=f.z;  m.m[2][3]=0;
    m.m[3][0]=-vec3_dot(r,eye); m.m[3][1]=-vec3_dot(u,eye);
    m.m[3][2]=-vec3_dot(f,eye); m.m[3][3]=1;
    return m;
}

uz::Mat4 buildPerspLH(float fovY, float aspect, float nearZ, float farZ) {
    float h = 1.0f / std::tan(fovY * 0.5f);
    float w = h / aspect;
    float q = farZ / (farZ - nearZ);
    uz::Mat4 m = {};
    m.m[0][0]=w; m.m[1][1]=h;
    m.m[2][2]=q; m.m[2][3]=1;
    m.m[3][2]=-nearZ*q;
    return m;
}

bool projectWorldToScreen(const uz::Mat4& vp, float wx, float wy, float wz,
                          int vpW, int vpH, float& outSx, float& outSy) {
    // Mat4 * (wx,wy,wz,1) — manual 4-component transform
    float cx = vp.m[0][0]*wx + vp.m[1][0]*wy + vp.m[2][0]*wz + vp.m[3][0];
    float cy = vp.m[0][1]*wx + vp.m[1][1]*wy + vp.m[2][1]*wz + vp.m[3][1];
    float cz = vp.m[0][2]*wx + vp.m[1][2]*wy + vp.m[2][2]*wz + vp.m[3][2];  // unused
    float cw = vp.m[0][3]*wx + vp.m[1][3]*wy + vp.m[2][3]*wz + vp.m[3][3];
    (void)cz;
    if (cw <= 0.001f) return false;
    float ndcX = cx / cw;
    float ndcY = cy / cw;
    outSx = (ndcX * 0.5f + 0.5f) * vpW;
    outSy = (1.0f - (ndcY * 0.5f + 0.5f)) * vpH;
    return true;
}

uz::Mat4 buildCameraVP(float yaw, float pitch,
                       float camX, float camY, float camZ,
                       int vpW, int vpH) {
    uz::Vec3 camPos = {camX, camY, camZ, 0.0f};
    uz::Vec3 forward = {
        cosf(pitch) * sinf(yaw),
        sinf(pitch),
        cosf(pitch) * cosf(yaw),
        0.0f
    };
    uz::Vec3 target = camPos + forward;
    uz::Vec3 up = {0.0f, 1.0f, 0.0f, 0.0f};

    float aspect = (vpW > 0 && vpH > 0) ? (float)vpW / (float)vpH : 1.0f;
    uz::Mat4 view = buildLookAtLH(camPos, target, up);
    uz::Mat4 proj = buildPerspLH(uz::kHalfPi * 0.5f, aspect, 0.1f, 500.0f);
    return proj * view;  // reversed: row-major data in col-major multiply (matches camera_component)
}
