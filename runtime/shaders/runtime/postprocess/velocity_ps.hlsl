/// UzEngine Velocity Buffer Generation — Depth-Based Reprojection
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — velocity from depth reprojection
/// @constants NONE (no numeric constants — pure geometric reprojection)
///
/// Reconstructs world-space position from the depth buffer and the current
/// frame's inverse view-projection matrix, then reprojects that position
/// through the previous frame's view-projection matrix to compute per-pixel
/// screen-space motion vectors.
///
/// Output is RG16F: velocity in pixels (not NDC). Positive X = rightward
/// motion, positive Y = downward motion (screen-space convention).
///
/// Runs fullscreen after the GBuffer pass, paired with fullscreen_vs.hlsl
/// (DrawInstanced(3,1,0,0), no vertex buffer).

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — depth SRV descriptor heap index.
cbuffer DepthIndex : register(b0) {
    uint depthSrvIndex;
};
#endif

/// Per-frame CB at b1 — provides invViewProj for depth->world reconstruction.
/// Layout mirrors C++ PerFrameCB exactly.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane, farPlane;
    uint     irradianceSrvIndex, prefilteredSrvIndex;
    uint     brdfLutSrvIndex, prefilteredMipLevels;
    float    skyLightIntensity, skyLightColorR, skyLightColorG, skyLightColorB;
    float    skyLightMinAmbient, exposure;
    float4   skyIrradianceSH[7];
    uint     shadowDebugMode;
};

/// Velocity pass CB at b2 — previous frame's view-projection + screen size.
/// Layout mirrors C++ VelocityCB.
cbuffer VelocityCB : register(b2) {
    row_major float4x4 prevViewProj;
    row_major float4x4 unjitteredInvViewProj;
    float    screenWidth;
    float    screenHeight;
    float2   _velPad;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float> depthTex : register(t0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "High Quality Temporal Supersampling" — depth reprojection velocity
///
/// 1. Load raw depth at current pixel (point-sampled via Load).
/// 2. Reconstruct clip-space position from UV + depth.
/// 3. Transform to world space via invViewProj (perspective divide).
/// 4. Reproject to previous clip space via prevViewProj (perspective divide).
/// 5. Convert both to screen pixels.
/// 6. Output delta as RG16F.
float2 main(FSInput input) : SV_Target0 {

    int2 pixelCoord = int2(input.positionCS.xy);

    // ── 1. Load raw depth ───────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> depthBuf = ResourceDescriptorHeap[depthSrvIndex];
    float rawDepth = depthBuf.Load(int3(pixelCoord, 0)).r;
#else
    float rawDepth = depthTex.Load(int3(pixelCoord, 0)).r;
#endif

    // Sky pixels (depth == 0 in reversed-Z) have no meaningful world position.
    // Output zero velocity to avoid artifacts.
    if (rawDepth == 0.0f)
        return float2(0.0f, 0.0f);

    // ── 2. Reconstruct clip-space position ──────────────────────────────────
    float2 uv  = input.texcoord;
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3D UV top-left vs NDC Y-up

    float4 clipPos = float4(ndc, rawDepth, 1.0f);

    // ── 3. World-space position via UNJITTERED inverse view-projection ──────
    // Must use unjittered invVP so the jitter offset is not interpreted as motion.
    float4 worldH = mul(clipPos, unjitteredInvViewProj);
    float3 worldPos = worldH.xyz / worldH.w;

    // ── 4. Reproject through previous frame's view-projection ───────────────
    float4 prevClip = mul(float4(worldPos, 1.0f), prevViewProj);
    float2 prevNdc  = prevClip.xy / prevClip.w;

    // ── 5. Convert NDC to screen-pixel coordinates ──────────────────────────
    // NDC [-1,1] -> screen pixels [0, width/height]
    // Y flipped back (NDC Y-up -> screen Y-down)
    float2 currScreen = float2(
        ( ndc.x * 0.5f + 0.5f) * screenWidth,
        (-ndc.y * 0.5f + 0.5f) * screenHeight
    );
    float2 prevScreen = float2(
        ( prevNdc.x * 0.5f + 0.5f) * screenWidth,
        (-prevNdc.y * 0.5f + 0.5f) * screenHeight
    );

    // ── 6. Pixel-space velocity (current - previous) ────────────────────────
    return currScreen - prevScreen;
}
