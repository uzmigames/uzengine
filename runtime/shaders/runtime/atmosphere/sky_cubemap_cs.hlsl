/// UzEngine sky cubemap capture — renders atmosphere into cubemap for IBL.
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
/// 16-step ray march with transmittance LUT. No sun disk (IBL only).
/// Output: linear HDR, no tonemap. 64x64x6 RGBA16F.
///
/// b0: 15 root constants, u0: RWTexture2DArray, s0: linear sampler
///
/// The sun illuminance is per channel, the same quantity the sky pass scatters
/// (`skyCB.sunIlluminance`): block B.5c (2026-10-08) found the capture taking the
/// length of the illuminance vector (sqrt 3 x the per-channel value of a white sun,
/// grey for a tinted one), so every image-based term saw a sky brighter than the
/// one the frame renders.
///
/// The planet the march hits below the horizon is Lambertian with
/// `SkyAtmosphereComponent::groundAlbedo` (root constants groundAlbedoR/G/B), the
/// same field the scene authors as `ground_albedo`. Until 2026-11-11 it was a
/// hardcoded 0.7, which stays the component's default so an unauthored scene
/// captures exactly as before; the field is the per-scene knob. Solid-angle
/// weighted, 0.7 makes the captured ground hemisphere 4-9x brighter than the sky
/// hemisphere, and the SH L0 (the diffuse sky light, the fog and cloud sky colour)
/// and the prefiltered specular inherit it (phase3a-3.51.0 item 2.6).

#include "../common/constants.hlsli"

cbuffer SkyCubemapParams : register(b0) {
    float bottomRadiusKm;
    float topRadiusKm;
    float sunDirX, sunDirY, sunDirZ;
    float sunIllumR, sunIllumG, sunIllumB;   ///< Sun illuminance per channel (as the sky pass).
    uint  transmittanceLutIdx;
    uint  packedScatterScales;
    uint  scatteringLut3DIdx;
    float muSMin;
    float groundAlbedoR, groundAlbedoG, groundAlbedoB;   ///< SkyAtmosphereComponent::groundAlbedo (planet below the horizon).
};

RWTexture2DArray<float4> outputCubemap : register(u0);
SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4> g_TransmittanceLut : register(t0);
#endif

static const float3 RAYLEIGH_BETA = float3(5.802e-3, 13.558e-3, 33.1e-3);
static const float  MIE_BETA = 3.996e-3;
static const float  MIE_EXT_RATIO = 1.11;
static const float  MIE_G = 0.8;
static const float  RAYLEIGH_H = 8.0;
static const float  MIE_H = 1.2;
static const uint   FACE_SIZE = 64;
static const int    NUM_STEPS = 16;
static const float  OFFSET_KM = 0.005;

float RayleighPhase(float c) { return (3.0/(16.0*PI))*(1.0+c*c); }
float MiePhase(float c, float g) { float g2=g*g; float d=1.0+g2-2.0*g*c; return (1.0/(4.0*PI))*(1.0-g2)/(d*sqrt(d)); }

float2 RaySphere(float3 o, float3 d, float r) {
    float b=dot(o,d); float c=dot(o,o)-r*r; float disc=b*b-c;
    if(disc<0.0) return float2(-1,-1);
    float sq=sqrt(disc); return float2(-b-sq,-b+sq);
}

void TransmittanceUV(float h, float cosZ, float bR, float tR, out float2 uv) {
    float H=sqrt(max(0.0,tR*tR-bR*bR));
    float rho=sqrt(max(0.0,h*h-bR*bR));
    float disc=h*h*(cosZ*cosZ-1.0)+tR*tR;
    float d=max(0.0,-h*cosZ+sqrt(max(0.0,disc)));
    uv=float2((d-(tR-h))/(rho+H-(tR-h)+1e-6), rho/(H+1e-6));
    uv=saturate(uv);
}

float3 SampleTLut(Texture2D<float4> lut, float3 pos, float3 dir, float bR, float tR) {
    float h=length(pos); float cosZ=dot(normalize(pos),dir);
    float2 uv; TransmittanceUV(h,cosZ,bR,tR,uv);
    return lut.SampleLevel(linearSampler,uv,0).rgb;
}

