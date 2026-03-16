/// Sky Cubemap Capture Compute Shader — Renders atmosphere into a cubemap for IBL.
///
/// For each texel of a 64x64x6 cubemap (RWTexture2DArray), computes sky radiance
/// by ray-marching through the atmosphere using the same Hillaire 2020 single-scatter
/// model as sky_atmosphere_ps.hlsl, but outputs to a cubemap face array instead of
/// screen pixels.
///
/// Output is LINEAR HDR (no tonemap, no gamma) -- the IBL baker expects linear input.
/// The sun disk is intentionally excluded to avoid baking a point-source specular
/// highlight into the IBL cubemap (specular sun should come from direct lighting).
///
/// # Coordinate Convention
/// - Z-up world space: planet center at origin, surface at Z = bottomRadiusKm
/// - CubeFaceDirection() returns DX convention (Y-up), so we swizzle Y<->Z
///   to convert cubemap directions into the atmosphere's Z-up coordinate system
///
/// # Resources
/// - b0: Root constants (8 DWORDs) -- atmosphere radii, scale heights, sun direction
/// - u0: RWTexture2DArray<float4> output cubemap (64x64x6, RGBA16F)
/// - Transmittance LUT accessed via bindless ResourceDescriptorHeap[transmittanceLutIdx]
///
/// # Dispatch
/// [8, 8, 6] thread groups of [8, 8, 1] = 64x64 per face x 6 faces.

#include "../include/ibl.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants — 8 DWORDs (32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Packs atmosphere geometry, sun direction, and the bindless LUT index into
/// exactly 8 DWORDs. Scattering coefficients are hardcoded below as standard
/// Earth atmosphere values (matching the transmittance LUT bake).
cbuffer SkyCubemapParams : register(b0) {
    float bottomRadiusKm;       // DWORD 0: planet surface radius (km)
    float topRadiusKm;          // DWORD 1: top-of-atmosphere radius (km)
    float sunDirX;              // DWORD 2: sun direction X
    float sunDirY;              // DWORD 3: sun direction Y
    float sunDirZ;              // DWORD 4: sun direction Z
    float sunIntensity;         // DWORD 5: sun illuminance (uniform RGB, from DirectionalLight)
    uint  transmittanceLutIdx;  // DWORD 6: bindless SRV index for transmittance LUT
    uint  packedScatterScales;  // DWORD 7: packed f16 pair — low16 = rayleighScale, high16 = mieScale
};

// ─────────────────────────────────────────────────────────────────────────────
// Output UAV
// ─────────────────────────────────────────────────────────────────────────────

RWTexture2DArray<float4> outputCubemap : register(u0);

/// s0 = bilinear wrap sampler (D3D12_FILTER_MIN_MAG_MIP_LINEAR).
SamplerState linearSampler : register(s0);

