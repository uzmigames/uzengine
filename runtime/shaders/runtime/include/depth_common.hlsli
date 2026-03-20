/// Depth buffer utilities — Reversed-Z convention (standard).
///
/// UzEngine uses reversed-Z: near=1.0, far=0.0. This file provides helpers
/// so shaders never hardcode depth direction assumptions.
///
/// # Rules
/// - NEVER use raw `0.0` or `1.0` for depth comparisons — use these helpers.
/// - "Closer" means GREATER depth value in reversed-Z.
/// - Clear depth to DepthFar() (0.0).

#ifndef DEPTH_COMMON_HLSLI
#define DEPTH_COMMON_HLSLI

/// Returns the far plane depth value (clear target).
float DepthFar()  { return 0.0f; }

/// Returns the near plane depth value.
float DepthNear() { return 1.0f; }

/// Returns true if depth `a` is closer to the camera than depth `b`.
/// In reversed-Z: closer = greater value.
bool IsCloser(float a, float b) { return a > b; }

/// Returns true if the pixel is at the sky (far plane).
bool IsSkyDepth(float depth) { return depth <= 0.0001f; }

/// Linearizes a reversed-Z depth value to view-space distance.
///
/// # Arguments
/// - rawDepth: depth buffer sample in [1, 0] (reversed-Z).
/// - near: camera near plane distance.
/// - far: camera far plane distance.
///
/// # Returns
/// Linear view-space distance from the camera.
float LinearizeDepthReversedZ(float rawDepth, float near, float far) {
    // Reversed-Z perspective: z_ndc = near/(far-near) * z_view + (-near*far)/(far-near)
    // Solving for z_view: z_view = (near * far) / (near + rawDepth * (far - near))
    return (near * far) / (near + rawDepth * (far - near));
}

#endif // DEPTH_COMMON_HLSLI
