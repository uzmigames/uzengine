/// Deferred lighting pixel shader — Cook-Torrance PBR with directional + point lights
/// and cascaded shadow maps (CSM).
///
/// Reads G-Buffer render targets (albedo, world normal, metallic/roughness/AO) and
/// the depth buffer to reconstruct world-space position. Evaluates a single directional
/// light (with CSM shadow) and up to 64 point lights using the same GGX-based
/// Cook-Torrance BRDF as the forward pass (mesh_ps.hlsl).
///
/// SM6.6 (UZ_BINDLESS): fetches G-Buffer textures via ResourceDescriptorHeap[index]
/// using SRV descriptor indices passed as root constants at b0.
/// SM5 (!UZ_BINDLESS): uses register-bound Texture2D at t0-t3, shadow atlas at t4.
///
/// # G-Buffer Inputs
/// - Albedo       (R8G8B8A8_UNORM)    — rgb = base color (linear)
/// - Normal       (R16G16B16A16_FLOAT) — rgb = world-space normal [-1,1]
/// - MetalRough   (R8G8B8A8_UNORM)    — r = metallic, g = roughness, b = ao
/// - Depth        (R32_FLOAT SRV)      — hardware depth sampled as float
///
/// # Constant Buffers
/// - b0: Root constants (UZ_BINDLESS only) — 4 x uint32 SRV descriptor indices
/// - b1: PerFrameCB (viewProj, invViewProj, camera, lighting globals)
/// - b2: LightingCB (point light array + count)
/// - b4: ShadowDataCB (cascade view-proj matrices, splits, bias, atlas index)
///
/// # Output
/// Linear HDR color tonemapped via Reinhard and gamma-corrected to sRGB for
/// the R8G8B8A8_UNORM backbuffer.

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
struct GBufferIndices {
    uint albedoIdx;     ///< Descriptor index for albedo G-Buffer (R8G8B8A8_UNORM).
    uint normalIdx;     ///< Descriptor index for normal G-Buffer (R16G16B16A16_FLOAT).
    uint metalRoughIdx; ///< Descriptor index for metallic/roughness G-Buffer (R8G8B8A8_UNORM).
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
    float    _framePad[12];  ///< Remaining padding to 256-byte boundary
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
/// SM5 path: G-Buffer textures bound via SRV descriptor table at t0-t3.
Texture2D<float4> gbufferAlbedo     : register(t0); ///< rgb = linear base color
Texture2D<float4> gbufferNormal     : register(t1); ///< rgb = world normal [-1,1]
Texture2D<float4> gbufferMetalRough : register(t2); ///< r = metallic, g = roughness, b = ao
Texture2D<float>  gbufferDepth      : register(t3); ///< Hardware depth as R32_FLOAT SRV
Texture2D<float>  shadowAtlasTexture : register(t4); ///< CSM shadow depth atlas
#endif

/// Point/nearest sampler for G-Buffer reads — no filtering on discrete data.
SamplerState pointSampler : register(s0);

/// Comparison sampler for percentage-closer filtering on shadow depth maps.
/// Uses LESS comparison — returns 1.0 when the sample passes (fragment is lit).
SamplerComparisonState shadowSampler : register(s1);

// ─────────────────────────────────────────────────────────────────────────────
// PBR Constants
// ─────────────────────────────────────────────────────────────────────────────

static const float PI = 3.14159265359f;

/// Minimum roughness clamped before squaring to alpha, preventing a perfectly
/// smooth (singular) specular lobe that collapses to a delta function on GPU.
static const float MIN_ROUGHNESS = 0.04f;

// ─────────────────────────────────────────────────────────────────────────────
// PBR Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// GGX / Trowbridge-Reitz Normal Distribution Function.
///
/// D(h) = alpha^2 / (PI * ((N.H)^2 * (alpha^2 - 1) + 1)^2)
///
/// # Parameters
/// - NdotH: dot product of macrosurface normal N and half-vector H, in [0,1].
/// - alpha: perceptual roughness squared (r^2), in [MIN_ROUGHNESS^2, 1].
///
/// # Returns
/// Specular lobe density in [0, +inf). Integrates to 1 over the hemisphere.
float DistributionGGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

/// Schlick-GGX single-direction geometry masking/shadowing term.
///
/// G1(v) = N.V / (N.V * (1 - k) + k)   where k = alpha / 2 for direct lighting
///
/// # Parameters
/// - NdotV: saturated dot product of N and the view or light direction.
/// - alpha: perceptual roughness squared (r^2).
///
/// # Returns
/// Scalar in (0, 1].
float GeometrySchlickGGX(float NdotV, float alpha) {
    float k = alpha / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

/// Smith combined geometry function for both view and light directions.
///
/// # Parameters
/// - NdotV: saturated dot(N, V).
/// - NdotL: saturated dot(N, L).
/// - alpha: perceptual roughness squared (r^2).
///
/// # Returns
/// Scalar geometry attenuation in (0, 1].
float GeometrySmith(float NdotV, float NdotL, float alpha) {
    return GeometrySchlickGGX(NdotV, alpha) * GeometrySchlickGGX(NdotL, alpha);
}

/// Schlick Fresnel approximation.
///
/// F(v, h) = F0 + (1 - F0) * (1 - V.H)^5
///
/// # Parameters
/// - cosTheta: dot(V, H) clamped to [0,1].
/// - F0: reflectance at normal incidence.
///
/// # Returns
/// float3 Fresnel reflectance in [F0, 1].
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

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

/// Evaluates Cook-Torrance BRDF for a single light source.
///
/// # Parameters
/// - N:         world-space surface normal (normalised).
/// - V:         world-space view direction (normalised, toward camera).
/// - L:         world-space light direction (normalised, toward light).
/// - radiance:  incoming light radiance (color * intensity * attenuation).
/// - albedo:    surface base color (linear).
/// - metallic:  metallic factor [0,1].
/// - alpha:     roughness^2 (PBR alpha parameter).
/// - F0:        reflectance at normal incidence.
///
/// # Returns
/// Outgoing radiance contribution from this light.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float alpha, float3 F0) {
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.001f);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // Cook-Torrance specular BRDF
    float  D = DistributionGGX(NdotH, alpha);
    float  G = GeometrySmith(NdotV, NdotL, alpha);
    float3 F = FresnelSchlick(VdotH, F0);

