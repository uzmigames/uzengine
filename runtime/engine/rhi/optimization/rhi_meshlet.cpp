/// UzEngine Meshlet Rendering — Implementation.
///
/// See rhi_meshlet.h for design overview and type documentation.

#include "rhi_meshlet.h"

#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Stat Counters
// ─────────────────────────────────────────────────────────────────────────────

UZ_DEFINE_STAT(RHI, MeshletDispatches);

// ─────────────────────────────────────────────────────────────────────────────
// Capability Query
// ─────────────────────────────────────────────────────────────────────────────

bool IsMeshletRenderingSupported() {
    // Mesh shader support requires SM6.5+ and explicit device capability check.
    // Not yet wired through the RHI — will be enabled when the DX12 backend
    // exposes D3D12_FEATURE_D3D12_OPTIONS7::MeshShaderTier.
    UZ_LOG(RHI, Info, "IsMeshletRenderingSupported: mesh shaders not yet enabled in RHI");
    return false;
}

}  // namespace uz
