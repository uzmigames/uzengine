/// UzEngine Multi-Light System — CPU structs, GPU layout, and culling helpers.
///
/// Provides the `LightGPU` struct for efficient GPU upload, the
/// `PerFrameMultiLightCB` constant buffer that carries up to 8 dynamic lights
/// per frame, CPU-side light culling via `SelectNearestLights()`, and
/// standalone attenuation / cone-falloff math functions that can be unit-tested
/// without any RHI or hardware dependency.
///
/// # Design Constraints
/// - `LightGPU` is exactly 64 bytes (four float4 rows) for HLSL alignment.
/// - `PerFrameMultiLightCB` is a multiple of 256 bytes (D3D12 CB requirement).
/// - All math functions are `[[nodiscard]] inline` so tests need no .cpp file.
/// - `SelectNearestLights()` sorts by squared distance, skips lights whose
///   centre is further than their range from the camera, writes at most
///   `kMaxMultiLights` entries.
///
/// # See Also
/// - `constant_buffers.h` — existing `PerFrameCB`, `LightingCB`, `PerObjectCB`
/// - `scene_lighting.h`   — directional-light / ambient helpers

#pragma once

#include "vector3.h"
#include "vector4.h"
#include "mat4.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of dynamic lights uploaded per frame in `PerFrameMultiLightCB`.
///
/// 8 lights x 64 bytes = 512 bytes of light data in the constant buffer.
static constexpr uint32_t kMaxMultiLights = 8u;

// ─────────────────────────────────────────────────────────────────────────────
// LightType — discriminant stored in LightGPU.lightType
// ─────────────────────────────────────────────────────────────────────────────

/// Discriminant stored in `LightGPU::lightType` so the shader can branch once
/// per light without needing separate arrays for each type.
enum class LightType : uint32_t {
    Directional = 0, ///< Infinite directional light.
    Point       = 1, ///< Omnidirectional point light.
    Spot        = 2, ///< Cone-shaped spot light.
};

// ─────────────────────────────────────────────────────────────────────────────
// LightGPU — GPU-resident light descriptor (64 bytes, 16-byte aligned)
// ─────────────────────────────────────────────────────────────────────────────

/// GPU-resident descriptor for one dynamic light.
///
/// Exactly 64 bytes (four float4 rows).  All fields are HLSL-compatible:
/// float3 fields are padded to 16-byte rows by the adjacent scalar fields.
///
/// Memory layout (byte offsets):
/// ```
///  [ 0..11] positionOrDir  float3  world position (point/spot) or direction
///  [12..15] range          float   attenuation radius (0 = directional)
///  [16..27] color          float3  linear HDR color x intensity
///  [28..31] lightType      float   0=directional, 1=point, 2=spot
///  [32..43] spotDirection  float3  normalised cone axis
///  [44..47] innerConeCos   float   cos(innerAngle)
///  [48..51] outerConeCos   float   cos(outerAngle)
///  [52..63] _pad           float3  explicit padding to 64 bytes
/// ```
struct alignas(16) LightGPU {
    // Row 0  [bytes 0..15]
    Vec3  positionOrDir = {}; ///< World-space position (point/spot) or direction (directional).
    float range         = 0.f;///< Attenuation radius in world units. 0 for directional lights.

    // Row 1  [bytes 16..31]
    Vec3  color         = {}; ///< Linear HDR color multiplied by intensity.
    float lightType     = 0.f;///< Cast of `LightType` to float for HLSL scalar packing.

    // Row 2  [bytes 32..47]
    Vec3  spotDirection = {}; ///< Normalised cone axis (spot lights only).
    float innerConeCos  = 0.f;///< cos(innerAngle) — full intensity inside this threshold.

    // Row 3  [bytes 48..63]
    float outerConeCos  = 0.f;///< cos(outerAngle) — zero intensity outside this threshold.
    float _pad[3]       = {}; ///< Explicit padding to 64 bytes.
};

