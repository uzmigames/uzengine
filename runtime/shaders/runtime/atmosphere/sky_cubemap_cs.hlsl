/// UzEngine sky cubemap capture — renders atmosphere into cubemap for IBL.
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
/// 16-step ray march with transmittance LUT. No sun disk (IBL only).
/// Output: linear HDR, no tonemap. 64x64x6 RGBA16F.
///
/// b0: 10 root constants, u0: RWTexture2DArray, s0: linear sampler

#include "../common/constants.hlsli"

cbuffer SkyCubemapParams : register(b0) {
    float bottomRadiusKm;
    float topRadiusKm;
    float sunDirX, sunDirY, sunDirZ;
    float sunIntensity;
    uint  transmittanceLutIdx;
    uint  packedScatterScales;
    uint  scatteringLut3DIdx;
    float muSMin;
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
static const float3 GROUND_ALBEDO = float3(0.7, 0.7, 0.7);
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

        L += T * sunT * Sint * sunIntensity;
        T *= Ts;
    }

    // Ground contribution
    if (hitsGround) {
        float3 gPos = camKm + rayDir * gndHit.x;
        float3 gN = normalize(gPos);
        float3 gSunT = SampleTLut(tLut, gPos, sunDir, bottomRadiusKm, topRadiusKm);
        float NdotL = max(0.0, dot(gN, sunDir));
        float3 gRad = GROUND_ALBEDO * (1.0/PI) * NdotL * sunIntensity * gSunT;
        L += T * gRad;
    }

    // No sun disk — IBL cubemaps exclude point-source specular
    // Normalize: sky radiância → IBL-suitable range. The irradiance convolution
    // integrates over the hemisphere but the raw sky values include sunIntensity
    // which makes them too bright for IBL. Divide by PI to match the convention
    // where SkyLightIntensity=1.0 produces physically balanced ambient.
    // @ref UE5 SkyLightColor normalization — SH * SkyLightColor.rgb
    outputCubemap[uint3(texel, face)] = float4(L * M_1_PI, 1.0);
}
