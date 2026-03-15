/// Debug View pixel shader -- G-Buffer channel visualization for deferred rendering.
///
/// Reads the packed 2-RT G-Buffer (albedo+AO, octahedral normal+metallic+roughness)
/// plus the depth buffer and optional world-space position RT, then outputs a
/// false-colour visualization of the selected debug channel.
///
/// Supports 13 modes (matching `DebugViewMode` enum in debug_view_mode.h):
///   0 Lit           -- pass-through (outputs black; debug pass should be skipped)
///   1 Albedo        -- base colour from RT0.rgb
///   2 WorldNormal   -- octahedral-decoded normal remapped to [0,1]
///   3 Metallic      -- RT1.b as grayscale
///   4 Roughness     -- RT1.a as grayscale
///   5 AO            -- RT0.a as grayscale
///   6 Depth         -- linearized depth (white=near, black=far)
///   7 WorldPosition -- fractional world-space position for spatial debugging
///   8 Lighting      -- placeholder mid-grey (lighting buffer not available)
///   9 Shadows       -- placeholder mid-grey (shadow buffer not stored separately)
///  10 Wireframe     -- placeholder mid-grey (handled by rasterizer state)
///  11 Unlit         -- alias for Albedo
///  12 Overview      -- 4x2 composite grid of all channels
///
/// SM6.6 (UZ_BINDLESS): G-Buffer textures fetched via ResourceDescriptorHeap[index]
/// using descriptor indices in root constants at b0.
/// SM5 (!UZ_BINDLESS): register-bound Texture2D at t0-t3.
///
/// No tonemapping or gamma correction is applied -- all channels output raw linear
/// values for faithful diagnostic inspection.

#include "../include/octahedral.hlsli"

// -------------------------------------------------------------------------
// Input Structure
// -------------------------------------------------------------------------

/// Interpolants from the fullscreen triangle vertex shader (fullscreen_vs.hlsl).
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// -------------------------------------------------------------------------
// Debug View Mode Constants (must match DebugViewMode enum in C++)
// -------------------------------------------------------------------------

static const uint DEBUG_MODE_LIT           = 0;
static const uint DEBUG_MODE_ALBEDO        = 1;
static const uint DEBUG_MODE_WORLD_NORMAL  = 2;
static const uint DEBUG_MODE_METALLIC      = 3;
static const uint DEBUG_MODE_ROUGHNESS     = 4;
static const uint DEBUG_MODE_AO            = 5;
static const uint DEBUG_MODE_DEPTH         = 6;
static const uint DEBUG_MODE_WORLD_POS     = 7;
static const uint DEBUG_MODE_LIGHTING      = 8;
static const uint DEBUG_MODE_SHADOWS       = 9;
static const uint DEBUG_MODE_WIREFRAME     = 10;
static const uint DEBUG_MODE_UNLIT         = 11;
static const uint DEBUG_MODE_OVERVIEW      = 12;

// -------------------------------------------------------------------------
// Constant Buffers
// -------------------------------------------------------------------------

#ifdef UZ_BINDLESS
/// Root constants at b0 -- debug view mode + SRV descriptor heap indices.
struct DebugViewIndices {
    uint mode;          ///< Active DebugViewMode (0-12).
    uint albedoIdx;     ///< Descriptor index for albedo+AO G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal+metallic+roughness G-Buffer.
    uint depthIdx;      ///< Descriptor index for depth buffer SRV (R32_FLOAT).
    uint positionIdx;   ///< Descriptor index for world-space position G-Buffer.
};
ConstantBuffer<DebugViewIndices> dvIndices : register(b0);
#else
/// SM5 fallback: debug mode passed as a single root constant.
cbuffer DebugViewCB : register(b0) {
    uint debugMode;     ///< Active DebugViewMode (0-12).
    uint _dvPad[3];     ///< Padding to 16-byte alignment.
};
#endif

/// Per-frame CB at b1 -- same layout as deferred_lighting_ps.hlsl.
/// Only nearPlane, farPlane, and invViewProj are used by this shader.
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
    float    _framePad[10];
};

// -------------------------------------------------------------------------
// G-Buffer Textures & Sampler (SM5 path)
// -------------------------------------------------------------------------

#ifndef UZ_BINDLESS
Texture2D<float4> gbufferAlbedo     : register(t0); ///< rgb = linear base colour, a = AO
Texture2D<float4> gbufferNormal     : register(t1); ///< rg = octahedral normal, b = metallic, a = roughness
Texture2D<float>  gbufferDepth      : register(t2); ///< Hardware depth as R32_FLOAT SRV
Texture2D<float4> gbufferPositionWS : register(t3); ///< xyz = world-space position, a = 1
#endif

