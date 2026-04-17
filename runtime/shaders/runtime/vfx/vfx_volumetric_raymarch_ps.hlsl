/// VFX volumetric raymarch — fullscreen pixel shader (phase 8.15.0).
///
/// Samples the density volume written by `vfx_volumetric_splat_cs.hlsl` along
/// a ray from the camera through the volume AABB, accumulating in-scattering
/// (Henyey–Greenstein phase × scattering colour × transmittance) and reducing
/// transmittance by Beer–Lambert per step.
///
/// Mirrors the CPU-side `VfxHenyeyGreenstein`, `VfxBeerLambertTransmittance`,
/// and `VfxVolumetricStepContribution` helpers so unit tests exercise the
/// same math without a GPU.
///
/// # Output
/// SV_Target0: `(inScatter.rgb, 1 - transmittance)` premultiplied over the
/// existing HDR scene colour. Blend: SrcAlpha / InvSrcAlpha.
///
/// @ref Henyey & Greenstein 1941
/// @ref docs/engine/25-vfx/32-volumetric-renderer.md

#include "../include/depth_common.hlsli"

cbuffer VolumetricRaymarchCB : register(b0) {
    row_major float4x4 invViewProj;
    float3 cameraPos;
    float  halfExtent;
    float3 gridCenter;
    uint   gridResolution;
    float3 sunDir;       ///< World-space direction toward the sun.
    float  extinctionCoeff;
    float3 scatteringColor;
    float  hgAnisotropy;
    uint   raymarchSteps;
    uint   _pad0, _pad1, _pad2;
};

#ifdef UZ_BINDLESS
uint densityVolumeSrv : register(b1);
Texture3D<float>       GetVolume()  { return ResourceDescriptorHeap[densityVolumeSrv]; }
#else
Texture3D<float> densityVolume : register(t0);
Texture3D<float> GetVolume()    { return densityVolume; }
#endif

SamplerState linearSampler : register(s0);

static const float kFourPi = 12.566370614359172f;

float HenyeyGreenstein(float cosTheta, float g) {
    g = clamp(g, -0.9999f, 0.9999f);
    const float g2 = g * g;
    const float denom = max(1e-6f, 1.0f + g2 - 2.0f * g * cosTheta);
    return (1.0f - g2) / (kFourPi * pow(denom, 1.5f));
}

float BeerLambert(float sigma, float density, float stepDist) {
    return exp(-max(0.0f, sigma) * max(0.0f, density) * max(0.0f, stepDist));
}

/// Ray–AABB intersection returning the near/far t parameters. Negative
/// `tNear` is valid (ray origin inside the box); returns false when the
/// ray misses the box entirely.
bool IntersectAABB(float3 origin, float3 dir, float3 boxMin, float3 boxMax,
                   out float tNear, out float tFar) {
    float3 invDir = 1.0f / dir;
    float3 t0 = (boxMin - origin) * invDir;
    float3 t1 = (boxMax - origin) * invDir;
    float3 tMinV = min(t0, t1);
    float3 tMaxV = max(t0, t1);
    tNear = max(max(tMinV.x, tMinV.y), tMinV.z);
    tFar  = min(min(tMaxV.x, tMaxV.y), tMaxV.z);
    return tFar >= max(tNear, 0.0f);
}

struct PSInput {
    float4 positionCS : SV_Position;
    float2 uv         : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0 {
    // Reconstruct world-space ray from screen UV.
    const float2 ndc = input.uv * 2.0f - 1.0f;
    const float4 nearCS = float4(ndc.x, -ndc.y, 1.0f, 1.0f); // reversed-Z near
    const float4 farCS  = float4(ndc.x, -ndc.y, 0.0f, 1.0f); // reversed-Z far
    const float4 nearWS = mul(nearCS, invViewProj);
    const float4 farWS  = mul(farCS,  invViewProj);
    const float3 nearP  = nearWS.xyz / nearWS.w;
    const float3 farP   = farWS.xyz  / farWS.w;
    const float3 rayDir = normalize(farP - nearP);

    // Intersect with grid AABB.
    const float3 boxMin = gridCenter - halfExtent.xxx;
    const float3 boxMax = gridCenter + halfExtent.xxx;
    float tNear, tFar;
    if (!IntersectAABB(cameraPos, rayDir, boxMin, boxMax, tNear, tFar)) {
        return float4(0, 0, 0, 0); // miss — transparent
    }
    tNear = max(tNear, 0.0f);

    const float marchLength = tFar - tNear;
    if (marchLength <= 0.0f || raymarchSteps == 0u) {
        return float4(0, 0, 0, 0);
    }
    const float stepSize  = marchLength / (float)raymarchSteps;
    const float cosTheta  = dot(rayDir, -sunDir); // view-light angle
    const float invExtent = 1.0f / (2.0f * halfExtent);

    Texture3D<float> vol = GetVolume();

    float3 accumScatter    = float3(0, 0, 0);
    float  transmittance   = 1.0f;

    [loop] for (uint i = 0; i < raymarchSteps; ++i) {
        const float t = tNear + ((float)i + 0.5f) * stepSize;
        const float3 samplePos = cameraPos + rayDir * t;
        const float3 uvw = (samplePos - boxMin) * invExtent; // [0,1]^3

        const float density = vol.SampleLevel(linearSampler, uvw, 0);

        // Early-out when opacity saturates — the remaining march cannot
        // contribute visibly.
        if (transmittance < 0.005f) break;
        if (density <= 0.0001f)      continue;

        const float stepT = BeerLambert(extinctionCoeff, density, stepSize);
        const float phase = HenyeyGreenstein(cosTheta, hgAnisotropy);
        const float absorp = 1.0f - stepT;
        const float3 inscatter = scatteringColor * (phase * density * absorp * transmittance);

        accumScatter += inscatter;
        transmittance *= stepT;
    }

    return float4(accumScatter, 1.0f - transmittance);
}
