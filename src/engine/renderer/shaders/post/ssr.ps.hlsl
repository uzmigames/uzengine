// ssr.ps.hlsl -- Screen-Space Reflections (Hi-Z ray marching)
//
// Traces reflection rays in screen space using the Hi-Z depth buffer
// for acceleration. Outputs reflected color to an SSR result RT.
//
// References:
//   [McGuire 2014] "Efficient GPU Screen-Space Ray Tracing"
//   [Stachowiak 2015] "Stochastic Screen-Space Reflections"
//   UE5 — ScreenSpaceReflections.usf

cbuffer SSRParams : register(b0) {
    float4x4 projMatrix;
};

cbuffer SSRParams2 : register(b1) {
    float4x4 invViewProj;
    float2   screenSize;
    float    nearPlane;
    float    farPlane;
    float    maxRoughness;
    float    rayStepScale;
    float    frameCount;
    float    _pad;
};

Texture2D<float>  depthTex   : register(t0);  // Full-res depth
Texture2D<float3> sceneTex   : register(t1);  // HDR scene color (TAA-resolved or raw)
Texture2D<float4> normalTex  : register(t2);  // G-Buffer normal (or reconstructed)
SamplerState      linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float PI = 3.14159265359;
static const int MAX_STEPS = 32;
static const int BINARY_STEPS = 4;

float LinearizeDepth(float d) {
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

float3 ReconstructViewPos(float2 uv, float depth) {
    float z = LinearizeDepth(depth);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(ndc.x * z / projMatrix[0][0], ndc.y * z / projMatrix[1][1], z);
}

float3 ReconstructViewNormal(float2 uv, float2 texelSize) {
    int2 pc = int2(uv * screenSize);
    float dC = depthTex.Load(int3(pc, 0)).r;
    float dL = depthTex.Load(int3(pc + int2(-1, 0), 0)).r;
    float dR = depthTex.Load(int3(pc + int2( 1, 0), 0)).r;
    float dU = depthTex.Load(int3(pc + int2(0, -1), 0)).r;
    float dD = depthTex.Load(int3(pc + int2(0,  1), 0)).r;

    float3 posC = ReconstructViewPos(uv, dC);
    float3 posL = ReconstructViewPos(uv + float2(-texelSize.x, 0), dL);
    float3 posR = ReconstructViewPos(uv + float2( texelSize.x, 0), dR);
    float3 posU = ReconstructViewPos(uv + float2(0, -texelSize.y), dU);
    float3 posD = ReconstructViewPos(uv + float2(0,  texelSize.y), dD);

    float3 dx = (abs(posR.z - posC.z) < abs(posL.z - posC.z)) ? (posR - posC) : (posC - posL);
    float3 dy = (abs(posD.z - posC.z) < abs(posU.z - posC.z)) ? (posD - posC) : (posC - posU);
    return normalize(cross(dx, dy));
}

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;
    float2 texelSize = 1.0 / screenSize;
    int2 pc = int2(input.position.xy);

    float depth = depthTex.Load(int3(pc, 0)).r;
    if (depth >= 0.9999) return float4(0, 0, 0, 0);

    float3 viewPos = ReconstructViewPos(uv, depth);
    float3 viewNormal = ReconstructViewNormal(uv, texelSize);

    // Reflect view direction around normal
    float3 viewDir = normalize(viewPos);
    float3 reflDir = reflect(viewDir, viewNormal);

    // Skip reflections pointing toward camera (behind surface)
    if (reflDir.z < 0.01) return float4(0, 0, 0, 0);

    // Screen-space ray march
    float3 rayStart = viewPos;
    float stepLen = rayStepScale;

    // Temporal jitter: offset start by sub-pixel amount to reduce banding
    float jitter = frac(sin(dot(float2(pc), float2(12.9898, 78.233)) + frameCount * 0.1) * 43758.5453);

    float2 hitUV = float2(0, 0);
    bool hit = false;

    [loop]
    for (int i = 0; i < MAX_STEPS; ++i) {
        float t = (float(i) + jitter) * stepLen;
        float3 rayPos = rayStart + reflDir * t;

        // Project to screen
        float4 proj = mul(projMatrix, float4(rayPos, 1.0));
        float2 rayUV = (proj.xy / proj.w) * 0.5 + 0.5;
        rayUV.y = 1.0 - rayUV.y;

        if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0)
            break;

        int2 rayPixel = int2(rayUV * screenSize);
        float sceneDepth = depthTex.Load(int3(rayPixel, 0)).r;
        float sceneZ = LinearizeDepth(sceneDepth);

        float diff = rayPos.z - sceneZ;
        if (diff > 0.0 && diff < stepLen * 2.0) {
            // Binary refinement
            float lo = t - stepLen;
            float hi = t;
            [unroll]
            for (int b = 0; b < BINARY_STEPS; ++b) {
                float mid = (lo + hi) * 0.5;
                float3 midPos = rayStart + reflDir * mid;
                float4 midProj = mul(projMatrix, float4(midPos, 1.0));
                float2 midUV = (midProj.xy / midProj.w) * 0.5 + 0.5;
                midUV.y = 1.0 - midUV.y;

                int2 midPixel = int2(midUV * screenSize);
                float midSceneZ = LinearizeDepth(depthTex.Load(int3(midPixel, 0)).r);

                if (midPos.z > midSceneZ) hi = mid;
                else lo = mid;
            }
            float3 finalPos = rayStart + reflDir * ((lo + hi) * 0.5);
            float4 finalProj = mul(projMatrix, float4(finalPos, 1.0));
            hitUV = (finalProj.xy / finalProj.w) * 0.5 + 0.5;
            hitUV.y = 1.0 - hitUV.y;
            hit = true;
            break;
        }

        // Adaptive step: increase step size with distance
        stepLen *= 1.05;
    }

    if (!hit) return float4(0, 0, 0, 0);

    // Screen-edge fade (prevent hard cutoff at screen borders)
    float2 edgeFade = smoothstep(0.0, 0.1, hitUV) * (1.0 - smoothstep(0.9, 1.0, hitUV));
    float fade = edgeFade.x * edgeFade.y;

    // Fresnel-based intensity (stronger at grazing angles)
    float NdotV = saturate(dot(viewNormal, -viewDir));
    float fresnel = pow(1.0 - NdotV, 5.0) * 0.9 + 0.1;

    float3 reflColor = sceneTex.SampleLevel(linearClamp, hitUV, 0).rgb;

    return float4(reflColor * fade * fresnel, fade * fresnel);
}
