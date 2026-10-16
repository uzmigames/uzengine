/// UzEngine Per-Frame Shader Parameters — metadata-annotated version of PerFrameCB.
///
/// `PerFrameParams` mirrors the scalar layout of `PerFrameCB` byte-for-byte
/// (first 1024 bytes are identical), then appends RHI resource handles for IBL
/// textures. The `ShaderParameterBinder` uses the metadata to:
/// 1. Copy the 1024-byte scalar section to the CB ring.
/// 2. Bind IBL textures at register slots t5/t6/t7 (SM5 fallback path).
///
/// # Migration Strategy
/// `PerFrameParams` coexists with `PerFrameCB`. Passes that adopt the new
/// binder use `PerFrameParams`; legacy passes continue using `PerFrameCB`.
/// `PerFrameCB` remains the shared scalar layout for both paths.
///
/// # HLSL Register Layout for IBL (SM5)
/// - t0-t4: G-Buffer textures (albedo, normal, depth, shadow, position)
/// - t5: Irradiance cubemap (IBL diffuse)
/// - t6: Prefiltered specular cubemap (IBL specular)
/// - t7: BRDF integration LUT (split-sum)
///
/// # See Also
/// - `constant_buffers.h` — Original `PerFrameCB` definition.
/// - `shader_parameters.h` — Metadata types used by `GetMetadata()`.
/// - `shader_parameter_binder.h` — Binder that consumes this metadata.

#pragma once

#include "shader_parameters.h"
#include "constant_buffers.h"

#include <cstddef>
#include <cstdint>
#include <climits>
#include <type_traits>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameParams — extends PerFrameCB with IBL resource handles
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame shader parameters with metadata for automatic CB upload and
/// IBL texture binding.
///
/// The scalar section (bytes 0-1023) is byte-identical to `PerFrameCB`.
/// Resource handles are appended after the scalar section and are NOT
/// copied to the GPU constant buffer — they are bound via SRV descriptors.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        64     viewProj (Mat4)
/// 64       16     cameraPos (Vec4)
/// 80       16     timeParams (Vec4)
/// 96       16     lightDir (Vec4)
/// 112      16     lightColor (Vec4)
/// 128      16     ambientColor (Vec4)
/// 144      64     invViewProj (Mat4)
/// 208      4      nearPlane
/// 212      4      farPlane
/// 216      4      irradianceSrvIndex
/// 220      4      prefilteredSrvIndex
/// 224      4      brdfLutSrvIndex
/// 228      4      prefilteredMipLevels
/// 232      4      skyLightIntensity
/// 236      4      skyLightColorR
/// 240      4      skyLightColorG
/// 244      4      skyLightColorB
/// 248      4      skyLightMinAmbient
/// 252      4      exposure
/// 256      112    skyIrradianceSH[7] (7 * Vec4)
/// 368      4      shadowDebugMode
/// 372      4      aerialPerspectiveEnabled
/// 376      4      transmittanceLutSrvIndex
/// 380      56     apBottomRadiusKm..apMieScale (14 floats)
/// 436      20     toneCurveSlope..toneCurveWhiteClip (5 floats)
/// 456      4      gtaoSrvIndex
/// 460      4      brdfDebugMode
/// 464      4      heightFogContribution
/// 468      4      filmGrainIntensity
/// 472      4      vignetteIntensity
/// 476      4      chromaAbIntensity
/// 480      4      contactShadowLength
/// 484      540    Remaining PerFrameCB scalars (see constant_buffers.h)
/// ─── 1024 bytes (CB boundary) ───
/// 1024     8      irradianceCubemap (RHITextureHandle, NOT in CB)
/// 1032     8      prefilteredSpecular (RHITextureHandle, NOT in CB)
/// 1040     8      brdfLut (RHITextureHandle, NOT in CB)
/// ```
struct PerFrameParams : PerFrameCB {
    // ── Resource handles (NOT copied to CB) ──────────────────────────────
    // These are bound by ShaderParameterBinder at their HLSL register slots.

    RHITextureHandle irradianceCubemap;                ///< IBL irradiance cubemap (bound at t5 on SM5).
    RHITextureHandle prefilteredSpecular;              ///< IBL prefiltered specular cubemap (bound at t6 on SM5).
    RHITextureHandle brdfLut;                          ///< BRDF integration LUT (bound at t7 on SM5).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    ///
    /// The metadata tells `ShaderParameterBinder` which bytes to copy to
    /// the CB (the first 1024 bytes) and which resource handles to bind.
    ///
    /// # Returns
    /// Reference to a static `ShaderParameterMetadata` instance.
    /// Valid for the lifetime of the program.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify scalar section matches PerFrameCB exactly
static_assert(std::is_standard_layout_v<PerFrameCB>);
static_assert(offsetof(PerFrameParams, irradianceCubemap) == sizeof(PerFrameCB));
static_assert(offsetof(PerFrameParams, sdfSampleRotation) == 968);
static_assert(offsetof(PerFrameParams, materialMapMask) == 972);
static_assert(offsetof(PerFrameParams, sdfRawClipmapSrv) == 976);
static_assert(offsetof(PerFrameParams, debugRegionUav) == 992);
static_assert(offsetof(PerFrameParams, debugRegionFields) == 1008);
static_assert(offsetof(PerFrameParams, materialRoughnessOverride) == 1012);
static_assert(offsetof(PerFrameParams, materialMetallicOverride) == 1016);
static_assert(offsetof(PerFrameParams, componentReplaySrv) == 1020);
static_assert(offsetof(PerFrameParams, irradianceCubemap) == 1024,
              "PerFrameParams resource section must start at byte 1024 (after PerFrameCB-compatible scalar section)");

} // namespace uz
