/// UzEngine exponential height fog — Frostbite analytical integration.
/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite"
/// Blend: SrcAlpha, InvSrcAlpha — output (inscatter/opacity, opacity)

#include "../common/constants.hlsli"

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

#ifdef UZ_BINDLESS
struct FogIndices { uint depthIdx; uint _p0, _p1, _p2; };
ConstantBuffer<FogIndices> fogIndices : register(b0);
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
    float    nearPlane;
    float    farPlane;
};

cbuffer HeightFogCB : register(b2) {
    float4 fogColor;
    float  fogDensity;
    float  fogHeightFalloff;
    float  fogBaseHeight;
    float  fogMaxOpacity;
    float  fogStartDistance;
    float  fogCutoffDistance;
    float  maxObserverHeight;
    float  rawDensity;
    float  secondFogDensity;
    float  secondFogHeightFalloff;
    float  secondFogRawDensity;
    float  secondFogBaseHeight;
    float4 directionalInscatteringColor;
    float  directionalInscatteringExponent;
    float  directionalInscatteringStartDistance;
    float  _fogPad2[2];
    float4 skyAtmContribColorScale;
    float  heightFogContribution;
    float  skyLightColorR, skyLightColorG, skyLightColorB;
    float  sunIllumPostTransR, sunIllumPostTransG, sunIllumPostTransB, sunIllumPostTransA;
    float  sunDirX, sunDirY, sunDirZ;
    float  _sunPad;
};

SamplerState linearSampler : register(s0);

/// @ref Hillaire 2016 — Frostbite exponential fog ray integration
float IntegrateExponentialFogRay(float falloffCoeff, float rayDirZ, float rayOriginTerms) {
    float Falloff = max(-127.0, falloffCoeff * rayDirZ);
    float LineIntegral = (1.0 - exp2(-Falloff)) / Falloff;
    float Ln2 = log(2.0);
    float LineIntegralTaylor = Ln2 - (0.5 * Ln2 * Ln2) * Falloff;
    return rayOriginTerms * (abs(Falloff) > 0.01 ? LineIntegral : LineIntegralTaylor);
}

float3 ReconstructWorldPos(float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 wp = mul(float4(ndc, depth, 1.0), invViewProj);
    return wp.xyz / wp.w;
}

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[fogIndices.depthIdx];
    float depth = depthTex.Sample(linearSampler, uv).r;
#else
    float depth = sceneDepth.Sample(linearSampler, uv).r;
#endif

    float3 camPos3 = cameraPos.xyz;
    float3 CameraToReceiver;
    float  CameraToReceiverLength;
    bool isSky = (depth <= 0.0001);

    if (isSky) {
        float3 farWorld = ReconstructWorldPos(uv, 0.0002);
        float3 rayDir = normalize(farWorld - camPos3);
        CameraToReceiver = rayDir * farPlane;
        CameraToReceiverLength = farPlane;
    } else {
        float3 worldPos = ReconstructWorldPos(uv, depth);
        CameraToReceiver = worldPos - camPos3;
        CameraToReceiverLength = length(CameraToReceiver);
    }

    float MinFogOpacity = 1.0 - fogMaxOpacity;
    float3 WorldObserverOrigin = float3(camPos3.xy, min(camPos3.z, maxObserverHeight));
    CameraToReceiver.z += camPos3.z - WorldObserverOrigin.z;

    float CameraToReceiverLengthSqr = dot(CameraToReceiver, CameraToReceiver);
    float CameraToReceiverLengthInv = rsqrt(max(CameraToReceiverLengthSqr, 1e-8));
    CameraToReceiverLength = CameraToReceiverLengthSqr * CameraToReceiverLengthInv;
    float3 CameraToReceiverNormalized = CameraToReceiver * CameraToReceiverLengthInv;

    float RayOriginTerms = fogDensity;
    float RayOriginTermsSecond = secondFogDensity;
    float RayLength = CameraToReceiverLength;
    float RayDirectionZ = CameraToReceiver.z;

    // Start distance exclusion
    if (fogStartDistance > 0.0) {
        float t = fogStartDistance * CameraToReceiverLengthInv;
        float camToExZ = t * CameraToReceiver.z;
        float exZ = WorldObserverOrigin.z + camToExZ;
        RayLength = (1.0 - t) * CameraToReceiverLength;
        RayDirectionZ = CameraToReceiver.z - camToExZ;
        RayOriginTerms = rawDensity * exp2(-max(-127.0, fogHeightFalloff * (exZ - fogBaseHeight)));
        RayOriginTermsSecond = secondFogRawDensity * exp2(-max(-127.0, secondFogHeightFalloff * (exZ - secondFogBaseHeight)));
    }

    // Two-layer fog integration
    float lineIntegralShared = IntegrateExponentialFogRay(fogHeightFalloff, RayDirectionZ, RayOriginTerms);
    if (secondFogRawDensity > 0.0 || secondFogDensity > 0.0)
        lineIntegralShared += IntegrateExponentialFogRay(secondFogHeightFalloff, RayDirectionZ, RayOriginTermsSecond);

    float totalOpticalDepth = lineIntegralShared * RayLength;

    // Inscattering color + sky ambient injection
    float3 InscatteringColor = fogColor.xyz;
    InscatteringColor += skyAtmContribColorScale.xyz * heightFogContribution
                       * float3(skyLightColorR, skyLightColorG, skyLightColorB);

    // Directional inscattering (sun glow)
    float3 DirectionalInscattering = float3(0, 0, 0);
    {
        float3 sunDir = float3(sunDirX, sunDirY, sunDirZ);
        float3 dirColor = directionalInscatteringColor.xyz;
        if (sunIllumPostTransA > 0.0)
            dirColor += heightFogContribution * float3(sunIllumPostTransR, sunIllumPostTransG, sunIllumPostTransB);

        float cosAngle = saturate(dot(CameraToReceiverNormalized, sunDir));
        float phase = pow(cosAngle, directionalInscatteringExponent) * (1.0 / (4.0 * PI));
        float dirLineIntegral = lineIntegralShared * max(RayLength - directionalInscatteringStartDistance, 0.0);
        float dirFogFactor = saturate(exp2(-dirLineIntegral));
        DirectionalInscattering = dirColor * phase * (1.0 - dirFogFactor);
    }

    float ExpFogFactor = max(saturate(exp2(-totalOpticalDepth)), MinFogOpacity);
    float3 FogColor = InscatteringColor * (1.0 - ExpFogFactor) + DirectionalInscattering;
    float fogOpacity = max(1.0 - ExpFogFactor, 0.0);

    // Output for SrcAlpha/InvSrcAlpha blending
    float3 outputColor = FogColor / max(fogOpacity, 0.001);
    return float4(outputColor, fogOpacity);
}
