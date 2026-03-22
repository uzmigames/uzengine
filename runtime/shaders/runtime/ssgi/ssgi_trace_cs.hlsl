/// SSGI Trace Compute Shader — Screen-Space Global Illumination
///
/// Per-pixel screen-space ray marching for diffuse indirect lighting (one-bounce GI).
/// For each pixel, generates SSGI_RAY_COUNT cosine-hemisphere distributed rays,
/// marches them in screen space against the depth buffer, and on hit samples the
/// previous frame's reduced scene color to capture indirect lighting.
///
/// The algorithm follows the screen-space diffuse indirect pipeline:
/// 1. Load GBuffer depth + normal
/// 2. Generate quasi-random ray directions via Hammersley + cosine hemisphere
/// 3. March each ray in screen space (HiZ-like depth comparison)
/// 4. On hit: sample prev-frame color, weight by Karis for firefly suppression
/// 5. Average all rays, output indirect diffuse + AO mask
///
/// # Resources
/// - t0: Depth buffer (R32_FLOAT)
/// - t1: Normal+Material G-Buffer (R8G8B8A8_UNORM: rg=oct normal, b=metallic, a=roughness)
/// - t2: Prev-frame reduced color (R11G11B10F or RGBA16F, mip chain)
/// - u0: Indirect diffuse output (RGBA16F, half-res)
/// - u1: AO output (R16F, half-res)
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @ref Jimenez 2016 "Screen-Space Indirect Lighting" — SSGI trace pipeline
/// @ref McGuire & Mara 2014 "Efficient GPU Screen-Space Ray Tracing" — screen-space ray march
/// @ref Hammersley 1964 "Monte Carlo Methods" — low-discrepancy sampling
/// @ref Karis 2014 — luminance weighting for firefly suppression
/// @ref standard Monte Carlo sampling utilities — PCG hash, cosine hemisphere, tangent basis
/// @constants SSGI_RAY_COUNT=8 (Q2 quality)
///            SSGI_MARCH_STEPS=12
///            StartMipLevel=1.0
///            Karis weight=rcp(1+Luminance)
/// @deviations Uses simple linear screen-space march instead of full HiZ
///   hierarchical traversal. The core algorithm (ray generation, hit shading,
///   Karis weighting, accumulation) follows the standard SSGI approach.

#include "../include/math_util.hlsli"
#include "../include/octahedral.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Quality Defines
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 — Q2=8 rays for diffuse indirect
#ifndef SSGI_RAY_COUNT
#define SSGI_RAY_COUNT 8
#endif

/// @ref Jimenez 2016 — Q2=8 steps, Q4=12 steps
#ifndef SSGI_MARCH_STEPS
#define SSGI_MARCH_STEPS 12
#endif

/// Start at mip 1 to skip single-pixel noise in the depth pyramid.
#define SSGI_START_MIP_LEVEL 1.0f

/// Far depth sentinel. Reversed-Z: 0 = far plane.
#define FAR_DEPTH_VALUE 0.0f

#define PI 3.14159265358979323846f