static_assert(sizeof(LightGPU)  == 64u,  "LightGPU must be exactly 64 bytes");
static_assert(alignof(LightGPU) == 16u,  "LightGPU must be 16-byte aligned");

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameMultiLightCB — per-frame CB with 8-light dynamic array
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame constant buffer for the multi-light forward/deferred pass.
///
/// Total size: 768 bytes = 3 x 256 (satisfies D3D12 CB alignment).
///
/// Field layout:
/// ```
///   [  0.. 63] viewProj       Mat4         64 bytes
///   [ 64.. 79] cameraPos      Vec4         16 bytes
///   [ 80.. 95] timeParams     Vec4         16 bytes
///   [ 96..111] ambientColor   Vec4         16 bytes
///   [112..115] lightCount     uint32        4 bytes
///   [116..127] _pad0          uint32[3]    12 bytes  (align array to 16)
///   [128..639] lightArray     LightGPU[8] 512 bytes
///   [640..767] _pad1          float[32]   128 bytes  (pad to 768)
/// ```
struct PerFrameMultiLightCB {
    Mat4     viewProj                    = {}; ///< Combined view-projection matrix.
    Vec4     cameraPos                   = {}; ///< xyz = world camera position, w = unused.
    Vec4     timeParams                  = {}; ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    Vec4     ambientColor                = {}; ///< xyz = ambient contribution, w = unused.
    uint32_t lightCount                  = 0;  ///< Number of active entries in `lightArray`.
    uint32_t _pad0[3]                    = {}; ///< Align `lightArray` to 16-byte boundary.
    LightGPU lightArray[kMaxMultiLights] = {}; ///< GPU light descriptors (first `lightCount` live).
    uint32_t irradianceSrvIndex          = UINT32_MAX; ///< SRV index for irradiance cubemap (UINT32_MAX = no IBL).
    uint32_t prefilteredSrvIndex         = UINT32_MAX; ///< SRV index for prefiltered specular cubemap.
    uint32_t brdfLutSrvIndex             = UINT32_MAX; ///< SRV index for BRDF integration LUT.
    uint32_t prefilteredMipLevels        = 8;          ///< Number of prefiltered mip levels (must match IBLBaker::kPrefilteredMipLevels).
    float    _pad1[28]                   = {}; ///< Pad struct to 768 bytes (3 x 256).
};

