/// UzEngine Multi-Light Support — GPU-accelerated per-pixel multi-light evaluation
///
/// Supports up to 8 dynamic point and spot lights per render pass with
/// per-light distance falloff, spot-light cone attenuation, and
/// CPU-side light culling (sort + nearest-N selection).
///
/// # Phase Status
/// **IMPLEMENTATION PENDING**: Spec defined. Tests in test_multi_light.cpp.
/// This module will be completed in phase3a-3.6.0-multi-light.

#pragma once

#include "../../math/vector3.h"
#include <cstdint>
#include <array>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Light Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Maximum number of dynamic lights that can be evaluated per-pixel.
///
/// Limited to 8 to fit within a D3D12 constant buffer register constraint
/// (8 × 64-byte LightGPU structures = 512 bytes of light data).
constexpr uint32_t kMaxMultiLights = 8;

// ─────────────────────────────────────────────────────────────────────────────
// Light GPU Data
// ─────────────────────────────────────────────────────────────────────────────

/// Per-light GPU data for multi-light shading (exactly 64 bytes, HLSL-friendly).
///
/// Packed struct for constant buffer transmission to GPU shaders.
/// Layout is HLSL-compatible with float3 → float4 padding:
/// - [0..15] positionOrDir (float3) + range (float)
/// - [16..31] color (float3) + lightType (float)
/// - [32..47] spotDirection (float3) + innerConeCos (float)
/// - [48..63] outerConeCos (float) + padding (float3)
///
/// The float3 fields are implicitly padded to float4 (16 bytes each) to match
/// D3D HLSL constant buffer alignment requirements.
struct alignas(16) LightGPU {
    /// World-space position for point/spot lights, direction for directional.
    Vec3 positionOrDir;

    /// Effective range/radius in world units. 0 = directional (infinite range).
    float range = 0.0f;

    /// Linear HDR color (RGB), pre-multiplied by intensity.
    Vec3 color = Vec3(0.0f, 0.0f, 0.0f);

    /// Light type: 0 = directional, 1 = point, 2 = spot.
    float lightType = 0.0f;

    /// Normalized cone axis direction (for spot lights only).
    Vec3 spotDirection = Vec3(0.0f, 0.0f, 1.0f);

    /// cos(innerConeHalfAngle) for smooth spot-light falloff.
    float innerConeCos = -1.0f;

    /// cos(outerConeHalfAngle) for hard spot-light cutoff.
    float outerConeCos = -1.0f;

    /// Padding to reach 64 bytes (required for HLSL constant buffer alignment).
    float _pad = 0.0f;

    /// Default constructor zeroes all fields for safe constant buffer initialization.
    LightGPU() = default;
};

static_assert(sizeof(LightGPU) == 64, "LightGPU must be exactly 64 bytes");
static_assert(alignof(LightGPU) == 16, "LightGPU must be 16-byte aligned");

/// Per-frame multi-light constant buffer (carries up to 8 lights + metadata).
///
/// Binds to a D3D12 constant buffer register. Total size is a multiple of 256
/// bytes (D3D12 requirement). The CPU populates this each frame by culling and
/// sorting nearby lights from the scene.
struct alignas(256) PerFrameMultiLightCB {
    /// Number of active lights in this frame (clamped to [0, 8]).
    uint32_t lightCount = 0;

    /// Padding to 16-byte alignment before light array.
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    uint32_t _pad2 = 0;

    /// Array of up to 8 lights. Only the first `lightCount` are valid.
    /// Each element is 64 bytes; 8 elements = 512 bytes total.
    std::array<LightGPU, kMaxMultiLights> lightArray = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// Light Attenuation Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Compute point-light attenuation using inverse-square falloff (UE4/Frostbite style).
///
/// Formula:
/// ```
/// distRatio = clamp(distance / range, 0, 1)
/// attenuation = (1 - distRatio^2)^2 × (1 / (distance^2 + 1))
/// ```
///
/// The second term softens the singularity at distance=0 while preserving the
/// correct falloff shape. Reaches exactly 0.0 at the range boundary.
///
/// # Parameters
/// * `distance` — Distance from light to surface point (in world units).
/// * `range` — Effective radius of the light (in world units).
///
/// # Returns
/// Attenuation factor in [0, 1]. Returns 0.0 if distance > range or range <= 0.
///
/// # Examples
/// ```cpp
/// float att = ComputePointAttenuation(5.0f, 10.0f);  // ~0.56
/// float att = ComputePointAttenuation(10.0f, 10.0f); // 0.0 (at boundary)
/// float att = ComputePointAttenuation(15.0f, 10.0f); // 0.0 (beyond range)
/// ```
[[nodiscard]] float ComputePointAttenuation(float distance, float range);

/// Compute spot-light cone attenuation using smooth falloff between inner and outer angles.
///
/// The cone falloff is a smooth Hermite step between the inner (full intensity)
/// and outer (zero intensity) cone angles. This avoids harsh aliasing at the cone edge.
///
/// Formula:
/// ```
/// cosConeAngle = dot(normalize(direction), normalize(lightToPoint))
/// attenuation = smoothstep(cosOuterAngle, cosInnerAngle, cosConeAngle)
/// ```
///
/// # Parameters
/// * `cosConeAngle` — cos(angle between spot direction and light-to-point vector).
/// * `cosInnerAngle` — cos(inner half-cone angle) — full intensity.
/// * `cosOuterAngle` — cos(outer half-cone angle) — zero intensity.
///
/// # Returns
/// Cone attenuation factor in [0, 1]. Returns 1.0 if within inner cone,
/// smoothly blends between inner and outer, returns 0.0 beyond outer cone.
///
/// # Examples
/// ```cpp
/// float att = ComputeSpotConeAttenuation(0.9f, 0.87f, 0.71f);  // ~0.75 (edge)
/// float att = ComputeSpotConeAttenuation(0.95f, 0.87f, 0.71f); // 1.0 (inside)
/// float att = ComputeSpotConeAttenuation(0.5f, 0.87f, 0.71f);  // 0.0 (outside)
/// ```
[[nodiscard]] float ComputeSpotConeAttenuation(float cosConeAngle, float cosInnerAngle, float cosOuterAngle);

} // namespace uz
