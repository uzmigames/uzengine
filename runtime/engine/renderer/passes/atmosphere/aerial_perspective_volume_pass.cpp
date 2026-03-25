/// UzEngine Aerial Perspective Volume Pass — Implementation.
///
/// Registers and executes a compute pass that fills a 32x32x16 RGBA16F
/// 3D texture via aerial_perspective_volume_cs.hlsl.
///
/// @ref RenderCameraAerialPerspectiveVolumeCS

#include "aerial_perspective_volume_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Volume Dispatch Dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matches [numthreads(8, 8, 1)] in the compute shader.
static constexpr uint32_t kThreadGroupSize = 8;

/// Volume XY resolution.
static constexpr uint32_t kVolumeXY = 32;

// ─────────────────────────────────────────────────────────────────────────────
// AddAerialPerspectiveVolumePass
// ─────────────────────────────────────────────────────────────────────────────

void AddAerialPerspectiveVolumePass(rg::RgBuilder& builder, const APVolumePassDesc& desc)
{
    builder.AddPass("APVolume", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.volumeTarget, RHIResourceState::UnorderedAccess);
        if (desc.transmittanceLutDep.IsValid()) {
            pb.Read(desc.transmittanceLutDep, RHIResourceState::ShaderResource);
        }
        if (desc.multiScatterLutDep.IsValid()) {
            pb.Read(desc.multiScatterLutDep, RHIResourceState::ShaderResource);
        }

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("APVolume::Execute");

            if (!cmd) {
                UZ_LOG(Renderer, Error, "APVolumePass: null command list");
                return;
            }

            // ── Bind compute pipeline ──────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Set root constants at slot 0 (b0) — 8 DWORDs ──────────
            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&desc.rootConstants), 8);

            // ── Bind APVolumeCB at root param 2 (b2) ────────────────────
            if (desc.cbAlloc.gpuAddr != 0) {
                cmd->SetRootCBV(2, desc.cbAlloc.gpuAddr);
            }

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                const uint32_t groupsXY = kVolumeXY / kThreadGroupSize;
                UZ_LOG(Renderer, Info,
                       "APVolumePass: first execution — "
                       "dispatch=({}, {}, {}), pipeline={}, volume={}x{}x{}",
                       groupsXY, groupsXY, desc.depthSlices,
                       desc.pipeline.index,
                       kVolumeXY, kVolumeXY, desc.depthSlices);
                loggedOnce = true;
            }

            // ── Dispatch: 32/8=4 groups X, 32/8=4 groups Y, 16 groups Z ──
            cmd->Dispatch(kVolumeXY / kThreadGroupSize,
                          kVolumeXY / kThreadGroupSize,
                          desc.depthSlices);
        });
    });
}

} // namespace uz
