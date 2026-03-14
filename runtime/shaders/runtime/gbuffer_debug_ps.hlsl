/// G-Buffer debug visualization pixel shader.
///
/// Reads the G-Buffer textures (albedo+AO, octahedral normal+metallic+roughness,
/// depth) and outputs a single diagnostic channel selected at runtime via a
/// debug mode value.  The pass is intended for editor/debug overlays and is
/// never active in shipping builds.
///
/// SM6.6 (UZ_BINDLESS): fetches G-Buffer textures via ResourceDescriptorHeap[index]
/// using SRV descriptor indices from root constants at b0. Debug mode is passed
/// via DebugCB at b2.
/// SM5 (!UZ_BINDLESS): uses register-bound Texture2D at t0-t2 and debug mode
/// from root constants at b0.
///
/// # G-Buffer Inputs (2-RT packed layout, same as deferred_lighting_ps.hlsl)
/// - AlbedoAO     (R8G8B8A8_UNORM) -- rgb = base color (linear), a = AO
/// - NormalMatPbr  (R8G8B8A8_UNORM) -- rg = octahedral normal, b = metallic, a = roughness
/// - Depth         (R32_FLOAT SRV)  -- hardware depth sampled as float
///
/// # Constant Buffers (UZ_BINDLESS)
/// - b0: Root constants -- 4 x uint32 SRV descriptor indices (albedo, normal, depth, velocity)
/// - b1: PerFrameCB     -- invViewProj for world-position reconstruction (mode 6)
/// - b2: DebugCB        -- debugMode uint selecting the visualization mode
///
/// # Constant Buffers (!UZ_BINDLESS / SM5)
/// - b0: DebugConstants -- single uint selecting the visualization mode
/// - b1: PerFrameCB     -- invViewProj for world-position reconstruction (mode 6)
///
/// # Sampler
/// - s0: point/nearest -- no interpolation of discrete G-Buffer data.
///
/// # Debug Modes
/// | Value | Name      | Output                                          |
/// |-------|-----------|-------------------------------------------------|
/// |   0   | Albedo    | rgb = base color (linear)                       |
/// |   1   | Normal    | rgb = N * 0.5 + 0.5  (decoded octahedral)       |
/// |   2   | Metallic  | rgb = metallic.xxx (grayscale)                  |
/// |   3   | Roughness | rgb = roughness.xxx (grayscale)                 |
/// |   4   | AO        | rgb = ao.xxx (grayscale)                        |
/// |   5   | Depth     | rgb = linearized depth (white=near, black=far)  |
/// |   6   | WorldPos  | rgb = frac(worldPos) for position heat-map      |
/// |   7   | Velocity  | rg = velocity mapped to color, b = 0            |

#include "include/octahedral.hlsli"

// ---------------------------------------------------------------------------
// Input Structure
// ---------------------------------------------------------------------------

/// Interpolants from fullscreen_vs.hlsl.
struct FSInput {
    float4 positionCS : SV_Position; ///< Clip-space position (unused in PS body).
    float2 texcoord   : TEXCOORD0;   ///< Screen UV in [0,1] (top-left origin).
};

// ---------------------------------------------------------------------------
// Constant Buffers
// ---------------------------------------------------------------------------

#ifdef UZ_BINDLESS
/// Root constants at b0 -- SRV descriptor heap indices for G-Buffer textures.
///
/// Same layout as deferred_lighting_ps.hlsl so both passes share the same
/// root constant binding at slot 0. Only 3 indices with the packed 2-RT layout.
struct GBufferIndices {
    uint albedoIdx;     ///< Descriptor index for albedo+AO G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal+metallic+roughness G-Buffer (R8G8B8A8_UNORM).
    uint depthIdx;      ///< Descriptor index for depth buffer SRV (R32_FLOAT).
    uint velocityIdx;   ///< Descriptor index for velocity buffer SRV (R16G16_FLOAT).
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);

/// Debug state CB at b2 -- contains the visualization mode selector.
///
/// In bindless mode, root param 0 (b0) is fully occupied by the 3 SRV indices,
/// so debugMode is passed via a root CBV at b2 (reusing the per-object slot
/// which is unused by fullscreen passes).
cbuffer DebugCB : register(b2) {
    uint  debugMode; ///< Visualization mode -- see table in file header.
    uint3 _debugPad; ///< Padding to 16-byte minimum constant buffer size.
};
#else
/// Debug mode selector constant buffer at b0.
///
/// A single uint selects which G-Buffer channel to visualize.
/// The three padding uints keep the struct at 16 bytes (minimum CB size).
cbuffer DebugConstants : register(b0) {
    uint  debugMode; ///< Visualization mode -- see table in file header.
    uint3 _pad;      ///< Padding to 16-byte minimum constant buffer size.
};
#endif

