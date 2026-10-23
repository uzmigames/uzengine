/// UzEngine Shadow Data Shader Parameters — metadata-annotated ShadowDataCB.
///
/// `ShadowDataParams` mirrors the scalar layout of `ShadowDataCB` byte-for-byte
/// (first 768 bytes are identical), then appends RHI resource handles for the
/// shadow atlas texture, static shadow atlas, and DFS output texture.
///
/// The `ShaderParameterBinder` uses the metadata to:
/// 1. Copy the 768-byte scalar section to the CB ring.
/// 2. Bind shadow textures at register slots t3/t8/t9 (SM5 fallback path).
///
/// # Migration Strategy
/// `ShadowDataParams` coexists with `ShadowDataCB`. Passes that adopt the new
/// binder use `ShadowDataParams`; legacy passes continue using `ShadowDataCB`.
/// Once all passes are migrated, `ShadowDataCB` can be simplified.
///
/// # HLSL Register Layout for Shadow Textures (SM5)
/// - t3: Shadow atlas depth texture (dynamic shadows)
/// - t8: Static shadow atlas (cached geometry shadows)
/// - t9: DFS output (distance field shadow screen-space result)
///
/// # See Also
/// - `constant_buffers.h` — Original `ShadowDataCB` definition.
/// - `shader_parameters.h` — Metadata types used by `GetMetadata()`.
/// - `shader_parameter_macros.h` — Macros used for declaration.

#pragma once

#include "shader_parameter_macros.h"
#include "constant_buffers.h"
#include "mat4.h"

#include <cstddef>
#include <cstdint>
#include <climits>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataParams — extends ShadowDataCB with shadow texture handles
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow data shader parameters with metadata for automatic CB upload and
/// shadow texture binding.
///
/// The scalar section (bytes 0-767) is byte-identical to `ShadowDataCB`.
/// Resource handles are appended after the scalar section and are NOT
/// copied to the GPU constant buffer.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        512    cascadeViewProj[8] (8 * Mat4)
/// 512      32     cascadeSplits[8]
/// 544      32     cascadeFadeStarts[8]
/// 576      32     cascadeDepthBias[8]
/// 608      32     cascadeNormalBias[8]
/// 640      4      shadowAtlasIndex (SRV heap index)
/// 644      4      cascadeCount
/// 648      4      shadowMapSize
/// 652      4      receiverBias
/// 656      4      shadowFadeStart
/// 660      4      shadowFadeEnd
/// 664      4      filterMethod
/// 668      4      tanLightSourceAngle
/// 672      4      maxSoftKernelSize
/// 676      4      atlasColumns
/// 680      4      staticShadowAtlasIndex
/// 684      4      useShadowCache
/// 688      4      dfsEnabled
/// 692      4      dfsOutputSrvIndex
/// 696      4      localShadowAtlasIndex
/// 700      4      localShadowCount
/// 704      4      localShadowNormalOffset
/// 708      4      softPCSS
/// 712      4      pcssMinPenumbraTexels
/// 716      4      pcssMaxPenumbraTexels
/// 720      32     cascadePcssDepthToTexels[8]
/// 752      4      localSoftPCSS
/// 756      4      localPcssSourceRadius
/// 760      8      _pad2[2]
/// ─── 768 bytes (CB boundary) ───
/// 768      8      shadowAtlas (RHITextureHandle, NOT in CB)
/// 776      8      staticShadowAtlas (RHITextureHandle, NOT in CB)
/// 784      8      dfsOutputTexture (RHITextureHandle, NOT in CB)
/// ```
struct ShadowDataParams {
    // ── Scalar section (768 bytes, copied to CB) ─────────────────────────
    // Layout MUST match ShadowDataCB exactly — same offsets, same padding.

    Mat4     cascadeViewProj[8];                          ///< Light-space viewProj per cascade.
    float    cascadeSplits[8]      = {};                  ///< View-space extended far split per cascade.
    float    cascadeFadeStarts[8]  = {};                  ///< View-space original far split per cascade.
    float    cascadeDepthBias[8]   = {};                  ///< Per-cascade computed depth bias.
    float    cascadeNormalBias[8]  = {};                  ///< Per-cascade computed normal bias.
    uint32_t shadowAtlasIndex;                             ///< SRV heap index for shadow atlas depth texture.
    uint32_t cascadeCount;                                ///< Number of active cascades (1 to 8).
    float    shadowMapSize;                               ///< Per-cascade resolution in texels.
    float    receiverBias;                                ///< Shadow receiver bias (Default: 0.9).
    float    shadowFadeStart;                             ///< World distance where last-cascade fade begins.
    float    shadowFadeEnd;                               ///< World distance where shadows fully disappear.
    uint32_t filterMethod;                                ///< 0=5x5 PCF, 1=Poisson, 2=PCSS.
    float    tanLightSourceAngle;                         ///< tan(sun disk angle) for PCSS.
    float    maxSoftKernelSize;                           ///< Max PCSS kernel radius in texels.
    uint32_t atlasColumns;                                ///< Atlas grid columns.
    uint32_t staticShadowAtlasIndex = UINT32_MAX;         ///< SRV index for cached static shadow atlas.
    uint32_t useShadowCache        = 0;                   ///< 0 = no cache, 1 = composite.
    uint32_t dfsEnabled            = 0;                   ///< 1 = DFS active.
    uint32_t dfsOutputSrvIndex     = UINT32_MAX;          ///< SRV index for DFS result texture.
    uint32_t localShadowAtlasIndex = UINT32_MAX;          ///< SRV index for local light shadow atlas.
    uint32_t localShadowCount      = 0;                   ///< Number of active local light shadows.
    float    localShadowNormalOffset = 0.005f;            ///< r.Shadow.Local.NormalOffset (block A.1).
    uint32_t softPCSS              = 0;                   ///< r.Shadows.SoftPCSS (1 = PCSS on the CSM).
    float    pcssMinPenumbraTexels = 0.5f;                ///< r.Shadow.PCSSMinPenumbra (texels).
    float    pcssMaxPenumbraTexels = 16.0f;               ///< r.Shadow.PCSSMaxPenumbra (texels).
    float    cascadePcssDepthToTexels[8] = {};            ///< Per-cascade penumbra texels per unit depth gap.
    uint32_t localSoftPCSS         = 0;                   ///< r.Shadows.SoftPCSS.Local.
    float    localPcssSourceRadius = 5.0f;                 ///< Local emitter radius in cm.
    float    _pad2[2]              = {};                  ///< Pad CSM section to 768 bytes.

    // ── Local Light Shadow Data ──────────────────────────────────────────
    LocalLightShadowData localShadows[kMaxLocalLightShadows] = {}; ///< Per-light shadow data (16 * 96 = 1536B).

    // ── Resource handles (NOT copied to CB) ──────────────────────────────

    RHITextureHandle shadowAtlas;                         ///< Shadow atlas depth texture (t3 on SM5).
    RHITextureHandle staticShadowAtlas;                   ///< Cached static shadow atlas (t8 on SM5).
    RHITextureHandle dfsOutputTexture;                    ///< DFS result texture (t9 on SM5).
    RHITextureHandle localShadowAtlas;                    ///< Local light shadow atlas (t11 on SM5).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify scalar section matches ShadowDataCB exactly
static_assert(offsetof(ShadowDataParams, shadowAtlas) == 2304,
              "ShadowDataParams resource section must start at byte 2304 (after ShadowDataCB-compatible scalar section)");

} // namespace uz
