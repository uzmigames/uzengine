/// UzEngine Lighting Shader Parameters — metadata-annotated LightingCB.
///
/// `LightingParams` mirrors the scalar layout of `LightingCB` byte-for-byte.
/// Currently has no resource handles (all textures in the lighting pass are
/// bound through `PerFrameParams` or `ShadowDataParams`), but the metadata
/// infrastructure is in place for future extensions (e.g., light cookie textures).
///
/// # See Also
/// - `constant_buffers.h` — Original `LightingCB` definition.
/// - `shader_parameters.h` — Metadata types used by `GetMetadata()`.

#pragma once

#include "shader_parameters.h"
#include "constant_buffers.h"

#include <cstddef>
#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LightingParams — metadata wrapper around LightingCB
// ─────────────────────────────────────────────────────────────────────────────

/// Lighting shader parameters with metadata for automatic CB upload.
///
/// The scalar section is byte-identical to `LightingCB`. No resource handles
/// are appended at this time — all textures used by the lighting pass are
/// bound through other parameter structs.
///
/// # Layout
/// ```
/// Offset   Size   Field
/// 0        3072   pointLights[64] (64 * 48 bytes)
/// 3072     4      pointLightCount
/// 3076     4      spotLightCount
/// 3080     8      _pad[2]
/// 3088     1024   spotLights[16] (16 * 64 bytes)
/// 4112     240    _pad2[60]
/// ─── 4352 bytes (CB boundary, 256-byte aligned) ───
/// ```
struct LightingParams {
    // ── Scalar section (4352 bytes, copied to CB) ────────────────────────
    // Layout MUST match LightingCB exactly.

    PointLightData pointLights[kMaxPointLights];     ///< Point light array (3072 bytes).
    uint32_t       pointLightCount = 0;              ///< Number of active point lights.
    uint32_t       spotLightCount  = 0;              ///< Number of active spot lights.
    uint32_t       _pad[2]         = {};             ///< Align counts to 16 bytes.
    SpotLightData  spotLights[kMaxSpotLights];       ///< Spot light array (1024 bytes).
    float          _pad2[60]       = {};             ///< Pad to 4352 bytes (256-byte multiple).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify layout parity with LightingCB
static_assert(sizeof(LightingParams) == sizeof(LightingCB),
              "LightingParams scalar section must match LightingCB size exactly");

} // namespace uz