/// Per-frame CB at b1 -- mirrors the C++ PerFrameCB layout.
///
/// Only `invViewProj` and `cameraPos` are consumed by this shader (mode 6).
/// The remaining fields are declared to match the C++ struct exactly so that
/// no special partial-binding path is needed.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;      ///< xyz = world-space camera position, w = unused.
    float4   timeParams;     ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    float4   lightDir;       ///< xyz = direction toward the primary light, w = unused.
    float4   lightColor;     ///< xyz = color * intensity, w = unused.
    float4   ambientColor;   ///< xyz = ambient radiance, w = unused.
    row_major float4x4 invViewProj; ///< Inverse view-projection for world-position reconstruction.
    float    nearPlane;          ///< Near clip plane distance in world units.
    float    farPlane;           ///< Far clip plane distance in world units.
    float    _framePad[10];      ///< Remaining padding to 256-byte alignment.
};

// ---------------------------------------------------------------------------
// G-Buffer Textures & Sampler
// ---------------------------------------------------------------------------

#ifndef UZ_BINDLESS
/// SM5 path: G-Buffer textures bound via SRV descriptor table at t0-t3.
Texture2D<float4> gbufferAlbedo   : register(t0); ///< rgb = linear base color, a = AO.
Texture2D<float4> gbufferNormal   : register(t1); ///< rg = octahedral normal, b = metallic, a = roughness.
Texture2D<float>  gbufferDepth    : register(t2); ///< Hardware depth as R32_FLOAT SRV.
Texture2D<float2> gbufferVelocity : register(t3); ///< Screen-space velocity in pixels (RG16F).
#endif

/// Point sampler -- G-Buffer texels map 1:1 to pixels; filtering would smear edges.
SamplerState pointSampler : register(s0);

// ---------------------------------------------------------------------------
// Debug Mode Enumerants
// ---------------------------------------------------------------------------

/// Symbolic constants for debugMode values -- mirrors GBufferDebugMode in C++.
static const uint kModeAlbedo    = 0;
static const uint kModeNormal    = 1;
static const uint kModeMetallic  = 2;
static const uint kModeRoughness = 3;
static const uint kModeAO        = 4;
static const uint kModeDepth     = 5;
static const uint kModeWorldPos  = 6;
static const uint kModeVelocity  = 7;

// ---------------------------------------------------------------------------
// Utility Functions
// ---------------------------------------------------------------------------

/// Converts a non-linear hardware depth value to a linear view-space depth.
///
/// Uses the standard perspective depth linearization formula derived from the
/// projection matrix near/far parameters (read from PerFrameCB).
/// The result is in [0, 1]:
///   0.0 = at the near plane (white in visualization)
///   1.0 = at the far plane  (black in visualization)
///
/// # Parameters
/// - depth: raw depth value from the depth buffer in [0, 1].
///
/// # Returns
/// Linear depth normalized to [0, 1] within [nearPlane, farPlane].
float LinearizeDepth(float depth) {
    // Perspective depth is stored as: z_ndc = (far*(z-near)) / (z*(far-near))
    // Solving for linear z:  z_linear = (near * far) / (far - depth*(far-near))
    float linearZ = (nearPlane * farPlane) /
                    (farPlane - depth * (farPlane - nearPlane));
    // Normalize to [0,1] so 0=near, 1=far
    return saturate((linearZ - nearPlane) / (farPlane - nearPlane));
}

/// Reconstructs world-space position from screen UV and hardware depth.
///
/// Applies the inverse view-projection transform and performs the perspective
/// divide.  Matches ReconstructWorldPosition in deferred_lighting_ps.hlsl.
///
/// # Parameters
/// - uv:    screen UV in [0, 1] (top-left origin).
/// - depth: raw hardware depth from the depth buffer in [0, 1].
///
/// # Returns
/// World-space position as float3.
float3 ReconstructWorldPosition(float2 uv, float depth) {
    // Map UV to NDC: X in [-1,1], Y flipped (D3D UV origin is top-left, NDC Y points up)
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clipPos  = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);

    // Perspective divide to recover Cartesian world position
    return worldPos.xyz / worldPos.w;
}

