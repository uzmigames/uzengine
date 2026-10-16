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
#include "constant_buffers.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
/// 20       4      emissiveIndex (uint32_t)
/// 24       4      alphaClipThreshold (float)
/// 28       4      opacitySrvIndex (uint32_t)
/// 32       64     model (Mat4)
/// 96       64     normalMatrix (Mat4)
/// 160      12     emissiveColor (3 floats)
/// 172      4      emissiveIntensity (float)
/// 176      12     baseColorTint (3 floats)
/// 188      4      roughnessMultiplier (float)
/// 192      4      metallicMultiplier (float)
/// 196      4      normalIntensity (float)
/// 200      8      uvScale (2 floats)
/// 208      8      uvOffset (2 floats)
/// 216      4      aoIntensity (float)
/// 220      4      opacity (float)
/// 224      28     displacementScale..pomSelfShadow (POM disabled by default)
/// 252      4      materialFlags (uint32_t)
/// ─── 256 bytes (CB boundary) ───
/// 256      8      albedoTexture (RHITextureHandle, NOT in CB)
/// 264      8      normalTexture (RHITextureHandle, NOT in CB)
/// 272      8      roughnessTexture (RHITextureHandle, NOT in CB)
/// 280      8      metallicTexture (RHITextureHandle, NOT in CB)
/// 288      8      aoTexture (RHITextureHandle, NOT in CB)
/// 296      8      emissiveTexture (RHITextureHandle, NOT in CB)
/// ```
struct GBufferPerObjectParams : PerObjectCB {
    // The shared POM defaults are uploaded too; displacementScale = 0 keeps POM off.

    // ── Resource handles (NOT copied to CB) ──────────────────────────────
    // Bound by ShaderParameterBinder at their HLSL register slots on SM5.

    RHITextureHandle albedoTexture;     ///< BaseColor map (bound at t0 on SM5).
    RHITextureHandle normalTexture;     ///< Normal map (bound at t1 on SM5).
    RHITextureHandle roughnessTexture;  ///< Roughness map (bound at t2 on SM5).
    RHITextureHandle metallicTexture;   ///< Metallic map (bound at t3 on SM5).
    RHITextureHandle aoTexture;         ///< Ambient occlusion map (bound at t4 on SM5).
    RHITextureHandle emissiveTexture;   ///< Emissive map (bound at t5 on SM5).

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
static_assert(std::is_standard_layout_v<PerObjectCB>);
static_assert(offsetof(GBufferPerObjectParams, albedoTexture) == sizeof(PerObjectCB));
static_assert(offsetof(GBufferPerObjectParams, albedoTexture) == 256,
              "GBufferPerObjectParams resource section must start at byte 256 "
              "(after PerObjectCB-compatible scalar section)");

} // namespace uz
