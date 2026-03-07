// velocity.ps.hlsl -- Per-pixel motion vector generation from depth reprojection
//
// Full-screen pass: reconstructs world position from depth, reprojects to
// previous frame, outputs screen-space velocity in R16G16_FLOAT.
//
// Used by TAA, GTAO temporal accumulation, and SSR temporal stability.
//
// References:
//   [Karis 2014] "High Quality Temporal Supersampling" (UE4 TAA)
//   UE5 — Engine/Shaders/Private/VelocityShader.usf

cbuffer InvViewProjCB : register(b0) {
    float4x4 invViewProj;       // current frame inverse view-projection
};

cbuffer PrevViewProjCB : register(b3) {
    float4x4 prevViewProj;      // previous frame view-projection
};

Texture2D<float> depthTex    : register(t0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float2 PSMain(PSInput input) : SV_TARGET {
    float depth = depthTex.Load(int3(int2(input.position.xy), 0)).r;

    // Sky pixels (depth ~1.0) get zero velocity — no geometry to track
    if (depth >= 0.9999)
        return float2(0.0, 0.0);

    // Reconstruct NDC position (DX convention: Y up in NDC, UV.y=0 is top)
    float2 ndc = input.texcoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0);

    // Current frame: clip → world
    float4 worldPos = mul(invViewProj, clipPos);
    worldPos /= worldPos.w;

    // Reproject to previous frame: world → prev clip → prev UV
    float4 prevClip = mul(prevViewProj, worldPos);
    float2 prevNDC = prevClip.xy / prevClip.w;
    float2 prevUV = prevNDC * float2(0.5, -0.5) + 0.5;

    // Motion vector = current UV - previous UV
    // Positive = object moved right/down since last frame
    return input.texcoord - prevUV;
}
