/// Cloud composite pixel shader — blends half-res cloud output over HDR scene.
///
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
///
/// # Blend Mode (set on pipeline)
/// SrcBlend = One, DstBlend = SrcAlpha, BlendOp = Add
/// final.rgb = cloud.rgb * 1 + scene.rgb * cloud.a
/// where cloud.a = transmittance (1 = transparent, 0 = opaque)
///
/// # Root Constants (b0)
/// - [0]: cloud output SRV index
/// - [1]: depth SRV index (for geometry masking)
///
/// # Depth Check
/// Geometry pixels (reversed-Z depth > 0.0001) are masked to prevent clouds
/// from appearing in front of meshes. This check is done here (not in the
/// compute shader) because the compute runs on a separate command list where
/// the depth texture may not be in a readable state.

#include "../common/constants.hlsli"
#include "../common/bindless.hlsli"
#include "cloud_aerial_math.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
cbuffer CompositeIndices : register(b0) {
    uint cloudSrvIdx;
    uint depthSrvIdx;
    uint cloudDepthSrvIdx;
    uint cloudAerialPerspective;
};
#else
Texture2D<float4> cloudTexture : register(t0);
Texture2D<float>  sceneDepth   : register(t1);
#endif

// Existing PerFrameCB atmosphere contract, populated by frame_constants.cpp.
cbuffer PerFrameCB : register(b1) {
    float3 lightDir : packoffset(c6);
    float3 lightColor : packoffset(c7);
    uint aerialPerspectiveEnabled : packoffset(c23.y);
    uint transmittanceLutSrvIndex : packoffset(c23.z);
    float apBottomRadiusKm : packoffset(c23.w);
    float apTopRadiusKm : packoffset(c24.x);
    float apRayleighBetaX : packoffset(c24.y);
    float apRayleighBetaY : packoffset(c24.z);
    float apRayleighBetaZ : packoffset(c24.w);
    float apRayleighScaleH : packoffset(c25.x);
    float apMieBeta : packoffset(c25.y);
    float apMieScaleH : packoffset(c25.z);
    float apMieAnisotropy : packoffset(c25.w);
    float apSunIntensity : packoffset(c26.x);
    float apRayleighScale : packoffset(c26.y);
    float apMieScale : packoffset(c26.z);
};

// Prefix of the unchanged 1024-byte CloudCB: unjittered reconstruction in km.
cbuffer CloudCompositeCB : register(b2) {
    row_major float4x4 g_invViewProj;
    float3 g_cameraPos;
};

// LUTs and depth must clamp, never wrap at the horizon.
SamplerState linearSampler : register(s2);

#ifdef UZ_BINDLESS
float3 CloudAtmosphereToBoundary(float3 position, float3 direction) {
    float r = clamp(length(position), apBottomRadiusKm, apTopRadiusKm);
    float mu = dot(normalize(position), direction);
    // Same Bruneton parameterization as sky_atmosphere_ps / AP volume.
    float H = sqrt(max(0.0f, apTopRadiusKm * apTopRadiusKm - apBottomRadiusKm * apBottomRadiusKm));
    float rho = sqrt(max(0.0f, r * r - apBottomRadiusKm * apBottomRadiusKm));
    float d = max(0.0f, -r * mu + sqrt(max(0.0f, r * r * (mu * mu - 1.0f)
        + apTopRadiusKm * apTopRadiusKm)));
    float2 uv = saturate(float2((d - (apTopRadiusKm - r))
        / (rho + H - (apTopRadiusKm - r) + 1e-6f), rho / (H + 1e-6f)));
    Texture2D<float4> lut = ResourceDescriptorHeap[transmittanceLutSrvIndex];
    return lut.SampleLevel(linearSampler, uv, 0).rgb;
}

float3 CloudAtmosphereSegment(float3 start, float3 end, float3 direction) {
    // The LUT stores point -> top boundary, so T(start,end)=T(start,top)/T(end,top).
    // Reverse ground-intersecting rays: the forward boundary is hidden by the planet.
    float r = length(start), rMu = dot(start, direction);
    bool ground = rMu < 0.0f && rMu * rMu - r * r
        + apBottomRadiusKm * apBottomRadiusKm >= 0.0f;
    float3 numerator = CloudAtmosphereToBoundary(ground ? end : start, ground ? -direction : direction);
    float3 denominator = CloudAtmosphereToBoundary(ground ? start : end, ground ? -direction : direction);
    return saturate(float3(
        CloudSegmentTransmittance(numerator.r, denominator.r),
        CloudSegmentTransmittance(numerator.g, denominator.g),
        CloudSegmentTransmittance(numerator.b, denominator.b)));
}

