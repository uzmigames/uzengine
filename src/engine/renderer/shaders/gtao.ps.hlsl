// gtao.ps.hlsl -- Ground Truth Ambient Occlusion
//
// Faithful reimplementation of XeGTAO (Intel, MIT license) algorithm:
//   - Per-slice normal projection to compute tangent angle 'n'
//   - Horizon search tracking max cos(theta) with viewVec
//   - Correct visibility integral: (-cos(2h-n) + cosN + 2h*sinN) / 4
//   - Thin-object heuristic via distance-weighted lerp
//
// References:
//   [Jimenez 2016] "Practical Real-Time Strategies for Accurate Indirect Occlusion"
//   Intel XeGTAO — github.com/GameTechDev/XeGTAO

cbuffer ProjCB : register(b0) {
    float4x4 projMatrix;
};

cbuffer GTAOParams : register(b1) {
    float2   screenSize;
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

static const float PI      = 3.14159265359;
static const float HALF_PI = 1.57079632679;

static const int NUM_DIRECTIONS = 4;
static const int NUM_STEPS      = 8;

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

// Fast arccos approximation (XeGTAO style, max error ~0.07 rad)
float FastACos(float x) {
    float res = -0.156583 * abs(x) + HALF_PI;
    res *= sqrt(1.0 - abs(x));
    return (x >= 0) ? res : PI - res;
}

float4 PSMain(PSInput input) : SV_TARGET {
    int2 pixelCoord = int2(input.position.xy);
    float2 texelSize = 1.0 / screenSize;

    float centerDepthRaw = depthTex.Load(int3(pixelCoord, 0)).r;
    if (centerDepthRaw >= 0.9999)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 viewPos = ReconstructViewPos(input.texcoord, centerDepthRaw);
    float centerZ  = viewPos.z;

    // viewVec: from pixel toward camera (camera at origin in view space)
    float3 viewVec = normalize(-viewPos);

    // Reconstruct normal from depth cross products
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

    // Temporal rotation to reduce banding
    float rotAngle = (PI / float(NUM_DIRECTIONS)) * float((pixelCoord.x ^ pixelCoord.y) & 3)
                   + (PI / 8.0) * fmod(frameCount, 8.0);

    // Screen-space search radius
    float radiusPixels = aoRadius * projMatrix[0][0] / centerZ;
    radiusPixels = max(radiusPixels, 3.0);
    float stepSize = radiusPixels / float(NUM_STEPS);

    float totalVisibility = 0.0;

    [unroll]
    for (int dir = 0; dir < NUM_DIRECTIONS; ++dir) {
        float phi = (PI / float(NUM_DIRECTIONS)) * float(dir) + rotAngle;
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);

        // Screen UV has +V = down, but view space has +Y = up.
        // XeGTAO: directionVec = (cosPhi, sinPhi, 0) in view space,
        //         omega = (cosPhi, -sinPhi) for screen-space stepping (Y flipped).
        float3 dirVS = float3(cosPhi, sinPhi, 0.0);
        float2 sliceDir = float2(cosPhi, -sinPhi);  // Y flipped for screen UV

        // Orthogonal decomposition in slice plane
        // orthoDir: component of dirVS perpendicular to viewVec (NOT normalized, like XeGTAO)
        float3 orthoDir = dirVS - dot(dirVS, viewVec) * viewVec;

        // axisVec: normal to the slice plane
        float3 axisVec = normalize(cross(dirVS, viewVec));

        // Project surface normal onto slice plane
        float3 projN = viewNormal - axisVec * dot(viewNormal, axisVec);
        float projNLen = length(projN);

        // Signed angle of projected normal from viewVec
        float signN = sign(dot(orthoDir, projN));
        float cosN = saturate(dot(projN / max(projNLen, 1e-6), viewVec));
        float n = signN * FastACos(cosN);
        float sinN = sin(n);

        // Initialize horizon cos at tangent plane boundaries (fully open = no occlusion)
        // cos(n + pi/2) = -sin(n) for positive search direction
        // cos(n - pi/2) =  sin(n) for negative search direction
        float horizonCos0 = -sinN;  // cos(n + HALF_PI)
        float horizonCos1 =  sinN;  // cos(n - HALF_PI)

        // Combined horizon search: both directions per step
        [unroll]
        for (int step = 1; step <= NUM_STEPS; ++step) {
            float stepF = float(step);

            // Positive direction
            {
                float2 sampleUV = input.texcoord + sliceDir * stepF * stepSize * texelSize;
                if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 &&
                    sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
                    int2 sc = int2(sampleUV * screenSize);
                    float sd = depthTex.Load(int3(sc, 0)).r;
                    float3 sp = ReconstructViewPos(sampleUV, sd);
                    float3 hv = sp - viewPos;
                    float hd = length(hv);
                    if (hd > 0.001) {
                        float shc = dot(hv / hd, viewVec);
                        // Distance falloff (squared, within aoRadius)
                        float falloff = saturate(1.0 - (hd / aoRadius) * (hd / aoRadius));
                        // Thin object heuristic: blend toward current horizon when far
                        shc = lerp(horizonCos0, shc, falloff);
                        horizonCos0 = max(horizonCos0, shc);
                    }
                }
            }

            // Negative direction
            {
                float2 sampleUV = input.texcoord - sliceDir * stepF * stepSize * texelSize;
                if (sampleUV.x >= 0.0 && sampleUV.x <= 1.0 &&
                    sampleUV.y >= 0.0 && sampleUV.y <= 1.0) {
                    int2 sc = int2(sampleUV * screenSize);
                    float sd = depthTex.Load(int3(sc, 0)).r;
                    float3 sp = ReconstructViewPos(sampleUV, sd);
                    float3 hv = sp - viewPos;
                    float hd = length(hv);
                    if (hd > 0.001) {
                        float shc = dot(hv / hd, viewVec);
                        float falloff = saturate(1.0 - (hd / aoRadius) * (hd / aoRadius));
                        shc = lerp(horizonCos1, shc, falloff);
                        horizonCos1 = max(horizonCos1, shc);
                    }
                }
            }
        }

        // Convert tracked cos to horizon angles (XeGTAO convention)
        // Positive search → negative angle; negative search → positive angle
        float h0 = -FastACos(horizonCos0);
        float h1 =  FastACos(horizonCos1);

        // Clamp to normal hemisphere
        h0 = n + max(h0 - n, -HALF_PI);
        h1 = n + min(h1 - n,  HALF_PI);

        // Visibility integral (Jimenez 2016, eq. from XeGTAO)
        float iarc0 = (-cos(2.0 * h0 - n) + cosN + 2.0 * h0 * sinN) * 0.25;
        float iarc1 = (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN) * 0.25;

        totalVisibility += projNLen * (iarc0 + iarc1);
    }

    totalVisibility /= float(NUM_DIRECTIONS);
    totalVisibility = saturate(totalVisibility);

    // Apply intensity: 1.0 = fully visible, 0.0 = fully occluded
    float ao = 1.0 - (1.0 - totalVisibility) * aoIntensity;
    ao = saturate(ao);

    // Multi-bounce approximation (Jimenez 2016)
    ao = ao + ao * (1.0 - ao) * 0.2;
    ao = saturate(ao);

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

    float result = ao * contactShadow;
    return float4(result, result, result, 1.0);
}
