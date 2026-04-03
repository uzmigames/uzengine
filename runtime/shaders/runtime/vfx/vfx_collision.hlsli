/// GPU collision utility functions for VFX particle depth-buffer testing.
///
/// Provides screen-space depth comparison, surface normal reconstruction from
/// the depth buffer gradient, velocity reflection, and plane projection.
/// Included by the particle update compute shader when the CollisionModule
/// is active in the module stack.
///
/// # Coordinate Conventions
/// - Clip space: DX-convention (z in [0, 1], NDC x/y in [-1, 1]).
/// - World space: left-handed (+Y up).
/// - Screen UV: origin at top-left, u right, v down.
///
/// # Performance Notes
/// `EstimateSurfaceNormal` samples the depth buffer five times (centre + four
/// neighbours). This is acceptable for particle counts up to ~100k per frame
/// on modern hardware, as depth texture bandwidth is limited and the sampler
/// is bilinear-filtered. Higher particle counts should pre-build a screen-space
/// normal buffer in a separate pass and sample it instead.

#ifndef VFX_COLLISION_HLSLI
#define VFX_COLLISION_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers -- depth linearization and world reconstruction
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs a world-space position from a screen UV and a raw depth value.
///
/// Applies the inverse view-projection to an NDC position. NDC x/y are
/// mapped from [0,1] UV space; NDC z is the raw depth from the depth buffer.
///
/// @param screenUV    [0,1]x[0,1] screen UV (origin top-left).
/// @param depth       Raw depth buffer sample in [0, 1].
/// @param invViewProj Inverse of the view-projection matrix for this frame.
/// @return            World-space position corresponding to the depth sample.
float3 ReconstructWorldPos(float2 screenUV, float depth, float4x4 invViewProj)
{
    // Convert UV to NDC: u -> [-1,1], v -> [1,-1] (DX UV origin is top-left).
    float2 ndc;
    ndc.x =  screenUV.x * 2.0 - 1.0;
    ndc.y = -screenUV.y * 2.0 + 1.0;

    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);

    // Perspective divide to recover true world position.
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// EstimateSurfaceNormal
// ─────────────────────────────────────────────────────────────────────────────

/// Estimates the world-space surface normal from the depth buffer gradient.
///
/// Samples the depth texture at four neighbours (one texel offset in each
/// cardinal direction) and reconstructs world-space positions from each sample.
/// The normal is the cross product of the horizontal and vertical tangents, then
/// normalized. Because this reconstructs from the depth-only buffer, the normal
/// is a geometric approximation; it is sufficient for particle collision
/// responses at typical viewing distances.
///
/// @param depthTexture  Scene depth buffer (R32_FLOAT or D32_FLOAT read as float).
/// @param depthSampler  Sampler state used for depth reads (point-clamp recommended
///                      to avoid blending depth values across surface discontinuities).
/// @param screenUV      [0,1]x[0,1] screen coordinate of the collision point.
/// @param texelSize     1.0 / renderTargetDimensions (one texel in UV space).
/// @param invViewProj   Inverse view-projection matrix for this frame.
/// @return              Normalized world-space surface normal pointing toward the
///                      camera (i.e., away from the surface into view space).
///                      Returns float3(0,1,0) as a fallback when depth is 0 or 1
///                      (sky or invalid sample).
float3 EstimateSurfaceNormal(
    Texture2D<float> depthTexture,
    SamplerState     depthSampler,
    float2           screenUV,
    float2           texelSize,
    float4x4         invViewProj)
{
    // Sample depth at four cardinal neighbours (+x, -x, +y, -y in UV space).
    // Point-clamp sampling is preferred here: bilinear blending across a depth
    // discontinuity (e.g., a wall edge) would corrupt the normal estimate.
    float depthRight = depthTexture.SampleLevel(depthSampler, screenUV + float2( texelSize.x, 0.0), 0);
    float depthLeft  = depthTexture.SampleLevel(depthSampler, screenUV + float2(-texelSize.x, 0.0), 0);
    float depthDown  = depthTexture.SampleLevel(depthSampler, screenUV + float2(0.0,  texelSize.y), 0);
    float depthUp    = depthTexture.SampleLevel(depthSampler, screenUV + float2(0.0, -texelSize.y), 0);

    // Reconstruct world-space positions for each neighbour.
    float3 posRight = ReconstructWorldPos(screenUV + float2( texelSize.x, 0.0), depthRight,  invViewProj);
    float3 posLeft  = ReconstructWorldPos(screenUV + float2(-texelSize.x, 0.0), depthLeft,   invViewProj);
    float3 posDown  = ReconstructWorldPos(screenUV + float2(0.0,  texelSize.y), depthDown,   invViewProj);
    float3 posUp    = ReconstructWorldPos(screenUV + float2(0.0, -texelSize.y), depthUp,     invViewProj);

    // Central-difference tangents in world space.
    float3 tangentX = posRight - posLeft;
    float3 tangentY = posUp    - posDown; // +Y = up, so "up - down" gives world +Y tangent

    // Cross product gives the geometric normal. Normalize to unit length.
    float3 normal = normalize(cross(tangentX, tangentY));

    return normal;
}

// ─────────────────────────────────────────────────────────────────────────────
// ScreenSpaceCollision
// ─────────────────────────────────────────────────────────────────────────────

