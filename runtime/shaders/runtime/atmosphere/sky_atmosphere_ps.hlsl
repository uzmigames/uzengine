/// UzEngine sky atmosphere — Bruneton 32-step ray march with Rayleigh + Mie.
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
/// @ref Hillaire 2020 "A Scalable Sky and Atmosphere Rendering Technique"
/// @ref StillTravelling 2020 "The sun, the sky and the clouds" (Shadertoy tdSXzD)
///      — sun core/corona/halo pattern.
/// Volumetric clouds live in cloud_raymarch_cs.hlsl, NOT here.

#include "../common/constants.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct SkyIndices {
    uint depthIdx;
    uint transmittanceLutIdx;
    uint multiScatteringLutIdx;
    uint apVolumeSrvIdx;
    uint scatteringLut3DIdx;
    uint irradianceLutIdx;
};
ConstantBuffer<SkyIndices> skyIndices : register(b0);
#else
Texture2D<float> sceneDepth : register(t0);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
};

cbuffer SkyAtmosphereCB : register(b2) {
    float4 rayleighScattering;    // xyz = beta (km^-1), w = scale height (km)
    float4 mieScattering;         // x = beta (km^-1), y = anisotropy g, z = scale height (km)
    float4 groundAlbedo;
    float4 sunIlluminance;        // xyz = color*intensity, w = angular radius (rad)
    float  bottomRadiusKm;
    float  atmosphereHeightKm;
    float  sunDiskIntensity;
    float  multiScatteringFactor;
    float  rayleighScatteringScale;
    float  mieScatteringScale;
};

SamplerState linearSampler : register(s0);

static const float WORLD_TO_KM = 0.00001;
static const int   NUM_STEPS   = 32;

/// @ref Bruneton 2017 — transmittance LUT UV parameterization
void TransmittanceLutParamsToUv(float viewHeight, float viewZenithCos,
    float bR, float tR, out float2 uv) {
    float H   = sqrt(max(0.0, tR * tR - bR * bR));
    float rho = sqrt(max(0.0, viewHeight * viewHeight - bR * bR));
    float disc = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0) + tR * tR;
    float d = max(0.0, -viewHeight * viewZenithCos + sqrt(max(0.0, disc)));
    uv = float2((d - (tR - viewHeight)) / (rho + H - (tR - viewHeight) + 1e-6),
                rho / (H + 1e-6));
    uv = saturate(uv);
}

float3 SampleTransmittanceLut(float3 samplePos, float3 queryDir, float bR, float tR) {
    float h = length(samplePos);
    float cosZ = dot(normalize(samplePos), queryDir);
    float2 uv;
    TransmittanceLutParamsToUv(h, cosZ, bR, tR, uv);
#ifdef UZ_BINDLESS
    if (skyIndices.transmittanceLutIdx != 0xFFFFFFFF) {
        Texture2D<float4> tLut = ResourceDescriptorHeap[skyIndices.transmittanceLutIdx];
        return tLut.SampleLevel(linearSampler, uv, 0).rgb;
    }
#endif
    return float3(1, 1, 1); // fallback: no attenuation
}

float RayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

float MiePhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 / (4.0 * PI)) * (1.0 - g2) / (denom * sqrt(denom));
}

float2 RaySphereIntersect(float3 origin, float3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return float2(-1, -1);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}


