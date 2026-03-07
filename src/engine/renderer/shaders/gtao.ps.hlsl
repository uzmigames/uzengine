// gtao.ps.hlsl -- Ground Truth Ambient Occlusion
//
// Replaces hemisphere-sample SSAO with horizon-search algorithm.
// Traces screen-space depth along 2-4 slice directions per pixel,
// finds horizon angle, integrates visible solid angle vs surface normal.
//
// References:
//   [Jimenez 2016] "Practical Real-Time Strategies for Accurate Indirect Occlusion"
//   [Activision 2016] "GTAO: A Temporal Approach to Screen-Space Ground Truth AO"
//   UE5 — Engine/Shaders/Private/GTAOSpatialFilter.usf

cbuffer ProjCB : register(b0) {
    float4x4 projMatrix;       // camera projection matrix (16 DWORDs)
};

cbuffer GTAOParams : register(b1) {
    float2   screenSize;       // full resolution (width, height)
    float    nearPlane;        // camera near plane (0.1)
    float    farPlane;         // camera far plane (500)
    float    aoRadius;         // world-space AO radius (default 0.5)
    float    aoIntensity;      // AO strength multiplier (default 1.5)
    float    aoFalloff;        // distance falloff exponent (default 2.0)
    float    frameCount;       // for temporal rotation offset
    // Contact shadows — light direction in view space for screen-space ray-march
    float3   lightDirView;     // normalized light direction in view space
    float    contactShadowLen; // ray length in view-space units (default 0.3)
};

