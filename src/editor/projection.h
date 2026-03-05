// ---------------------------------------------------------------------------
// projection.h -- Camera projection math helpers (uz:: engine math).
//
// Left-handed DirectX convention. Used by main.cpp for CPU-side picking and
// camera matrix construction, matching camera_component.cpp's renderer-side
// matrices.
//
// RULE RHI-2: No DirectXMath above rhi.h — uses uz::Mat4 / uz::Vec3.
// ---------------------------------------------------------------------------
#pragma once

#include "engine/core/math.h"

/// Build a left-handed look-at view matrix (row-major data layout).
uz::Mat4 buildLookAtLH(uz::Vec3 eye, uz::Vec3 target, uz::Vec3 up);

/// Build a left-handed perspective projection matrix.
uz::Mat4 buildPerspLH(float fovY, float aspect, float nearZ, float farZ);

/// Project a world position through a VP matrix. Returns false if behind camera.
bool projectWorldToScreen(const uz::Mat4& vp, float wx, float wy, float wz,
                          int vpW, int vpH, float& outSx, float& outSy);

/// Build a VP matrix from camera yaw/pitch/position (matching camera_component fly-cam).
uz::Mat4 buildCameraVP(float yaw, float pitch,
                       float camX, float camY, float camZ,
                       int vpW, int vpH);