// ---------------------------------------------------------------------------
// Pixel Shader Entry Point
// ---------------------------------------------------------------------------

/// Outputs a single G-Buffer channel selected by the debug mode constant.
///
/// Reads the G-Buffer textures and visualizes one channel as an RGB
/// image suitable for display on the R8G8B8A8_UNORM backbuffer.  The mode
/// value must be in [0, 6]; out-of-range values output magenta (1, 0, 1)
/// as an error indicator.
///
/// # Returns
/// float4 RGBA color with alpha forced to 1.0.
float4 main(FSInput input) : SV_Target {

    float2 uv = input.texcoord;

    // -- Read G-Buffer texels (2-RT packed layout) ----------------------------
#ifdef UZ_BINDLESS
    // SM6.6 bindless: fetch G-Buffer textures from the descriptor heap
    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[gBufIndices.depthIdx];

    float4 albedoAO     = gbAlbedo.Sample(pointSampler, uv);
    float4 normalPacked = gbNormal.Sample(pointSampler, uv);
    float  depth        = gbDepth.Sample(pointSampler, uv).r;
#else
    // SM5 path: register-bound textures at t0-t2
    float4 albedoAO     = gbufferAlbedo.Sample(pointSampler, uv);
    float4 normalPacked = gbufferNormal.Sample(pointSampler, uv);
    float  depth        = gbufferDepth.Sample(pointSampler, uv).r;
#endif

    float3 albedo    = albedoAO.rgb;
    float  ao        = albedoAO.a;
    float  metallic  = normalPacked.b;
    float  roughness = normalPacked.a;

    // -- Select visualization output ------------------------------------------
    float3 outColor;

    if (debugMode == kModeAlbedo) {
        // Linear base color -- displayed as-is (no gamma correction so the
        // debug view shows the raw stored value).
        outColor = albedo;

    } else if (debugMode == kModeNormal) {
        // Decode octahedral normal and remap from [-1,1] to [0,1] for display.
        float3 N = OctDecode(normalPacked.rg);
        outColor = N * 0.5f + 0.5f;

    } else if (debugMode == kModeMetallic) {
        // Grayscale metallic factor
        outColor = metallic.xxx;

    } else if (debugMode == kModeRoughness) {
        // Grayscale roughness factor
        outColor = roughness.xxx;

    } else if (debugMode == kModeAO) {
        // Grayscale ambient occlusion factor (1 = fully lit, 0 = fully occluded)
        outColor = ao.xxx;

    } else if (debugMode == kModeDepth) {
        // Linearize hardware depth and invert: white=near, black=far.
        // Raw hardware depth is non-linear; directly displaying it makes the
        // scene appear nearly white because depth is highly non-linear near
        // the camera and compresses the far range.
        float linearDepth = LinearizeDepth(depth);
        outColor = (1.0f - linearDepth).xxx;

    } else if (debugMode == kModeWorldPos) {
        // Fractional world position creates a repeating color grid that makes
        // the spatial layout of the scene immediately readable at a glance.
        float3 worldPos = ReconstructWorldPosition(uv, depth);
        outColor = frac(worldPos);

    } else if (debugMode == kModeVelocity) {
        // Velocity visualization: remap pixel-space velocity to [0,1] color.
        // Zero motion = neutral gray (0.5, 0.5, 0).  Positive X (rightward)
        // shifts red brighter; positive Y (downward) shifts green brighter.
        // The 0.1 scale factor keeps typical velocities (<5px) visible without
        // saturating; artists can adjust if needed.
        int2 pixelCoord = int2(input.positionCS.xy);
#ifdef UZ_BINDLESS
        Texture2D<float2> velTex = ResourceDescriptorHeap[gBufIndices.velocityIdx];
        float2 vel = velTex.Load(int3(pixelCoord, 0));
#else
        float2 vel = gbufferVelocity.Load(int3(pixelCoord, 0));
#endif
        outColor = float3(vel.x * 0.1f + 0.5f, vel.y * 0.1f + 0.5f, 0.0f);

    } else {
        // Out-of-range mode -- magenta error indicator
        outColor = float3(1.0f, 0.0f, 1.0f);
    }

    return float4(outColor, 1.0f);
}
