// gtao.ps.hlsl -- Ground Truth Ambient Occlusion
//
// Horizon-search AO with contact shadows.
// Traces screen-space depth along 2-4 slice directions per pixel,
// finds horizon angle, integrates visible solid angle vs surface normal.
//
// IMPORTANT: This pass runs at HALF resolution but samples the FULL-res
// depth buffer. All depthTex reads use UV-derived full-res coordinates.

cbuffer ProjCB : register(b0) {
    float4x4 projMatrix;
};

cbuffer GTAOParams : register(b1) {
    float2   screenSize;       // FULL resolution (width, height)
    float    nearPlane;
    float    farPlane;
    float    aoRadius;
    float    aoIntensity;
    float    aoFalloff;
    float    frameCount;
    float3   lightDirView;
    float    contactShadowLen;
};

Texture2D<float> depthTex : register(t0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float PI = 3.14159265359;

static const int NUM_DIRECTIONS = 4;
static const int NUM_STEPS      = 6;

float LinearizeDepth(float d) {
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

float3 ReconstructViewPos(float2 uv, float depth) {
    float z = LinearizeDepth(depth);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float x = ndc.x * z / projMatrix[0][0];
    float y = ndc.y * z / projMatrix[1][1];
    return float3(x, y, z);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float2 uv = input.texcoord;
    float2 texelSize = 1.0 / screenSize;

    // CRITICAL: derive full-res pixel coords from UV, NOT from SV_POSITION
    // (this pass renders at half-res but depthTex is full-res)
    int2 pixelCoord = int2(uv * screenSize);

    float centerDepthRaw = depthTex.Load(int3(pixelCoord, 0)).r;
    if (centerDepthRaw >= 0.9999)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 viewPos = ReconstructViewPos(uv, centerDepthRaw);
    float centerZ  = viewPos.z;

    // Reconstruct normal from depth via cross products
    float depthL = depthTex.Load(int3(pixelCoord + int2(-1, 0), 0)).r;
    float depthR = depthTex.Load(int3(pixelCoord + int2( 1, 0), 0)).r;
    float depthU = depthTex.Load(int3(pixelCoord + int2(0, -1), 0)).r;
    float depthD = depthTex.Load(int3(pixelCoord + int2(0,  1), 0)).r;

    float3 posL = ReconstructViewPos(uv + float2(-texelSize.x, 0), depthL);
    float3 posR = ReconstructViewPos(uv + float2( texelSize.x, 0), depthR);
    float3 posU = ReconstructViewPos(uv + float2(0, -texelSize.y), depthU);
    float3 posD = ReconstructViewPos(uv + float2(0,  texelSize.y), depthD);

    float3 dPdx = (abs(posR.z - centerZ) < abs(posL.z - centerZ)) ? (posR - viewPos) : (viewPos - posL);
    float3 dPdy = (abs(posD.z - centerZ) < abs(posU.z - centerZ)) ? (posD - viewPos) : (viewPos - posU);
    float3 viewNormal = normalize(cross(dPdx, dPdy));

    // Temporal rotation: rotate slice directions per frame to reduce banding
    float rotAngle = (PI / NUM_DIRECTIONS) * ((pixelCoord.x ^ pixelCoord.y) & 3)
                   + (PI / 8.0) * fmod(frameCount, 8.0);

    // Scale search radius to screen space
    float radiusPixels = aoRadius * projMatrix[0][0] / centerZ;
    radiusPixels = max(radiusPixels, 3.0);
    float stepSize = radiusPixels / NUM_STEPS;

    float totalAO = 0.0;

    [unroll]
    for (int dir = 0; dir < NUM_DIRECTIONS; ++dir) {
        float angle = (PI / NUM_DIRECTIONS) * dir + rotAngle;
        float2 sliceDir = float2(cos(angle), sin(angle));

        // Positive direction: track max horizon angle
        float horizonCos = -1.0;

        [unroll]
        for (int step = 1; step <= NUM_STEPS; ++step) {
            float2 offset = sliceDir * step * stepSize * texelSize;
            float2 sampleUV = uv + offset;

            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
                break;

            int2 sampleCoord = int2(sampleUV * screenSize);
            float sampleDepthRaw = depthTex.Load(int3(sampleCoord, 0)).r;
            float3 samplePos = ReconstructViewPos(sampleUV, sampleDepthRaw);

            float3 horizonVec = samplePos - viewPos;
            float horizonDist = length(horizonVec);

            if (horizonDist > 0.001) {
                float3 horizonDir = horizonVec / horizonDist;
                float cosBetween = dot(viewNormal, horizonDir);

                float falloff = saturate(1.0 - (horizonDist / aoRadius) * (horizonDist / aoRadius));

                float weightedCos = cosBetween * falloff;
                horizonCos = max(horizonCos, weightedCos);
            }
        }

        // Negative direction
        float horizonCosNeg = -1.0;
        [unroll]
        for (int step2 = 1; step2 <= NUM_STEPS; ++step2) {
            float2 offset = -sliceDir * step2 * stepSize * texelSize;
            float2 sampleUV = uv + offset;

            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
                break;

            int2 sampleCoord = int2(sampleUV * screenSize);
            float sampleDepthRaw2 = depthTex.Load(int3(sampleCoord, 0)).r;
            float3 samplePos = ReconstructViewPos(sampleUV, sampleDepthRaw2);

            float3 horizonVec = samplePos - viewPos;
            float horizonDist = length(horizonVec);

            if (horizonDist > 0.001) {
                float3 horizonDir = horizonVec / horizonDist;
                float cosBetween = dot(viewNormal, horizonDir);
                float falloff = saturate(1.0 - (horizonDist / aoRadius) * (horizonDist / aoRadius));
                float weightedCos = cosBetween * falloff;
                horizonCosNeg = max(horizonCosNeg, weightedCos);
            }
        }

        float aoContribPos = saturate(0.5 + 0.5 * horizonCos);
        float aoContribNeg = saturate(0.5 + 0.5 * horizonCosNeg);
        totalAO += (aoContribPos + aoContribNeg) * 0.5;
    }

    totalAO /= NUM_DIRECTIONS;

    float visibility = 1.0 - totalAO * aoIntensity;
    visibility = saturate(visibility);

    // Multi-bounce approximation (Jimenez 2016)
    visibility = visibility + visibility * (1.0 - visibility) * 0.2;
    visibility = saturate(visibility);

    // ========================================================================
    // Screen-Space Contact Shadows
    // ========================================================================
    float contactShadow = 1.0;
    if (contactShadowLen > 0.001) {
        float3 rayDir = normalize(lightDirView);
        float rayStep = contactShadowLen / 16.0;

        int2 bayerCoord = pixelCoord % 2;
        float bayerOffset = float((bayerCoord.x + bayerCoord.y * 2)) * 0.25;
        float jitter = frac(bayerOffset + fmod(frameCount, 4.0) * 0.25);

        [loop]
        for (int cs = 0; cs < 16; ++cs) {
            float t = (float(cs) + jitter) * rayStep;
            float3 rayPos = viewPos - rayDir * t;

            float4 proj = mul(projMatrix, float4(rayPos, 1.0));
            float2 rayUV = (proj.xy / proj.w) * 0.5 + 0.5;
            rayUV.y = 1.0 - rayUV.y;

            if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0)
                break;

            int2 rayPixel = int2(rayUV * screenSize);
            float depthAtRay = depthTex.Load(int3(rayPixel, 0)).r;
            float zAtRay = LinearizeDepth(depthAtRay);

            float depthDiff = rayPos.z - zAtRay;
            if (depthDiff > 0.005 && depthDiff < contactShadowLen * 0.5) {
                contactShadow = 0.0;
                break;
            }
        }

        float distFade = 1.0 - saturate(centerZ / 5.0);
        contactShadow = lerp(1.0, contactShadow, distFade);
    }

    float result = visibility * contactShadow;
    return float4(result, result, result, 1.0);
}