    // D*G*F / (4 * N.V * N.L) — the 4 is the half-vector Jacobian
    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

    // Energy conservation: kD zeroed for metals (no diffuse lobe)
    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    // Lambertian diffuse normalised by PI
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cascaded Shadow Map Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Fraction of each cascade's range used for blending with the next cascade.
/// At 10%, a cascade covering [0, 100] units begins blending at 90 units.
static const float CASCADE_BLEND_BAND = 0.1f;

/// Samples the shadow atlas with 3x3 PCF at the given atlas UV and depth.
///
/// Performs 9 taps in a 3x3 grid around the sample point, each offset by one
/// texel. The comparison sampler returns 1.0 for lit and 0.0 for shadowed per
/// tap, and the results are averaged for a soft penumbra.
///
/// # Parameters
/// - atlasUV:   UV coordinates within the full shadow atlas, in [0,1].
/// - depth:     light-space depth to compare against, in [0,1].
/// - texelSize: 1.0 / shadow atlas width in texels.
///
/// # Returns
/// Shadow factor in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowPCF(float2 atlasUV, float depth, float texelSize) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    // SM5 path uses register-bound texture
    #define shadowAtlas shadowAtlasTexture
#endif

    float shadow = 0.0f;

    // 3x3 PCF kernel — 9 taps centered on the sample point
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            shadow += shadowAtlas.SampleCmpLevelZero(
                shadowSampler, atlasUV + offset, depth);
        }
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 9.0f;
}

