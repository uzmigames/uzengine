/// Irradiance cubemap convolution compute shader.
///
/// Convolves an environment cubemap into a diffuse irradiance cubemap by
/// integrating cosine-weighted radiance over the hemisphere for each texel
/// direction. The result captures the total diffuse illumination arriving
/// from all directions, used in the IBL diffuse term:
///
///   diffuseIBL = albedo * irradianceMap.Sample(N)
///
/// Output is a 32x32-per-face cubemap stored as a Texture2DArray with 6
/// slices (one per cube face, DX12 convention).
///
/// Uses 64 uniform hemisphere samples per texel for real-time baking speed.
/// Quality is sufficient because irradiance is inherently low-frequency.
///
/// # References
/// - Ramamoorthi & Hanrahan 2001, "An Efficient Representation for Irradiance"
/// - LearnOpenGL, "Diffuse Irradiance"

#include "../include/ibl.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0.
cbuffer RootConstants : register(b0) {
    uint envMapIndex;  // SRV heap index of the source environment cubemap
    uint faceSize;     // output face resolution (e.g., 32)
};

#ifdef UZ_BINDLESS
// Environment cubemap fetched from descriptor heap at runtime
#else
TextureCube<float3> envMap : register(t0);
#endif

SamplerState linearSampler : register(s0);

RWTexture2DArray<float4> irradianceOut : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Compute Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    uint faceIndex = dtid.z;

    if (dtid.x >= faceSize || dtid.y >= faceSize || faceIndex >= 6u) return;

    // Compute the world-space direction for this texel
    float2 uv = (float2(dtid.xy) + 0.5f) / float(faceSize);
    float3 N = normalize(CubeFaceDirection(faceIndex, uv));

    // Build a tangent frame around N for hemisphere sampling
    float3 up = (abs(N.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f)
                                     : float3(1.0f, 0.0f, 0.0f);
    float3 right   = normalize(cross(up, N));
    float3 forward = cross(N, right);

#ifdef UZ_BINDLESS
    TextureCube<float3> envCube = ResourceDescriptorHeap[envMapIndex];
#else
    #define envCube envMap
#endif

    // Integrate cosine-weighted radiance over the hemisphere
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);

    static const uint SAMPLE_COUNT = 64u;
    static const float INV_SAMPLES = 1.0f / float(SAMPLE_COUNT);

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);

        // Uniform cosine-weighted hemisphere sampling:
        //   phi = 2*PI*Xi.x
        //   cosTheta = sqrt(1 - Xi.y)  (cosine-weighted distribution)
        //   sinTheta = sqrt(Xi.y)
        float phi      = 2.0f * PI * Xi.x;
        float cosTheta = sqrt(1.0f - Xi.y);
        float sinTheta = sqrt(Xi.y);

        // Tangent-space to world-space
        float3 sampleDir = right   * (cos(phi) * sinTheta)
                         + forward * (sin(phi) * sinTheta)
                         + N       * cosTheta;

        // For cosine-weighted sampling, the PDF already accounts for the
        // cos(theta)/PI factor, so we just average the samples.
        irradiance += envCube.SampleLevel(linearSampler, sampleDir, 0.0f).rgb;
    }

    // The cosine-weighted PDF is cos(theta)/PI. The Monte Carlo estimator
    // with N samples divides by N and multiplies by the hemisphere area (PI).
    // These cancel, leaving a simple average.
    irradiance *= INV_SAMPLES;

    irradianceOut[uint3(dtid.xy, faceIndex)] = float4(irradiance, 1.0f);

#ifndef UZ_BINDLESS
    #undef envCube
#endif
}
