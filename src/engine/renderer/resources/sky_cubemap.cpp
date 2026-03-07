// sky_cubemap.cpp -- CPU-side procedural sky cubemap generation for IBL.
//
// Generates a hemisphere sky gradient, bakes it into a source cubemap,
// then convolves into irradiance (diffuse) and prefiltered (specular) cubemaps.
//
// References:
//   [Ramamoorthi & Hanrahan 2001] "Efficient Irradiance with SH"
//   [Karis 2013] "Real Shading in Unreal Engine 4" (split-sum, GGX importance sampling)
//   [Lagarde & de Rousiers 2014] "Moving Frostbite to PBR"

#include "sky_cubemap.h"
#include "engine/core/diag.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace uz {

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

static constexpr uint32_t kEnvSize       = 256;  // source cubemap face resolution
static constexpr uint32_t kIrrSize       = 32;   // irradiance cubemap face resolution
static constexpr uint32_t kPrefiltSize   = 128;  // prefiltered cubemap face resolution (mip 0)
static constexpr uint32_t kPrefiltMips   = 5;    // mip levels: 128, 64, 32, 16, 8
static constexpr uint32_t kIrrSamples    = 1024; // hemisphere samples for irradiance convolution
static constexpr uint32_t kPrefiltSamples = 256; // GGX importance samples per texel
static constexpr float kPi              = 3.14159265358979323846f;
static constexpr float kHalfMax         = 65504.0f; // float16 representable max

// -----------------------------------------------------------------------
// float32 -> float16 conversion (IEEE 754 half-precision)
// Same implementation as brdf_lut.cpp.
// -----------------------------------------------------------------------

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

// -----------------------------------------------------------------------
// Clamp float to [0, kHalfMax] then convert to half.
// -----------------------------------------------------------------------

static uint16_t clamp_to_half(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > kHalfMax) f = kHalfMax;
    return float_to_half(f);
}

// -----------------------------------------------------------------------
// Simple 3D vector helpers (avoid pulling in math.h for this standalone module)
// -----------------------------------------------------------------------

struct Vec3f {
    float x, y, z;
};

static Vec3f vec3f(float x, float y, float z) {
    return {x, y, z};
}