/// SM5 non-bindless: transmittance LUT bound at t0 via SetFragmentTexture(0).
#ifndef UZ_BINDLESS
Texture2D<float4> g_TransmittanceLut : register(t0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Standard Earth Atmosphere Constants
// ─────────────────────────────────────────────────────────────────────────────
//
// These are the canonical Bruneton 2017 / UE5 values for Earth's atmosphere.
// They match the coefficients used in transmittance_lut_cs.hlsl and
// sky_atmosphere_ps.hlsl (via SkyAtmosphereCB). Hardcoding them here avoids
// exceeding the 8 DWORD root constant budget while keeping physical accuracy.

/// Rayleigh scattering coefficients at sea level (km^-1).
/// Wavelength-dependent: blue scatters ~6x more than red.
static const float3 RAYLEIGH_BETA = float3(5.802e-3f, 13.558e-3f, 33.1e-3f);

/// Mie scattering coefficient at sea level (km^-1).
/// Wavelength-independent (aerosol scattering).
static const float MIE_BETA = 3.996e-3f;

/// Mie extinction-to-scattering ratio (absorption included).
/// Mie extinction = MIE_BETA * MIE_EXTINCTION_RATIO.
static const float MIE_EXTINCTION_RATIO = 1.11f;

/// Henyey-Greenstein anisotropy parameter for Mie phase function.
/// 0.8 = strong forward scattering (typical for atmospheric aerosols).
static const float MIE_ANISOTROPY = 0.8f;

/// Rayleigh and Mie scale heights — hardcoded Earth defaults (rarely change).
static const float RAYLEIGH_SCALE_H = 8.0f;   // km
static const float MIE_SCALE_H      = 1.2f;   // km

/// Ground albedo for Lambertian reflection when view ray hits the planet.
/// Dark earth-tone average (~0.3).
static const float3 GROUND_ALBEDO = float3(0.3f, 0.3f, 0.3f);

// ─────────────────────────────────────────────────────────────────────────────
// Integration Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Output cubemap face resolution. Must match the texture allocation on the CPU side.
static const uint CUBEMAP_FACE_SIZE = 64u;

/// Number of ray-march steps through the atmosphere per texel.
/// 16 steps is sufficient for low-frequency IBL capture (not screen-space).
static const int NUM_STEPS = 16;

/// Small epsilon to prevent division by zero in analytical integration.
static const float EXT_EPS = 1e-7f;

/// Safety margin: place camera at least 5m above ground in sky space (UE5 pattern).
static const float PLANET_RADIUS_OFFSET_KM = 0.005f;

// ─────────────────────────────────────────────────────────────────────────────
// Atmosphere Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Rayleigh phase function: (3 / 16 PI)(1 + cos^2 theta).
/// Symmetric forward/backward scattering from molecules much smaller than
/// the wavelength of light.
float RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase function for Mie scattering.
/// Models forward-peaked scattering from aerosol particles (~wavelength size).
///
/// # Parameters
/// - cosTheta: cosine of angle between view and light direction.
/// - g: anisotropy parameter in (-1, 1). Positive = forward scattering.
float MiePhase(float cosTheta, float g) {
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

/// Exponential density profile: exp(-altitude / scaleHeight).
/// Models how atmospheric density decreases with altitude for both
/// Rayleigh (molecular) and Mie (aerosol) scattering.
float DensityAtAltitude(float altitudeKm, float scaleHeightKm) {
    return exp(-max(0.0f, altitudeKm) / scaleHeightKm);
}

/// Ray-sphere intersection. Returns (tNear, tFar).
/// tNear < 0 means ray origin is inside sphere.
/// Returns (-1, -1) if no intersection.
float2 RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius) {
    float3 oc   = origin - center;
    float  b    = dot(oc, dir);
    float  c    = dot(oc, oc) - radius * radius;
    float  disc = b * b - c;
    if (disc < 0.0f) return float2(-1.0f, -1.0f);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transmittance LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Maps (viewHeight, viewZenithCosAngle) to UV for the transmittance LUT.
///
/// Uses Bruneton 2017 non-linear parameterization that concentrates precision
/// near the horizon where transmittance gradients are steepest. Must match
/// the parameterization in transmittance_lut_cs.hlsl exactly.
void TransmittanceLutParamsToUv(float viewHeight, float viewZenithCosAngle,
    float bRadius, float tRadius, out float2 uv)
{
    float H   = sqrt(max(0.0f, tRadius * tRadius - bRadius * bRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bRadius * bRadius));

    float discriminant = viewHeight * viewHeight
        * (viewZenithCosAngle * viewZenithCosAngle - 1.0f)
        + tRadius * tRadius;
    float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(max(0.0f, discriminant))));

    float dMin = tRadius - viewHeight;
    float dMax = rho + H;
    float uU   = (d - dMin) / (dMax - dMin);
    float uV   = rho / H;

    uv = float2(uU, uV);
}