float3 CloudAtmosphereInscatter(float3 origin, float3 direction, float distanceKm) {
    float3 sun = normalize(lightDir);
    float cosTheta = dot(direction, sun);
    float phaseR = (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
    float g = apMieAnisotropy;
    float denom = max(1e-4f, 1.0f + g * g - 2.0f * g * cosTheta);
    float phaseM = (1.0f - g * g) / (4.0f * PI * denom * sqrt(denom));
    float3 betaR = float3(apRayleighBetaX, apRayleighBetaY, apRayleighBetaZ);
    float3 inscatter = 0.0f;
    // Same bounded eight-step analytical source integration as the existing AP volume.
    float stepKm = distanceKm / 8.0f;
    for (uint i = 0; i < 8; ++i) {
        float3 samplePos = origin + direction * ((float(i) + 0.5f) * stepKm);
        float altitude = max(0.0f, length(samplePos) - apBottomRadiusKm);
        float densityR = exp(-altitude / apRayleighScaleH);
        float densityM = exp(-altitude / apMieScaleH);
        float3 extinction = betaR * densityR + apMieBeta * 1.11f * densityM;
        float3 source = CloudAtmosphereToBoundary(samplePos, sun)
            * (betaR * densityR * phaseR * apRayleighScale
                + apMieBeta * densityM * phaseM * apMieScale);
        // Planet eclipses the sun at this atmospheric sample.
        float sunMu = dot(samplePos, sun);
        if (sunMu < 0.0f && sunMu * sunMu - dot(samplePos, samplePos)
            + apBottomRadiusKm * apBottomRadiusKm > 0.0f) source = 0.0f;
        float3 segmentStart = origin + direction * (float(i) * stepKm);
        inscatter += CloudAtmosphereSegment(origin, segmentStart, direction)
            * source * (1.0f - exp(-extinction * stepKm)) / max(extinction, 1e-6f);
    }
    return inscatter * lightColor;
}
#endif

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Depth check — skip geometry pixels ──
    // Reversed-Z: geometry depth > 0.0001, sky depth near 0.0.
    // For geometry pixels, output (0,0,0,1) so the blend becomes:
    //   final = 0 + scene * 1 = scene (no change)
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[depthSrvIdx];
    float deviceZ = depthTex.SampleLevel(linearSampler, uv, 0).r;
#else
    float deviceZ = sceneDepth.SampleLevel(linearSampler, uv, 0).r;
#endif

    if (deviceZ > 0.0001f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // ── Sample cloud output ──
#ifdef UZ_BINDLESS
    Texture2D<float4> cloudTex = ResourceDescriptorHeap[cloudSrvIdx];
    float4 cloud = cloudTex.SampleLevel(linearSampler, uv, 0);
#else
    float4 cloud = cloudTexture.SampleLevel(linearSampler, uv, 0);
#endif

#ifdef UZ_BINDLESS
    if (cloudAerialPerspective != 0 && aerialPerspectiveEnabled != 0
        && transmittanceLutSrvIndex != 0xFFFFFFFF && cloud.a < 1.0f) {
        Texture2D<float> cloudDepth = ResourceDescriptorHeap[cloudDepthSrvIdx];
        float distanceKm = cloudDepth.SampleLevel(linearSampler, uv, 0);
        float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
        float4 p0 = mul(float4(ndc, 0.25f, 1), g_invViewProj);
        float4 p1 = mul(float4(ndc, 0.75f, 1), g_invViewProj);
        float3 direction = normalize(p0.xyz / p0.w - p1.xyz / p1.w);
        float3 origin = g_cameraPos + float3(0, 0, apBottomRadiusKm);
        float r = length(origin);
        origin *= max(r, apBottomRadiusKm + 0.005f) / r;
        // Clip to the atmosphere shell for cameras/clouds above its top.
        float b = dot(origin, direction);
        float disc = b * b - dot(origin, origin) + apTopRadiusKm * apTopRadiusKm;
        if (disc > 0.0f) {
            float entry = max(0.0f, -b - sqrt(disc));
            float exit = -b + sqrt(disc);
            float lengthKm = max(0.0f, min(distanceKm, exit) - entry);
            origin += direction * entry;
            float3 atmT = CloudAtmosphereSegment(origin, origin + direction * lengthKm, direction);
            float3 atmS = CloudAtmosphereInscatter(origin, direction, lengthKm);
            cloud.rgb = float3(
                CloudAtmosphereRadiance(cloud.r, atmT.r, atmS.r, cloud.a),
                CloudAtmosphereRadiance(cloud.g, atmT.g, atmS.g, cloud.a),
                CloudAtmosphereRadiance(cloud.b, atmT.b, atmS.b, cloud.a));
        }
        return cloud;
    }
#endif

    // ── Legacy cloud aerial perspective (A/B control only) ──
    /// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
    /// Apply atmospheric scattering to cloud color based on distance from camera.
    /// Clouds far from the camera pick up the sky's blue-ish tint from
    /// Rayleigh scattering, matching how real distant clouds appear hazy.
    // Distance estimation: clouds are at the cloud layer, roughly proportional to UV deviation from center
    float2 centerOffset = abs(uv - 0.5) * 2.0; // 0 at center, 1 at edges
    float distEstimate = length(centerOffset) * 0.5 + 0.1; // rough [0.1, 0.6] range
    float apFade = saturate(distEstimate * 2.0);

    // Simple Rayleigh-like blue tint for distant clouds
    float3 skyTint = float3(0.6, 0.7, 0.9); // approximate sky blue
    cloud.rgb = lerp(cloud.rgb, cloud.rgb * skyTint, apFade * 0.15);

    // cloud.rgb = premultiplied cloud color (with aerial perspective)
    // cloud.a   = transmittance (1 = transparent, 0 = opaque)
    return cloud;
}
