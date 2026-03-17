/// UzEngine GBuffer Per-Object Shader Parameters — metadata-annotated PerObjectCB.
///
/// `GBufferPerObjectParams` mirrors the scalar layout of `PerObjectCB` byte-for-byte
/// (first 256 bytes are identical), then appends RHI resource handles for material
/// textures. The `ShaderParameterBinder` uses the metadata to:
/// 1. Copy the 256-byte scalar section to the CB ring.
/// 2. Bind material textures at register slots t0-t4 (SM5 fallback path).
///
/// # Migration Strategy
/// `GBufferPerObjectParams` coexists with `PerObjectCB`. Passes that adopt the new
/// binder use `GBufferPerObjectParams`; legacy passes continue using `PerObjectCB`
/// with manual `SetFragmentTexture` calls. Once all passes are migrated, the manual
/// binding code can be removed.
///
/// # HLSL Register Layout for Material Textures (SM5)
/// - t0: Albedo (BaseColor) map
/// - t1: Normal map
/// - t2: Roughness map
/// - t3: Metallic map
/// - t4: Ambient Occlusion map
///
/// # See Also
/// - `constant_buffers.h` — Original `PerObjectCB` definition.
/// - `shader_parameters.h` — Metadata types used by `GetMetadata()`.
/// - `shader_parameter_binder.h` — Binder that consumes this metadata.

#pragma once

#include "shader_parameters.h"
#include "mat4.h"

#include <cstddef>
#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GBufferPerObjectParams — extends PerObjectCB with material texture handles
// ─────────────────────────────────────────────────────────────────────────────

/// Per-object shader parameters with metadata for automatic CB upload and
/// material texture binding.
///
/// The scalar section (bytes 0-255) is byte-identical to `PerObjectCB`.
/// Resource handles are appended after the scalar section and are NOT
/// copied to the GPU constant buffer — they are bound via SRV descriptors.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        4      albedoIndex (uint32_t)
/// 4        4      normalIndex (uint32_t)
/// 8        4      roughnessIndex (uint32_t)
/// 12       4      metallicIndex (uint32_t)
/// 16       4      aoIndex (uint32_t)
/// 20       12     _pad[3]
/// 32       64     model (Mat4)
/// 96       64     normalMatrix (Mat4)
/// 160      96     _pad2[24]
/// ─── 256 bytes (CB boundary) ───
/// 256      8      albedoTexture (RHITextureHandle, NOT in CB)
/// 264      8      normalTexture (RHITextureHandle, NOT in CB)
/// 272      8      roughnessTexture (RHITextureHandle, NOT in CB)
/// 280      8      metallicTexture (RHITextureHandle, NOT in CB)
/// 288      8      aoTexture (RHITextureHandle, NOT in CB)
/// ```
struct GBufferPerObjectParams {
    // ── Scalar section (256 bytes, copied to CB) ─────────────────────────
    // Layout MUST match PerObjectCB exactly — same offsets, same padding.
    // Scalars first (offset 0) to avoid std140 matrix padding issues.

    uint32_t albedoIndex    = 0;    ///< SRV heap index for BaseColor map.
    uint32_t normalIndex    = 0;    ///< SRV heap index for normal map.
    uint32_t roughnessIndex = 0;    ///< SRV heap index for roughness map.
    uint32_t metallicIndex  = 0;    ///< SRV heap index for metallic map.
    uint32_t aoIndex        = 0;    ///< SRV heap index for AO map.
    float    _pad[3]        = {};   ///< Align to 16-byte boundary before matrices.
    Mat4     model;                 ///< Object-to-world transform.
    Mat4     normalMatrix;          ///< transpose(inverse(model)) for lighting.
    float    _pad2[24]      = {};   ///< Pad to 256-byte alignment.

    // ── Resource handles (NOT copied to CB) ──────────────────────────────
    // Bound by ShaderParameterBinder at their HLSL register slots on SM5.

    RHITextureHandle albedoTexture;     ///< BaseColor map (bound at t0 on SM5).
    RHITextureHandle normalTexture;     ///< Normal map (bound at t1 on SM5).
    RHITextureHandle roughnessTexture;  ///< Roughness map (bound at t2 on SM5).
    RHITextureHandle metallicTexture;   ///< Metallic map (bound at t3 on SM5).
    RHITextureHandle aoTexture;         ///< Ambient occlusion map (bound at t4 on SM5).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    ///
    /// The metadata tells `ShaderParameterBinder` which bytes to copy to
    /// the CB (the first 256 bytes) and which resource handles to bind.
    ///
    /// # Returns
    /// Reference to a static `ShaderParameterMetadata` instance.
    /// Valid for the lifetime of the program.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify scalar section matches PerObjectCB exactly
static_assert(offsetof(GBufferPerObjectParams, albedoTexture) == 256,
              "GBufferPerObjectParams resource section must start at byte 256 "
              "(after PerObjectCB-compatible scalar section)");

} // namespace uz