/// Linear (bilinear wrap) sampler at s0 -- standard engine-wide sampler.
SamplerState linearSampler : register(s0);

// -------------------------------------------------------------------------
// Utility: Linearize Depth
// -------------------------------------------------------------------------

/// Converts hardware depth [0,1] to linear eye-space distance.
///
/// Uses the standard perspective projection inverse:
///   linearZ = (near * far) / (far - depth * (far - near))
///
/// # Parameters
/// - depth: hardware depth value [0,1] from the depth buffer.
/// - near:  near clip plane distance in world units.
/// - far:   far clip plane distance in world units.
///
/// # Returns
/// Linear depth in world units, clamped to [near, far].
float LinearizeDepth(float depth, float near, float far) {
    // Guard against degenerate near==far (would cause division by zero)
    float range = far - near;
    if (abs(range) < 1e-6f) return near;

    return (near * far) / (far - depth * range);
}

// -------------------------------------------------------------------------
// Utility: Sample G-Buffer at UV
// -------------------------------------------------------------------------

/// Samples all G-Buffer textures at the given UV coordinate.
///
/// Handles both SM6.6 bindless and SM5 register-bound paths.
/// Results are returned via out parameters to avoid struct overhead.
void SampleGBuffer(
    float2 uv,
    out float4 albedoAO,
    out float4 normalPacked,
    out float  depth,
    out float4 positionWS)
{
#ifdef UZ_BINDLESS
    Texture2D<float4> gbAlbedo   = ResourceDescriptorHeap[dvIndices.albedoIdx];
    Texture2D<float4> gbNormal   = ResourceDescriptorHeap[dvIndices.normalIdx];
    Texture2D<float>  gbDepth    = ResourceDescriptorHeap[dvIndices.depthIdx];
    Texture2D<float4> gbPosition = ResourceDescriptorHeap[dvIndices.positionIdx];

    albedoAO     = gbAlbedo.Sample(linearSampler, uv);
    normalPacked = gbNormal.Sample(linearSampler, uv);
    depth        = gbDepth.Sample(linearSampler, uv).r;
    positionWS   = gbPosition.Sample(linearSampler, uv);
#else
    albedoAO     = gbufferAlbedo.Sample(linearSampler, uv);
    normalPacked = gbufferNormal.Sample(linearSampler, uv);
    depth        = gbufferDepth.Sample(linearSampler, uv).r;
    positionWS   = gbufferPositionWS.Sample(linearSampler, uv);
#endif
}

// -------------------------------------------------------------------------
// Core: Visualize a Single Mode
// -------------------------------------------------------------------------

/// Produces a false-colour RGB output for the given debug mode and G-Buffer data.
///
/// Each mode extracts one channel or derived quantity from the G-Buffer and
/// remaps it to a visually useful [0,1] RGB range. No tonemapping or gamma
/// correction is applied.
///
/// # Parameters
/// - mode:         DebugViewMode integer (0-12).
/// - albedoAO:     RT0 sample (rgb=base colour, a=AO).
/// - normalPacked: RT1 sample (rg=octahedral normal, b=metallic, a=roughness).
/// - depth:        Hardware depth [0,1].
/// - positionWS:   World-space position (xyz).
///
/// # Returns
/// Linear RGB colour for display.
float3 VisualizeMode(
    uint   mode,
    float4 albedoAO,
    float4 normalPacked,
    float  depth,
    float4 positionWS)
{
    switch (mode) {

    case DEBUG_MODE_ALBEDO:
        /// Base colour -- direct G-Buffer readback, no modification.
        return albedoAO.rgb;

    case DEBUG_MODE_WORLD_NORMAL: {
        /// Octahedral-decoded world normal remapped from [-1,1] to [0,1].
        /// Red=+X, Green=+Y, Blue=+Z following standard normal-map convention.
        float3 N = OctDecode(normalPacked.rg);
        return N * 0.5f + 0.5f;
    }

    case DEBUG_MODE_METALLIC:
        /// Metallic factor as uniform grayscale (0=dielectric, 1=full metal).
        return normalPacked.bbb;

    case DEBUG_MODE_ROUGHNESS:
        /// Roughness factor as uniform grayscale (0=mirror, 1=fully rough).
        return normalPacked.aaa;

    case DEBUG_MODE_AO:
        /// Ambient occlusion as uniform grayscale (1=fully lit, 0=fully occluded).
        return albedoAO.aaa;

    case DEBUG_MODE_DEPTH: {
        /// Linearized depth normalized to [0,1] with near=white, far=black.
        /// This gives intuitive visual feedback where close surfaces are bright.
        float linZ = LinearizeDepth(depth, nearPlane, farPlane);
        float normalized = saturate(1.0f - (linZ - nearPlane) / (farPlane - nearPlane));
        return float3(normalized, normalized, normalized);
    }

    case DEBUG_MODE_WORLD_POS:
        /// World-space position visualized with frac() tiling at 0.1 world-unit
        /// period. Creates a repeating colour pattern that makes spatial extent
        /// and scale visible without clipping to [0,1].
        return frac(abs(positionWS.xyz) * 0.1f);

    case DEBUG_MODE_LIGHTING:
    case DEBUG_MODE_SHADOWS:
    case DEBUG_MODE_WIREFRAME:
        /// Placeholder mid-grey for modes that require buffers not available
        /// in the G-Buffer debug pass (lighting result, shadow factor, wireframe).
        return float3(0.5f, 0.5f, 0.5f);

    case DEBUG_MODE_UNLIT:
        /// Unlit mode -- same as Albedo (base colour without any lighting).
        return albedoAO.rgb;

    case DEBUG_MODE_LIT:
    default:
        /// Lit mode should never reach this shader (the debug pass is skipped).
        /// Output black as a safe sentinel.
        return float3(0.0f, 0.0f, 0.0f);
    }
}

