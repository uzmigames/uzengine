// clouds.ps.hlsl — Physically-based volumetric cloud raymarching
//
// Reference: Guerrilla Games "Horizon Zero Dawn" (SIGGRAPH 2015)
//            Schneider & Vos "Nubis" (SIGGRAPH 2017)
//            Hillaire "Frostbite Volumetric Clouds" (SIGGRAPH 2015)
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
    float  maxSteps;       // quality-driven: 32/64/96/128
    float  debugMode;      // 0=off, 1=density, 2=steps, 3=transmittance, 4=lighting
    float  fogDensity;     // integrated volumetric fog density (0=disabled)
    float  fogHeightFalloff;
    float  fogBaseHeight;
    float  fogMaxOpacity;
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
// Improved 3D noise — Jarzynski & Olano 2020 hash, gradient noise
// ============================================================================

float3 hash33(float3 p) {
    // Two-stage hash with large primes for better distribution
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(p) * 43758.5453123);
}

// Gradient noise with quintic Hermite (C2 continuous — no mach bands)
float gnoise(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float3 ga = hash33(i + float3(0,0,0)) * 2.0 - 1.0;
    float3 gb = hash33(i + float3(1,0,0)) * 2.0 - 1.0;
    float3 gc = hash33(i + float3(0,1,0)) * 2.0 - 1.0;
    float3 gd = hash33(i + float3(1,1,0)) * 2.0 - 1.0;
    float3 ge = hash33(i + float3(0,0,1)) * 2.0 - 1.0;
    float3 gf = hash33(i + float3(1,0,1)) * 2.0 - 1.0;
    float3 gg = hash33(i + float3(0,1,1)) * 2.0 - 1.0;
    float3 gh = hash33(i + float3(1,1,1)) * 2.0 - 1.0;

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

// Worley (cellular) noise for cloud erosion — returns distance to closest point
float worley(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    float minDist = 1.0;

    [unroll]
    for (int x = -1; x <= 1; ++x)
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int z = -1; z <= 1; ++z) {
        float3 neighbor = float3(x, y, z);
        float3 point = hash33(i + neighbor);
        float3 diff = neighbor + point - f;
        minDist = min(minDist, dot(diff, diff));
    }
    return sqrt(minDist);
}

// 4-octave FBM for base cloud shape — remapped to [0,1]
float fbm4(float3 p) {
    float v  = 0.5   * gnoise(p);       p *= 2.03;
    v       += 0.25  * gnoise(p);       p *= 2.07;
    v       += 0.125 * gnoise(p);       p *= 2.01;
    v       += 0.0625 * gnoise(p);
    return v * 0.72 + 0.5;  // remap ~[-0.7,0.7] → [0,1]
}

// 2-octave detail noise for edge erosion
float detail_fbm(float3 p) {
    float v  = 0.5  * gnoise(p);  p *= 2.13;
    v       += 0.25 * gnoise(p);
    return v * 0.8 + 0.5;
}

// Low-frequency 2D weather map — spatially varies coverage
float weatherMap(float2 pos) {
    float3 p = float3(pos * 0.00008, 0.0);
    float v  = 0.5  * gnoise(p);  p *= 2.1;
    v       += 0.25 * gnoise(p);
    return saturate(v * 0.9 + 0.55);
}

// ============================================================================
// Cloud density
// ============================================================================

// Height gradient — cumulus/stratocumulus profile with smooth cosine falloff
float HeightGradient(float h) {
    // Smooth bottom ramp + cosine-shaped top falloff
    float bottom = smoothstep(0.0, 0.15, h);
    float top    = 1.0 - smoothstep(0.6, 1.0, h);
    // Slight density bump at anvil region (cumulus shape)
    float anvil  = saturate(1.0 - abs(h - 0.35) * 2.5) * 0.15;
    return bottom * top + anvil;
}

float CloudDensityFast(float3 pos, float2 wind) {
    float h = saturate((pos.y - cloudBase) / max(cloudTop - cloudBase, 1.0));
    float hGrad = HeightGradient(h);
    if (hGrad < 0.001) return 0.0;

    // Spatial coverage variation from weather map
    float weather = weatherMap(pos.xz + wind * 500.0);
    float localCoverage = coverage * weather;

    // Large-scale shape noise (4-octave FBM)
    float3 sp = float3(pos.xz * 0.00035 + wind, pos.y * 0.0003);
    float shape = fbm4(sp);

    // Worley erosion — creates puffy cumulus edges
    float3 wp = float3(pos.xz * 0.0006 + wind * 0.7, pos.y * 0.0005);
    float cellNoise = worley(wp);
    shape = shape - cellNoise * 0.15;

    // Coverage remap (Schneider 2015)
    float threshold = 1.0 - localCoverage;
    float cloudShape = saturate((shape - threshold) / max(localCoverage, 0.01));

    // Smooth Hermite remap to soften edges
    cloudShape = smoothstep(0.0, 1.0, cloudShape);

    return cloudShape * hGrad;
}

