/// Velocity buffer generation — depth-based reprojection.
///
/// Reconstructs world-space position from the depth buffer and the current
/// frame's inverse view-projection matrix, then reprojects that position
/// through the previous frame's view-projection matrix to compute per-pixel
/// screen-space motion vectors.
///
/// Output is RG16F: velocity in **pixels** (not NDC).  Positive X = rightward
/// motion, positive Y = downward motion (screen-space convention).
///
/// This pass runs fullscreen after the G-Buffer pass, paired with
/// `fullscreen_vs.hlsl` (DrawInstanced(3,1,0,0), no vertex buffer).
///
/// # Constant Buffers
/// - b1 (PerFrameCB): viewProj, invViewProj, nearPlane, farPlane (standard layout).
/// - b2 (VelocityCB): prevViewProj (64 bytes) + screenDimensions (8 bytes).
///
/// # Textures
/// - SM6.6 (UZ_BINDLESS): depth SRV index passed via root constants at b0.
/// - SM5: depth texture bound at t0.
///
/// # Output
/// - SV_Target0: float2 velocity in pixels (RG16F render target).

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from fullscreen_vs.hlsl.
struct FSInput {
    float4 positionCS : SV_Position; ///< Pixel coordinates (xy = integer pixel center).
    float2 texcoord   : TEXCOORD0;   ///< Screen UV in [0,1] (top-left origin).
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — depth SRV descriptor heap index.
cbuffer DepthIndex : register(b0) {
    uint depthSrvIndex; ///< Descriptor heap index for the depth buffer SRV (R32_FLOAT).
};
#endif

/// Per-frame CB at b1 — mirrors the C++ PerFrameCB layout.
///
/// Only invViewProj is consumed by this shader, but the full struct is declared
/// to match the C++ layout so no partial-binding path is needed.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;     ///< Current frame view-projection (unused here).
    float4   cameraPos;              ///< xyz = camera world position, w = unused.
    float4   timeParams;             ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    float4   lightDir;               ///< xyz = direction toward primary light, w = unused.
    float4   lightColor;             ///< xyz = color * intensity, w = unused.
    float4   ambientColor;           ///< xyz = ambient radiance, w = unused.
    row_major float4x4 invViewProj;  ///< Inverse view-projection for world-position reconstruction.
    float    nearPlane;              ///< Near clip plane distance in world units.
    float    farPlane;               ///< Far clip plane distance in world units.
    float    _framePad[10];          ///< Remaining padding to 256-byte alignment.
};

/// Velocity pass CB at b2 — previous frame's view-projection and screen size.
///
/// prevViewProj is the view-projection matrix from the previous frame, used
/// to reproject world positions into the previous frame's screen space.
/// screenWidth/screenHeight convert NDC deltas to pixel-space velocity.
cbuffer VelocityCB : register(b2) {
    row_major float4x4 prevViewProj; ///< Previous frame's view-projection matrix.
    float    screenWidth;            ///< Render target width in pixels.
    float    screenHeight;           ///< Render target height in pixels.
    float2   _velPad;                ///< Padding to 16-byte alignment.
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// SM5 path: depth buffer SRV bound at t0.
Texture2D<float> depthTex : register(t0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes per-pixel screen-space velocity via depth reprojection.
///
/// # Algorithm
/// 1. Load raw depth at the current pixel (point-sampled via Load, no sampler needed).
/// 2. Reconstruct clip-space position from UV + depth.
/// 3. Transform to world space via invViewProj (perspective divide).
/// 4. Reproject to previous clip space via prevViewProj (perspective divide).
/// 5. Convert both current and previous positions to screen pixels.
/// 6. Output the delta as RG16F.
///
/// # Returns
/// float2 velocity in pixels. Zero = static geometry with no camera motion.
float2 main(FSInput input) : SV_Target0 {

    int2 pixelCoord = int2(input.positionCS.xy);

    // -- 1. Load raw depth (integer coords, no sampler) -----------------------
#ifdef UZ_BINDLESS
    Texture2D<float> depthBuf = ResourceDescriptorHeap[depthSrvIndex];
    float rawDepth = depthBuf.Load(int3(pixelCoord, 0)).r;
#else
    float rawDepth = depthTex.Load(int3(pixelCoord, 0)).r;
#endif

    // Sky pixels (depth == 0 in reversed-Z, depth == 1 in standard) have no
    // meaningful world position.  Output zero velocity to avoid artifacts.
    if (rawDepth == 0.0f || rawDepth == 1.0f) {
        return float2(0.0f, 0.0f);
    }

    // -- 2. Reconstruct clip-space position -----------------------------------
    // UV -> NDC: X in [-1,1], Y flipped (D3D UV top-left vs NDC Y-up)
    float2 uv  = input.texcoord;
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clipPos = float4(ndc, rawDepth, 1.0f);

    // -- 3. World-space position via inverse view-projection ------------------
    float4 worldH = mul(clipPos, invViewProj);
    float3 worldPos = worldH.xyz / worldH.w;

    // -- 4. Reproject through previous frame's view-projection ----------------
    float4 prevClip = mul(float4(worldPos, 1.0f), prevViewProj);
    float2 prevNdc  = prevClip.xy / prevClip.w;

    // -- 5. Convert NDC to screen-pixel coordinates ---------------------------
    // NDC [-1,1] -> screen pixels [0, width/height]
    // Note: Y is flipped back (NDC Y-up -> screen Y-down)
    float2 currScreen = float2(
        (ndc.x  * 0.5f + 0.5f) * screenWidth,
        (-ndc.y * 0.5f + 0.5f) * screenHeight
    );
    float2 prevScreen = float2(
        (prevNdc.x  * 0.5f + 0.5f) * screenWidth,
        (-prevNdc.y * 0.5f + 0.5f) * screenHeight
    );

    // -- 6. Pixel-space velocity (current - previous) -------------------------
    float2 velocity = currScreen - prevScreen;

    return velocity;
}
