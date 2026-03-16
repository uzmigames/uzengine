/// Sky Cubemap Capture Pixel Shader — SM5 fallback for sky_cubemap_cs.hlsl.
///
/// Renders one cubemap face per draw call (6 draws total) using a fullscreen
/// triangle. Produces identical output to the compute shader but uses
/// `register(t0)` for the transmittance LUT instead of bindless
/// `ResourceDescriptorHeap[idx]`, making it fully SM5 compatible.
///
/// The atmosphere ray-marching math is identical to sky_cubemap_cs.hlsl:
/// Hillaire 2020 single-scatter model with Frostbite analytical integration.
///
/// # I/O
/// - Input: fullscreen UV from fullscreen_vs.hlsl + face index as root constant
/// - Output: SV_Target (float4, RGBA16F render target = one cubemap face)
/// - Transmittance LUT: Texture2D<float4> at register(t0) (NOT bindless)
///
/// # Coordinate Convention
/// - Z-up world space: planet center at origin, surface at Z = bottomRadiusKm
/// - CubeFaceDirection() returns DX convention (Y-up), so we swizzle Y<->Z

#include "../include/ibl.hlsli"

// ---------------------------------------------------------------------------
// Root Constants -- 8 DWORDs (32 bytes) at b0
// ---------------------------------------------------------------------------

/// Matches sky_cubemap_cs.hlsl SkyCubemapParams except DWORD 6 is replaced
/// with cubeFaceIndex (no longer a bindless SRV index).
cbuffer SkyCubemapFaceParams : register(b0) {
    float bottomRadiusKm;       // DWORD 0: planet surface radius (km)
    float topRadiusKm;          // DWORD 1: top-of-atmosphere radius (km)
    float sunDirX;              // DWORD 2: sun direction X
    float sunDirY;              // DWORD 3: sun direction Y
    float sunDirZ;              // DWORD 4: sun direction Z
    float sunIntensity;         // DWORD 5: sun illuminance (uniform RGB)
    uint  cubeFaceIndex;        // DWORD 6: which cubemap face to render (0-5)
    uint  packedScatterScales;  // DWORD 7: packed f16 pair -- low16 = rayleighScale, high16 = mieScale
};

// ---------------------------------------------------------------------------
// SM5 Register Bindings (no bindless)
// ---------------------------------------------------------------------------

/// Precomputed 256x64 RGBA16F transmittance LUT, bound at t0 (SM5 register).
Texture2D<float4> transmittanceLut : register(t0);

/// Bilinear wrap sampler for smooth transmittance LUT interpolation.
SamplerState linearSampler : register(s0);

// ---------------------------------------------------------------------------
// Standard Earth Atmosphere Constants
// ---------------------------------------------------------------------------
//
// Canonical Bruneton 2017 / UE5 values for Earth's atmosphere.
// Must match the coefficients used in transmittance_lut_cs.hlsl and
// sky_cubemap_cs.hlsl exactly. Hardcoded to avoid exceeding root constant
// budget while keeping physical accuracy.

/// Rayleigh scattering coefficients at sea level (km^-1).
/// Wavelength-dependent: blue scatters ~6x more than red.
static const float3 RAYLEIGH_BETA = float3(5.802e-3f, 13.558e-3f, 33.1e-3f);

/// Mie scattering coefficient at sea level (km^-1).
/// Wavelength-independent (aerosol scattering).
static const float MIE_BETA = 3.996e-3f;

/// Mie extinction-to-scattering ratio (absorption included).
static const float MIE_EXTINCTION_RATIO = 1.11f;

/// Henyey-Greenstein anisotropy parameter for Mie phase function.
/// 0.8 = strong forward scattering (typical for atmospheric aerosols).
static const float MIE_ANISOTROPY = 0.8f;

/// Rayleigh and Mie scale heights -- hardcoded Earth defaults.
static const float RAYLEIGH_SCALE_H = 8.0f;   // km
static const float MIE_SCALE_H      = 1.2f;   // km

/// Ground albedo for Lambertian reflection when view ray hits the planet.
static const float3 GROUND_ALBEDO = float3(0.3f, 0.3f, 0.3f);

// ---------------------------------------------------------------------------
// Integration Constants
// ---------------------------------------------------------------------------

/// Number of ray-march steps through the atmosphere per texel.
/// 16 steps is sufficient for low-frequency IBL capture.
static const int NUM_STEPS = 16;

/// Small epsilon to prevent division by zero in analytical integration.
static const float EXT_EPS = 1e-7f;

/// Safety margin: place camera at least 5m above ground in sky space (UE5 pattern).
static const float PLANET_RADIUS_OFFSET_KM = 0.005f;

// ---------------------------------------------------------------------------
// Atmosphere Helpers
// ---------------------------------------------------------------------------

