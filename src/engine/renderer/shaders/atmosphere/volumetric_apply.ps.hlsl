// volumetric_apply.ps.hlsl -- Volumetric fog application pass
//
// Reads scene depth, computes exponential height fog + distance fog,
// and applies inscattering from the directional light.
//
// References:
//   [UE5] HeightFogPixelShader.usf
//   [Frostbite 2014] "Physically-Based & Unified Volumetric Rendering"

cbuffer FogParams : register(b0) {
    float3 fogColor;         // base fog color (RGB, linear)
    float  fogDensity;       // global fog density (0=off, 0.02=light, 0.1=dense)
    float3 cameraPos;        // world-space camera position
    float  fogHeightFalloff; // exponential height falloff rate (0.1 = slow, 2.0 = fast)
    float3 lightDir;         // directional light direction (normalized)
    float  fogBaseHeight;    // world height where fog is densest
    float3 lightColor;       // inscattering light color
    float  fogMaxOpacity;    // max fog opacity (0..1, default 0.95)
};

cbuffer InvVPCB : register(b3) {
    float4x4 invViewProj;
    // We only need invViewProj, but root constants are 16 DWORDs
};

Texture2D    depthTex    : register(t0);
Texture2D    colorTex    : register(t1);  // HDR scene color to fog over
SamplerState linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;
    float4 wp = mul(invViewProj, ndc);
    return wp.xyz / wp.w;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float depth = depthTex.Load(int3(input.position.xy, 0)).r;
    float3 color = colorTex.Load(int3(input.position.xy, 0)).rgb;

    // Skip fog for sky pixels (depth = 1.0 or 0.0 depending on convention)
    if (depth >= 0.999 || fogDensity < 0.0001) {
        return float4(color, 1.0);
    }

    float3 worldPos = ReconstructWorldPos(input.texcoord, depth);
    float3 viewDir = worldPos - cameraPos;
    float viewDist = length(viewDir);

    // Distance fog: exponential Beer-Lambert
    float distanceFog = 1.0 - exp(-fogDensity * viewDist);

    // Height fog: exponential density decrease with altitude
    float heightDelta = worldPos.y - fogBaseHeight;
    float heightFactor = exp(-fogHeightFalloff * max(heightDelta, 0.0));
    float heightFog = heightFactor * distanceFog;

    // Combined fog amount (clamped to max opacity)
    float fogAmount = saturate(heightFog * fogMaxOpacity);

    // Inscattering from directional light (Henyey-Greenstein phase, g=0.7)
    float cosAngle = dot(normalize(viewDir), -normalize(lightDir));
    float g = 0.7;
    float g2 = g * g;
    float phase = (1.0 - g2) / (4.0 * 3.14159 * pow(1.0 + g2 - 2.0 * g * cosAngle, 1.5));
    float3 inscatter = lightColor * phase * fogDensity * 2.0;

    float3 fogFinal = fogColor + inscatter;
    color = lerp(color, fogFinal, fogAmount);

    return float4(color, 1.0);
}
