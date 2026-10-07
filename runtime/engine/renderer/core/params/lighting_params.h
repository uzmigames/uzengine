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
/// 0        4096   pointLights[64] (64 * 64 bytes)
/// 4096     4      pointLightCount
/// 4100     4      spotLightCount
/// 4104     8      _pad[2]
/// 4112     1280   spotLights[16] (16 * 80 bytes)
/// 5392     240    _pad2[60]
/// ─── 5632 bytes (CB boundary, 256-byte aligned) ───
/// ```
struct LightingParams {
    // ── Scalar section (43264 bytes, copied to CB) ───────────────────────
    // Layout MUST match LightingCB exactly.

    PointLightData pointLights[kMaxPointLights];     ///< Point light array (32768 bytes).
    uint32_t       pointLightCount = 0;              ///< Number of active point lights.
    uint32_t       spotLightCount  = 0;              ///< Number of active spot lights.
    uint32_t       _pad[2]         = {};             ///< Align counts to 16 bytes.
    SpotLightData  spotLights[kMaxSpotLights];       ///< Spot light array (10240 bytes).
    float    clusterForwardX = 0.0f, clusterForwardY = 0.0f, clusterForwardZ = 1.0f;   ///< Block B.7 cluster parameters (LightingCB).
    float    clusterNear = 10.0f, clusterFar = 100000.0f, clusterLogScale = 0.0f;
    uint32_t clusterTileSize = kClusterTileSize, clusterTilesX = 0, clusterTilesY = 0, clusterSlices = kClusterSlices;
    uint32_t clusterListSrv = UINT32_MAX, clusterListUav = UINT32_MAX, clusterSlots = kClusterSlotsMax, clusterFlags = 0;
    float    clusterViewWidth = 0.0f, clusterViewHeight = 0.0f;
    float          _pad2[44]       = {};             ///< Pad to 43264 bytes (256-byte multiple).

    // ── Metadata ─────────────────────────────────────────────────────────

    /// Returns compile-time metadata describing this struct's layout.
    static const ShaderParameterMetadata& GetMetadata();
};

// Verify layout parity with LightingCB
static_assert(sizeof(LightingParams) == sizeof(LightingCB),
              "LightingParams scalar section must match LightingCB size exactly");

} // namespace uz