float4 main(FSInput input) : SV_Target {
    int2 px = int2(input.positionCS.xy);

    // Read depth — only draw sky pixels (reversed-Z: far = 0)
#ifdef UZ_BINDLESS
    Texture2D<float> depthBuf = ResourceDescriptorHeap[skyIndices.depthIdx];
    float depth = depthBuf.Load(int3(px, 0)).r;
#else
    float depth = sceneDepth.Load(int3(px, 0)).r;
#endif
    if (depth > 0.0001) discard;

    // Reconstruct view direction (2-point unproject for numerical stability)
    float2 ndc = input.texcoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 pt0 = mul(float4(ndc, 0.25, 1.0), invViewProj);
    float4 pt1 = mul(float4(ndc, 0.75, 1.0), invViewProj);
    float3 rayDir = normalize(pt0.xyz / pt0.w - pt1.xyz / pt1.w);

    // Camera in sky-space (km, planet center at origin)
    float3 cameraSkyKm = cameraPos.xyz * WORLD_TO_KM + float3(0, 0, bottomRadiusKm);
    float topRadiusKm = bottomRadiusKm + atmosphereHeightKm;

    // Snap camera above surface
    float distToCenter = length(cameraSkyKm);
    float minSafe = bottomRadiusKm + 0.005; // 5m above surface
    if (distToCenter < minSafe)
        cameraSkyKm = cameraSkyKm * (minSafe / distToCenter);

    // Intersect atmosphere
    float2 atmoHit = RaySphereIntersect(cameraSkyKm, rayDir, topRadiusKm);
    if (atmoHit.y < 0.0) return float4(0, 0, 0, 1);

    float tStart = max(atmoHit.x, 0.0);
    float tEnd   = atmoHit.y;

    // Clip at planet surface (for ground-level camera, cap at 200km)
    float2 groundHit = RaySphereIntersect(cameraSkyKm, rayDir, bottomRadiusKm);
    float cameraAlt = length(cameraSkyKm) - bottomRadiusKm;
    if (groundHit.x > 0.0) {
        if (cameraAlt > 10.0)
            tEnd = min(tEnd, groundHit.x);
        else
            tEnd = min(tEnd, tStart + 200.0);
    }

    float stepLen = (tEnd - tStart) / float(NUM_STEPS);
    if (stepLen <= 0.0) return float4(0, 0, 0, 1);

    // Scattering params
    float3 sunDir   = normalize(lightDir.xyz);
    float  cosTheta = dot(rayDir, sunDir);
    float  phaseR   = RayleighPhase(cosTheta);
    float  phaseM   = MiePhaseHG(cosTheta, mieScattering.y);

    float3 betaR = rayleighScattering.xyz * rayleighScatteringScale;
    float  betaM = mieScattering.x * mieScatteringScale;
    float  scaleHR = rayleighScattering.w;
    float  scaleHM = mieScattering.z;

    // Ray march
    float3 L = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);

    for (int i = 0; i < NUM_STEPS; ++i) {
        float t = tStart + (float(i) + 0.5) * stepLen;
        float3 sampleKm = cameraSkyKm + rayDir * t;
        float altKm = max(0.0, length(sampleKm) - bottomRadiusKm);

        float densR = exp(-altKm / scaleHR);
        float densM = exp(-altKm / scaleHM);

        // Extinction (physical, not artistic)
        float3 extR = rayleighScattering.xyz * densR;
        float3 extM = mieScattering.x * 1.11 * densM;
        float3 ext = extR + extM;

        float3 T_step = exp(-ext * stepLen);

        // Sun transmittance from precomputed LUT (Bruneton 2017)
        float3 sunT = SampleTransmittanceLut(sampleKm, sunDir, bottomRadiusKm, topRadiusKm);

        // In-scattering
        float3 scatterR = betaR * densR * phaseR;
        float3 scatterM = betaM * densM * phaseM;
        float3 scatter = sunT * (scatterR + scatterM);

        // Frostbite analytical integration
        float3 Sint = (scatter - scatter * T_step) / max(ext, 1e-6);
        L += throughput * Sint * sunIlluminance.xyz;
        throughput *= T_step;
    }

    // ── Sun: hard core + Mie-style halo with explicit cutoff ─────────────
    // @ref StillTravelling 2020 "The sun, the sky and the clouds"
    //      (Shadertoy tdSXzD) — getSunPoint + getMie pattern.
    //
    // Three components, each with a finite footprint so the sun stays
    // localized instead of bleeding across the whole sky:
    //   1. Core    — hard HDR disk inside `sunRadius` (smoothstep edge).
    //   2. Corona  — warm falloff that ends at ~6× the disk radius.
    //   3. Halo    — wide soft glow that ends at ~25× the disk radius.
    {
        float sunRadius = max(sunIlluminance.w, 0.00935);  // radians
        float theta     = acos(clamp(cosTheta, -1.0, 1.0));

        // 1. Core — bright white disk with a soft 1px edge.
        float core = 1.0 - smoothstep(sunRadius * 0.85, sunRadius * 1.05, theta);
        core *= 50.0;  // HDR — drives bloom + lens flare downstream.

        // 2. Corona — tight falloff over ~2.5× the disk radius.
        float coronaR  = sunRadius * 2.5;
        float corona   = saturate(1.0 - theta / coronaR);
        corona = corona * corona * (3.0 - 2.0 * corona);
        corona *= 1.5;

        // 3. Halo — narrow soft glow, hard cutoff at ~8× sunRadius.
        float haloR  = sunRadius * 8.0;
        float halo   = saturate(1.0 - theta / haloR);
        halo = pow(halo, 4.0) * 0.2;

        // Warm tint for corona/halo; the core stays neutral white.
        float3 warmTint = float3(1.00, 0.72, 0.40);
        float3 sunCol = sunIlluminance.xyz * core
                      + sunIlluminance.xyz * warmTint * (corona + halo);

        L += sunCol * sunDiskIntensity * throughput;
    }

    return float4(max(L, 0.0), 1.0);
}
