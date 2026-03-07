// clouds.ps.hlsl — Physically-based volumetric cloud raymarching
//
// Algorithm: Beer-Lambert extinction + Henyey-Greenstein phase + powder scattering
// Noise: 3D hash-based fbm (no texture dependency — pure procedural)
// Reference: Guerrilla Games "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn" (SIGGRAPH 2015)
//            Schneider 2015, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn"
//            Andrew Schneider, Nathan Vos — SIGGRAPH 2017 "Nubis: Authoring Real-Time Cloudscapes"
//
// Root signature (shared):
//   b0: 16 root constants (CloudParams)
//   b2: CBV  (LightFrameCB — sun dir/color/intensity, camera pos, time)
//   b3: 16 root constants (invViewProj as float4x4)
//   t0: depth buffer (R24 or R32_FLOAT) — used to mask out solid geometry

static const float PI = 3.14159265359;

cbuffer CloudParams : register(b0) {
    float  coverage;        // [0..1] sky coverage (0=clear sky, 0.5=scattered, 1=overcast)
    float  density;         // extinction coefficient multiplier (0.05..0.5)
    float  cloudBase;       // world Y of cloud layer bottom (meters)
    float  cloudTop;        // world Y of cloud layer top (meters)
    float  windSpeed;       // cloud drift speed in m/s (typ. 5-20)
    float  windDirX;        // wind horizontal direction X (normalized)
    float  windDirZ;        // wind horizontal direction Z (normalized)
    float  absorption;      // single-scattering albedo (0=absorb, 0.9=bright clouds)
    float  ambientStrength; // ambient sky light contribution
    float  _pad0;
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
    // (remaining fields not needed for clouds)
};

cbuffer InvViewProjCB : register(b3) {
    float4x4 invViewProj;
};

// Depth buffer — sample to mask pixels where solid geometry exists
Texture2D    depthTex      : register(t0);
SamplerState pointSampler  : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// ============================================================================
// 3D Procedural Noise — hash-based value noise, no texture required
// [Inigo Quilez — https://iquilezles.org/articles/gradientnoise/]
// ============================================================================

float hash1(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.x + p.y) * p.z);
}

float noise3(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    // Quintic interpolation curve (C2-continuous)
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float n000 = hash1(i + float3(0,0,0)); float n100 = hash1(i + float3(1,0,0));
    float n010 = hash1(i + float3(0,1,0)); float n110 = hash1(i + float3(1,1,0));
    float n001 = hash1(i + float3(0,0,1)); float n101 = hash1(i + float3(1,0,1));
    float n011 = hash1(i + float3(0,1,1)); float n111 = hash1(i + float3(1,1,1));

    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

// 4-octave FBM for large cloud shape
float fbm_shape(float3 p) {
    float v = 0.0, a = 0.5;
    float3 pp = p;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        v  += a * noise3(pp);
        a  *= 0.5;
        pp *= 2.1;
    }
    return v;
}

// 2-octave high-frequency detail noise for cloud wisps
float fbm_detail(float3 p) {
    float v = 0.0, a = 0.5;
    float3 pp = p;
    [unroll]
    for (int i = 0; i < 2; ++i) {
        v  += a * noise3(pp);
        a  *= 0.5;
        pp *= 2.8;
    }
    return v;
}

// ============================================================================
// Cloud density field
// Returns volumetric density [0..1] at a world-space position.
// ============================================================================

float CloudDensity(float3 pos) {
    // Normalised height [0=base, 1=top]
    float h = saturate((pos.y - cloudBase) / max(cloudTop - cloudBase, 1.0));

    // Height gradient: anvil-top cumulus profile
    //   Fade in smoothly from base (0→0.15), full at mid (0.15→0.7), fade out at top (0.7→1)
    float heightGrad = smoothstep(0.0, 0.15, h) * smoothstep(1.0, 0.72, h);

    // Wind drift — animate noise position over time
    float2 wind = float2(windDirX, windDirZ) * windSpeed * time * 0.0004;

    // Sample large-scale cloud shape (scale: 1 unit ≈ 2500m)
    float3 shapePos = float3(pos.x * 0.0004 + wind.x,
                             pos.y * 0.00012,
                             pos.z * 0.0004 + wind.y);
    float shape = fbm_shape(shapePos);

    // Remap by coverage: threshold so higher coverage fills more sky
    float cloudMask = saturate((shape - (1.0 - coverage)) * 7.0) * heightGrad;

    // Erode edges with high-frequency detail noise
    [branch]
    if (cloudMask > 0.001) {
        float3 detailPos = float3(pos.x * 0.0014 + wind.x * 2.0,
                                  pos.y * 0.0006,
                                  pos.z * 0.0014 + wind.y * 2.0);
        float detail = fbm_detail(detailPos);
        // Erode: subtract detail at cloud edges, add in center
        float erosion = lerp(0.25, -0.1, cloudMask) * detail;
        cloudMask = saturate(cloudMask - erosion);
    }

    return cloudMask;
}

// ============================================================================
// Henyey-Greenstein phase function
// g > 0: forward-scattering (sun behind cloud — silver lining)
// g < 0: back-scattering
// ============================================================================
float HenyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(abs(1.0 + g2 - 2.0 * g * cosTheta), 1.5));
}