/// Batched sample count for the screen-space march inner loop.
/// @ref Stachowiak 2015 — batch size 4 for march efficiency
#define SAMPLE_BATCH_SIZE 4

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 — SSGI dispatch parameter layout
cbuffer SSGIRootCB : register(b0) {
    uint  depthSrvIndex;       // DWORD 0 — depth buffer SRV (bindless)
    uint  normalSrvIndex;      // DWORD 1 — normal+material G-Buffer SRV (bindless)
    uint  prevColorSrvIndex;   // DWORD 2 — reduced prev-frame color SRV (bindless)
    uint  outputUavIndex;      // DWORD 3 — indirect diffuse output UAV (unused for register-bound)
    uint  aoOutputUavIndex;    // DWORD 4 — AO output UAV (unused for register-bound)
    uint  outputWidth;         // DWORD 5 — half-res output width
    uint  outputHeight;        // DWORD 6 — half-res output height
    uint  frameIndex;          // DWORD 7 — frame counter for temporal jitter
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1) — mirrors deferred_lighting_ps.hlsl layout
// ─────────────────────────────────────────────────────────────────────────────

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
    float    _pad_pf[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float>  depthBuffer    : register(t0);
Texture2D<float4> normalBuffer   : register(t1);
Texture2D<float4> prevColorTex   : register(t2);
#endif

#ifndef UZ_BINDLESS
RWTexture2D<float4> indirectOutput : register(u0);
RWTexture2D<float>  aoOutput       : register(u1);
#endif

SamplerState pointSampler  : register(s0);
SamplerState linearSampler : register(s2);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Sample hardware depth at given UV.
float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[depthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthBuffer.SampleLevel(pointSampler, uv, 0).r;
#endif
}

/// Sample normal + material from G-Buffer.
float4 SampleNormalMaterial(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[normalSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return normalBuffer.SampleLevel(pointSampler, uv, 0);
#endif
}

/// Sample previous frame reduced color at UV with mip level.
/// @ref Jimenez 2016 — sample prev-frame color at hit mip for indirect radiance
float3 SamplePrevColor(float2 uv, float mipLevel) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[prevColorSrvIndex];
    float3 c = tex.SampleLevel(linearSampler, uv, mipLevel).rgb;
#else
    float3 c = prevColorTex.SampleLevel(linearSampler, uv, mipLevel).rgb;
#endif
    // Sanitize NaN/negative (standard NaN-safe clamp pattern)
    return -min(-c, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth & Position Reconstruction
// ─────────────────────────────────────────────────────────────────────────────

/// Linearize hardware depth to view-space Z distance (reversed-Z).
/// deviceZ=1 at near plane, deviceZ=0 at far plane.
float LinearizeDepth(float deviceZ) {
    return nearPlane * farPlane / (nearPlane + deviceZ * (farPlane - nearPlane));
}

/// Reconstruct world-space position from UV + device depth.
float3 ReconstructWorldPos(float2 uv, float deviceZ) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, deviceZ, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Luminance
// ─────────────────────────────────────────────────────────────────────────────

/// Rec. 709 luminance.
/// @ref ITU-R BT.709 — standard luminance coefficients (0.2126, 0.7152, 0.0722)
float Luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Random Number Generation
// ─────────────────────────────────────────────────────────────────────────────

/// 3D PCG hash for per-pixel random seed.
/// @ref O'Neill 2014 "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation"
/// @constants 1664525u, 1013904223u, 16u shift (PCG standard multiplier/increment)
uint3 Rand3DPCG16(int3 p) {
    uint3 v = (uint3)p;
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v >> 16u;
}

/// Radical inverse (van der Corput sequence) for Hammersley.
/// @ref Hammersley 1964 "Monte Carlo Methods" — radical inverse base-2 via bit reversal
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

/// Hammersley quasi-random 2D sequence with per-pixel random offset.
/// @ref Hammersley 1964 "Monte Carlo Methods" — low-discrepancy 2D sequence
/// @constants Uses RadicalInverse_VdC for y, linear for x
float2 Hammersley(uint i, uint N, uint3 randomSeed) {
    float x = frac((float)i / (float)N + float(randomSeed.x & 0xFFFFu) / 65536.0f);
    float y = frac(RadicalInverse_VdC(i) + float(randomSeed.y & 0xFFFFu) / 65536.0f);
    return float2(x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cosine-Weighted Hemisphere Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Cosine-weighted hemisphere sample using concentric disk mapping.
/// Returns a direction in tangent space (Z-up = surface normal direction).
/// @ref Shirley & Chiu 1997 "A Low Distortion Map Between Disk and Square" — concentric disk mapping
float3 CosineSampleHemisphere(float2 E) {
    // Concentric disk mapping: map [0,1]^2 to unit disk preserving area
    float2 p = 2.0f * E - 1.0f;
    float2 diskPoint;

    // Handle degeneracy at origin
    if (abs(p.x) < 1e-8f && abs(p.y) < 1e-8f) {
        diskPoint = float2(0.0f, 0.0f);
    } else {
        float r, theta;
        if (abs(p.x) > abs(p.y)) {
            r = p.x;
            theta = (PI / 4.0f) * (p.y / p.x);
        } else {
            r = p.y;
            theta = (PI / 2.0f) - (PI / 4.0f) * (p.x / p.y);
        }
        diskPoint = r * float2(cos(theta), sin(theta));
    }

    // Project disk to hemisphere: z = sqrt(1 - r^2)
    // This gives a cosine-weighted distribution on the hemisphere.
    float z = sqrt(max(0.0f, 1.0f - dot(diskPoint, diskPoint)));
    return float3(diskPoint.x, diskPoint.y, z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tangent Basis Construction
// ─────────────────────────────────────────────────────────────────────────────

/// Build an orthonormal tangent basis from a normal vector.
/// @ref Frisvad 2012 "Building an Orthonormal Basis from a 3D Unit Vector Without Normalization"
float3x3 BuildTangentBasis(float3 N) {
    float3 T;
    // Frisvad's method — numerically stable for all orientations
    if (N.z < -0.9999f) {
        T = float3(0.0f, -1.0f, 0.0f);
    } else {
        float a = 1.0f / (1.0f + N.z);
        float b = -N.x * N.y * a;
        T = float3(1.0f - N.x * N.x * a, b, -N.x);
    }
    float3 B = cross(N, T);
    // Returns matrix where row 0 = T, row 1 = B, row 2 = N
    // mul(tangentDir, basis) transforms tangent-space to world-space
    return float3x3(T, B, N);
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen-Space Ray March (Linear — simplified from HiZ)
// ─────────────────────────────────────────────────────────────────────────────

/// Screen-space ray structure.
struct SSRTRay {
    float3 RayStartUVz;  // UV-space start (xy in [0,1], z = device depth)
    float3 RayStepUVz;   // UV-space step per iteration
};

/// Initialize a screen-space ray from world-space origin and direction.
/// Projects start and end to UV space, clips to screen edges.
/// @ref McGuire & Mara 2014 — world-to-screen ray initialization
SSRTRay InitScreenSpaceRay(
    float3 worldOrigin,
    float3 worldDir,
    float  maxWorldDist)
{
    float3 rayEnd = worldOrigin + worldDir * maxWorldDist;

    // Project start and end to clip space
    float4 startClip = mul(float4(worldOrigin, 1.0f), viewProj);
    float4 endClip   = mul(float4(rayEnd, 1.0f), viewProj);

    // Skip rays where end point is behind the camera (w <= 0)
    float3 startNDC = startClip.xyz / max(startClip.w, 0.0001f);
    float3 endNDC   = endClip.xyz / max(endClip.w, 0.0001f);

    // Convert NDC to UV space: x: [-1,1] -> [0,1], y: [-1,1] -> [1,0] (flipped)
    float3 startUVz = float3(startNDC.xy * float2(0.5f, -0.5f) + 0.5f, startNDC.z);
    float3 endUVz   = float3(endNDC.xy   * float2(0.5f, -0.5f) + 0.5f, endNDC.z);

    float3 stepUVz = endUVz - startUVz;

    // Clip ray to screen edges [0,1] — prevent UV from going off-screen
    float tClip = 1.0f;
    [unroll]
    for (int i = 0; i < 2; i++) {
        if (abs(stepUVz[i]) > 1e-8f) {
            // Clip to UV bounds [0, 1]
            float t0 = (0.0f - startUVz[i]) / stepUVz[i];
            float t1 = (1.0f - startUVz[i]) / stepUVz[i];
            // We want the smallest positive t that clips the ray
            float tMax = max(t0, t1);
            if (tMax > 0.0f) {
                tClip = min(tClip, tMax);
            }
        }
    }
    stepUVz *= saturate(tClip);

    SSRTRay ray;
    ray.RayStartUVz = startUVz;
    ray.RayStepUVz  = stepUVz;
    return ray;
}

/// March a ray through screen space with linear depth comparison.
/// Returns hit UV+depth, whether a hit was found, and the march level (for mip selection).
/// @ref McGuire & Mara 2014 — screen-space ray march with depth comparison
/// @constants CompareTolerance = step.z * tolerance
void CastScreenSpaceRay(
    SSRTRay ray,
    uint    numSteps,
    float   stepOffset,
    out float3 outHitUVz,
    out float  outMipLevel,
    out bool   bHit)
{
    float3 stepUVz = ray.RayStepUVz / (float)numSteps;
    float3 currentUVz = ray.RayStartUVz + stepUVz * stepOffset;

    // Compare tolerance: proportional to the depth range per step
    float compareTolerance = abs(stepUVz.z) * 2.0f;

    bHit = false;
    outHitUVz = float3(0.0f, 0.0f, 0.0f);
    outMipLevel = 0.0f;

    float lastDepthDiff = 0.0f;

    [loop]
    for (uint i = 0; i < numSteps; i++) {
        currentUVz += stepUVz;

        // Bounds check — skip if ray exits screen
        if (any(currentUVz.xy < 0.0f) || any(currentUVz.xy > 1.0f)) {
            break;
        }

        // Sample scene depth at current ray UV
        float sceneDepth = SampleDepth(currentUVz.xy);

        // Skip sky pixels (depth = 0 in reversed-Z = far plane)
        if (sceneDepth == FAR_DEPTH_VALUE) {
            lastDepthDiff = currentUVz.z - sceneDepth;
            continue;
        }

        // Depth comparison: ray depth vs scene depth
        float depthDiff = currentUVz.z - sceneDepth;

        // Hit: ray is behind the surface (positive diff) but within tolerance
        if (abs(depthDiff + compareTolerance) < compareTolerance) {
            bHit = true;

            // Linear interpolation for sub-step refinement
            float timeLerp = saturate(lastDepthDiff / (lastDepthDiff - depthDiff));
            outHitUVz = currentUVz - stepUVz * (1.0f - timeLerp);

            // Mip level increases with ray distance for coarser sampling at range
            outMipLevel = SSGI_START_MIP_LEVEL + (float)i * (4.0f / (float)numSteps);
            return;
        }

        lastDepthDiff = depthDiff;
    }

    // No hit — output end-of-ray position
    outHitUVz = currentUVz;
    outMipLevel = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen-Edge Vignette
// ─────────────────────────────────────────────────────────────────────────────

/// Fade at screen edges to avoid artifacts from missing data.
/// @ref Stachowiak 2015 — screen-edge vignette
/// @constants 5, 4 (vignette ramp parameters)
float ComputeHitVignette(float2 hitUV) {
    float2 screenPos = hitUV * 2.0f - 1.0f;
    float2 vignette = saturate(abs(screenPos) * 5.0f - 4.0f);
    return saturate(1.0f - dot(vignette, vignette));
}

// ─────────────────────────────────────────────────────────────────────────────
// Interleaved Gradient Noise
// ─────────────────────────────────────────────────────────────────────────────

/// Per-pixel temporal noise for ray offset jitter.
/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty: Advanced Warfare"
/// @constants 52.9829189, 0.06711056, 0.00583715
float InterleavedGradientNoise(float2 pixelPos, float frame) {
    pixelPos += frame * float2(47.0f, 17.0f) * 0.695f;
    return frac(52.9829189f * frac((pixelPos.x * 0.06711056f) + (pixelPos.y * 0.00583715f)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 — SSGI main dispatch, one thread per half-res pixel
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    // Bounds check — dispatch covers half-res output
    if (dispatchThreadId.x >= outputWidth || dispatchThreadId.y >= outputHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 uv = pixelCenter / float2((float)outputWidth, (float)outputHeight);

    // ── Read G-Buffer ──
    // For half-res SSGI, sample at the center of the corresponding full-res 2x2 block.
    float deviceZ = SampleDepth(uv);

    // Skip sky pixels (reversed-Z: 0 = far plane)
    if (deviceZ <= FAR_DEPTH_VALUE) {
#ifdef UZ_BINDLESS
        RWTexture2D<float4> outColor = ResourceDescriptorHeap[outputUavIndex];
        RWTexture2D<float>  outAO   = ResourceDescriptorHeap[aoOutputUavIndex];
        outColor[dispatchThreadId.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        outAO[dispatchThreadId.xy]    = 1.0f; // Fully unoccluded for sky
#else
        indirectOutput[dispatchThreadId.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        aoOutput[dispatchThreadId.xy] = 1.0f; // Fully unoccluded for sky
#endif
        return;
    }

    float4 normalMaterial = SampleNormalMaterial(uv);
    float3 worldNormal = OctDecode(normalMaterial.rg);
    float3 worldPos = ReconstructWorldPos(uv, deviceZ);
    float  sceneDepth = LinearizeDepth(deviceZ);

    // ── Per-pixel random seed (PCG hash of pixel position + temporal frame index) ──
    uint3 randomSeed = Rand3DPCG16(int3(dispatchThreadId.xy, frameIndex & 7u));

    // ── Build tangent basis from surface normal ──
    float3x3 tangentBasis = BuildTangentBasis(worldNormal);

    // ── Step offset for temporal jitter ──
    float stepOffset = InterleavedGradientNoise(pixelCenter, fmod((float)frameIndex, 8.0f));

    // ── Max trace distance: proportional to scene depth, capped ──
    // Cap prevents ground pixels far from camera from tracing excessively far,
    // which increases false hits on sky-adjacent silhouette edges.
    /// @ref Jimenez 2016 — practical trace distance clamping for SSGI
    /// @constants 2000.0 cm = 20 m max trace distance
    float maxTraceDist = min(sceneDepth, 2000.0f);

    // ── Trace rays ──
    float3 totalColor = float3(0.0f, 0.0f, 0.0f);
    float  totalWeight = 0.0f;
    uint   hitCount = 0;

    [loop]
    for (uint rayIdx = 0; rayIdx < SSGI_RAY_COUNT; rayIdx++) {
        // Generate quasi-random 2D sample point
        float2 E = Hammersley(rayIdx, SSGI_RAY_COUNT, randomSeed);

        // Convert to cosine-hemisphere direction in tangent space
        float3 tangentDir = CosineSampleHemisphere(E);

        // Transform to world space
        float3 worldDir = mul(tangentDir, tangentBasis);

        // Ensure ray points away from surface (should always be true for cosine hemisphere
        // but guard against degenerate normals)
        if (dot(worldDir, worldNormal) < 0.0f) {
            worldDir = -worldDir;
        }

        // Initialize screen-space ray
        SSRTRay ray = InitScreenSpaceRay(worldPos, worldDir, maxTraceDist);

        // March the ray
        float3 hitUVz;
        float  hitMipLevel;
        bool   bHit;
        CastScreenSpaceRay(ray, SSGI_MARCH_STEPS, stepOffset + 0.1f * (float)rayIdx, hitUVz, hitMipLevel, bHit);

        [branch]
        if (bHit) {
            // Post-hit sky rejection: the march depth comparison can pass for
            // pixels at the sky boundary (silhouette edges) where bilinear
            // sampling bleeds sky luminance into the hit color. Re-sample depth
            // at the refined hit UV and reject if it lands on the far plane.
            float hitDepth = SampleDepth(hitUVz.xy);
            if (hitDepth <= FAR_DEPTH_VALUE + 0.001f) {
                continue; // Sky pixel — treat as miss
            }

            // Screen-edge vignette
            float vignette = ComputeHitVignette(hitUVz.xy);

            if (vignette > 0.0f) {
                // Sample previous frame color at hit point
                float3 hitColor = SamplePrevColor(hitUVz.xy, hitMipLevel);

                // Clamp extreme luminance to prevent sky-bright samples from
                // dominating indirect lighting via bilinear bleed at silhouette
                // edges. This is a secondary guard beyond the depth rejection above.
                /// @ref Karis 2014 — luminance clamping for firefly suppression
                /// @constants 2.0 max luminance (conservative cap for indirect)
                float hitLum = Luminance(hitColor);
                if (hitLum > 2.0f) {
                    hitColor *= 2.0f / hitLum;
                }

                // Karis luminance weighting for firefly suppression
                float karisWeight = rcp(1.0f + Luminance(hitColor));

                totalColor  += hitColor * karisWeight * vignette;
                totalWeight += karisWeight * vignette;
                hitCount++;
            } else {
                // Vignette killed this sample — count as miss
            }
        }
    }

    // ── Resolve: average all rays and undo Karis weighting ──
    float3 indirectDiffuse = float3(0.0f, 0.0f, 0.0f);
    if (totalWeight > 0.0f) {
        // Undo Karis weighting: divide by total weight normalizes back to
        // the Karis-weighted average, then the inverse Karis is implicit
        // in the division (we accumulated color * w / sum(w) which is the
        // weighted mean in Karis space).
        indirectDiffuse = totalColor / totalWeight;
    }

    // ── AO output: fraction of rays that hit = occlusion (inverted: 1=unoccluded) ──
    float ao = 1.0f - (float)hitCount / (float)SSGI_RAY_COUNT;

    // Write outputs
#ifdef UZ_BINDLESS
    RWTexture2D<float4> outColor = ResourceDescriptorHeap[outputUavIndex];
    RWTexture2D<float>  outAO   = ResourceDescriptorHeap[aoOutputUavIndex];
    outColor[dispatchThreadId.xy] = float4(indirectDiffuse, 1.0f);
    outAO[dispatchThreadId.xy]    = ao;
#else
    indirectOutput[dispatchThreadId.xy] = float4(indirectDiffuse, 1.0f);
    aoOutput[dispatchThreadId.xy] = ao;
#endif
}