static Vec3f vec3f_add(Vec3f a, Vec3f b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3f vec3f_scale(Vec3f v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

static float vec3f_dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float vec3f_length(Vec3f v) {
    return std::sqrt(vec3f_dot(v, v));
}

static Vec3f vec3f_normalize(Vec3f v) {
    float len = vec3f_length(v);
    if (len < 1e-8f) return {0.0f, 1.0f, 0.0f};
    float inv = 1.0f / len;
    return {v.x * inv, v.y * inv, v.z * inv};
}

static Vec3f vec3f_lerp(Vec3f a, Vec3f b, float t) {
    return {a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

static Vec3f vec3f_cross(Vec3f a, Vec3f b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

// -----------------------------------------------------------------------
// Cubemap face direction computation.
//
// For each cubemap face (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z),
// given texel coordinates (u, v) in [0, 1], compute the world-space
// direction vector. Uses the standard DX12/OpenGL cubemap convention.
// -----------------------------------------------------------------------

static Vec3f cubemap_direction(uint32_t face, float u, float v) {
    // Map [0,1] to [-1,1]
    float uc = 2.0f * u - 1.0f;
    float vc = 2.0f * v - 1.0f;

    Vec3f dir;
    switch (face) {
    case 0: dir = { 1.0f,   -vc,  -uc}; break; // +X
    case 1: dir = {-1.0f,   -vc,   uc}; break; // -X
    case 2: dir = {  uc,   1.0f,   vc}; break; // +Y
    case 3: dir = {  uc,  -1.0f,  -vc}; break; // -Y
    case 4: dir = {  uc,   -vc,  1.0f}; break; // +Z
    default: dir = { -uc,   -vc, -1.0f}; break; // -Z (face 5)
    }
    return vec3f_normalize(dir);
}

// -----------------------------------------------------------------------
// Sky color evaluation.
//
// Hemisphere gradient: zenith at Y=+1, horizon at Y=0, ground at Y=-1.
// Blend is: skyBlend = dir.y * 0.5 + 0.5 (maps [-1,1] to [0,1]).
//   skyBlend >= 0.5: lerp(horizon, zenith, (skyBlend - 0.5) * 2)
//   skyBlend <  0.5: lerp(ground, horizon, skyBlend * 2)
// Sun disk: sharp dot-product threshold at cos(0.53 deg) ~= 0.9995.
// -----------------------------------------------------------------------

static constexpr float kSunDiskCosAngle = 0.9995f;

static Vec3f evaluate_sky(const SkyCubemapParams& params, Vec3f dir) {
    Vec3f zenith  = vec3f(params.sky_zenith[0],  params.sky_zenith[1],  params.sky_zenith[2]);
    Vec3f horizon = vec3f(params.sky_horizon[0], params.sky_horizon[1], params.sky_horizon[2]);
    Vec3f ground  = vec3f(params.sky_ground[0],  params.sky_ground[1],  params.sky_ground[2]);

    float sky_blend = dir.y * 0.5f + 0.5f; // 0 = nadir, 0.5 = horizon, 1 = zenith

    Vec3f color;
    if (sky_blend >= 0.5f) {
        float t = (sky_blend - 0.5f) * 2.0f; // 0 at horizon, 1 at zenith
        color = vec3f_lerp(horizon, zenith, t);
    } else {
        float t = sky_blend * 2.0f; // 0 at nadir, 1 at horizon
        color = vec3f_lerp(ground, horizon, t);
    }

    // Sun disk contribution
    Vec3f sun_dir = vec3f_normalize(
        vec3f(params.sun_dir[0], params.sun_dir[1], params.sun_dir[2]));
    float sun_dot = vec3f_dot(dir, sun_dir);
    if (sun_dot > kSunDiskCosAngle) {
        // Soft edge falloff within the sun disk
        float t = (sun_dot - kSunDiskCosAngle) / (1.0f - kSunDiskCosAngle);
        float sun_contrib = t * t * params.sun_intensity;
        // Sun color: warm white
        color = vec3f_add(color, vec3f(sun_contrib * 1.0f,
                                       sun_contrib * 0.95f,
                                       sun_contrib * 0.85f));
    }

    return color;
}

// -----------------------------------------------------------------------
// Van der Corput radical inverse (base 2) for Hammersley sequence.
// -----------------------------------------------------------------------

static float radical_inverse_vdc(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

// -----------------------------------------------------------------------
// Hammersley quasi-random 2D point in [0,1)^2.
// -----------------------------------------------------------------------

static void hammersley(uint32_t i, uint32_t N, float& xi1, float& xi2) {
    xi1 = static_cast<float>(i) / static_cast<float>(N);
    xi2 = radical_inverse_vdc(i);
}

// -----------------------------------------------------------------------
// Build an orthonormal tangent frame from a normal vector N.
// Returns tangent T and bitangent B such that (T, B, N) is right-handed.
// -----------------------------------------------------------------------

static void build_tangent_frame(Vec3f N, Vec3f& T, Vec3f& B) {
    // Choose an up vector that isn't parallel to N
    Vec3f up = (std::abs(N.y) < 0.999f) ? vec3f(0, 1, 0) : vec3f(1, 0, 0);
    T = vec3f_normalize(vec3f_cross(up, N));
    B = vec3f_cross(N, T);
}

// -----------------------------------------------------------------------
// Sample the source cubemap at a given direction.
// The cubemap is stored as 6 contiguous face images, row-major, top-to-bottom.
// Each pixel is 4x uint16_t (RGBA16F).
// -----------------------------------------------------------------------

static Vec3f sample_cubemap(const std::vector<uint16_t>& pixels,
                            uint32_t face_size, Vec3f dir) {
    // Determine which face and (u,v) coordinates
    float abs_x = std::abs(dir.x);
    float abs_y = std::abs(dir.y);
    float abs_z = std::abs(dir.z);

    uint32_t face;
    float uc, vc, ma;

    if (abs_x >= abs_y && abs_x >= abs_z) {
        ma = abs_x;
        if (dir.x > 0) { face = 0; uc = -dir.z; vc = -dir.y; }  // +X
        else            { face = 1; uc =  dir.z; vc = -dir.y; }  // -X
    } else if (abs_y >= abs_x && abs_y >= abs_z) {
        ma = abs_y;
        if (dir.y > 0) { face = 2; uc =  dir.x; vc =  dir.z; }  // +Y
        else            { face = 3; uc =  dir.x; vc = -dir.z; }  // -Y
    } else {
        ma = abs_z;
        if (dir.z > 0) { face = 4; uc =  dir.x; vc = -dir.y; }  // +Z
        else            { face = 5; uc = -dir.x; vc = -dir.y; }  // -Z
    }

    // Map to [0, 1] texture coordinates
    float u = 0.5f * (uc / ma + 1.0f);
    float v = 0.5f * (vc / ma + 1.0f);

    // Clamp to valid texel range
    u = std::max(0.0f, std::min(u, 1.0f - 1e-6f));
    v = std::max(0.0f, std::min(v, 1.0f - 1e-6f));

    uint32_t px = static_cast<uint32_t>(u * face_size);
    uint32_t py = static_cast<uint32_t>(v * face_size);
    if (px >= face_size) px = face_size - 1;
    if (py >= face_size) py = face_size - 1;

    // Pixel index: faces are stored contiguously, 4 channels (RGBA) per pixel
    uint32_t face_stride = face_size * face_size * 4;
    uint32_t idx = face * face_stride + (py * face_size + px) * 4;

    // Convert float16 back to float32 for sampling
    // Simple half->float: decode sign, exponent, mantissa
    auto half_to_float = [](uint16_t h) -> float {
        uint32_t sign = (h & 0x8000u) << 16;
        uint32_t exp  = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x03FF;
        if (exp == 0) {
            return 0.0f; // Zero / denorm -> 0
        } else if (exp == 31) {
            // Inf/NaN -> large value
            return (sign ? -kHalfMax : kHalfMax);
        }
        uint32_t f = sign | ((exp + 112) << 23) | (mant << 13);
        float result;
        memcpy(&result, &f, sizeof(float));
        return result;
    };

    return vec3f(half_to_float(pixels[idx + 0]),
                 half_to_float(pixels[idx + 1]),
                 half_to_float(pixels[idx + 2]));
}

// -----------------------------------------------------------------------
// Importance-sample GGX NDF: returns half-vector H in tangent space.
// Same as brdf_lut.cpp.
// -----------------------------------------------------------------------

static Vec3f importance_sample_ggx(float xi1, float xi2, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0f * kPi * xi1;
    float cos_theta = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
    float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

    return vec3f(std::cos(phi) * sin_theta,
                 std::sin(phi) * sin_theta,
                 cos_theta);
}

// -----------------------------------------------------------------------
// Store 4 float values as RGBA16F into a pixel buffer at the given index.
// -----------------------------------------------------------------------

static void store_rgba16f(std::vector<uint16_t>& pixels, uint32_t idx,
                          float r, float g, float b, float a) {
    pixels[idx + 0] = clamp_to_half(r);
    pixels[idx + 1] = clamp_to_half(g);
    pixels[idx + 2] = clamp_to_half(b);
    pixels[idx + 3] = clamp_to_half(a);
}

// -----------------------------------------------------------------------
// Create and upload a cubemap texture via the RHI.
//
// `pixels` contains all 6 faces contiguously, each face is
// face_size x face_size x 4 channels (RGBA16F = uint16_t per channel).
//
// NOTE: Current RHI texture_upload() only copies data into subresource 0
// (face 0, mip 0). A full cubemap requires per-subresource upload which
// the RHI does not yet support. The texture is created with the correct
// cubemap descriptor so the SRV is a TEXTURECUBE. Once the RHI gains
// texture_upload_subresource(), this function should upload each face
// individually.
// -----------------------------------------------------------------------

static RhiTexture create_cubemap_texture(const std::vector<uint16_t>& pixels,
                                          uint32_t face_size,
                                          uint32_t mip_levels,
                                          const char* debug_name) {
    RhiTextureDesc desc{};
    desc.width      = face_size;
    desc.height     = face_size;
    desc.depth      = 1;
    desc.mip_levels = mip_levels;
    desc.array_size = 6;
    desc.format     = RHI_FORMAT_RGBA16_FLOAT;
    desc.dimension  = RHI_TEXTURE_DIM_CUBE;
    desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;
    desc.debug_name = debug_name;

    RhiTexture tex = rhi()->texture_create(&desc);
    if (tex == RHI_NULL) {
        DIAG("WARNING: Failed to create cubemap texture");
        return RHI_NULL;
    }

    // Upload all 6 faces (and all mip levels) via per-subresource upload.
    // D3D12 subresource index = mip + (face * mip_levels).
    uint32_t pixel_offset = 0;
    for (uint32_t mip = 0; mip < mip_levels; ++mip) {
        uint32_t mip_size = face_size >> mip;
        uint32_t face_pixels = mip_size * mip_size * 4; // RGBA16F = 4 uint16_t
        uint32_t face_bytes  = face_pixels * sizeof(uint16_t);

        for (uint32_t face = 0; face < 6; ++face) {
            uint32_t subresource = mip + face * mip_levels;
            rhi()->texture_upload_subresource(
                tex, subresource,
                pixels.data() + pixel_offset + face * face_pixels,
                face_bytes);
        }
        pixel_offset += 6 * face_pixels;
    }

    return tex;
}

// =======================================================================
// Step 1: Generate source environment cubemap (256x256 per face)
// =======================================================================

static std::vector<uint16_t> generate_env_pixels(const SkyCubemapParams& params) {
    // 6 faces x face_size x face_size x 4 channels (RGBA16F)
    uint32_t total_pixels = 6 * kEnvSize * kEnvSize * 4;
    std::vector<uint16_t> pixels(total_pixels);

    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < kEnvSize; ++y) {
            for (uint32_t x = 0; x < kEnvSize; ++x) {
                // Texel center in [0, 1]
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kEnvSize);
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kEnvSize);

                Vec3f dir = cubemap_direction(face, u, v);
                Vec3f color = evaluate_sky(params, dir);

                uint32_t idx = (face * kEnvSize * kEnvSize + y * kEnvSize + x) * 4;
                store_rgba16f(pixels, idx, color.x, color.y, color.z, 1.0f);
            }
        }
    }

    return pixels;
}

// =======================================================================
// Step 2: Convolve irradiance cubemap (32x32 per face)
//
// For each output texel direction N, integrate the source cubemap over
// the cosine-weighted hemisphere:
//   irradiance(N) = (1/pi) * integral[ L(omega) * max(N.omega, 0) d_omega ]
//
// Uses Hammersley quasi-random sampling with uniform hemisphere mapping,
// then cosine-weights the contribution.
// =======================================================================

static std::vector<uint16_t> convolve_irradiance(
    const std::vector<uint16_t>& env_pixels) {

    uint32_t total_pixels = 6 * kIrrSize * kIrrSize * 4;
    std::vector<uint16_t> pixels(total_pixels);

    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t y = 0; y < kIrrSize; ++y) {
            for (uint32_t x = 0; x < kIrrSize; ++x) {
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kIrrSize);
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kIrrSize);

                Vec3f N = cubemap_direction(face, u, v);

                // Build tangent frame around N
                Vec3f T, B;
                build_tangent_frame(N, T, B);

                Vec3f irradiance = vec3f(0, 0, 0);

                for (uint32_t i = 0; i < kIrrSamples; ++i) {
                    float xi1, xi2;
                    hammersley(i, kIrrSamples, xi1, xi2);

                    // Cosine-weighted hemisphere sampling:
                    // theta = acos(sqrt(1 - xi1)), phi = 2*pi*xi2
                    // PDF = cos(theta) / pi, so weight = 1 (already baked into sampling)
                    float sin_theta = std::sqrt(xi1);
                    float cos_theta = std::sqrt(1.0f - xi1);
                    float phi = 2.0f * kPi * xi2;

                    // Sample direction in tangent space
                    float sx = sin_theta * std::cos(phi);
                    float sy = sin_theta * std::sin(phi);
                    float sz = cos_theta;

                    // Transform to world space
                    Vec3f sample_dir = vec3f_normalize(
                        vec3f_add(vec3f_add(vec3f_scale(T, sx),
                                            vec3f_scale(B, sy)),
                                  vec3f_scale(N, sz)));

                    Vec3f sample_color = sample_cubemap(env_pixels, kEnvSize, sample_dir);

                    // For cosine-weighted sampling, each sample contributes equally
                    // (the PDF cancels cos(theta)/pi, leaving L(omega) * pi / (pi * N))
                    irradiance = vec3f_add(irradiance, sample_color);
                }

                // Average: integral = pi * (1/N) * sum, then divide by pi = 1/N * sum
                // With cosine-weighted sampling: E = (1/N) * sum(L)
                float inv_n = 1.0f / static_cast<float>(kIrrSamples);
                irradiance = vec3f_scale(irradiance, inv_n);

                uint32_t idx = (face * kIrrSize * kIrrSize + y * kIrrSize + x) * 4;
                store_rgba16f(pixels, idx,
                              irradiance.x, irradiance.y, irradiance.z, 1.0f);
            }
        }
    }

    return pixels;
}