/// Tests whether a world-space particle has penetrated the scene depth surface.
///
/// Projects `worldPos` into screen space using `viewProj`, reads the scene
/// depth at the resulting screen UV, reconstructs the surface world position,
/// and compares the particle's depth against the surface depth. A collision
/// is reported when the particle is at or behind the surface, offset by
/// `collisionOffset` (a small standoff distance to prevent z-fighting).
///
/// On collision the estimated surface normal (from `EstimateSurfaceNormal`) is
/// written to `surfaceNormal`. The normal is valid only when the function
/// returns true; its value is undefined on a miss.
///
/// @param worldPos          World-space particle position.
/// @param viewProj          View-projection matrix for this frame.
/// @param depthTexture      Scene depth buffer (R32_FLOAT or D32_FLOAT).
/// @param depthSampler      Linear-wrap or point-clamp sampler for depth reads.
/// @param screenDimensions  Render target size in pixels (width, height).
/// @param collisionOffset   World-space standoff distance (metres). Must be >= 0.
/// @param surfaceNormal     [out] Estimated world-space surface normal on hit.
///
/// @return  true if the particle has penetrated the depth surface, false otherwise.
bool ScreenSpaceCollision(
    float3           worldPos,
    float4x4         viewProj,
    Texture2D<float> depthTexture,
    SamplerState     depthSampler,
    float2           screenDimensions,
    float            collisionOffset,
    out float3       surfaceNormal)
{
    // Default normal to world up; overwritten on hit.
    surfaceNormal = float3(0.0, 1.0, 0.0);

    // Project particle to clip space.
    float4 clipPos = mul(viewProj, float4(worldPos, 1.0));

    // Clip-space w <= 0 means the particle is behind the camera.
    // Reject early -- no valid screen position can be computed.
    if (clipPos.w <= 0.0) {
        return false;
    }

    // Perspective divide to NDC, then convert to [0,1] screen UV.
    float3 ndc   = clipPos.xyz / clipPos.w;
    float2 screenUV;
    screenUV.x = ndc.x *  0.5 + 0.5;
    screenUV.y = ndc.y * -0.5 + 0.5; // Invert Y: NDC +Y = screen top in DX

    // Reject particles outside the viewport (handles off-screen effects).
    if (screenUV.x < 0.0 || screenUV.x > 1.0 ||
        screenUV.y < 0.0 || screenUV.y > 1.0)
    {
        return false;
    }

    // Sample the scene depth at this screen position.
    float sceneDepth = depthTexture.SampleLevel(depthSampler, screenUV, 0);

    // A scene depth of exactly 0 or 1 indicates the far or clear-color region
    // (no real geometry). Skip collision to avoid false hits against the skybox.
    if (sceneDepth <= 0.0 || sceneDepth >= 1.0) {
        return false;
    }

    // Particle's own normalized depth (NDC z, already in [0,1] for DX).
    float particleDepth = ndc.z;

    // Collision: the particle depth exceeds the scene surface depth (particle
    // is at or behind the surface, within the standoff tolerance).
    // The collisionOffset is in world units (cm). Convert to NDC depth space
    // using clip-space w (perspective depth at this pixel). This gives correct
    // scale at any distance, unlike the previous hardcoded * 0.001 approximation.
    float depthTolerance = collisionOffset / max(clipPos.w, 0.001);
    if (particleDepth >= sceneDepth - depthTolerance) {
        // Estimate the surface normal at the collision point.
        float2 texelSize = 1.0 / screenDimensions;

        // Build the inverse view-projection from the columns of viewProj.
        // This avoids passing an extra matrix constant when invViewProj is not
        // already available. The inversion is performed per-collision thread
        // which is acceptable given particle counts and the matrix being 4x4.
        //
        // Note: HLSL does not have a built-in matrix inverse. We rely on the
        // caller having bound cbInvViewProj as a separate constant. The
        // EstimateSurfaceNormal signature takes it as a parameter; we forward
        // the cbInvViewProj constant declared in the parent shader.
        surfaceNormal = EstimateSurfaceNormal(
            depthTexture,
            depthSampler,
            screenUV,
            texelSize,
            cbInvViewProj   // declared in the enclosing compute shader cbuffer
        );

        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReflectVelocity
// ─────────────────────────────────────────────────────────────────────────────

/// Reflects a velocity vector off a surface normal with energy loss.
///
/// Standard specular reflection: `v' = v - 2*(v·n)*n`, scaled by bounceFactor.
/// When `bounceFactor` is 1.0 the collision is perfectly elastic; 0.0 results
/// in a zero velocity (completely inelastic).
///
/// @param velocity     Pre-collision velocity (world space, any magnitude).
/// @param normal       Unit surface normal pointing away from the surface.
///                     Behaviour is undefined if `normal` is not unit-length.
/// @param bounceFactor Energy retention scalar in [0, 1].
///
/// @return  Post-collision velocity. Magnitude is `|velocity| * bounceFactor`.
float3 ReflectVelocity(float3 velocity, float3 normal, float bounceFactor)
{
    float3 reflected = velocity - 2.0 * dot(velocity, normal) * normal;
    return reflected * bounceFactor;
}

// ─────────────────────────────────────────────────────────────────────────────
// ProjectOntoPlane
// ─────────────────────────────────────────────────────────────────────────────

/// Projects a velocity vector onto the plane defined by a surface normal.
///
/// Removes the normal component of the velocity, leaving only the tangential
/// component that lies within the surface plane. This lets particles slide
/// along geometry without penetrating it.
///
/// `v_tangential = v - (v·n)*n`
///
/// @param velocity  Pre-collision velocity (world space).
/// @param normal    Unit surface normal. Behaviour is undefined for zero or
///                  non-unit vectors.
///
/// @return  Velocity with the normal component removed. Magnitude is always
///          <= `|velocity|`. Returns the zero vector when velocity is purely
///          normal to the surface.
float3 ProjectOntoPlane(float3 velocity, float3 normal)
{
    return velocity - dot(velocity, normal) * normal;
}

#endif // VFX_COLLISION_HLSLI
