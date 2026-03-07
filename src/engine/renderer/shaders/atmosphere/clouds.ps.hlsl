// clouds.ps.hlsl — Physically-based volumetric cloud raymarching
//
// Reference: Guerrilla Games "Horizon Zero Dawn" (SIGGRAPH 2015)
//            Schneider & Vos "Nubis" (SIGGRAPH 2017)
//            UE5 VolumetricCloudRendering.usf
//
// Root signature (shared):
//   b0: 16 root constants (CloudParams)
//   b2: CBV  (LightFrameCB — sun dir/color/intensity, camera pos, time)
//   b3: 16 root constants (invViewProj as float4x4)
//   t0: depth buffer — used to mask out solid geometry

static const float PI = 3.14159265359;

cbuffer CloudParams : register(b0) {
    float  coverage;
    float  density;
    float  cloudBase;
    float  cloudTop;
    float  windSpeed;
    float  windDirX;
    float  windDirZ;
    float  absorption;
    float  ambientStrength;
    float  frameIndex;
    float  _pad1;
    float  _pad2;
    float  _pad3;
    float  _pad4;
    float  _pad5;
    float  _pad6;
};

cbuffer LightFrameCB : register(b2) {
    float3 lightDir;
    float  lightIntensity;
    float3 lightColor;
    float  _lightPad;
    float3 cameraWorldPos;
    float  time;
};

cbuffer InvViewProjCB : register(b3) {
    float4x4 invViewProj;
};

Texture2D    depthTex      : register(t0);
SamplerState pointSampler  : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// ============================================================================
// Improved 3D noise — better hash to eliminate banding
// Uses two-stage hash with large primes for better distribution
// [Jarzynski & Olano 2020, "Hash Functions for GPU Rendering"]
// ============================================================================

float3 hash33(float3 p) {
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(p) * 43758.5453123);
}

// Gradient noise — much smoother than value noise, eliminates grid banding
float gnoise(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    // Quintic Hermite for C2 continuity (eliminates mach bands)
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    // 8 gradient vectors at cube corners
    float3 ga = hash33(i + float3(0,0,0)) * 2.0 - 1.0;
    float3 gb = hash33(i + float3(1,0,0)) * 2.0 - 1.0;
    float3 gc = hash33(i + float3(0,1,0)) * 2.0 - 1.0;
    float3 gd = hash33(i + float3(1,1,0)) * 2.0 - 1.0;
    float3 ge = hash33(i + float3(0,0,1)) * 2.0 - 1.0;
    float3 gf = hash33(i + float3(1,0,1)) * 2.0 - 1.0;
    float3 gg = hash33(i + float3(0,1,1)) * 2.0 - 1.0;
    float3 gh = hash33(i + float3(1,1,1)) * 2.0 - 1.0;

    // Dot products with distance vectors
    float va = dot(ga, f - float3(0,0,0));
    float vb = dot(gb, f - float3(1,0,0));
    float vc = dot(gc, f - float3(0,1,0));
    float vd = dot(gd, f - float3(1,1,0));
    float ve = dot(ge, f - float3(0,0,1));
    float vf = dot(gf, f - float3(1,0,1));
    float vg = dot(gg, f - float3(0,1,1));
    float vh = dot(gh, f - float3(1,1,1));

    return lerp(lerp(lerp(va, vb, u.x), lerp(vc, vd, u.x), u.y),
                lerp(lerp(ve, vf, u.x), lerp(vg, vh, u.x), u.y), u.z);
}

// 3-octave FBM — remapped to [0,1] (gradient noise outputs ~[-0.7, 0.7])
float fbm3(float3 p) {
    float v  = 0.5  * gnoise(p);       p *= 2.03;
    v       += 0.25 * gnoise(p);       p *= 2.07;
    v       += 0.125 * gnoise(p);
    return v * 0.8 + 0.5;  // remap to ~[0,1]
}

// 1-octave detail
float detail_noise(float3 p) {
    return gnoise(p) * 0.5 + 0.5;
}

// ============================================================================
// Cloud density
// ============================================================================

// Height gradient — cumulus anvil profile (Schneider 2015)
float HeightGradient(float h) {
    // Rounded bottom (0→0.2), solid middle, anvil top falloff (0.65→1)
    return smoothstep(0.0, 0.2, h) * smoothstep(1.0, 0.65, h);
}

float CloudDensityFast(float3 pos, float2 wind) {
    float h = saturate((pos.y - cloudBase) / max(cloudTop - cloudBase, 1.0));
    float hGrad = HeightGradient(h);
    if (hGrad < 0.001) return 0.0;

    // Large-scale shape noise
    float3 sp = float3(pos.xz * 0.00035 + wind, pos.y * 0.0003);
    float shape = fbm3(sp);

    // Coverage remap: Schneider's remap function
    // remap(shape, 1-coverage, 1, 0, 1) — maps noise to cloud presence
    float threshold = 1.0 - coverage;
    float cloudShape = saturate((shape - threshold) / max(coverage, 0.01));

    return cloudShape * hGrad;
}

