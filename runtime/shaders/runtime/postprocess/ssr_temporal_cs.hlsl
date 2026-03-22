/// SSR Temporal Accumulation Compute Shader
///
/// Blends the current frame's resolved SSR with the previous frame's history
/// using motion-vector reprojection and neighborhood clamping (variance
/// clipping) for ghost-free temporal stability.
///
/// Single-frame SSR is noisy due to interleaved gradient noise in the ray
/// offset and limited ray counts. Temporal accumulation integrates signal
/// across frames, producing clean reflections with ~0.05 new-frame weight
/// for stable pixels.
///
/// # Algorithm
/// 1. Load current resolved SSR (rgb=reflection color, a=blend weight).
/// 2. Load velocity (motion vector) from the full-res GBuffer.
/// 3. Reproject: historyUV = currentUV - velocity.
/// 4. Sample history at reprojected UV (bilinear).
/// 5. Neighborhood clamp: compute min/max color AABB from 3x3 neighborhood.
/// 6. Clamp history to AABB (variance clipping to reject ghosting).
/// 7. Blend: result = lerp(clampedHistory, current, alpha).
///    - alpha = 0.05 for stable pixels (strong temporal accumulation).
///    - alpha = 1.0 for disoccluded pixels (out of bounds or large velocity).
///
/// # Resources
/// - t0: Current resolved SSR (RGBA16F, full-res) — rgb=reflection, a=blend weight
/// - t1: History SSR (RGBA16F, full-res) — previous frame's accumulated SSR
/// - t2: Velocity buffer (RG16F, full-res GBuffer) — motion vectors in pixels
/// - u0: Output accumulated SSR (RGBA16F, full-res) — also the new history
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" — temporal accumulation
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — neighborhood clamping
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" — temporal accumulation for SSR
/// @constants BlendAlpha=0.05 (HistoryWeight ~0.95)
///            NeighborhoodClamp 3x3 AABB
///            DisocclusionAlpha=1.0
///            VelocityThreshold=0.01 (UV space)
/// @deviations Standard neighborhood-clamp TAA-style temporal filter applied to SSR output.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Temporal blend factor for stable (non-disoccluded) pixels.
/// Lower = more history reuse = smoother but more ghosting risk.
/// @ref Schied et al. 2017 — ~0.05 effective new-frame weight for stable pixels
#define TEMPORAL_BLEND_ALPHA 0.05f

/// Temporal blend factor for disoccluded pixels (no valid history).
#define DISOCCLUSION_ALPHA 1.0f