// -------------------------------------------------------------------------
// Overview Grid: Mode-to-Cell Mapping
// -------------------------------------------------------------------------

/// Maps overview grid cell index (0-7) to the corresponding debug mode.
///
/// Grid layout (4 columns x 2 rows):
/// ```
/// [Albedo]      [WorldNormal] [Metallic]  [Roughness]
/// [AO]          [Depth]       [WorldPos]  [Lit(grey)]
/// ```
static const uint kOverviewCellModes[8] = {
    DEBUG_MODE_ALBEDO,       // cell 0: top-left
    DEBUG_MODE_WORLD_NORMAL, // cell 1
    DEBUG_MODE_METALLIC,     // cell 2
    DEBUG_MODE_ROUGHNESS,    // cell 3
    DEBUG_MODE_AO,           // cell 4: bottom-left
    DEBUG_MODE_DEPTH,        // cell 5
    DEBUG_MODE_WORLD_POS,    // cell 6
    DEBUG_MODE_LIGHTING      // cell 7: bottom-right (placeholder grey)
};

// -------------------------------------------------------------------------
// Pixel Shader Entry Point
// -------------------------------------------------------------------------

/// Debug view pixel shader entry point.
///
/// Reads G-Buffer textures and outputs a diagnostic visualization of the
/// selected channel. In Overview mode (12), subdivides the screen into a
/// 4x2 grid showing 8 channels simultaneously with thin black borders.
///
/// # Returns
/// float4 colour for the R8G8B8A8_UNORM backbuffer (alpha always 1.0).
float4 main(FSInput input) : SV_Target {

    float2 uv = input.texcoord;

#ifdef UZ_BINDLESS
    uint mode = dvIndices.mode;
#else
    uint mode = debugMode;
#endif

    // ── Overview composite grid ──
    if (mode == DEBUG_MODE_OVERVIEW) {
        // 4 columns x 2 rows
        static const float2 kCellCount = float2(4.0f, 2.0f);

        // Which cell the pixel falls into
        uint2 cellIdx = uint2(uv * kCellCount);
        cellIdx = min(cellIdx, uint2(3, 1)); // Clamp to valid range

        // UV within the cell [0,1] -- used for sampling so each cell shows full image
        float2 cellUV = frac(uv * kCellCount);

        // Thin black border between cells (1.5% of cell size on each edge)
        static const float kBorderWidth = 0.015f;
        float2 borderMask = step(kBorderWidth, cellUV) * step(cellUV, 1.0f - kBorderWidth);
        if (borderMask.x * borderMask.y < 0.5f) {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }

        // Determine which mode this cell displays
        uint cellLinear = cellIdx.y * 4 + cellIdx.x;
        uint cellMode = kOverviewCellModes[min(cellLinear, 7u)];

        // Sample G-Buffer at cell UV (each cell shows the full scene)
        float4 albedoAO, normalPacked, positionWS;
        float  depth;
        SampleGBuffer(cellUV, albedoAO, normalPacked, depth, positionWS);

        float3 result = VisualizeMode(cellMode, albedoAO, normalPacked, depth, positionWS);
        return float4(result, 1.0f);
    }

    // ── Single channel mode ──
    float4 albedoAO, normalPacked, positionWS;
    float  depth;
    SampleGBuffer(uv, albedoAO, normalPacked, depth, positionWS);

    float3 result = VisualizeMode(mode, albedoAO, normalPacked, depth, positionWS);
    return float4(result, 1.0f);
}
