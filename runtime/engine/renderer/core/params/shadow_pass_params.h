/// UzEngine Shadow Pass Shader Parameters — metadata-annotated ShadowCB and shadow PerObjectCB.
///
/// Two parameter structs for the shadow depth pass:
///
/// 1. `ShadowCascadeParams` — Per-cascade CB containing the light-space view-projection
///    matrix. Bound at root parameter slot 3 (register b4). Scalar section is 256 bytes
///    (the Mat4 viewProj occupies 64 bytes, remainder is padding).
///
/// 2. `ShadowPerObjectParams` — Per-object CB containing only the world matrix.
///    Byte-identical to `PerObjectCB` scalar layout but carries no texture resources
///    (shadow pass is depth-only, no pixel shader textures needed).
///
/// # See Also
/// - `shadow_pass.h` — Shadow pass descriptor and registration.
/// - `constant_buffers.h` — Original `PerObjectCB` definition.
/// - `shader_parameter_binder.h` — Binder that consumes this metadata.

#pragma once

#include "shader_parameters.h"
#include "mat4.h"

#include <cstddef>
#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCascadeParams — per-cascade CB at root slot 3 (register b4)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-cascade constant buffer for shadow rendering.
///
/// Contains the light-space view-projection matrix for the current cascade.
/// No resource handles — the shadow pass is depth-only.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        64     viewProj (Mat4)
/// 64       192    _pad[48]
/// ─── 256 bytes (CB boundary) ───
/// ```
struct ShadowCascadeParams {
    // ── Scalar section (256 bytes, copied to CB) ─────────────────────────

    Mat4  viewProj;            ///< Light-space view-projection matrix.
    float _pad[48] = {};       ///< Pad to 256-byte alignment.

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    static const ShaderParameterMetadata& GetMetadata();
};

static_assert(sizeof(ShadowCascadeParams) == 256,
              "ShadowCascadeParams must be 256 bytes (D3D12 CB alignment)");

// ─────────────────────────────────────────────────────────────────────────────
// ShadowPerObjectParams — per-object CB at root slot 2 (register b2)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-object constant buffer for shadow rendering (depth-only).
///
/// Scalar layout is byte-identical to `PerObjectCB`. The shadow pass only
/// needs the model matrix — material indices and normal matrix are ignored
/// by the depth-only vertex shader but remain in the layout for binary
/// compatibility with the shared root signature.
///
/// No resource handles: shadow pass has no pixel shader textures.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        4      albedoIndex (uint32_t, unused by shadow shader)
/// 4        4      normalIndex (uint32_t, unused)
/// 8        4      roughnessIndex (uint32_t, unused)
/// 12       4      metallicIndex (uint32_t, unused)
/// 16       4      aoIndex (uint32_t, unused)
/// 20       12     _pad[3]
/// 32       64     model (Mat4) — only field read by shadow VS
/// 96       64     normalMatrix (Mat4, unused)
/// 160      96     _pad2[24]
/// ─── 256 bytes (CB boundary) ───
/// ```
struct ShadowPerObjectParams {
    // ── Scalar section (256 bytes, copied to CB) ─────────────────────────
    // Layout MUST match PerObjectCB exactly.

    uint32_t albedoIndex    = 0;    ///< Unused by shadow shader.
    uint32_t normalIndex    = 0;    ///< Unused by shadow shader.
    uint32_t roughnessIndex = 0;    ///< Unused by shadow shader.
    uint32_t metallicIndex  = 0;    ///< Unused by shadow shader.
    uint32_t aoIndex        = 0;    ///< Unused by shadow shader.
    float    _pad[3]        = {};   ///< Align to 16-byte boundary.
    Mat4     model;                 ///< Object-to-world transform (read by shadow VS).
    Mat4     normalMatrix;          ///< Unused by shadow shader.
    float    _pad2[24]      = {};   ///< Pad to 256-byte alignment.

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    static const ShaderParameterMetadata& GetMetadata();
};

static_assert(sizeof(ShadowPerObjectParams) == 256,
              "ShadowPerObjectParams must be 256 bytes "
              "(must match PerObjectCB for root signature compatibility)");

} // namespace uz