/// Velocity magnitude threshold to consider a pixel "fast moving."
/// Fast pixels use higher alpha (more current frame, less history).
/// In UV space (0-1 range), so 0.01 = 1% of screen per frame.
#define VELOCITY_THRESHOLD 0.01f

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// SSR temporal accumulation dispatch parameters.
cbuffer SsrTemporalRootCB : register(b0) {
    uint  currentSsrSrvIndex;    // DWORD 0 — current resolved SSR SRV
    uint  historySsrSrvIndex;    // DWORD 1 — previous frame history SSR SRV
    uint  velocitySrvIndex;      // DWORD 2 — velocity buffer SRV (full-res)
    uint  depthSrvIndex;         // DWORD 3 — depth buffer SRV (disocclusion test)
    uint  outputWidth;           // DWORD 4 — output texture width
    uint  outputHeight;          // DWORD 5 — output texture height
    uint  firstFrame;            // DWORD 6 — 1 on first frame (no valid history)
    uint  _reserved;             // DWORD 7 — padding
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _pad_pf[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: SRV textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float4> currentSsrTex : register(t0);
Texture2D<float4> historySsrTex : register(t1);
Texture2D<float2> velocityTex   : register(t2);
Texture2D<float>  depthTex      : register(t3);
#endif

RWTexture2D<float4> accumOutput : register(u0);

SamplerState pointSampler  : register(s0);
SamplerState linearSampler : register(s2);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

float4 SampleCurrentSsr(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[currentSsrSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return currentSsrTex.SampleLevel(pointSampler, uv, 0);
#endif
}

float4 LoadCurrentSsr(int2 coord) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[currentSsrSrvIndex];
    return tex.Load(int3(coord, 0));
#else
    return currentSsrTex.Load(int3(coord, 0));
#endif
}

float4 SampleHistorySsr(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[historySsrSrvIndex];
    return tex.SampleLevel(linearSampler, uv, 0);
#else
    return historySsrTex.SampleLevel(linearSampler, uv, 0);
#endif
}

float2 SampleVelocity(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float2> tex = ResourceDescriptorHeap[velocitySrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).rg;
#else
    return velocityTex.SampleLevel(pointSampler, uv, 0).rg;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Schied et al. 2017 — temporal accumulation pass with neighborhood clamping
/// @ref Stachowiak 2015 — temporal accumulation for stochastic SSR
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    // Bounds check for non-power-of-two output
    if (dispatchThreadId.x >= outputWidth || dispatchThreadId.y >= outputHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 invSize = 1.0f / float2((float)outputWidth, (float)outputHeight);
    float2 uv = pixelCenter * invSize;

    // ── Load current frame resolved SSR ──
    float4 currentSsr = SampleCurrentSsr(uv);

    // ── First frame: no valid history, output current directly ──
    if (firstFrame != 0) {
        accumOutput[dispatchThreadId.xy] = currentSsr;
        return;
    }

    // ── Load velocity for reprojection ──
    // Velocity buffer contains screen-space motion in pixels (currScreen - prevScreen).
    // Convert to UV space by dividing by output dimensions (SSR is full-res).
    float2 velocityPixels = SampleVelocity(uv);
    float2 velocity = velocityPixels * invSize;

    // ── Reproject to history UV ──
    float2 historyUV = uv - velocity;

    // ── Check for disocclusion (history UV out of bounds) ──
    bool isDisoccluded = any(historyUV < 0.0f) || any(historyUV > 1.0f);

    // ── Compute 3x3 neighborhood color AABB for variance clipping ──
    // This prevents ghosting by rejecting history samples that are outside
    // the range of plausible values given the current frame's local neighborhood.
    // @ref Salvi 2016 — neighborhood AABB for variance clipping
    float4 neighborMin = float4(1e10f, 1e10f, 1e10f, 1e10f);
    float4 neighborMax = float4(-1e10f, -1e10f, -1e10f, -1e10f);

    [unroll]
    for (int dy = -1; dy <= 1; dy++) {
        [unroll]
        for (int dx = -1; dx <= 1; dx++) {
            int2 neighborCoord = (int2)dispatchThreadId.xy + int2(dx, dy);

            // Clamp to screen bounds
            neighborCoord = clamp(neighborCoord, int2(0, 0),
                                  int2((int)outputWidth - 1, (int)outputHeight - 1));

            float4 neighborColor = LoadCurrentSsr(neighborCoord);
            neighborMin = min(neighborMin, neighborColor);
            neighborMax = max(neighborMax, neighborColor);
        }
    }

    // ── Sample history ──
    float4 historySsr;
    if (isDisoccluded) {
        // No valid history — use current frame directly
        historySsr = currentSsr;
    } else {
        historySsr = SampleHistorySsr(historyUV);

        // ── Clamp history to neighborhood AABB (variance clipping) ──
        // Prevents ghosting artifacts from stale history data.
        historySsr = clamp(historySsr, neighborMin, neighborMax);
    }

    // ── Compute blend alpha ──
    float alpha = TEMPORAL_BLEND_ALPHA;

    // Increase alpha for disoccluded pixels (use more current frame)
    if (isDisoccluded) {
        alpha = DISOCCLUSION_ALPHA;
    }

    // Increase alpha for fast-moving pixels to reduce ghosting
    float velocityMagnitude = length(velocity);
    if (velocityMagnitude > VELOCITY_THRESHOLD) {
        // Smoothly ramp alpha up as velocity increases
        float velocityFactor = saturate(velocityMagnitude / (VELOCITY_THRESHOLD * 10.0f));
        alpha = lerp(alpha, 1.0f, velocityFactor);
    }

    // ── Temporal blend ──
    // result = lerp(history, current, alpha) = history * (1-alpha) + current * alpha
    float4 result = lerp(historySsr, currentSsr, alpha);

    // Write accumulated output (this becomes history for next frame)
    accumOutput[dispatchThreadId.xy] = result;
}