/// DX cubemap face direction (Y-up) → swizzle to Z-up
float3 CubeFaceDirZUp(uint face, float2 uv) {
    float u=uv.x*2.0-1.0, v=uv.y*2.0-1.0;
    float3 d;
    switch(face) {
        case 0: d=normalize(float3( 1,-v,-u)); break;
        case 1: d=normalize(float3(-1,-v, u)); break;
        case 2: d=normalize(float3( u, 1, v)); break;
        case 3: d=normalize(float3( u,-1,-v)); break;
        case 4: d=normalize(float3( u,-v, 1)); break;
        case 5: d=normalize(float3(-u,-v,-1)); break;
        default: d=float3(0,0,1);
    }
    return normalize(float3(d.x, d.z, d.y)); // Y-up → Z-up
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 texel = DTid.xy;
    uint face = DTid.z;
    if (texel.x >= FACE_SIZE || texel.y >= FACE_SIZE || face >= 6) return;

    // The planet's Lambertian factor albedo / PI, rounded once as its own value: `precise` keeps the
    // compiler from re-associating the root constants into the ground product below, so the default 0.7
    // yields exactly the constant the shader hardcoded until 2026-11-11 (fl(0.7 * fl(1 / PI))) and the
    // default capture is bit-identical to that build's (measured: 0 of 524,288 env values differ; the
    // plain `albedo * (1.0/PI) * NdotL * ...` chain moved 18 texels by one f16 ulp). Computed before
    // any float branch so the attribute marks nothing else.
    precise float3 kGround = float3(groundAlbedoR, groundAlbedoG, groundAlbedoB) * (1.0/PI);

    float2 uv = (float2(texel) + 0.5) / float(FACE_SIZE);
    float3 rayDir = CubeFaceDirZUp(face, uv);

    float3 camKm = float3(0, 0, bottomRadiusKm + OFFSET_KM);
    float3 sunDir = normalize(float3(sunDirX, sunDirY, sunDirZ));
    float cosTheta = dot(rayDir, sunDir);
    float phaseR = RayleighPhase(cosTheta);
    float phaseM = MiePhase(cosTheta, MIE_G);

    // Unpack artistic scales
    float rScale = f16tof32(packedScatterScales & 0xFFFF);
    float mScale = f16tof32(packedScatterScales >> 16);
    rScale = (rScale > 0.0) ? rScale : 1.0;
    mScale = (mScale > 0.0) ? mScale : 1.0;
    float3 artBetaR = RAYLEIGH_BETA * rScale;
    float  artBetaM = MIE_BETA * mScale;

    float2 atmoHit = RaySphere(camKm, rayDir, topRadiusKm);
    if (atmoHit.y < 0.0) { outputCubemap[uint3(texel,face)] = float4(0,0,0,1); return; }

    float tStart = max(atmoHit.x, 0.0);
    float tEnd = atmoHit.y;
    float2 gndHit = RaySphere(camKm, rayDir, bottomRadiusKm);
    bool hitsGround = (gndHit.x > 0.0);
    if (hitsGround) tEnd = min(tEnd, gndHit.x);

    float stepLen = (tEnd - tStart) / float(NUM_STEPS);
    if (stepLen <= 0.0) { outputCubemap[uint3(texel,face)] = float4(0,0,0,1); return; }

#ifdef UZ_BINDLESS
    Texture2D<float4> tLut = ResourceDescriptorHeap[transmittanceLutIdx];
#else
    Texture2D<float4> tLut = g_TransmittanceLut;
#endif

    float3 L = float3(0, 0, 0);
    float3 T = float3(1, 1, 1);
    const float3 sunIllum = float3(sunIllumR, sunIllumG, sunIllumB);

    for (int i = 0; i < NUM_STEPS; ++i) {
        float t = tStart + (float(i) + 0.5) * stepLen;
        float3 sKm = camKm + rayDir * t;
        float alt = max(0.0, length(sKm) - bottomRadiusKm);

        float dR = exp(-alt / RAYLEIGH_H);
        float dM = exp(-alt / MIE_H);

        float3 extR = RAYLEIGH_BETA * dR;
        float3 extM = MIE_BETA * MIE_EXT_RATIO * dM;
        float3 ext = extR + extM;
        float3 Ts = exp(-ext * stepLen);

        float3 sunT = SampleTLut(tLut, sKm, sunDir, bottomRadiusKm, topRadiusKm);

        float3 scatter = artBetaR * dR * phaseR + artBetaM * dM * phaseM;
        float3 Sint = (scatter - scatter * Ts) / max(ext, 1e-7);

        L += T * sunT * Sint * sunIllum;
        T *= Ts;
    }

    // Ground contribution: the sun-lit Lambertian planet with the component's ground albedo
    // (direct sun only, as Bruneton's ground term; no sky light on the ground).
    if (hitsGround) {
        float3 gPos = camKm + rayDir * gndHit.x;
        float3 gN = normalize(gPos);
        float3 gSunT = SampleTLut(tLut, gPos, sunDir, bottomRadiusKm, topRadiusKm);
        float NdotL = max(0.0, dot(gN, sunDir));
        // The sun-lit Lambertian planet: kGround (albedo / PI, see the top of main) x NdotL x the sun's
        // illuminance x its transmittance to the ground. Keep this shape: with kGround a `precise`
        // value, the product compiles to what the hardcoded-0.7 build computed, bit for bit.
        const float3 nk = NdotL * kGround;
        float3 gRad = nk * sunIllum * gSunT;
        L += T * gRad;
    }

    // No sun disk — IBL cubemaps exclude point-source specular.
    // Store radiance, not radiance/PI. The atmospheric integration above already
    // produces radiance (the ground term carries its own Lambertian 1/PI), and the
    // consumers divide by PI themselves: PackSHToCompactFormat() bakes the UE5
    // cosine-convolution constants (c0 = 1/(2*sqrt(PI)) = A0/PI * Y00, etc.), so
    // EvalSH_L2 already returns E/PI. The old 1/PI here was a second division that
    // darkened both the SH diffuse and the prefiltered specular by PI.
    /// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
    /// @constants Alpha 1 marks valid radiance; diffuse consumers apply their own 1/PI.
    outputCubemap[uint3(texel, face)] = float4(L, 1.0);
}
