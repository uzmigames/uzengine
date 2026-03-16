/// UzEngine Equirectangular-to-Cubemap Conversion Pass -- Implementation.
///
/// Registers and executes a compute pass that converts an HDR equirectangular
/// panorama into an NxNx6 RGBA16F cubemap via the equirect_to_cubemap_cs.hlsl
/// compute shader.
///
/// Uses root constants (4 DWORDs at slot 0 = b0) instead of a CBV,
/// matching the sky cubemap capture and IBL baker patterns.

#include "equirect_to_cubemap_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Dispatch Dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matches [numthreads(8, 8, 1)] in the compute shader.
static constexpr uint32_t kThreadGroupSize = 8;

/// Number of cubemap faces. Dispatch Z = 6.
static constexpr uint32_t kCubeFaceCount = 6;

// ─────────────────────────────────────────────────────────────────────────────
// AddEquirectToCubemapPass
// ─────────────────────────────────────────────────────────────────────────────

void AddEquirectToCubemapPass(rg::RgBuilder& builder, const EquirectToCubemapPassDesc& desc)
{
    UZ_CHECK(desc.cubemapSize > 0, "Cubemap size must be non-zero");
    UZ_CHECK(desc.cubemapSize % kThreadGroupSize == 0,
        "Cubemap size must be a multiple of thread group size (8)");

    builder.AddPass("EquirectToCubemap", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.cubemapTarget, RHIResourceState::UnorderedAccess);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("EquirectToCubemap::Execute");

            // ── Bind compute pipeline ──────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Set root constants at slot 0 (b0) -- 4 DWORDs ──────────
            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&desc.rootConstants), 4);

            // ── Bind output UAV at root param 4 (u0 descriptor table) ──
            cmd->SetDescriptorTable(4, desc.cubemapUavIndex);

            // ── Dispatch: size/8 groups X, size/8 groups Y, 6 faces Z ──
            const uint32_t groups = desc.cubemapSize / kThreadGroupSize;
            cmd->Dispatch(groups, groups, kCubeFaceCount);
        });
    });
}

} // namespace uz