/// Rayleigh phase function: (3 / 16 PI)(1 + cos^2 theta).
float RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase function for Mie scattering.
float MiePhase(float cosTheta, float g) {
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

/// Exponential density profile: exp(-altitude / scaleHeight).
float DensityAtAltitude(float altitudeKm, float scaleHeightKm) {
    return exp(-max(0.0f, altitudeKm) / scaleHeightKm);
}

/// Ray-sphere intersection. Returns (tNear, tFar).
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

// ---------------------------------------------------------------------------
// Transmittance LUT Sampling
// ---------------------------------------------------------------------------

/// Bruneton 2017 non-linear parameterization: maps (viewHeight, cos) to UV.
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

// ---------------------------------------------------------------------------
// Cubemap Direction Helper (Z-up Adaptation)
// ---------------------------------------------------------------------------

/// Converts cubemap face index + UV to a Z-up world-space direction.
/// CubeFaceDirection() from ibl.hlsli returns Y-up DX convention.
/// Swizzle: DX (X, Y, Z) -> atmosphere Z-up (X, Z, Y).
float3 CubeFaceDirectionZUp(uint faceIndex, float2 uv) {
    float3 dxDir = CubeFaceDirection(faceIndex, uv);
    return normalize(float3(dxDir.x, dxDir.z, dxDir.y));
}

// ---------------------------------------------------------------------------
// Pixel Shader Entry Point
// ---------------------------------------------------------------------------

/// Fullscreen pixel shader input from fullscreen_vs.hlsl.
struct PSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

/// Renders one face of the sky cubemap.
///
/// Called 6 times (once per face) with the render target set to the
/// corresponding cubemap array slice. The face index is passed via
/// root constant cubeFaceIndex.
float4 main(PSInput input) : SV_Target
{
    float2 uv = input.texcoord;

    // -- Compute world-space ray direction (Z-up) --------------------------
    float3 rayDir = CubeFaceDirectionZUp(cubeFaceIndex, uv);

    // -- Camera position in sky-space (km) ---------------------------------
    // Camera on the planet surface, Z-up.
    float3 cameraSkyKm = float3(0.0f, 0.0f, bottomRadiusKm + PLANET_RADIUS_OFFSET_KM);

    // -- Intersect ray with atmosphere top ---------------------------------
    float2 atmoHit = RaySphereIntersect(cameraSkyKm, rayDir, float3(0, 0, 0), topRadiusKm);
    if (atmoHit.y < 0.0f) {
        return float4(0, 0, 0, 1);
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
        return float4(0, 0, 0, 1);
    }

    // -- Sun direction and phase functions ---------------------------------
    float3 sunDir   = normalize(float3(sunDirX, sunDirY, sunDirZ));
    float  cosTheta = dot(rayDir, sunDir);
    float  phaseR   = RayleighPhase(cosTheta);
    float  phaseM   = MiePhase(cosTheta, MIE_ANISOTROPY);

    // -- Unpack artistic scattering scale factors from packed f16 pair -----
    float rayleighScale = f16tof32(packedScatterScales & 0xFFFFu);
    float mieScale      = f16tof32(packedScatterScales >> 16u);
    rayleighScale = (rayleighScale > 0.0f) ? rayleighScale : 1.0f;
    mieScale      = (mieScale > 0.0f)      ? mieScale      : 1.0f;

    // Artistic scattering coefficients -- affect in-scattering color only.
    // Extinction uses the unscaled physical values to match the transmittance LUT.
    float3 artRayleighBeta = RAYLEIGH_BETA * rayleighScale;
    float  artMieBeta      = MIE_BETA * mieScale;

    // -- Frostbite analytical integration (UE5 SkyAtmosphere.usf pattern) --
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
        float3 sunT = SampleTransmittanceLut(transmittanceLut, linearSampler,
            sampleKm, sunDir, bottomRadiusKm, topRadiusKm);

        // In-scattering uses ARTISTIC coefficients for color control
        float3 scatterR = artRayleighBeta * densR * phaseR;
        float3 scatterM = artMieBeta * densM * phaseM;
        float3 scatter  = scatterR + scatterM;

        // Analytical in-scattering integral over the step
        float3 Sint = (scatter - scatter * T_step) / max(extTotal, EXT_EPS);

        // Accumulate luminance weighted by path throughput and sun transmittance
        L         += Throughput * sunT * Sint * float3(sunIntensity, sunIntensity, sunIntensity);
        Throughput *= T_step;
    }

    // -- Ground contribution -----------------------------------------------
    if (hitsGround) {
        float3 groundPosKm = cameraSkyKm + rayDir * groundHit.x;
        float3 groundNormal = normalize(groundPosKm);

        float3 groundSunT = SampleTransmittanceLut(transmittanceLut, linearSampler,
            groundPosKm, sunDir, bottomRadiusKm, topRadiusKm);

        float NdotL = max(0.0f, dot(groundNormal, sunDir));
        float3 groundRadiance = GROUND_ALBEDO * (1.0f / PI) * NdotL
                              * float3(sunIntensity, sunIntensity, sunIntensity) * groundSunT;

        L += Throughput * groundRadiance;
    }

    // -- NO sun disk (same as compute shader) ------------------------------
    // IBL cubemaps should not contain point-source specular highlights.

    // -- Output linear HDR (no tonemap, no gamma) --------------------------
    return float4(L, 1.0f);
}
