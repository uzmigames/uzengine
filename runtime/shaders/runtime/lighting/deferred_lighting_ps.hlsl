/// Deferred lighting pixel shader — Cook-Torrance PBR with directional + point lights
/// and cascaded shadow maps (CSM).
///
/// Reads G-Buffer render targets (albedo+AO, octahedral normal+metallic+roughness)
/// and the depth buffer to reconstruct world-space position. Evaluates a single
/// directional light (with CSM shadow) and up to 64 point lights using the same
/// GGX-based Cook-Torrance BRDF as the forward pass (mesh_ps.hlsl).
///
/// SM6.6 (UZ_BINDLESS): fetches G-Buffer textures via ResourceDescriptorHeap[index]
/// using SRV descriptor indices passed as root constants at b0.
/// SM5 (!UZ_BINDLESS): uses register-bound Texture2D at t0-t2, shadow atlas at t3.
///
/// # G-Buffer Inputs (2-RT packed layout)
/// - AlbedoAO     (R8G8B8A8_UNORM) — rgb = base color (linear), a = AO
/// - NormalMatPbr  (R8G8B8A8_UNORM) — rg = octahedral normal, b = metallic, a = roughness
/// - Depth         (R32_FLOAT SRV)  — hardware depth sampled as float
///
/// # Constant Buffers
/// - b0: Root constants (UZ_BINDLESS only) — 3 x uint32 SRV descriptor indices
/// - b1: PerFrameCB (viewProj, invViewProj, camera, lighting globals)
/// - b2: LightingCB (point light array + count)
/// - b4: ShadowDataCB (cascade view-proj matrices, splits, bias, atlas index)
///
/// # Output
/// Linear HDR color tonemapped via ACES filmic and gamma-corrected to sRGB for
/// the R8G8B8A8_UNORM backbuffer.

#include "../include/pbr_common.hlsli"
#include "../include/octahedral.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the fullscreen triangle vertex shader (fullscreen_vs.hlsl).
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV descriptor heap indices for G-Buffer textures.
///
/// In SM6.6 bindless mode, the root signature has no SRV descriptor table.
/// Instead, each texture is accessed via ResourceDescriptorHeap[index] using
/// indices pushed as 32-bit root constants.
///
/// Only 3 indices needed with the packed 2-RT G-Buffer layout:
/// albedo+AO in RT0, octahedral normal+metallic+roughness in RT1, depth.
struct GBufferIndices {
    uint albedoIdx;     ///< Descriptor index for albedo+AO G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal+metallic+roughness G-Buffer (R8G8B8A8_UNORM).
    uint depthIdx;      ///< Descriptor index for depth buffer SRV (R32_FLOAT).
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);
#endif

/// Per-frame CB at b1 — extended with invViewProj in the padding area.
///
/// The original PerFrameCB has 28 floats (112 bytes) of padding after ambientColor.
/// We place invViewProj (16 floats = 64 bytes) at the start of that padding,
/// leaving 12 floats (48 bytes) still available for future use.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;      ///< xyz = world position, w = unused
    float4   timeParams;     ///< x = totalTime, y = deltaTime, z = frameCount, w = unused
    float4   lightDir;       ///< xyz = direction TOWARD the light (normalised), w = unused
    float4   lightColor;     ///< xyz = color * intensity (radiance), w = unused
    float4   ambientColor;   ///< xyz = ambient radiance, w = unused
    row_major float4x4 invViewProj; ///< Inverse view-projection for world position reconstruction
    float    nearPlane;          ///< Near clip plane distance in world units.
    float    farPlane;           ///< Far clip plane distance in world units.
    float    _framePad[10];      ///< Remaining padding to 256-byte boundary
};

/// Single point light for the deferred lighting pass.
struct PointLight {
    float3 position;  ///< World-space position.
    float  radius;    ///< Attenuation radius (light intensity reaches zero at this distance).
    float3 color;     ///< Pre-multiplied color * intensity (radiance).
    float  _pad;      ///< Padding to 16-byte alignment.
};

/// Maximum number of point lights evaluated per pixel.
static const uint MAX_POINT_LIGHTS = 64;

/// Lighting CB at b2 — point light array and count.
///
/// Moved from b3 to b2 because fullscreen deferred passes do not use
/// per-object data, and the SM6.6 root signature only has 3 root params
/// (b0 root constants, b1 per-frame CBV, b2 per-object/lighting CBV).
cbuffer LightingCB : register(b2) {
    PointLight pointLights[MAX_POINT_LIGHTS];
    uint       pointLightCount;
    uint3      _lightPad;
};

/// Cascaded shadow map parameters — must match C++ ShadowDataCB struct exactly.
///
/// Supports 1-4 cascades arranged in a 2x2 atlas layout. Each cascade occupies
/// one quadrant of the shadow depth atlas texture.
cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[4];  ///< Light-space view-projection per cascade.
    float4   cascadeSplits;                  ///< View-space far distance per cascade (x=C0, y=C1, z=C2, w=C3).
    uint     shadowAtlasIndex;               ///< SRV heap index for shadow depth atlas.
    uint     cascadeCount;                   ///< Number of active cascades (1-4).
    float    normalBias;                     ///< World-space offset along surface normal.
    float    depthBias;                      ///< Bias subtracted from shadow depth comparison.
    float    shadowMapSize;                  ///< Per-cascade resolution in texels.
};