Texture2D<float> depthTex : register(t0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float PI = 3.14159265359;

// Number of slice directions and steps per slice
static const int   NUM_DIRECTIONS = 4;
static const int   NUM_STEPS      = 6;

// Linearize depth from [0,1] NDC to view-space Z
float LinearizeDepth(float d) {
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

// Reconstruct view-space position from UV + depth
float3 ReconstructViewPos(float2 uv, float depth) {
    float z = LinearizeDepth(depth);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float x = ndc.x * z / projMatrix[0][0];
    float y = ndc.y * z / projMatrix[1][1];
    return float3(x, y, z);
}

float4 PSMain(PSInput input) : SV_TARGET {
    int2 pixelCoord = int2(input.position.xy);
    float2 texelSize = 1.0 / screenSize;

    float centerDepthRaw = depthTex.Load(int3(pixelCoord, 0)).r;
    if (centerDepthRaw >= 0.9999)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 viewPos = ReconstructViewPos(input.texcoord, centerDepthRaw);
    float centerZ  = viewPos.z;

    // Reconstruct normal from depth via cross products
    float depthL = depthTex.Load(int3(pixelCoord + int2(-1, 0), 0)).r;
    float depthR = depthTex.Load(int3(pixelCoord + int2( 1, 0), 0)).r;
    float depthU = depthTex.Load(int3(pixelCoord + int2(0, -1), 0)).r;
    float depthD = depthTex.Load(int3(pixelCoord + int2(0,  1), 0)).r;

    float3 posL = ReconstructViewPos(input.texcoord + float2(-texelSize.x, 0), depthL);
    float3 posR = ReconstructViewPos(input.texcoord + float2( texelSize.x, 0), depthR);
    float3 posU = ReconstructViewPos(input.texcoord + float2(0, -texelSize.y), depthU);
    float3 posD = ReconstructViewPos(input.texcoord + float2(0,  texelSize.y), depthD);

    float3 dPdx = (abs(posR.z - centerZ) < abs(posL.z - centerZ)) ? (posR - viewPos) : (viewPos - posL);
    float3 dPdy = (abs(posD.z - centerZ) < abs(posU.z - centerZ)) ? (posD - viewPos) : (viewPos - posU);
    float3 viewNormal = normalize(cross(dPdx, dPdy));

    // Temporal rotation: rotate slice directions per frame to reduce banding
    float rotAngle = (PI / NUM_DIRECTIONS) * ((pixelCoord.x ^ pixelCoord.y) & 3)
                   + (PI / 8.0) * fmod(frameCount, 8.0);

    // Scale search radius to screen space
    float radiusPixels = aoRadius * projMatrix[0][0] / centerZ;
    radiusPixels = max(radiusPixels, 3.0);  // minimum 3 pixels to avoid undersampling
    float stepSize = radiusPixels / NUM_STEPS;

    float totalAO = 0.0;

    [unroll]
    for (int dir = 0; dir < NUM_DIRECTIONS; ++dir) {
        float angle = (PI / NUM_DIRECTIONS) * dir + rotAngle;
        float2 sliceDir = float2(cos(angle), sin(angle));

        // March along the slice in both directions (+/-)
        // Track max horizon angle from surface tangent
        float horizonCos = -1.0;  // worst case: fully visible hemisphere

        [unroll]
        for (int step = 1; step <= NUM_STEPS; ++step) {
            float2 offset = sliceDir * step * stepSize * texelSize;
            float2 sampleUV = input.texcoord + offset;

            // Bounds check
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

                // Distance-based falloff
                float falloff = saturate(1.0 - (horizonDist / aoRadius) * (horizonDist / aoRadius));

                // Update max horizon cosine (higher cos = more occluded)
                float weightedCos = cosBetween * falloff;
                horizonCos = max(horizonCos, weightedCos);
            }
        }

        // Negative direction
        float horizonCosNeg = -1.0;
        [unroll]
        for (int step2 = 1; step2 <= NUM_STEPS; ++step2) {
            float2 offset = -sliceDir * step2 * stepSize * texelSize;
            float2 sampleUV = input.texcoord + offset;

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

        // Integrate: visible fraction = 1.0 when horizonCos is low (open sky)
        // horizonCos near 1.0 = fully occluded, near -1.0 = fully open
        float aoContribPos = saturate(0.5 + 0.5 * horizonCos);    // remap to [0,1]
        float aoContribNeg = saturate(0.5 + 0.5 * horizonCosNeg);
        totalAO += (aoContribPos + aoContribNeg) * 0.5;
    }

    totalAO /= NUM_DIRECTIONS;

    // Invert: totalAO is occlusion factor, we output visibility (1 = no occlusion)
    float visibility = 1.0 - totalAO * aoIntensity;
    visibility = saturate(visibility);

    // Multi-bounce approximation (Jimenez 2016):
    // Recovers indirect light lost by AO in colored surfaces
    visibility = visibility + visibility * (1.0 - visibility) * 0.2;
    visibility = saturate(visibility);

    // ========================================================================
    // Screen-Space Contact Shadows — ray-march along light direction in depth
    // Catches small-scale shadows shadow maps can't resolve (object contact).
    // 16 steps, temporally dithered (4-frame Bayer), distance-faded.
    // ========================================================================
    float contactShadow = 1.0;
    if (contactShadowLen > 0.001) {
        float3 rayDir = normalize(lightDirView);  // light direction in view space
        float rayStep = contactShadowLen / 16.0;

        // Temporal dither: 2x2 Bayer pattern + frame rotation
        int2 bayerCoord = pixelCoord % 2;
        float bayerOffset = float((bayerCoord.x + bayerCoord.y * 2)) * 0.25;
        float jitter = frac(bayerOffset + fmod(frameCount, 4.0) * 0.25);

        [loop]
        for (int cs = 0; cs < 16; ++cs) {
            float t = (float(cs) + jitter) * rayStep;
            float3 rayPos = viewPos - rayDir * t;  // march toward light

            // Project ray position to screen UV
            float4 proj = mul(projMatrix, float4(rayPos, 1.0));
            float2 rayUV = (proj.xy / proj.w) * 0.5 + 0.5;
            rayUV.y = 1.0 - rayUV.y;

            if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0)
                break;

            int2 rayPixel = int2(rayUV * screenSize);
            float depthAtRay = depthTex.Load(int3(rayPixel, 0)).r;
            float zAtRay = LinearizeDepth(depthAtRay);

            // If scene depth is closer than ray depth → occluded
            float depthDiff = rayPos.z - zAtRay;
            if (depthDiff > 0.005 && depthDiff < contactShadowLen * 0.5) {
                // Soft contact shadow: fade based on how early the hit is
                float hitT = float(cs) / 16.0;
                contactShadow = min(contactShadow, lerp(0.3, 0.85, hitT));
                break;
            }
        }

        // Distance fade: contact shadows meaningful only within ~5m of camera
        float distFade = 1.0 - saturate(centerZ / 5.0);
        contactShadow = lerp(1.0, contactShadow, distFade);
    }

    // Output: AO * contact shadow combined
    float result = visibility * contactShadow;

    return float4(result, result, result, 1.0);
}
