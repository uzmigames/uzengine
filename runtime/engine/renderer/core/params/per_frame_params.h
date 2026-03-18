/// UzEngine Per-Frame Shader Parameters — metadata-annotated version of PerFrameCB.
///
/// `PerFrameParams` mirrors the scalar layout of `PerFrameCB` byte-for-byte
/// (first 512 bytes are identical), then appends RHI resource handles for IBL
/// textures. The `ShaderParameterBinder` uses the metadata to:
/// 1. Copy the 512-byte scalar section to the CB ring.
/// 2. Bind IBL textures at register slots t5/t6/t7 (SM5 fallback path).
///
/// # Migration Strategy
/// `PerFrameParams` coexists with `PerFrameCB`. Passes that adopt the new
/// binder use `PerFrameParams`; legacy passes continue using `PerFrameCB`.
/// Once all passes are migrated, `PerFrameCB` can be removed.
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
#include "mat4.h"
#include "vector4.h"

#include <cstddef>
#include <cstdint>
#include <climits>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameParams — extends PerFrameCB with IBL resource handles
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame shader parameters with metadata for automatic CB upload and
/// IBL texture binding.
///
/// The scalar section (bytes 0-511) is byte-identical to `PerFrameCB`.
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
/// 436      20     filmSlope..filmWhiteClip (5 floats)
/// 456      4      gtaoSrvIndex
/// 460      4      brdfDebugMode
/// 464      4      heightFogContribution
/// 468      4      filmGrainIntensity
/// 472      4      vignetteIntensity
/// 476      4      chromaAbIntensity
/// 480      4      contactShadowLength
/// 484      36     _pad2[9]
/// ─── 512 bytes (CB boundary) ───
/// 512      8      irradianceCubemap (RHITextureHandle, NOT in CB)
/// 520      8      prefilteredSpecular (RHITextureHandle, NOT in CB)
/// 528      8      brdfLut (RHITextureHandle, NOT in CB)
/// ```
struct PerFrameParams {
    // ── Scalar section (512 bytes, copied to CB) ─────────────────────────
    // Layout MUST match PerFrameCB exactly — same offsets, same padding.

    Mat4     viewProj;                                 ///< Combined view-projection matrix.
    Vec4     cameraPos;                                ///< xyz = world-space camera position, w = unused.
    Vec4     timeParams;                               ///< x = totalTime, y = deltaTime, z = frameCount, w = unused.
    Vec4     lightDir;                                 ///< xyz = direction toward primary light, w = 0.
    Vec4     lightColor;                               ///< xyz = light color * intensity, w = unused.
    Vec4     ambientColor;                             ///< xyz = ambient color, w = unused.
    Mat4     invViewProj;                              ///< Inverse view-projection for position reconstruction.
    float    nearPlane            = 10.0f;             ///< Near clip distance (cm).
    float    farPlane             = 100000.0f;         ///< Far clip distance (cm).
    uint32_t irradianceSrvIndex   = UINT32_MAX;        ///< Bindless SRV index for irradiance cubemap.
    uint32_t prefilteredSrvIndex  = UINT32_MAX;        ///< Bindless SRV index for prefiltered specular.
    uint32_t brdfLutSrvIndex      = UINT32_MAX;        ///< Bindless SRV index for BRDF LUT.
    uint32_t prefilteredMipLevels = 5;                 ///< Number of prefiltered mip levels.
    float    skyLightIntensity    = 1.0f;              ///< IBL ambient multiplier.
    float    skyLightColorR       = 1.0f;              ///< Sky light tint (R).
    float    skyLightColorG       = 1.0f;              ///< Sky light tint (G).
    float    skyLightColorB       = 1.0f;              ///< Sky light tint (B).
    float    skyLightMinAmbient   = 0.0f;              ///< Minimum ambient floor (0-1).
    float    exposure             = 1.0f;              ///< Auto-exposure multiplier.
    Vec4     skyIrradianceSH[7]   = {};                ///< L2 SH coefficients for sky diffuse irradiance.
    uint32_t shadowDebugMode      = 0;                 ///< Shadow debug mode (0=off).

