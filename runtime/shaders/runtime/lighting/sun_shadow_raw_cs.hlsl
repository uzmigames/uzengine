/// Directional PCSS visibility for the sun-shadow denoiser (r.Shadows.SoftPCSS.Denoise), stage 1 of 2.
///
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — the estimate (shadow.hlsli CascadeShadowSoft)
/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty: Advanced Warfare" — IGN kernel rotation
///
/// The deferred lighting evaluates the sun's PCSS visibility per pixel with one 16-tap kernel rotation. That
/// rotation is a fixed function of the pixel (or, with r.Shadows.SoftPCSS.StableKernel, of the surface's shadow
/// texel cell), so the 16-tap estimate keeps a grain of about one tap (1/16 of the light) across the penumbra.
/// A still camera keeps it in place; under motion the grain moves over the surface (pixel seed) or becomes a
/// high-contrast surface texture that TAA has to reconstruct (cell seed) - the residual tremble of the
/// penumbra edge.
///
/// With the denoiser the same estimate runs here, before the lighting, with one change: the rotation advances by
/// the golden ratio every frame (shadow.hlsli UZ_PCSS_DENOISE_INPUT), so consecutive frames sample the disc at
/// well-spread rotations and sun_shadow_denoise_cs.hlsl, which accumulates the estimate where each surface point
/// was on the previous frame, converges to the kernel's integral instead of one rotation's grain. A pixel whose
/// accumulation restarted within the last frames (its previous geometry history holds a count below
/// kRotateMinCount: a disocclusion, or a geometric edge that TAA's jitter moves between two surfaces every frame, so
/// no history ever matches) keeps the lighting's own rotation: nothing would average the advancing one there, and it
/// would only add a per-frame flicker the stock estimate does not have. The receiver,
/// its normal and its view distance are those of deferred_lighting_ps.hlsl (the G-buffer position and shading
/// normal), so the lighting reads the denoised value of exactly the receiver it would have estimated.
///
/// Output (RG16F): x the visibility, y the PCSS filter radius on the light's image plane in world units (cm; the
/// penumbra width that guides the denoiser, 0 where no blocker was found). y < 0 marks a pixel that is not a sun
/// receiver (sky, or facing away from the sun: the lighting gives it 0 without reading the visibility) and that no
/// neighbour may use.
///
/// Root constants at b0 (8): see RootConstants. PerFrameCB at b1 (the camera position and the sun direction),
/// ShadowDataCB at b4 (the cascades, through shadow.hlsli).

#ifndef UZ_BINDLESS
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {}
#else

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"

// ─── Root Constants (b0) ──────────────────────────────────────────────────
cbuffer RootConstants : register(b0) {
    uint  g_normalIdx;
    uint  g_depthIdx;
    uint  g_positionIdx;
    uint  g_rawUavIdx;       ///< This frame's visibility and filter radius (RG16F UAV)
    uint  g_width;
    uint  g_height;
    float g_rotationOffset;  ///< frac(frame x golden ratio): the per-frame kernel rotation, in turns
    uint  g_prevGeometrySrvIdx; ///< The denoiser's previous geometry history (RGBA16F, w = count; UINT32_MAX = none)
};

/// Accumulated samples below which a pixel keeps the lighting's rotation (see above).
static const float kRotateMinCount = 4.0;

// ─── PerFrameCB (b1) ──────────────────────────────────────────────────────
// The prefix of the engine's PerFrameCB (constant_buffers.h) this pass reads; it binds the frame's allocation.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;   // c0-c3   (0-63)
    float4 cameraPos;              // c4      (64-79)
    float4 timeParams;             // c5      (80-95)
    float4 lightDir;               // c6      (96-111)
};

/// shadow.hlsli's fixed-radius PCF (SampleShadowCascade, the r.Shadows.SoftPCSS 0 path) reads this from the
/// lighting's PerFrameCB; this pass never calls it and needs only the declaration.
static const float shadowPCFPenumbra = 2.0;

// The soft sampler with the per-frame rotation and the filter-radius output (shadow.hlsli).
#define UZ_PCSS_DENOISE_INPUT 1
#include "shadow.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_width || DTid.y >= g_height) return;
    int2 px = int2(DTid.xy);

    RWTexture2D<float2> rawOut = ResourceDescriptorHeap[g_rawUavIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[g_depthIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[g_normalIdx];
    Texture2D<float4> gbPos    = ResourceDescriptorHeap[g_positionIdx];

    // Sky: reversed-Z far = 0, the lighting's test (deferred_lighting_ps.hlsl).
    float z = gbDepth.Load(int3(px, 0)).r;
    if (z <= 0.0001) {
        rawOut[px] = float2(1.0, -1.0);
        return;
    }

    float3 N        = OctDecode(gbNormal.Load(int3(px, 0)).rg);
    float3 worldPos = gbPos.Load(int3(px, 0)).xyz;
    float  viewDepth = length(cameraPos.xyz - worldPos);
    float3 sunL     = normalize(lightDir.xyz);
    float  sunNoL   = max(dot(N, sunL), 0.0);

    // Facing away from the sun: the lighting uses 0 without a shadow lookup; not a receiver.
    if (sunNoL <= 0.0) {
        rawOut[px] = float2(0.0, -1.0);
        return;
    }

    float rotationOffset = g_rotationOffset;
    if (g_prevGeometrySrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> prevGeometry = ResourceDescriptorHeap[g_prevGeometrySrvIdx];
        if (prevGeometry.Load(int3(px, 0)).w < kRotateMinCount) rotationOffset = 0.0;
    }
    float penumbraWorld = 0.0;
    float visibility = CascadeShadowSoft(worldPos, viewDepth, N, sunNoL, float2(px), rotationOffset, penumbraWorld);
    rawOut[px] = float2(visibility, penumbraWorld);
}

#endif // UZ_BINDLESS
