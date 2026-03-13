/// G-Buffer debug visualization pixel shader.
///
/// Reads the four G-Buffer textures (albedo, normals, metallic/roughness/AO,
/// depth) and outputs a single diagnostic channel selected at runtime via a
/// debug mode value.  The pass is intended for editor/debug overlays and is
/// never active in shipping builds.
///
/// SM6.6 (UZ_BINDLESS): fetches G-Buffer textures via ResourceDescriptorHeap[index]
/// using SRV descriptor indices from root constants at b0. Debug mode is passed
/// via DebugCB at b2.
/// SM5 (!UZ_BINDLESS): uses register-bound Texture2D at t0-t3 and debug mode
/// from root constants at b0.
///
/// # G-Buffer Inputs (same layout as deferred_lighting_ps.hlsl)
/// - Albedo       (R8G8B8A8_UNORM)    — rgb = base color (linear)
/// - Normal       (R16G16B16A16_FLOAT) — rgb = world-space normal in [-1,1]
/// - MetalRough   (R8G8B8A8_UNORM)    — r = metallic, g = roughness, b = ao
/// - Depth        (R32_FLOAT SRV)      — hardware depth sampled as float
///
/// # Constant Buffers (UZ_BINDLESS)
/// - b0: Root constants — 4 x uint32 SRV descriptor indices (albedo, normal, metalRough, depth)
/// - b1: PerFrameCB     — invViewProj for world-position reconstruction (mode 6)
/// - b2: DebugCB        — debugMode uint selecting the visualization mode
///
/// # Constant Buffers (!UZ_BINDLESS / SM5)
/// - b0: DebugConstants — single uint selecting the visualization mode
/// - b1: PerFrameCB     — invViewProj for world-position reconstruction (mode 6)
///
/// # Sampler
/// - s0: point/nearest — no interpolation of discrete G-Buffer data.
///
/// # Debug Modes
/// | Value | Name      | Output                                          |
/// |-------|-----------|-------------------------------------------------|
/// |   0   | Albedo    | rgb = base color (linear)                       |
/// |   1   | Normal    | rgb = N * 0.5 + 0.5  ([-1,1] -> [0,1])         |
/// |   2   | Metallic  | rgb = metallic.xxx (grayscale)                  |
/// |   3   | Roughness | rgb = roughness.xxx (grayscale)                 |
/// |   4   | AO        | rgb = ao.xxx (grayscale)                        |
/// |   5   | Depth     | rgb = linearized depth (white=near, black=far)  |
/// |   6   | WorldPos  | rgb = frac(worldPos) for position heat-map      |

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from fullscreen_vs.hlsl.
struct FSInput {
    float4 positionCS : SV_Position; ///< Clip-space position (unused in PS body).
    float2 texcoord   : TEXCOORD0;   ///< Screen UV in [0,1] (top-left origin).
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV descriptor heap indices for G-Buffer textures.
///
/// Same layout as deferred_lighting_ps.hlsl so both passes share the same
/// root constant binding at slot 0.
struct GBufferIndices {
    uint albedoIdx;     ///< Descriptor index for albedo G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal G-Buffer (R16G16B16A16_FLOAT).
    uint metalRoughIdx; ///< Descriptor index for metallic/roughness G-Buffer (R8G8B8A8_UNORM).
    uint depthIdx;      ///< Descriptor index for depth buffer SRV (R32_FLOAT).
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);

/// Debug state CB at b2 — contains the visualization mode selector.
///
/// In bindless mode, root param 0 (b0) is fully occupied by the 4 SRV indices,
/// so debugMode is passed via a root CBV at b2 (reusing the per-object slot
/// which is unused by fullscreen passes).
cbuffer DebugCB : register(b2) {
    uint  debugMode; ///< Visualization mode — see table in file header.
    uint3 _debugPad; ///< Padding to 16-byte minimum constant buffer size.
};
#else
/// Debug mode selector constant buffer at b0.
///
/// A single uint selects which G-Buffer channel to visualize.
/// The three padding uints keep the struct at 16 bytes (minimum CB size).
cbuffer DebugConstants : register(b0) {
    uint  debugMode; ///< Visualization mode — see table in file header.
    uint3 _pad;      ///< Padding to 16-byte minimum constant buffer size.
};
#endif

/// Per-frame CB at b1 — mirrors the C++ PerFrameCB layout.
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

// ─────────────────────────────────────────────────────────────────────────────
// G-Buffer Textures & Sampler
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// SM5 path: G-Buffer textures bound via SRV descriptor table at t0-t3.
Texture2D<float4> gbufferAlbedo     : register(t0); ///< rgb = linear base color, a = unused.
Texture2D<float4> gbufferNormal     : register(t1); ///< rgb = world-space normal in [-1,1].
Texture2D<float4> gbufferMetalRough : register(t2); ///< r = metallic, g = roughness, b = ao.
Texture2D<float>  gbufferDepth      : register(t3); ///< Hardware depth as R32_FLOAT SRV.
#endif

/// Point sampler — G-Buffer texels map 1:1 to pixels; filtering would smear edges.
SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Debug Mode Enumerants
// ─────────────────────────────────────────────────────────────────────────────

/// Symbolic constants for debugMode values — mirrors GBufferDebugMode in C++.
static const uint kModeAlbedo    = 0;
static const uint kModeNormal    = 1;
static const uint kModeMetallic  = 2;
static const uint kModeRoughness = 3;
static const uint kModeAO        = 4;
static const uint kModeDepth     = 5;
static const uint kModeWorldPos  = 6;

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a non-linear hardware depth value to a linear view-space depth.
///
/// Uses the standard perspective depth linearization formula derived from the
/// projection matrix near/far parameters.  The result is in [0, 1]:
///   0.0 = at the near plane (white in visualization)
///   1.0 = at the far plane  (black in visualization)
///
/// # Parameters
/// - depth: raw depth value from the depth buffer in [0, 1].
///
/// # Returns
/// Linear depth normalized to [0, 1] within [kNearPlane, kFarPlane].
float LinearizeDepth(float depth) {
    // Perspective depth is stored as: z_ndc = (far*(z-near)) / (z*(far-near))
    // Solving for linear z:  z_linear = (near * far) / (far - depth*(far-near))
    float linearZ = (kNearPlane * kFarPlane) /
                    (kFarPlane - depth * (kFarPlane - kNearPlane));
    // Normalize to [0,1] so 0=near, 1=far
    return saturate((linearZ - kNearPlane) / (kFarPlane - kNearPlane));
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

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Outputs a single G-Buffer channel selected by the debug mode constant.
///
/// Reads the four G-Buffer textures and visualizes one channel as an RGB
/// image suitable for display on the R8G8B8A8_UNORM backbuffer.  The mode
/// value must be in [0, 6]; out-of-range values output magenta (1, 0, 1)
/// as an error indicator.
///
/// # Returns
/// float4 RGBA color with alpha forced to 1.0.
float4 main(FSInput input) : SV_Target {

    float2 uv = input.texcoord;

    // ── Read G-Buffer texels ──────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    // SM6.6 bindless: fetch G-Buffer textures from the descriptor heap
    Texture2D<float4> gbAlbedo     = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal     = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float4> gbMetalRough = ResourceDescriptorHeap[gBufIndices.metalRoughIdx];
    Texture2D<float>  gbDepth      = ResourceDescriptorHeap[gBufIndices.depthIdx];

    float3 albedo     = gbAlbedo.Sample(pointSampler, uv).rgb;
    float3 normal     = gbNormal.Sample(pointSampler, uv).rgb;
    float4 metalRough = gbMetalRough.Sample(pointSampler, uv);
    float  depth      = gbDepth.Sample(pointSampler, uv).r;
#else
    // SM5 path: register-bound textures at t0-t3
    float3 albedo     = gbufferAlbedo.Sample(pointSampler, uv).rgb;
    float3 normal     = gbufferNormal.Sample(pointSampler, uv).rgb;
    float4 metalRough = gbufferMetalRough.Sample(pointSampler, uv);
    float  depth      = gbufferDepth.Sample(pointSampler, uv).r;
#endif

    float metallic  = metalRough.r;
    float roughness = metalRough.g;
    float ao        = metalRough.b;

    // ── Select visualization output ──────────────────────────────────────────
    float3 outColor;

    if (debugMode == kModeAlbedo) {
        // Linear base color — displayed as-is (no gamma correction so the
        // debug view shows the raw stored value).
        outColor = albedo;

    } else if (debugMode == kModeNormal) {
        // Remap world-space normal from [-1,1] to [0,1] for display.
        // The stored normal is not guaranteed to be unit length after G-Buffer
        // encode/decode; normalize before remapping to avoid scaling artifacts.
        float3 N = normalize(normal);
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

    } else {
        // Out-of-range mode — magenta error indicator
        outColor = float3(1.0f, 0.0f, 1.0f);
    }

    return float4(outColor, 1.0f);
}
