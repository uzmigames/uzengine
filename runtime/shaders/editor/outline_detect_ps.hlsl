/// Outline detection pixel shader — reads mask texture, detects edges, outputs colored outline.
///
/// Samples a 3x3 kernel around each pixel in the binary mask texture.
/// If the center pixel is outside the mask (0) but any neighbor is inside (1),
/// we are on the silhouette edge and output the outline color.
/// This produces a clean outline of configurable thickness.
///
/// SM6.6 (UZ_BINDLESS): mask fetched via ResourceDescriptorHeap[maskSrvIndex].
/// SM5 (!UZ_BINDLESS): register-bound Texture2D<float> at t0.

// ─────────────────────────────────────────────────────────────────────────────
// Root constants at b0 — maskSrvIndex for bindless mask texture lookup
// ─────────────────────────────────────────────────────────────────────────────

cbuffer OutlineRootCB : register(b0) {
    uint maskSrvIndex;
    uint _rootPad0;
    uint _rootPad1;
    uint _rootPad2;
    uint _rootPad3;
    uint _rootPad4;
    uint _rootPad5;
    uint _rootPad6;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant buffer — matches OutlineCB in outline_pass.cpp
// ─────────────────────────────────────────────────────────────────────────────

cbuffer OutlineCB : register(b1) {
    float4 outlineColor;    // RGB = color, A = thickness in pixels
    float2 texelSize;       // 1.0/width, 1.0/height
    float2 _pad;
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources — SM5 fallback
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float> maskTex : register(t0);
#endif

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input from fullscreen_vs.hlsl
// ─────────────────────────────────────────────────────────────────────────────

struct PSInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: sample mask texture (SM5 or SM6.6 bindless)
// ─────────────────────────────────────────────────────────────────────────────

float SampleMask(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[maskSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return maskTex.SampleLevel(pointSampler, uv, 0);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(PSInput input) : SV_Target {
    float center = SampleMask(input.uv);

    // Interior pixels are fully inside the mesh — skip them.
    if (center > 0.5)
        discard;

    // Sample 8 neighbors at offset scaled by outline thickness.
    float thickness = outlineColor.a;

    float maxNeighbor = 0.0;
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2(-1.0, -1.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2( 0.0, -1.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2( 1.0, -1.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2(-1.0,  0.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2( 1.0,  0.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2(-1.0,  1.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2( 0.0,  1.0) * texelSize * thickness));
    maxNeighbor = max(maxNeighbor, SampleMask(input.uv + float2( 1.0,  1.0) * texelSize * thickness));

    // If any neighbor has mask=1, we are on the edge — draw outline.
    if (maxNeighbor > 0.5) {
        return float4(outlineColor.rgb, 1.0);
    }

    discard;
    return float4(0.0, 0.0, 0.0, 0.0);
}