float CloudDensity(float3 pos, float2 wind) {
    float d = CloudDensityFast(pos, wind);
    if (d < 0.01) return 0.0;

    // Erode edges with 2-octave detail noise
    if (d < 0.8) {
        float3 dp = float3(pos.xz * 0.0015 + wind * 1.5, pos.y * 0.001);
        float detail = detail_fbm(dp);
        // Stronger erosion at cloud boundaries, weaker in core
        float erosionStrength = (1.0 - d) * 0.45;
        d = saturate(d - erosionStrength * detail);
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
    // Dual-lobe: 70% forward (g=0.6) + 30% backward (g=-0.3)
    return lerp(HG(cosTheta, 0.6), HG(cosTheta, -0.3), 0.3);
}

// ============================================================================
// Sun transmittance — 8 logarithmic steps for self-shadowing
// ============================================================================

float SunTransmittance(float3 pos, float2 wind) {
    float3 sunDir = normalize(-lightDir);
    float opticalDepth = 0.0;
    float stepLen = 60.0;

    [unroll]
    for (int i = 0; i < 8; ++i) {
        pos += sunDir * stepLen;
        if (pos.y >= cloudBase && pos.y <= cloudTop)
            opticalDepth += CloudDensityFast(pos, wind) * stepLen;
        stepLen *= 1.6;  // logarithmic spacing
    }
    return exp(-opticalDepth * density * 0.06);
}

// Multi-scattering approximation (Frostbite 2015)
// Approximates multiple bounces within dense clouds to prevent unnaturally dark interiors
float MultiScatter(float opticalDepth) {
    return (1.0 - exp(-2.0 * opticalDepth)) * 0.25;
}

// ============================================================================
// Blue noise jitter — R2 quasi-random sequence (Martin Roberts 2018)
// Better blue-noise distribution than IGN alone
// ============================================================================

float BlueNoiseJitter(float2 pixelPos, float frame) {
    // Spatial hash (Bayer-like offset per pixel)
    float spatial = frac(52.9829189 * frac(dot(pixelPos, float2(0.06711056, 0.00583715))));
    // R2 temporal sequence — irrational offset per frame
    float temporal = frac(frame * 0.7548776662466927);
    return frac(spatial + temporal);
}

// ============================================================================
// Volumetric fog — analytical height fog (Frostbite 2014)
// ============================================================================

float AnalyticalHeightFog(float3 rayOrigin, float3 rayEnd) {
    if (fogDensity < 0.0001) return 0.0;

    float3 viewDir = rayEnd - rayOrigin;
    float dist = length(viewDir);
    if (dist < 0.01) return 0.0;

    float3 dir = viewDir / dist;
    float rayDirY = dir.y;

    // Camera height relative to fog base
    float camRelHeight = rayOrigin.y - fogBaseHeight;

    // Frostbite analytical integral for exponential height fog
    float falloff = fogHeightFalloff;
    float fogAmount;

    if (abs(rayDirY * falloff) > 0.001) {
        // Standard case: ray has vertical component
        float a = fogDensity * exp(-camRelHeight * falloff);
        float b = rayDirY * falloff;
        fogAmount = a * (1.0 - exp(-dist * b)) / b;
    } else {
        // Near-horizontal ray: use linear approximation
        fogAmount = fogDensity * exp(-camRelHeight * falloff) * dist;
    }

    return saturate(fogAmount * fogMaxOpacity);
}

// ============================================================================
// Debug output helpers
// ============================================================================

float3 StepCountHeatmap(float steps, float maxS) {
    float t = saturate(steps / maxS);
    // Blue → Cyan → Green → Yellow → Red
    float3 c;
    if (t < 0.25)      c = lerp(float3(0,0,1), float3(0,1,1), t * 4.0);
    else if (t < 0.5)  c = lerp(float3(0,1,1), float3(0,1,0), (t - 0.25) * 4.0);
    else if (t < 0.75) c = lerp(float3(0,1,0), float3(1,1,0), (t - 0.5) * 4.0);
    else                c = lerp(float3(1,1,0), float3(1,0,0), (t - 0.75) * 4.0);
    return c;
}

// ============================================================================
// Main
// ============================================================================

float4 PSMain(PSInput input) : SV_TARGET {

    float sceneDepth = depthTex.Sample(pointSampler, input.uv).r;
    bool isSky = sceneDepth >= 0.9999;

    // Reconstruct world-space ray
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 nearH = mul(invViewProj, float4(ndc, 0.0, 1.0));
    float4 farH  = mul(invViewProj, float4(ndc, 1.0, 1.0));
    float3 nearW = nearH.xyz / nearH.w;
    float3 farW  = farH.xyz / farH.w;
    float3 rayDir = normalize(farW - nearW);
    float3 rayOrigin = cameraWorldPos;

    // For non-sky pixels, compute world position for fog
    float3 worldPos = rayOrigin;
    float  sceneDistance = 0.0;
    if (!isSky) {
        float4 clipPos = mul(invViewProj, float4(ndc, sceneDepth, 1.0));
        worldPos = clipPos.xyz / clipPos.w;
        sceneDistance = length(worldPos - rayOrigin);
    }

    // --- Volumetric Fog (applied to all non-sky pixels) ---
    float fogAmount = 0.0;
    float3 fogScatter = float3(0, 0, 0);
    if (!isSky && fogDensity > 0.0001) {
        fogAmount = AnalyticalHeightFog(rayOrigin, worldPos);

        // Inscattering from directional light
        float cosAngle = dot(rayDir, normalize(-lightDir));
        float phase = lerp(HG(cosAngle, 0.8), HG(cosAngle, -0.2), 0.25);
        fogScatter = lightColor * lightIntensity * phase * fogAmount * 0.5;
    }

    // --- Cloud Raymarching (sky pixels only) ---
    float3 totalScatter  = float3(0, 0, 0);
    float  transmittance = 1.0;
    float  debugStepCount = 0.0;
    float  debugDensityAccum = 0.0;
    float  debugLightAccum = 0.0;

    [branch]
    if (isSky && rayDir.y > 0.001) {
        // Ray-slab intersection with cloud layer
        float tStart = max((cloudBase - rayOrigin.y) / rayDir.y, 0.0);
        float tEnd   = (cloudTop  - rayOrigin.y) / rayDir.y;

        if (rayOrigin.y > cloudBase && rayOrigin.y < cloudTop) {
            tStart = 0.0;
        } else if (rayOrigin.y >= cloudTop) {
            // Above cloud layer looking down — skip
            tStart = tEnd + 1.0;
        }

        [branch]
        if (tEnd > tStart) {
            // Quality-driven step count
            int numSteps = clamp((int)maxSteps, 32, 128);
            float pathLen = min(tEnd - tStart, 35000.0);

            // Exponential step distribution: finer near entry, coarser at exit
            // ratio^numSteps = maxStep/minStep, solve for ratio
            float stepRatio = 1.0;
            if (numSteps > 32) {
                // For higher quality, use exponential distribution
                // minStep at entry, growing by ratio each iteration
                // Total path = minStep * (ratio^N - 1) / (ratio - 1)
                stepRatio = pow(3.0, 1.0 / (float)numSteps);  // ~3x range
            }

            // Compute base step size (geometric series sum)
            float baseStep;
            if (stepRatio > 1.001) {
                float geometricSum = (pow(stepRatio, (float)numSteps) - 1.0) / (stepRatio - 1.0);
                baseStep = pathLen / geometricSum;
            } else {
                baseStep = pathLen / (float)numSteps;
            }

            // Blue noise jitter — R2 quasi-random for smooth temporal accumulation
            float jitter = BlueNoiseJitter(input.position.xy, frameIndex);
            float t = tStart + baseStep * jitter;

            // Precompute lighting
            float cosTheta = dot(rayDir, normalize(-lightDir));
            float phase    = Phase(cosTheta);
            // Silver lining boost when looking near the sun through cloud edges
            float silverLining = (cosTheta > 0.95) ? (1.0 + pow(cosTheta, 64.0) * 3.0) : 1.0;
            float2 wind = float2(windDirX, windDirZ) * windSpeed * time * 0.0004;

            // Sky ambient (height-varying)
            float3 skyTop    = float3(0.35, 0.5, 0.85) * ambientStrength * 0.5;
            float3 skyBottom = float3(0.5, 0.55, 0.65) * ambientStrength * 0.3;

            // Accumulate optical depth for multi-scattering
            float totalOpticalDepth = 0.0;

            int emptySteps = 0;
            float currentStep = baseStep;

            [loop]
            for (int i = 0; i < 128; ++i) {  // max possible steps (compile-time)
                if (i >= numSteps) break;
                if (transmittance < 0.005) break;

                float3 pos = rayOrigin + rayDir * t;
                float d = CloudDensity(pos, wind);
                debugStepCount += 1.0;

                [branch]
                if (d > 0.001) {
                    emptySteps = 0;
                    float extinction = d * density;
                    float stepT = exp(-extinction * currentStep);

                    // Sun illumination with self-shadowing
                    float sunT = SunTransmittance(pos, wind);

                    // Powder effect (Schneider 2015) — depth-dependent
                    float powder = 1.0 - exp(-extinction * currentStep * 4.0);
                    powder = lerp(1.0, powder, 0.7);  // soften powder effect

                    // Height-based ambient variation
                    float h = saturate((pos.y - cloudBase) / max(cloudTop - cloudBase, 1.0));
                    float3 amb = lerp(skyBottom, skyTop, h);

                    // Multi-scattering approximation for dense interiors
                    totalOpticalDepth += extinction * currentStep;
                    float ms = MultiScatter(totalOpticalDepth);
                    float3 msLight = lightColor * lightIntensity * ms * absorption;

                    // Total light at this sample
                    float3 Li = lightColor * lightIntensity * sunT * phase * silverLining
                                * powder * absorption + amb + msLight;

                    // Energy-conserving integration:
                    // integral of (scatter * exp(-ext*t)) dt from 0 to stepSize
                    // = scatter * (1 - exp(-ext*step)) / ext
                    float3 scatter = Li * extinction;
                    float3 integScatter = scatter * (1.0 - stepT) / max(extinction, 1e-6);
                    totalScatter  += transmittance * integScatter;
                    transmittance *= stepT;

                    debugDensityAccum += d;
                    debugLightAccum += sunT * phase;

                    // Advance with current step size (exponential growth)
                    t += currentStep;
                    currentStep *= stepRatio;
                } else {
                    // Empty space — accelerate after consecutive misses
                    emptySteps++;
                    float skip = (emptySteps >= 2) ? currentStep * 2.0 : currentStep;
                    t += skip;
                    currentStep *= stepRatio;
                }
            }

            // Apply aerial perspective fog to distant clouds
            if (fogDensity > 0.0001) {
                float cloudDist = tStart;
                float aerialFog = saturate(1.0 - exp(-fogDensity * cloudDist * 0.3));
                float3 fogCol = float3(0.6, 0.65, 0.75) * lightIntensity * 0.3;
                totalScatter = lerp(totalScatter, fogCol * (1.0 - transmittance), aerialFog);
            }
        }
    }

    float alpha = saturate(1.0 - transmittance);

    // Soft horizon fade for clouds
    if (isSky) {
        alpha *= smoothstep(0.0, 0.04, rayDir.y);
    }

    // Encode fog in the output — alpha channel carries cloud opacity,
    // RGB carries cloud scattered light + fog contribution
    float3 finalColor = totalScatter;

    // For non-sky pixels, output fog blend info
    if (!isSky && fogAmount > 0.001) {
        float3 fogColor = float3(0.6, 0.65, 0.75) + fogScatter;
        finalColor = fogColor;
        alpha = fogAmount;
    }

    // --- Debug Visualization ---
    if (debugMode > 0.5 && isSky) {
        if (debugMode < 1.5) {
            // Mode 1: Density field (grayscale)
            float d = saturate(debugDensityAccum * 0.1);
            return float4(d, d, d, 1.0);
        } else if (debugMode < 2.5) {
            // Mode 2: Step count heatmap
            return float4(StepCountHeatmap(debugStepCount, maxSteps), 1.0);
        } else if (debugMode < 3.5) {
            // Mode 3: Transmittance (grayscale — white=transparent, black=opaque)
            return float4(transmittance, transmittance, transmittance, 1.0);
        } else {
            // Mode 4: Lighting contribution only
            float l = saturate(debugLightAccum * 0.05);
            return float4(l * lightColor, 1.0);
        }
    }

    return float4(finalColor, alpha);
}