// =======================================================================
// Step 3: Prefilter specular environment cubemap (128x128, 5 mip levels)
//
// Each mip level corresponds to a roughness value:
//   mip 0 = roughness 0.0 (mirror reflection)
//   mip 4 = roughness 1.0 (fully diffuse)
//
// Uses GGX importance sampling (same as BRDF LUT) to gather incoming
// radiance weighted by the NDF. This is the LD term in the split-sum
// approximation: LD(R, roughness) = sum(L(H_reflect) * NdotL) / sum(NdotL)
// =======================================================================

static std::vector<uint16_t> prefilter_specular(
    const std::vector<uint16_t>& env_pixels) {

    // Calculate total pixel count across all mip levels (all 6 faces)
    uint32_t total_pixels = 0;
    for (uint32_t mip = 0; mip < kPrefiltMips; ++mip) {
        uint32_t mip_size = kPrefiltSize >> mip;
        total_pixels += 6 * mip_size * mip_size * 4;
    }
    std::vector<uint16_t> pixels(total_pixels);

    uint32_t pixel_offset = 0;

    for (uint32_t mip = 0; mip < kPrefiltMips; ++mip) {
        uint32_t mip_size = kPrefiltSize >> mip;
        float roughness = static_cast<float>(mip) / static_cast<float>(kPrefiltMips - 1);
        // Clamp roughness away from zero to avoid singularity in GGX
        roughness = std::max(roughness, 0.01f);

        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < mip_size; ++y) {
                for (uint32_t x = 0; x < mip_size; ++x) {
                    float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(mip_size);
                    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(mip_size);

                    // Normal = View = Reflection direction (isotropic assumption)
                    Vec3f N = cubemap_direction(face, u, v);
                    Vec3f V = N;

                    // Build tangent frame around N
                    Vec3f T, B;
                    build_tangent_frame(N, T, B);

                    Vec3f prefiltered = vec3f(0, 0, 0);
                    float total_weight = 0.0f;

                    for (uint32_t i = 0; i < kPrefiltSamples; ++i) {
                        float xi1, xi2;
                        hammersley(i, kPrefiltSamples, xi1, xi2);

                        // Importance-sample GGX half-vector in tangent space
                        Vec3f H_tangent = importance_sample_ggx(xi1, xi2, roughness);

                        // Transform H to world space
                        Vec3f H = vec3f_normalize(
                            vec3f_add(vec3f_add(vec3f_scale(T, H_tangent.x),
                                                vec3f_scale(B, H_tangent.y)),
                                      vec3f_scale(N, H_tangent.z)));

                        // Reflect V around H to get light direction L
                        float VdotH = std::max(vec3f_dot(V, H), 0.0f);
                        Vec3f L = vec3f_add(vec3f_scale(H, 2.0f * VdotH),
                                            vec3f_scale(V, -1.0f));

                        float NdotL = vec3f_dot(N, L);
                        if (NdotL > 0.0f) {
                            Vec3f sample_color = sample_cubemap(env_pixels,
                                                                kEnvSize, L);
                            prefiltered = vec3f_add(prefiltered,
                                                    vec3f_scale(sample_color, NdotL));
                            total_weight += NdotL;
                        }
                    }

                    if (total_weight > 0.0f) {
                        prefiltered = vec3f_scale(prefiltered, 1.0f / total_weight);
                    }

                    uint32_t idx = pixel_offset +
                        (face * mip_size * mip_size + y * mip_size + x) * 4;
                    store_rgba16f(pixels, idx,
                                  prefiltered.x, prefiltered.y, prefiltered.z, 1.0f);
                }
            }
        }

        pixel_offset += 6 * mip_size * mip_size * 4;
    }

    return pixels;
}

