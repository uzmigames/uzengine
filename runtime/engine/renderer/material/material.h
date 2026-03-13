/// UzEngine PBR Material Definition
///
/// Defines the `PBRMaterial` struct uploaded to the GPU as a StructuredBuffer
/// element. Each mesh references a material by index via `PerObjectCB.materialIndex`.

#pragma once

#include "vector4.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PBRMaterial — Cook-Torrance BRDF parameters (48 bytes, GPU-uploadable)
// ─────────────────────────────────────────────────────────────────────────────

/// PBR material parameters for Cook-Torrance BRDF.
///
/// Uploaded to the GPU as an element of a StructuredBuffer. Each draw call
/// references a material by index stored in `PerObjectCB.materialIndex`.
///
/// The layout is identical to the HLSL struct — do not reorder fields.
///
/// # HLSL Layout
/// ```hlsl
/// struct PBRMaterial {
///     float4 baseColor;   // rgb = albedo (linear sRGB), a = opacity
///     float  metallic;    // 0 = dielectric, 1 = fully metallic
///     float  roughness;   // 0 = mirror surface, 1 = fully diffuse
///     float  ao;          // ambient occlusion multiplier [0, 1]
///     float  _pad0;       // explicit padding for HLSL alignment
///     float4 emissive;    // rgb = emissive color, a = intensity scale
/// };
/// ```
///
/// # Size
/// 48 bytes. No implicit padding — the struct is fully explicit.
///
/// # GPU Upload
/// Use a StructuredBuffer or ByteAddressBuffer. Stride = 48 bytes.
struct PBRMaterial {
    /// Albedo color in linear sRGB + opacity.
    ///
    /// rgb channels are the base reflectance color before metallic/roughness
    /// modulation. Alpha channel is the surface opacity (1 = fully opaque).
    Vec4 baseColor{1.0F, 1.0F, 1.0F, 1.0F};

    /// Metallic factor [0, 1].
    ///
    /// 0 = dielectric (uses F0 = 0.04). 1 = fully metallic (F0 = baseColor).
    /// Intermediate values are physically unusual — prefer 0 or 1 in practice.
    float metallic = 0.0F;

    /// Perceptual roughness [0, 1].
    ///
    /// 0 = perfect mirror (GGX lobe collapses to specular peak).
    /// 1 = fully diffuse (GGX distribution spread uniformly over hemisphere).
    /// Remapped to alpha = roughness^2 inside the shader.
    float roughness = 0.5F;

    /// Ambient occlusion multiplier [0, 1].
    ///
    /// Scales the indirect ambient contribution per-texel. 1 = no occlusion.
    /// Typically sampled from an AO texture baked offline.
    float ao = 1.0F;

    /// Explicit HLSL alignment padding. Must remain zero.
    float _pad0 = 0.0F;

    /// Emissive color and intensity.
    ///
    /// rgb channels are the emissive color in linear sRGB.
    /// w channel is an intensity multiplier (0 = no emission).
    /// The final emissive radiance = emissive.rgb * emissive.a.
    Vec4 emissive{0.0F, 0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(PBRMaterial) == 48,
    "PBRMaterial must be 48 bytes for GPU upload — check field order and padding");

} // namespace uz
