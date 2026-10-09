/// VFX Weighted Blended OIT resolve — fullscreen pixel shader (phase 8.16.0).
///
/// Inputs:
///   t0: accumTex  (RGBA16F) — RGB = Σ color·α·weight,  A = Σ α·weight
///   t1: revealTex (R8)      — revealage = Π (1 − α)
///
/// Output: composite over the current framebuffer via the blend equation
///   finalColor = srcColor * (1 − srcAlpha) + dstColor * srcAlpha
/// where srcColor = accumRGB / max(accumAlphaSum, 1e-4) and
///       srcAlpha = revealage.
///
/// Mirrors the CPU-side `VfxOITResolve` helper so unit tests validate the
/// composite math.
///
/// @ref McGuire & Bavoil 2013, "Weighted Blended OIT"
/// @ref docs/engine/25-vfx/33-weighted-oit.md

#ifdef UZ_BINDLESS
uint accumSrv  : register(b0);
uint revealSrv : register(b1);
Texture2D<float4> GetAccumTex()  { return ResourceDescriptorHeap[accumSrv]; }
Texture2D<float>  GetRevealTex() { return ResourceDescriptorHeap[revealSrv]; }
#else
Texture2D<float4> accumTex  : register(t0);
Texture2D<float>  revealTex : register(t1);
Texture2D<float4> GetAccumTex()  { return accumTex; }
Texture2D<float>  GetRevealTex() { return revealTex; }
#endif

SamplerState pointClampSampler : register(s0);

struct PSInput {
    float4 positionCS : SV_Position;
    float2 uv         : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    const float4 accum    = GetAccumTex().SampleLevel(pointClampSampler, input.uv, 0);
    const float  reveal   = GetRevealTex().SampleLevel(pointClampSampler, input.uv, 0);

    // Guard against accum.a == 0 (no fragments landed here) so empty
    // pixels stay transparent instead of producing NaN.
    const float accumSum = max(accum.a, 1e-4f);
    const float3 average = accum.rgb / accumSum;

    // Output srcColor = average, srcAlpha = revealage. The blend state is
    // configured so the framebuffer composites:
    //   framebuffer = framebuffer * revealage + average * (1 − revealage)
    // which is equivalent to `lerp(background, average, 1 − revealage)`.
    return float4(average, reveal);
}
