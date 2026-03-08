// volumetric_apply.ps.hlsl -- Volumetric fog application pass
//
// Analytical height-based exponential fog with directional inscattering.
// Uses the Frostbite 2014 analytical integral for proper height fog
// instead of naive screen-space overlay.
//
// References:
//   [Frostbite 2014] Hillaire "Physically-Based & Unified Volumetric Rendering"
//   [UE5] HeightFogPixelShader.usf

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
};

Texture2D    depthTex    : register(t0);
Texture2D    colorTex    : register(t1);  // HDR scene color to fog over
SamplerState linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float PI = 3.14159265359;

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;
    float4 wp = mul(invViewProj, ndc);
    return wp.xyz / wp.w;
}

// Henyey-Greenstein phase function
float HG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(abs(1.0 + g2 - 2.0 * g * cosTheta), 1.5));
}

float4 PSMain(PSInput input) : SV_TARGET {
    float depth = depthTex.Load(int3(input.position.xy, 0)).r;
    float3 color = colorTex.Load(int3(input.position.xy, 0)).rgb;

    // Skip fog for sky pixels or when fog is disabled
    if (depth >= 0.999 || fogDensity < 0.0001) {
        return float4(color, 1.0);
    }

    float3 worldPos = ReconstructWorldPos(input.texcoord, depth);
    float3 viewVec = worldPos - cameraPos;
    float viewDist = length(viewVec);
    float3 viewDir = viewVec / max(viewDist, 0.001);

    // -----------------------------------------------------------------------
    // Frostbite 2014 analytical height fog integral
    //
    // Fog density at height h:  rho(h) = fogDensity * exp(-falloff * (h - baseHeight))
    //
    // Integral along ray from camera to worldPos:
    //   integral = (fogDensity / falloff) * exp(-camHeight * falloff)
    //              * (1 - exp(-dist * rayDirY * falloff)) / (rayDirY * falloff)
    // -----------------------------------------------------------------------

    float camRelHeight = cameraPos.y - fogBaseHeight;
    float falloff = max(fogHeightFalloff, 0.001);
    float rayDirY = viewDir.y;

    float fogAmount;
    float verticalFactor = rayDirY * falloff;

    if (abs(verticalFactor) > 0.001) {
        // General case: ray has vertical component
        float baseAmount = fogDensity * exp(-camRelHeight * falloff) / falloff;
        fogAmount = baseAmount * (1.0 - exp(-viewDist * verticalFactor)) / verticalFactor;
    } else {
        // Near-horizontal ray: Taylor expansion to avoid division by zero
        fogAmount = fogDensity * exp(-camRelHeight * falloff) * viewDist;
    }

    fogAmount = saturate(fogAmount * fogMaxOpacity);

    // Smooth falloff at far distances (prevent harsh fog cutoff)
    float distanceFade = 1.0 - exp(-viewDist * 0.0001);
    fogAmount *= distanceFade;

    // -----------------------------------------------------------------------
    // Inscattering from directional light
    // Dual-lobe Henyey-Greenstein matching cloud phase function
    // -----------------------------------------------------------------------

    float cosAngle = dot(viewDir, normalize(-lightDir));
    float phase = lerp(HG(cosAngle, 0.8), HG(cosAngle, -0.2), 0.25);
    float3 inscatter = lightColor * phase * fogDensity * 1.5;

    // Height-based inscatter attenuation (less inscatter high up)
    float avgHeight = (cameraPos.y + worldPos.y) * 0.5;
    float inscatterHeight = exp(-max(avgHeight - fogBaseHeight, 0.0) * falloff * 0.5);
    inscatter *= inscatterHeight;

    // Final fog color = base fog + inscattering
    float3 fogFinal = fogColor + inscatter;

    // Apply fog
    color = lerp(color, fogFinal, fogAmount);

    return float4(color, 1.0);
}