static_assert(sizeof(PerFrameMultiLightCB) % 256 == 0,
    "PerFrameMultiLightCB must be a multiple of 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// LightCPU — CPU-side light descriptor (not uploaded directly)
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side light data consumed by the culling and sorting stage before upload.
struct LightCPU {
    Vec3      position    = {};               ///< World-space position.
    float     range       = 10.0f;            ///< Attenuation radius in world units.
    Vec3      color       = {1.f, 1.f, 1.f}; ///< Linear HDR color x intensity.
    LightType type        = LightType::Point;
    Vec3      direction   = {0.f, -1.f, 0.f};///< Cone axis (directional / spot).
    float     innerAngle  = 0.3f;             ///< Inner cone half-angle in radians.
    float     outerAngle  = 0.5f;             ///< Outer cone half-angle in radians.
};

// ─────────────────────────────────────────────────────────────────────────────
// Attenuation math
// ─────────────────────────────────────────────────────────────────────────────

/// Computes inverse-square attenuation for a point or spot light.
///
/// @ref Lagarde 2014 "Moving Frostbite to Physically Based Rendering" — smooth distance attenuation window
/// Formula:
/// ```
///   distRatio   = clamp(dist / range, 0, 1)
///   windowFn    = (1 - distRatio^2)^2
///   invSqFactor = 1 / (dist^2 + 1)
///   attenuation = windowFn * invSqFactor
/// ```
///
/// # Returns
/// Scalar in [0, 1]. Exactly 0.0 when `dist >= range` or `range <= 0`.
[[nodiscard]] inline float ComputePointAttenuation(float dist, float range) noexcept {
    if (range <= 0.0f) {
        return 0.0f;
    }
    float distRatio = dist / range;
    distRatio = distRatio < 0.0f ? 0.0f : (distRatio > 1.0f ? 1.0f : distRatio);
    const float window = (1.0f - distRatio * distRatio) * (1.0f - distRatio * distRatio);
    const float invSq  = 1.0f / (dist * dist + 1.0f);
    return window * invSq;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cone falloff math
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the cone intensity factor for a spot light.
///
/// Formula:
/// ```
///   t           = clamp((cosAngle - outerCos) / (innerCos - outerCos), 0, 1)
///   spotFactor  = t * t
/// ```
///
/// # Returns
/// Scalar in [0, 1]. 1.0 inside the inner cone, 0.0 outside the outer cone.
[[nodiscard]] inline float ComputeSpotConeFactor(float cosAngle,
                                                  float innerConeCos,
                                                  float outerConeCos) noexcept {
    const float denom = innerConeCos - outerConeCos;
    if (denom < 1e-6f && denom > -1e-6f) {
        return cosAngle >= innerConeCos ? 1.0f : 0.0f;
    }
    float t = (cosAngle - outerConeCos) / denom;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t;
}

// ─────────────────────────────────────────────────────────────────────────────
// SelectNearestLights — CPU culling and sorting
// ─────────────────────────────────────────────────────────────────────────────

/// Selects and converts the nearest visible lights from a CPU candidate array
/// into GPU-ready `LightGPU` entries.
///
/// Sorts by squared distance to `cameraPos`. Skips lights further than their
/// range from the camera. Writes at most `outCapacity` entries.
///
/// # Returns
/// Number of `LightGPU` entries written into `outLights` (0 to outCapacity).
[[nodiscard]] inline uint32_t SelectNearestLights(const LightCPU* candidates,
                                                   uint32_t        count,
                                                   const Vec3&     cameraPos,
                                                   LightGPU*       outLights,
                                                   uint32_t        outCapacity) noexcept {
    if (count == 0 || candidates == nullptr || outLights == nullptr || outCapacity == 0) {
        return 0u;
    }

    struct SortEntry {
        uint32_t index;
        float    sqDist;
    };

    constexpr uint32_t kMaxBuf = 256u;
    SortEntry sortBuf[kMaxBuf];
    uint32_t  validCount = 0u;

    for (uint32_t i = 0u; i < count && validCount < kMaxBuf; ++i) {
        const LightCPU& l = candidates[i];

        if (l.type == LightType::Directional) {
            sortBuf[validCount++] = {i, 0.0f};
            continue;
        }

        const float dx     = l.position.x - cameraPos.x;
        const float dy     = l.position.y - cameraPos.y;
        const float dz     = l.position.z - cameraPos.z;
        const float sqDist = dx * dx + dy * dy + dz * dz;

        if (sqDist > l.range * l.range) {
            continue; // Cull — camera outside attenuation radius.
        }

        sortBuf[validCount++] = {i, sqDist};
    }

    std::sort(sortBuf, sortBuf + validCount,
              [](const SortEntry& a, const SortEntry& b) noexcept {
                  return a.sqDist < b.sqDist;
              });

    uint32_t written = 0u;
    for (uint32_t i = 0u; i < validCount && written < outCapacity; ++i) {
        const LightCPU& src = candidates[sortBuf[i].index];
        LightGPU&       dst = outLights[written];

        dst               = LightGPU{};
        dst.positionOrDir = src.position;
        dst.range         = src.range;
        dst.color         = src.color;
        dst.lightType     = static_cast<float>(static_cast<uint32_t>(src.type));
        dst.spotDirection = src.direction;
        dst.innerConeCos  = std::cos(src.innerAngle);
        dst.outerConeCos  = std::cos(src.outerAngle);

        ++written;
    }

    return written;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildMultiLightCB — convenience factory
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a `PerFrameMultiLightCB` from CPU light data and ambient parameters.
///
/// Calls `SelectNearestLights()` internally. The caller fills `viewProj`,
/// `cameraPos`, and `timeParams` separately before GPU upload.
///
/// # Returns
/// A `PerFrameMultiLightCB` with `lightCount`, `lightArray`, and `ambientColor`
/// populated. `viewProj`, `cameraPos`, `timeParams` are zero-initialised.
[[nodiscard]] inline PerFrameMultiLightCB BuildMultiLightCB(const LightCPU* candidates,
                                                             uint32_t        count,
                                                             const Vec3&     cameraPos,
                                                             const Vec3&     ambientColor) noexcept {
    PerFrameMultiLightCB cb{};
    cb.ambientColor = {ambientColor.x, ambientColor.y, ambientColor.z, 0.0f};
    cb.lightCount   = SelectNearestLights(
        candidates, count, cameraPos, cb.lightArray, kMaxMultiLights);
    return cb;
}

} // namespace uz