// Double HG lobe: strong forward + subtle backward (Guerrilla 2015 dual-lobe)
float Phase(float cosTheta) {
    return lerp(HenyeyGreenstein(cosTheta, 0.6), HenyeyGreenstein(cosTheta, -0.2), 0.35);
}

// ============================================================================
// Sun transmittance — 6-step light march toward the sun
// Returns how much sun light reaches the sample point through the cloud.
// ============================================================================
float SunTransmittance(float3 pos) {
    float3 sunToward = normalize(-lightDir);
    float shadow = 0.0;
    float3 lpos = pos;
    [unroll]
    for (int i = 1; i <= 6; ++i) {
        lpos += sunToward * float(i) * 120.0;  // 120m steps
        // Only sample if still inside cloud layer
        if (lpos.y >= cloudBase && lpos.y <= cloudTop)
            shadow += CloudDensity(lpos) * 120.0;
    }
    // Beer-Lambert: T = exp(-extinction * optical_depth)
    return exp(-shadow * density * 0.1);
}

// ============================================================================
// Main PS
// ============================================================================
float4 PSMain(PSInput input) : SV_TARGET {

    // ── Depth test — skip pixels covered by solid geometry ────────────────────
    // Depth buffer == 1.0 means sky (no geometry). Anything < 1.0 is solid.
    // Use a small epsilon to handle precision issues near scene objects.
    float sceneDepth = depthTex.Sample(pointSampler, input.uv).r;
    if (sceneDepth < 0.9999) return float4(0, 0, 0, 0);

    // ── Reconstruct world-space ray direction ─────────────────────────────────
    // NDC [−1,+1] from UV [0,1]
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);

    float4 nearH = mul(invViewProj, float4(ndc, 0.0, 1.0));
    float4 farH  = mul(invViewProj, float4(ndc, 1.0, 1.0));
    float3 nearW = nearH.xyz / nearH.w;
    float3 farW  = farH.xyz  / farH.w;
    float3 rayDir = normalize(farW - nearW);

    float3 rayOrigin = cameraWorldPos;

    // ── Intersect ray with cloud shell (horizontal slab) ─────────────────────
    // Skip rays pointing downward — no clouds below ground
    [branch]
    if (rayDir.y <= 0.0001) return float4(0, 0, 0, 0);

    float tStart = (cloudBase - rayOrigin.y) / rayDir.y;
    float tEnd   = (cloudTop  - rayOrigin.y) / rayDir.y;

    // Camera above cloud base: only happens if inside or above cloud layer
    if (rayOrigin.y > cloudBase) {
        tStart = 0.0;
        if (rayDir.y > 0.0) {
            tEnd = (cloudTop - rayOrigin.y) / rayDir.y;
        } else {
            // Looking down from within layer
            tEnd = (cloudBase - rayOrigin.y) / rayDir.y;
        }
    }
    if (tEnd <= tStart || tStart < 0.0) return float4(0, 0, 0, 0);

    // ── Adaptive step size ────────────────────────────────────────────────────
    static const int MAX_STEPS = 64;
    float pathLen  = min(tEnd - tStart, 25000.0);  // cap at 25km
    float stepSize = pathLen / float(MAX_STEPS);

    // ── Light phase ───────────────────────────────────────────────────────────
    float cosTheta = dot(rayDir, normalize(-lightDir));
    float phase    = Phase(cosTheta);

    // ── Ambient sky color for cloud shading ───────────────────────────────────
    // Simple gradient: slightly blue from sky, gray at base
    float3 skyTop    = float3(0.3, 0.55, 0.95);
    float3 skyHorizon = float3(0.55, 0.65, 0.8);
    float3 ambient   = lerp(skyHorizon, skyTop, saturate(rayDir.y * 2.0))
                       * ambientStrength * 0.4;

    // ── Raymarch ─────────────────────────────────────────────────────────────
    float3 totalScatter   = float3(0, 0, 0);
    float  transmittance  = 1.0;

    float t = tStart + stepSize * 0.5;  // jitter: start at half-step

    [loop]
    for (int i = 0; i < MAX_STEPS; ++i) {
        [branch]
        if (transmittance < 0.005) break;

        float3 pos = rayOrigin + rayDir * t;

        [branch]
        if (pos.y >= cloudBase && pos.y <= cloudTop) {
            float d = CloudDensity(pos);

            [branch]
            if (d > 0.001) {
                // Extinction per step
                float extinction = d * density;
                float stepT      = exp(-extinction * stepSize);

                // Sun contribution
                float sunT = SunTransmittance(pos);
                // Beer-Powder: adds bright silver lining at cloud edges
                // [Guerrilla 2015] — powder doubles scattering at high density
                float powder = 1.0 - exp(-extinction * stepSize * 2.0);

                float3 sunLight  = lightColor * lightIntensity * sunT * phase * powder * absorption;
                float3 ambLight  = ambient * d;

                // Accumulate: energy-conserving integration
                float3 Li = (sunLight + ambLight) / max(extinction, 1e-4);
                totalScatter += transmittance * (1.0 - stepT) * Li;
                transmittance *= stepT;
            }
        }

        t += stepSize;
    }

    float alpha = saturate(1.0 - transmittance);

    // Soft fade at near/far horizon to avoid hard edges
    float horizonFade = smoothstep(0.0, 0.05, rayDir.y);
    alpha *= horizonFade;

    return float4(totalScatter, alpha);
}