/// Samples the precomputed transmittance LUT for a given position and direction.
///
/// Computes the UV from the sample point's height and the cosine of the angle
/// between the position vector and the query direction (typically the sun dir).
///
/// # Parameters
/// - lut: precomputed 256x64 RGBA16F transmittance texture.
/// - samp: bilinear sampler for smooth interpolation.
/// - samplePos: position in sky-space (km), relative to planet center at origin.
/// - queryDir: normalized direction to look up (typically sunDir).
/// - bRadius: planet surface radius (km).
/// - tRadius: top-of-atmosphere radius (km).
///
/// # Returns
/// RGB transmittance in [0,1]^3.
float3 SampleTransmittanceLut(Texture2D<float4> lut, SamplerState samp,
    float3 samplePos, float3 queryDir,
    float bRadius, float tRadius)
{
    float viewHeight = length(samplePos);
    float viewZenithCosAngle = dot(normalize(samplePos), queryDir);

    float2 uv;
    TransmittanceLutParamsToUv(viewHeight, viewZenithCosAngle, bRadius, tRadius, uv);
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Direction Helper (Z-up Adaptation)
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a cubemap face index + texel UV to a Z-up world-space direction.
///
/// CubeFaceDirection() (from ibl.hlsli) returns directions in DX convention
/// where Y is up. The atmosphere model uses Z-up, so we swizzle the result:
///   DX (X, Y, Z) -> Z-up (X, Z, Y)
///
/// This ensures that the "up" face of the cubemap (face 2, +Y in DX) maps to
/// the zenith direction (0, 0, 1) in the atmosphere's coordinate system, and
/// the horizon band of the cubemap correctly aligns with the atmosphere horizon.
///
/// # Parameters
/// - faceIndex: cubemap face [0, 5] in DX convention.
/// - uv: normalized UV [0, 1]^2 within that face.
///
/// # Returns
/// Normalized world-space direction in Z-up coordinates.
float3 CubeFaceDirectionZUp(uint faceIndex, float2 uv) {
    float3 dxDir = CubeFaceDirection(faceIndex, uv);
    // Swizzle: DX Y-up (x, y, z) -> atmosphere Z-up (x, z, y)
    return normalize(float3(dxDir.x, dxDir.z, dxDir.y));
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 texel     = dispatchThreadId.xy;
    uint  faceIndex = dispatchThreadId.z;

    // Out-of-bounds guard (dispatch covers exactly 64x64x6, but guard anyway)
    if (texel.x >= CUBEMAP_FACE_SIZE || texel.y >= CUBEMAP_FACE_SIZE || faceIndex >= 6u)
        return;

    // ── Compute world-space ray direction (Z-up) ────────────────────────────
    float2 uv     = (float2(texel) + 0.5f) / float(CUBEMAP_FACE_SIZE);
    float3 rayDir = CubeFaceDirectionZUp(faceIndex, uv);

    // ── Camera position in sky-space (km) ───────────────────────────────────
    // Camera is on the planet surface looking outward.
    // Z-up: planet center at origin, camera at (0, 0, bottomRadiusKm + offset).
    float3 cameraSkyKm = float3(0.0f, 0.0f, bottomRadiusKm + PLANET_RADIUS_OFFSET_KM);

    // ── Intersect ray with atmosphere top ───────────────────────────────────
    float2 atmoHit = RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), topRadiusKm);
    if (atmoHit.y < 0.0f) {
        outputCubemap[uint3(texel, faceIndex)] = float4(0, 0, 0, 1);
        return;
    }

    float tStart = max(atmoHit.x, 0.0f);
    float tEnd   = atmoHit.y;

    // Clip to planet surface if ray hits ground
    float2 groundHit = RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), bottomRadiusKm);
    bool   hitsGround = (groundHit.x > 0.0f);
    if (hitsGround) {
        tEnd = min(tEnd, groundHit.x);
    }

    float stepLenKm = (tEnd - tStart) / float(NUM_STEPS);
    if (stepLenKm <= 0.0f) {
        outputCubemap[uint3(texel, faceIndex)] = float4(0, 0, 0, 1);
        return;
    }

    // ── Sun direction and phase functions ───────────────────────────────────
    float3 sunDir   = normalize(float3(sunDirX, sunDirY, sunDirZ));
    float  cosTheta = dot(rayDir, sunDir);
    float  phaseR   = RayleighPhase(cosTheta);
    float  phaseM   = MiePhase(cosTheta, MIE_ANISOTROPY);

    // ── Unpack artistic scattering scale factors from packed f16 pair ───────
    // low 16 bits = rayleighScatteringScale, high 16 bits = mieScatteringScale
    float rayleighScale = f16tof32(packedScatterScales & 0xFFFFu);
    float mieScale      = f16tof32(packedScatterScales >> 16u);
    // Guard against zero scales (default = 1.0 if not set)
    rayleighScale = (rayleighScale > 0.0f) ? rayleighScale : 1.0f;
    mieScale      = (mieScale > 0.0f)      ? mieScale      : 1.0f;

    // Artistic scattering coefficients — affect in-scattering color only.
    // Extinction uses the unscaled physical RAYLEIGH_BETA / MIE_BETA
    // to match the precomputed transmittance LUT.
    float3 artRayleighBeta = RAYLEIGH_BETA * rayleighScale;
    float  artMieBeta      = MIE_BETA * mieScale;

    // ── Transmittance LUT ──────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> tLut = ResourceDescriptorHeap[transmittanceLutIdx];