float CloudDensity(float3 pos, float2 wind) {
    float d = CloudDensityFast(pos, wind);
    if (d < 0.01) return 0.0;

    // Erode edges with detail noise (only at cloud boundaries)
    if (d < 0.7) {
        float3 dp = float3(pos.xz * 0.0015 + wind * 1.5, pos.y * 0.001);
        float detail = detail_noise(dp);
        float erosion = (1.0 - d) * 0.4 * detail;
        d = saturate(d - erosion);
    }
    return d;
}

// ============================================================================
// Henyey-Greenstein dual-lobe phase (Guerrilla 2015)
// ============================================================================

float HG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(abs(1.0 + g2 - 2.0 * g * cosTheta), 1.5));
}

float Phase(float cosTheta) {
    return lerp(HG(cosTheta, 0.6), HG(cosTheta, -0.3), 0.3);
}

// ============================================================================
// Sun transmittance — 6 exponential steps, shape-only density
// ============================================================================

float SunTransmittance(float3 pos, float2 wind) {
    float3 sunDir = normalize(-lightDir);
    float opticalDepth = 0.0;
    float stepLen = 80.0;

    [unroll]
    for (int i = 0; i < 6; ++i) {
        pos += sunDir * stepLen;
        if (pos.y >= cloudBase && pos.y <= cloudTop)
            opticalDepth += CloudDensityFast(pos, wind) * stepLen;
        stepLen *= 1.7;
    }
    return exp(-opticalDepth * density * 0.08);
}

// ============================================================================
// Temporal jitter — Interleaved Gradient Noise (Jimenez 2014)
// ============================================================================

float IGN(float2 px) {
    return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
}

// ============================================================================
// Main
// ============================================================================

float4 PSMain(PSInput input) : SV_TARGET {

    float sceneDepth = depthTex.Sample(pointSampler, input.uv).r;
    if (sceneDepth < 0.9999) return float4(0, 0, 0, 0);

    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 nearH = mul(invViewProj, float4(ndc, 0.0, 1.0));
    float4 farH  = mul(invViewProj, float4(ndc, 1.0, 1.0));
    float3 rayDir = normalize(farH.xyz / farH.w - nearH.xyz / nearH.w);
    float3 rayOrigin = cameraWorldPos;

    [branch]
    if (rayDir.y <= 0.001) return float4(0, 0, 0, 0);

    // Ray-slab intersection
    float tStart = max((cloudBase - rayOrigin.y) / rayDir.y, 0.0);
    float tEnd   = (cloudTop  - rayOrigin.y) / rayDir.y;

    if (rayOrigin.y > cloudBase && rayOrigin.y < cloudTop) {
        tStart = 0.0;
    } else if (rayOrigin.y >= cloudTop) {
        // Above cloud layer — only if looking down (which we already filtered)
        return float4(0, 0, 0, 0);
    }

    if (tEnd <= tStart) return float4(0, 0, 0, 0);

    // 32 steps with temporal jitter
    static const int MAX_STEPS = 32;
    float pathLen  = min(tEnd - tStart, 30000.0);
    float stepSize = pathLen / float(MAX_STEPS);

    float jitter = IGN(input.position.xy + frameIndex * 5.37);
    float t = tStart + stepSize * jitter;

    // Precompute
    float cosTheta = dot(rayDir, normalize(-lightDir));
    float phase    = Phase(cosTheta);
    float2 wind    = float2(windDirX, windDirZ) * windSpeed * time * 0.0004;

    float3 skyAmbient = lerp(float3(0.5, 0.6, 0.75), float3(0.35, 0.5, 0.85),
                             saturate(rayDir.y * 2.5)) * ambientStrength * 0.5;

    // March
    float3 totalScatter  = float3(0, 0, 0);
    float  transmittance = 1.0;
    int    emptySteps    = 0;

    [loop]
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (transmittance < 0.01) break;

        float3 pos = rayOrigin + rayDir * t;
        float d = CloudDensity(pos, wind);

        [branch]
        if (d > 0.001) {
            emptySteps = 0;
            float extinction = d * density;
            float stepT = exp(-extinction * stepSize);

            // Sun light + powder effect
            float sunT   = SunTransmittance(pos, wind);
            float powder = 1.0 - exp(-extinction * stepSize * 2.0);

            // Height-based ambient variation (darker at base, brighter at top)
            float h = saturate((pos.y - cloudBase) / max(cloudTop - cloudBase, 1.0));
            float3 amb = skyAmbient * lerp(0.4, 1.0, h);

            float3 Li = lightColor * lightIntensity * sunT * phase * powder * absorption + amb;
            totalScatter  += transmittance * (1.0 - stepT) * Li;
            transmittance *= stepT;

            t += stepSize;
        } else {
            // Empty space skipping: 2x step after 2 consecutive misses
            emptySteps++;
            t += (emptySteps >= 2) ? stepSize * 2.0 : stepSize;
        }
    }

    float alpha = saturate(1.0 - transmittance);
    // Soft horizon fade + distance fade
    alpha *= smoothstep(0.0, 0.06, rayDir.y);

    return float4(totalScatter, alpha);
}