    // ── Aerial Perspective parameters (uses former padding) ───────────────
    uint32_t aerialPerspectiveEnabled = 0;             ///< 1 = apply aerial perspective in deferred lighting.
    uint32_t transmittanceLutSrvIndex = UINT32_MAX;    ///< SRV index for transmittance LUT.
    float    apBottomRadiusKm = 6360.0f;               ///< Planet surface radius (km).
    float    apTopRadiusKm    = 6420.0f;               ///< Top of atmosphere radius (km).
    float    apRayleighBetaX  = 5.802e-3f;             ///< Rayleigh scattering X (km^-1).
    float    apRayleighBetaY  = 13.558e-3f;            ///< Rayleigh scattering Y (km^-1).
    float    apRayleighBetaZ  = 33.1e-3f;              ///< Rayleigh scattering Z (km^-1).
    float    apRayleighScaleH = 8.0f;                  ///< Rayleigh scale height (km).
    float    apMieBeta        = 3.996e-3f;             ///< Mie scattering coefficient (km^-1).
    float    apMieScaleH      = 1.2f;                  ///< Mie scale height (km).
    float    apMieAnisotropy  = 0.8f;                  ///< Mie HG anisotropy parameter.
    float    apSunIntensity   = 1.0f;                  ///< Sun illuminance for AP inscattering.
    float    apRayleighScale  = 1.0f;                  ///< Artistic Rayleigh scattering multiplier.
    float    apMieScale       = 1.0f;                  ///< Artistic Mie scattering multiplier.

    // ── Film Tonemap parameters (UE5 parity) ─────────────────────────────
    /// @source UE5 PostProcessSettings.h — default values for FilmToneMap curve
    float    filmSlope      = 0.88f;                   ///< Steepness of the linear (straight) segment.
    float    filmToe        = 0.55f;                   ///< Size of the toe region (dark rolloff).
    float    filmShoulder   = 0.26f;                   ///< Size of the shoulder region (highlight rolloff).
    float    filmBlackClip  = 0.0f;                    ///< Lifts black level (adds to min output).
    float    filmWhiteClip  = 0.04f;                   ///< Lowers white ceiling (subtracts from max output).

    uint32_t gtaoSrvIndex        = UINT32_MAX;         ///< SRV index for GTAO filtered AO texture (UINT32_MAX = no GTAO).
    uint32_t brdfDebugMode       = 0;                 ///< BRDF debug (0=off, 1=split-screen: left=old, right=new).
    float    heightFogContribution = 0.0f;            ///< AP reduction factor when height fog active (0=none, 1=full).

    // ── Post-process film effects (UE5 parity) ─────────────────────────
    /// @source UE5 Scene.cpp:511 — FilmGrainIntensity default 0.0
    float    filmGrainIntensity = 0.0f;              ///< Film grain intensity (0=off). UE5 default: 0.0.
    /// @source UE5 Scene.cpp:505 — VignetteIntensity default 0.4
    float    vignetteIntensity  = 0.4f;              ///< Vignette darkening intensity. UE5 default: 0.4.
    /// @source UE5 Scene.cpp:419 — SceneFringeIntensity default 0.0
    float    chromaAbIntensity  = 0.0f;              ///< Chromatic aberration / scene fringe intensity (0=off). UE5 default: 0.0.

    /// @source UE5 LightRendering.cpp:640 — ContactShadowLength default 0.0 (off)
    /// @source UE5 DeferredLightingCommon.ush:172 — ContactShadowLength > 0 enables ray march
    /// Screen-space contact shadow ray length. 0.0 = disabled (UE5 default).
    /// Positive = screen-space fraction (0.02 = 2% of depth-scaled screen ray length).
    float    contactShadowLength = 0.0f;             ///< Contact shadow ray length (0=off, 0.02=typical).

    uint32_t ssrSrvIndex = UINT32_MAX;               ///< SRV index for resolved SSR texture (UINT32_MAX = no SSR).

    /// @source UE5 ViewUniformShaderParameters.h — MaterialTextureMipBias
    /// Global mipmap LOD bias for screen percentage compensation.
    /// 0 at 100%, negative below native resolution, positive for supersampling.
    float    mipBias = 0.0f;

    float    _pad2[7]            = {};                ///< Pad scalar section to 512 bytes.

    // ── Resource handles (NOT copied to CB) ──────────────────────────────
    // These are bound by ShaderParameterBinder at their HLSL register slots.

    RHITextureHandle irradianceCubemap;                ///< IBL irradiance cubemap (bound at t5 on SM5).
    RHITextureHandle prefilteredSpecular;              ///< IBL prefiltered specular cubemap (bound at t6 on SM5).
    RHITextureHandle brdfLut;                          ///< BRDF integration LUT (bound at t7 on SM5).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    ///
    /// The metadata tells `ShaderParameterBinder` which bytes to copy to
    /// the CB (the first 512 bytes) and which resource handles to bind.
    ///
    /// # Returns
    /// Reference to a static `ShaderParameterMetadata` instance.
    /// Valid for the lifetime of the program.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify scalar section matches PerFrameCB exactly
static_assert(offsetof(PerFrameParams, irradianceCubemap) == 512,
              "PerFrameParams resource section must start at byte 512 (after PerFrameCB-compatible scalar section)");

} // namespace uz