#else
    Texture2D<float4> tLut = g_TransmittanceLut;
#endif

    // ── Frostbite analytical integration (UE5 SkyAtmosphere.usf pattern) ────
    //
    // Per-step analytical integral of in-scattered luminance:
    //   Sint = (scatter - scatter * T_step) / max(ext, eps)
    //   L   += Throughput * sunTransmittance * Sint
    //   Throughput *= T_step
    //
    // This avoids float underflow from accumulated exp(-tau_total) at the
    // horizon that zeroed out scatter in naive summation.
    float3 L          = float3(0, 0, 0);
    float3 Throughput = float3(1, 1, 1);

    for (int i = 0; i < NUM_STEPS; ++i) {
        float  t        = tStart + (float(i) + 0.5f) * stepLenKm;
        float3 sampleKm = cameraSkyKm + rayDir * t;
        float  altKm    = max(0.0f, length(sampleKm) - bottomRadiusKm);

        // Density at this altitude
        float densR = DensityAtAltitude(altKm, RAYLEIGH_SCALE_H);
        float densM = DensityAtAltitude(altKm, MIE_SCALE_H);

        // Extinction uses PHYSICAL coefficients (must match transmittance LUT)
        float3 extR     = RAYLEIGH_BETA * densR;
        float3 extM     = MIE_BETA * MIE_EXTINCTION_RATIO * densM;
        float3 extTotal = extR + extM;

        // Transmittance over this single step
        float3 T_step = exp(-extTotal * stepLenKm);

        // Transmittance from sample point toward sun (via precomputed LUT)
        float3 sunT = SampleTransmittanceLut(tLut, linearSampler,
            sampleKm, sunDir, bottomRadiusKm, topRadiusKm);

        // In-scattering uses ARTISTIC coefficients for color control
        float3 scatterR = artRayleighBeta * densR * phaseR;
        float3 scatterM = artMieBeta * densM * phaseM;
        float3 scatter  = scatterR + scatterM;

        // Analytical in-scattering integral over the step:
        //   Sint = scatter * (1 - T_step) / ext
        float3 Sint = (scatter - scatter * T_step) / max(extTotal, EXT_EPS);

        // Accumulate luminance weighted by path throughput and sun transmittance
        L         += Throughput * sunT * Sint * float3(sunIntensity, sunIntensity, sunIntensity);
        Throughput *= T_step;
    }

    // ── Ground contribution ─────────────────────────────────────────────────
    // When the view ray hits the planet surface, add Lambertian reflection of
    // sunlight attenuated by atmosphere transmittance (sun->ground and
    // ground->camera). Gives the ground its base color at the horizon and
    // below-horizon directions.
    if (hitsGround) {
        float3 groundPosKm = cameraSkyKm + rayDir * groundHit.x;
        float3 groundNormal = normalize(groundPosKm);

        // Sun-to-ground transmittance via LUT
        float3 groundSunT = SampleTransmittanceLut(tLut, linearSampler,
            groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);

        // Lambertian BRDF: albedo / PI * max(N.L, 0)
        float NdotL = max(0.0f, dot(groundNormal, sunDir));
        float3 groundRadiance = GROUND_ALBEDO * (1.0f / PI) * NdotL
                              * float3(sunIntensity, sunIntensity, sunIntensity) * groundSunT;

        // Attenuate by path transmittance from ground to camera
        L += Throughput * groundRadiance;
    }

    // ── NO sun disk ─────────────────────────────────────────────────────────
    // Intentionally omitted: IBL cubemaps should not contain point-source
    // specular highlights. The sun's direct contribution comes from the
    // analytical directional light in the PBR shader, not from the env map.

    // ── Output linear HDR (no tonemap, no gamma) ────────────────────────────
    // The IBL baker (irradiance convolution + prefiltered specular) operates
    // on linear HDR data and applies its own processing.
    outputCubemap[uint3(texel, faceIndex)] = float4(L, 1.0f);
}