/// Computes the shadow factor for a world-space position using cascaded shadow maps.
///
/// Selects the tightest cascade that contains the fragment based on view-space
/// depth, projects into light space, applies normal and depth bias, then samples
/// with 3x3 PCF. In the last 10% of each cascade's range, the result is blended
/// with the next cascade to eliminate visible seams.
///
/// # Parameters
/// - positionWS: fragment world-space position.
/// - normalWS:   fragment world-space surface normal (normalized).
/// - viewDepth:  distance from camera to fragment along the view axis.
///
/// # Returns
/// Shadow factor in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth) {
    // Per-cascade atlas texel size: atlas is 2x the cascade resolution
    float texelSize = 1.0f / (shadowMapSize * 2.0f);

    // Access cascade split distances as an array
    float splits[4] = { cascadeSplits.x, cascadeSplits.y,
                         cascadeSplits.z, cascadeSplits.w };

    // Atlas quadrant offsets: 2x2 layout
    // 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right
    static const float2 cascadeOffsets[4] = {
        float2(0.0f, 0.0f), float2(0.5f, 0.0f),
        float2(0.0f, 0.5f), float2(0.5f, 0.5f)
    };

    // Select the first cascade whose far split exceeds the fragment depth
    uint cascadeIdx = 0;
    for (uint i = 0; i < cascadeCount; i++) {
        if (viewDepth <= splits[i]) {
            cascadeIdx = i;
            break;
        }
        cascadeIdx = i; // Fall through to last cascade if beyond all splits
    }

    // Offset position along surface normal to reduce shadow acne on slopes
    float3 biasedPos = positionWS + normalWS * normalBias;

    // Project into light clip space for the selected cascade
    float4 shadowClip = mul(float4(biasedPos, 1.0f), cascadeViewProj[cascadeIdx]);
    float3 shadowNDC  = shadowClip.xyz / shadowClip.w;

    // Convert from NDC [-1,1] to UV [0,1], flipping Y for texture space
    float2 shadowUV = float2(shadowNDC.x * 0.5f + 0.5f,
                             -shadowNDC.y * 0.5f + 0.5f);

    // Map UV into the cascade's atlas quadrant (each quadrant is 0.5 x 0.5)
    float2 atlasUV = shadowUV * 0.5f + cascadeOffsets[cascadeIdx];

    // Apply depth bias before comparison
    float shadowDepth = shadowNDC.z - depthBias;

    float shadow = SampleShadowPCF(atlasUV, shadowDepth, texelSize);

    // ── Cascade blending ──
    // In the last CASCADE_BLEND_BAND fraction of the cascade range, lerp
    // toward the next cascade's shadow to avoid hard seams between cascades.
    if (cascadeIdx < cascadeCount - 1) {
        float cascadeNear = (cascadeIdx == 0) ? 0.0f : splits[cascadeIdx - 1];
        float cascadeFar  = splits[cascadeIdx];
        float cascadeRange = cascadeFar - cascadeNear;
        float blendStart  = cascadeFar - cascadeRange * CASCADE_BLEND_BAND;

        if (viewDepth > blendStart) {
            // Compute next cascade's shadow
            uint nextIdx = cascadeIdx + 1;
            float3 biasedPosNext = positionWS + normalWS * normalBias;
            float4 nextClip = mul(float4(biasedPosNext, 1.0f), cascadeViewProj[nextIdx]);
            float3 nextNDC  = nextClip.xyz / nextClip.w;

            float2 nextUV = float2(nextNDC.x * 0.5f + 0.5f,
                                   -nextNDC.y * 0.5f + 0.5f);
            float2 nextAtlasUV = nextUV * 0.5f + cascadeOffsets[nextIdx];
            float nextDepth = nextNDC.z - depthBias;

            float nextShadow = SampleShadowPCF(nextAtlasUV, nextDepth, texelSize);

            // Blend factor: 0 at blendStart, 1 at cascadeFar
            float blendFactor = saturate((viewDepth - blendStart) /
                                         (cascadeFar - blendStart));
            shadow = lerp(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates deferred PBR lighting from the G-Buffer.
///
/// Reads surface attributes from the G-Buffer textures, reconstructs the
/// world-space position from depth, and evaluates Cook-Torrance BRDF for
/// one directional light and up to 64 point lights. Applies Reinhard
/// tonemapping and sRGB gamma correction.
///
/// # Returns
/// float4 color for the R8G8B8A8_UNORM backbuffer.
float4 main(FSInput input) : SV_Target {

    float2 uv = input.texcoord;

    // ── Read G-Buffer ──
#ifdef UZ_BINDLESS
    // SM6.6 bindless: fetch G-Buffer textures from the descriptor heap
    Texture2D<float4> gbAlbedo     = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal     = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float4> gbMetalRough = ResourceDescriptorHeap[gBufIndices.metalRoughIdx];
    Texture2D<float>  gbDepth      = ResourceDescriptorHeap[gBufIndices.depthIdx];

    float3 albedo     = gbAlbedo.Sample(pointSampler, uv).rgb;
    float3 N          = normalize(gbNormal.Sample(pointSampler, uv).rgb);
    float4 metalRough = gbMetalRough.Sample(pointSampler, uv);
    float  depth      = gbDepth.Sample(pointSampler, uv).r;
#else
    // SM5 path: register-bound textures at t0-t3
    float3 albedo     = gbufferAlbedo.Sample(pointSampler, uv).rgb;
    float3 N          = normalize(gbufferNormal.Sample(pointSampler, uv).rgb);
    float4 metalRough = gbufferMetalRough.Sample(pointSampler, uv);
    float  depth      = gbufferDepth.Sample(pointSampler, uv).r;
#endif

    float metallic  = metalRough.r;
    float roughness = metalRough.g;
    float ao        = metalRough.b;

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

    // ── Tonemap (Reinhard) + gamma correction ──
    // Reinhard: maps [0, +inf) -> [0, 1) while preserving dark values
    color = color / (color + 1.0f);
    // Convert linear to sRGB (approximate gamma 2.2 for UNORM backbuffer)
    color = pow(color, 1.0f / 2.2f);

    return float4(color, 1.0f);
}
