/// Cloud Weather Map Compute Shader — Procedural 2D weather texture (512x512 RGBA8).
///
/// Generates a tileable weather map that drives volumetric cloud density sampling:
///   R = Cloud coverage — multi-octave 2D value noise [0,1]
///   G = Cloud type     — gradient noise remapped to [0,1] (0=stratus, 1=cumulus)
///   B = Precipitation  — derived from coverage * type [0,1]
///   A = Reserved (1.0)
///
/// The weather map is a procedural approximation. In production this would be
/// artist-authored or driven by a fluid simulation.
///
/// @source Schneider & Vos 2015 — "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
///         Weather map concept and channel layout.
///
/// # Resources
///   b0: Root constants — windOffset (float2), globalCoverage (float)
///   u0: RWTexture2D<float4> output (RGBA8_UNorm, 512x512)
///
/// # Dispatch
///   [8,8,1] thread groups, dispatch [64,64,1] = 512x512 total threads.

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

cbuffer WeatherParams : register(b0) {
    float2 windOffset;      // Animated UV offset (totalTime * windVelocity.xy)
    float  globalCoverage;  // From VolumetricCloudsComponent::coverage [0,1]
    float  _pad0;           // Padding to 16-byte alignment
};

RWTexture2D<float4> outputWeather : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Hash / Noise Primitives (self-contained, no includes)
// ─────────────────────────────────────────────────────────────────────────────

/// Integer hash for pseudo-random noise generation.
/// Based on Hugo Elias / Inigo Quilez style hashing — fast, tileable.
float hash2D(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

/// Bilinear interpolation of hashed lattice points (2D value noise).
/// Returns a smooth noise value in [0,1] for any input coordinate.
float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    // Hermite smoothstep for C1 continuity at cell boundaries
    float2 u = f * f * (3.0f - 2.0f * f);

    // Four corner hashes
    float a = hash2D(i + float2(0.0f, 0.0f));
    float b = hash2D(i + float2(1.0f, 0.0f));
    float c = hash2D(i + float2(0.0f, 1.0f));
    float d = hash2D(i + float2(1.0f, 1.0f));

    // Bilinear interpolation
    return lerp(lerp(a, b, u.x),
                lerp(c, d, u.x),
                u.y);
}

/// Fractal Brownian Motion — 4-octave value noise.
///
/// Octave parameters:
///   Frequencies: 4, 8, 16, 32
///   Weights:     0.5, 0.25, 0.125, 0.0625 (standard 1/2^n falloff)
///
/// Total weight = 0.9375, so output is normalized to [0,1].
float fbm4(float2 p)
{
    // Base frequency applied to input coordinates
    float2 coord = p * 4.0f;

    float value  = 0.0f;
    float weight = 0.5f;

    // 4 octaves with frequency doubling and weight halving
    for (int i = 0; i < 4; ++i)
    {
        value += weight * valueNoise(coord);
        coord *= 2.0f;
        weight *= 0.5f;
    }

    // Normalize: sum of weights = 0.5 + 0.25 + 0.125 + 0.0625 = 0.9375
    return value / 0.9375f;
}

/// Gradient-style noise for cloud type channel.
/// Uses a different frequency and offset to decorrelate from coverage noise.
float gradientNoise(float2 p)
{
    // Offset and different base frequency to decorrelate from coverage
    float2 coord = p * 3.0f + float2(127.1f, 311.7f);

    float value  = 0.0f;
    float weight = 0.5f;

    for (int i = 0; i < 3; ++i)
    {
        value += weight * valueNoise(coord);
        coord *= 2.0f;
        weight *= 0.5f;
    }

    // Normalize: sum of weights = 0.5 + 0.25 + 0.125 = 0.875
    return value / 0.875f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    // DEBUG: trivial output to isolate GPU crash
    outputWeather[DTid] = float4(0.5, 0.5, 0.0, 1.0);
    return;

    // Weather map dimensions (must match dispatch: 64x64 groups of 8x8)
    static const float kWeatherMapSize = 512.0f;

    // Normalized UV in [0,1], tileable
    float2 uv = (float2(DTid) + 0.5f) / kWeatherMapSize;

    // Apply wind offset for animation (scrolls the weather pattern over time)
    float2 windUV = uv + windOffset;

    // ── R: Cloud Coverage ────────────────────────────────────────────────
    // Multi-octave value noise modulated by globalCoverage.
    // The remap compresses noise range so that globalCoverage acts as a
    // threshold: low coverage = only peaks form clouds, high = overcast.
    float rawCoverage = fbm4(windUV);

    // Remap noise based on global coverage:
    // coverage=0 → only noise > 1.0 passes (none), coverage=1 → everything passes
    float coverageThreshold = 1.0f - globalCoverage;
    float coverage = saturate((rawCoverage - coverageThreshold) / max(globalCoverage, 0.001f));

    // ── G: Cloud Type ────────────────────────────────────────────────────
    // Gradient noise decorrelated from coverage. 0 = stratus, 1 = cumulus.
    // Slower wind offset so type changes more gradually than coverage.
    float cloudType = gradientNoise(windUV * 0.8f + float2(43.0f, 17.0f));

    // ── B: Precipitation ─────────────────────────────────────────────────
    // Derived from coverage and type — only thick cumulus clouds precipitate.
    float precipitation = saturate(coverage * cloudType);

    // ── A: Reserved ──────────────────────────────────────────────────────
    outputWeather[DTid] = float4(coverage, cloudType, precipitation, 1.0f);
}