// =======================================================================
// Public API
// =======================================================================

SkyCubemapResult generate_sky_cubemaps(const SkyCubemapParams& params) {
    DIAG("Generating sky IBL cubemaps...");

    SkyCubemapResult result{};
    result.env_cubemap      = RHI_NULL;
    result.irradiance_cube  = RHI_NULL;
    result.prefiltered_cube = RHI_NULL;

    // --- Step 1: Source environment cubemap ---
    DIAG("  [1/3] Generating source environment cubemap (256x256)...");
    std::vector<uint16_t> env_pixels = generate_env_pixels(params);

    result.env_cubemap = create_cubemap_texture(env_pixels, kEnvSize, 1,
                                                 "Sky_EnvCubemap");
    if (result.env_cubemap == RHI_NULL) {
        DIAG("WARNING: Sky env cubemap creation failed");
        return result;
    }

    // --- Step 2: Irradiance cubemap ---
    DIAG("  [2/3] Convolving irradiance cubemap (32x32, 1024 samples)...");
    std::vector<uint16_t> irr_pixels = convolve_irradiance(env_pixels);

    result.irradiance_cube = create_cubemap_texture(irr_pixels, kIrrSize, 1,
                                                     "Sky_IrradianceCube");
    if (result.irradiance_cube == RHI_NULL) {
        DIAG("WARNING: Sky irradiance cubemap creation failed");
        return result;
    }

    // --- Step 3: Prefiltered specular cubemap ---
    DIAG("  [3/3] Prefiltering specular cubemap (128x128, 5 mips, 256 samples)...");
    std::vector<uint16_t> prefilt_pixels = prefilter_specular(env_pixels);

    result.prefiltered_cube = create_cubemap_texture(prefilt_pixels, kPrefiltSize,
                                                      kPrefiltMips,
                                                      "Sky_PrefilteredCube");
    if (result.prefiltered_cube == RHI_NULL) {
        DIAG("WARNING: Sky prefiltered cubemap creation failed");
        return result;
    }

    DIAG("Sky IBL cubemaps generated OK");
    return result;
}

} // namespace uz
