// brdf_lut.cpp -- CPU-side BRDF integration LUT generation.
//
// Precomputes the split-sum BRDF integral (Karis 2013, UE4/UE5 IBL pipeline).
// For each (NdotV, roughness) pair, runs importance-sampled GGX integration
// to produce (scale, bias) values:
//   F_env = F0 * scale + bias
//
// References:
//   [Karis 2013] "Real Shading in Unreal Engine 4"
//   [Lazarov 2013] "Getting More Physical in Call of Duty: Black Ops II"
//   learnopengl.com/PBR/IBL/Specular-IBL

#include "brdf_lut.h"
#include "engine/core/diag.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace uz {

static constexpr uint32_t kLutSize = 256;
static constexpr uint32_t kSampleCount = 1024;
static constexpr float kPi = 3.14159265358979323846f;

// Van der Corput radical inverse (base 2) for Hammersley sequence.
static float radical_inverse_vdc(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// Hammersley quasi-random 2D point in [0,1)^2.
static void hammersley(uint32_t i, uint32_t N, float& xi1, float& xi2) {
    xi1 = static_cast<float>(i) / static_cast<float>(N);
    xi2 = radical_inverse_vdc(i);
}

// Importance-sample GGX NDF: returns half-vector H in tangent space (N = 0,0,1).
// Xi = 2D quasi-random point, roughness = material roughness.
static void importance_sample_ggx(float xi1, float xi2, float roughness,
                                   float& hx, float& hy, float& hz) {
    float a = roughness * roughness;
    float phi = 2.0f * kPi * xi1;
    float cos_theta = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
    float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

    hx = std::cos(phi) * sin_theta;
    hy = std::sin(phi) * sin_theta;
    hz = cos_theta;
}

// Geometry function: Smith's method with GGX for IBL.
// Uses k = (roughness^2) / 2 (IBL remapping, NOT direct lighting remapping).
static float geometry_schlick_ggx_ibl(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

static float geometry_smith_ibl(float NdotV, float NdotL, float roughness) {
    float ggx_v = geometry_schlick_ggx_ibl(NdotV, roughness);
    float ggx_l = geometry_schlick_ggx_ibl(NdotL, roughness);
    return ggx_v * ggx_l;
}

// float32 to float16 (IEEE 754 half-precision).
static uint16_t float_to_half(float f) {
    union { float f; uint32_t u; } conv;
    conv.f = f;
    uint32_t x = conv.u;

    uint32_t sign = (x >> 16) & 0x8000;
    int32_t  exp  = ((x >> 23) & 0xFF) - 127;
    uint32_t mant = x & 0x007FFFFF;

    if (exp > 15) {
        return static_cast<uint16_t>(sign | 0x7C00); // Inf
    } else if (exp < -14) {
        return static_cast<uint16_t>(sign); // Zero / denorm (flush to zero)
    }

    uint16_t h_exp  = static_cast<uint16_t>((exp + 15) << 10);
    uint16_t h_mant = static_cast<uint16_t>(mant >> 13);
    return static_cast<uint16_t>(sign | h_exp | h_mant);
}

RhiTexture generate_brdf_lut() {
    DIAG("Generating BRDF integration LUT (256x256 RG16F)...");

    // Each texel is 2x float16 (RG16F) = 4 bytes.
    std::vector<uint16_t> pixels(kLutSize * kLutSize * 2);

    for (uint32_t y = 0; y < kLutSize; ++y) {
        float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(kLutSize);
        roughness = std::max(roughness, 0.01f); // Avoid singularity at roughness=0

        for (uint32_t x = 0; x < kLutSize; ++x) {
            float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(kLutSize);
            NdotV = std::max(NdotV, 0.001f); // Avoid division by zero

            // V in tangent space where N = (0, 0, 1)
            float vx = std::sqrt(1.0f - NdotV * NdotV); // sin(theta)
            float vy = 0.0f;
            float vz = NdotV; // cos(theta)

            float A = 0.0f; // Scale (F0-independent)
            float B = 0.0f; // Bias

            for (uint32_t i = 0; i < kSampleCount; ++i) {
                float xi1, xi2;
                hammersley(i, kSampleCount, xi1, xi2);

                // Importance-sample GGX half-vector
                float hx, hy, hz;
                importance_sample_ggx(xi1, xi2, roughness, hx, hy, hz);

                // Reflect V around H to get L
                float VdotH = vx * hx + vy * hy + vz * hz;
                float lx = 2.0f * VdotH * hx - vx;
                float ly = 2.0f * VdotH * hy - vy;
                float lz = 2.0f * VdotH * hz - vz;

                float NdotL = std::max(lz, 0.0f);
                float NdotH = std::max(hz, 0.0f);
                VdotH = std::max(VdotH, 0.0f);

                if (NdotL > 0.0f) {
                    float G  = geometry_smith_ibl(NdotV, NdotL, roughness);
                    float Gc = (G * VdotH) / (NdotH * NdotV + 1e-7f);
                    float Fc = std::pow(1.0f - VdotH, 5.0f);

                    A += (1.0f - Fc) * Gc;
                    B += Fc * Gc;
                }
            }

            A /= static_cast<float>(kSampleCount);
            B /= static_cast<float>(kSampleCount);

            // Clamp to valid range
            A = std::max(0.0f, std::min(A, 1.0f));
            B = std::max(0.0f, std::min(B, 1.0f));

            uint32_t idx = (y * kLutSize + x) * 2;
            pixels[idx + 0] = float_to_half(A);
            pixels[idx + 1] = float_to_half(B);
        }
    }

    // Create RHI texture
    RhiTextureDesc desc{};
    desc.width      = kLutSize;
    desc.height     = kLutSize;
    desc.depth      = 1;
    desc.mip_levels = 1;
    desc.array_size = 1;
    desc.format     = RHI_FORMAT_RG16_FLOAT;
    desc.dimension  = RHI_TEXTURE_DIM_2D;
    desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;
    desc.debug_name = "BRDF_LUT";

    RhiTexture tex = rhi()->texture_create(&desc);
    if (tex == RHI_NULL) {
        DIAG("WARNING: Failed to create BRDF LUT texture");
        return RHI_NULL;
    }

    rhi()->texture_upload(tex, pixels.data(), pixels.size() * sizeof(uint16_t));
    DIAG("BRDF LUT generated OK");
    return tex;
}

} // namespace uz
