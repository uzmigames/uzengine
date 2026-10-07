/// Depth Buffer Utilities — reversed-Z depth linearization and sky detection.
///
/// UzEngine uses reversed-Z depth (near=1.0, far~=0.0) for improved
/// floating-point precision at distance.
///
/// @ref Upchurch & Desbrun 2012 "Tightening the Precision of Perspective Rendering"
/// @ref Rasterization rules — reversed-Z maps [near, far] to [1, 0]

#ifndef DEPTH_COMMON_HLSLI
#define DEPTH_COMMON_HLSLI

/// Linearizes a reversed-Z depth value to view-space distance.
///
/// Reversed-Z (Mat4::Perspective: near -> 1, far -> 0) maps the view depth z to
/// d = near * (far - z) / (z * (far - near)), so z = near * far / (near + d * (far - near)).
/// Until block B.4b (2026-10-06) this carried the standard-Z inverse, near * far / (far - d * (far - near)),
/// which returns ~near for every reversed-Z depth; the contact shadows never found an occluder.
/// where z is the raw depth buffer value in [0, 1].
///
/// @param depth    Raw depth buffer value (reversed-Z: 1.0 = near, ~0.0 = far).
/// @param nearZ    Camera near plane distance.
/// @param farZ     Camera far plane distance.
/// @return         Linear view-space distance in [nearZ, farZ].
float LinearizeDepthReversedZ(float depth, float nearZ, float farZ) {
    return (nearZ * farZ) / (nearZ + depth * (farZ - nearZ));
}

/// Returns true if the depth value represents the sky (far plane).
///
/// In reversed-Z, the far plane maps to ~0. Values very close to zero
/// indicate sky/background pixels that should not participate in
/// depth-based effects (soft particles, SSAO, SSR, etc.).
///
/// @param depth    Raw depth buffer value (reversed-Z).
/// @return         True if the pixel is at the far plane (sky).
bool IsSkyDepth(float depth) {
    return depth < 0.00001f;
}

/// Reconstructs world-space position from screen UV and reversed-Z depth.
///
/// @param screenUV  Screen-space UV in [0,1]x[0,1].
/// @param depth     Raw depth buffer value (reversed-Z).
/// @param invVP     Inverse view-projection matrix.
/// @return          World-space position.
float3 ReconstructWorldPos(float2 screenUV, float depth, float4x4 invVP) {
    float4 clipPos = float4(screenUV * 2.0f - 1.0f, depth, 1.0f);
    clipPos.y = -clipPos.y; // D3D UV convention
    float4 worldPos = mul(clipPos, invVP);
    return worldPos.xyz / worldPos.w;
}

#endif // DEPTH_COMMON_HLSLI