// ─────────────────────────────────────────────────────────────────────────────
// G-Buffer Textures & Sampler
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// SM5 path: G-Buffer textures bound via SRV descriptor table at t0-t2.
Texture2D<float4> gbufferAlbedo     : register(t0); ///< rgb = linear base color, a = AO
Texture2D<float4> gbufferNormal     : register(t1); ///< rg = octahedral normal, b = metallic, a = roughness
Texture2D<float>  gbufferDepth      : register(t2); ///< Hardware depth as R32_FLOAT SRV
Texture2D<float>  shadowAtlasTexture : register(t3); ///< CSM shadow depth atlas
#endif

/// Point/nearest sampler for G-Buffer reads — no filtering on discrete data.
SamplerState pointSampler : register(s0);

/// Comparison sampler for percentage-closer filtering on shadow depth maps.
/// Uses LESS comparison — returns 1.0 when the sample passes (fragment is lit).
SamplerComparisonState shadowSampler : register(s1);

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen UV and hardware depth.
///
/// Transforms the screen-space coordinate back through the inverse view-projection
/// matrix. The depth value is used as-is (assumes standard [0,1] depth range).
///
/// # Parameters
/// - uv:    screen UV in [0,1] (top-left origin).
/// - depth: hardware depth value from the depth buffer [0,1].
///
/// # Returns
/// World-space position (float3).
float3 ReconstructWorldPosition(float2 uv, float depth) {
    // Convert UV to NDC: x [-1,1], y [-1,1] (flip Y), z [0,1]
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // D3D UV origin is top-left, NDC Y points up

    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);

    // Perspective divide
    return worldPos.xyz / worldPos.w;
}

#include "../include/shadow_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates deferred PBR lighting from the G-Buffer.
///
/// Reads surface attributes from the G-Buffer textures, reconstructs the
/// world-space position from depth, and evaluates Cook-Torrance BRDF for
/// one directional light and up to 64 point lights. Applies ACES filmic
/// tonemapping and sRGB gamma correction.
///
/// # Returns
/// float4 color for the R8G8B8A8_UNORM backbuffer.
float4 main(FSInput input) : SV_Target {

    float2 uv = input.texcoord;

    // ── Read G-Buffer (2-RT packed layout) ──
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
    float3 N         = OctDecode(normalPacked.rg);
    float  metallic  = normalPacked.b;
    float  roughness = normalPacked.a;

    // ── Reconstruct world position from depth ──
    float3 worldPos = ReconstructWorldPosition(uv, depth);

    // ── PBR parameter preparation ──
    float3 V = normalize(cameraPos.xyz - worldPos);

    // Perceptual roughness -> alpha: squaring linearises the perceived response
    float alpha = max(roughness * roughness, MIN_ROUGHNESS * MIN_ROUGHNESS);

    // F0: dielectrics use 0.04 (IOR ~1.5), metals use albedo as tinted reflectance
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // ── Cascaded shadow map ──
    float viewDepth = length(worldPos - cameraPos.xyz);
    float shadow = 1.0f;
    if (cascadeCount > 0) {
        shadow = SampleShadowMap(worldPos, N, viewDepth);
    }

    // ── Directional light (shadowed) ──
    float3 L_dir = normalize(lightDir.xyz);
    float3 Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz,
                             albedo, metallic, alpha, F0) * shadow;

    // ── Point lights ──
    for (uint i = 0; i < pointLightCount; ++i) {
        float3 lightVec  = pointLights[i].position - worldPos;
        float  dist      = length(lightVec);
        float  radius    = pointLights[i].radius;

        // Skip lights beyond their attenuation radius
        if (dist > radius)
            continue;

        float3 L_point = lightVec / dist; // normalise

        // Smooth inverse-square attenuation with windowing function:
        // attenuation = saturate(1 - (d/r)^4)^2 / (d^2 + 1)
        // The windowing function ensures a smooth falloff to zero at radius
        // while closely matching physical inverse-square near the light.
        float distOverRadius = dist / radius;
        float distRatio4     = distOverRadius * distOverRadius * distOverRadius * distOverRadius;
        float windowing      = saturate(1.0f - distRatio4);
        float attenuation    = (windowing * windowing) / (dist * dist + 1.0f);

        float3 radiance = pointLights[i].color * attenuation;
        Lo += EvaluateBRDF(N, V, L_point, radiance,
                           albedo, metallic, alpha, F0);
    }

    // ── Ambient (constant approximation; will be replaced with GI probes) ──
    float3 ambient = ambientColor.xyz * albedo * ao;

    // ── Combine ──
    float3 color = ambient + Lo;

    // ── Tonemap (ACES filmic) + gamma correction ──
    // ACES filmic curve: better highlight rolloff than Reinhard, preserves
    // color saturation in bright areas.
    color = ACESFilm(color);
    // Linear → sRGB via math_util.hlsli (exact IEC 61966-2-1 curve).
    color = LinearToSrgb(color);

    return float4(color, 1.0f);
}
