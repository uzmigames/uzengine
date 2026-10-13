/// UzEngine GPU Frustum + HiZ Occlusion Culling Pass — Implementation
///
/// Registers and executes a compute pass that performs two-phase culling:
/// 1. Frustum culling via 6-plane Gribb-Hartmann test.
/// 2. HiZ occlusion culling via sphere-to-screen-rect projection and HZB sampling.
///

#include "gpu_cull_pass.h"

#include "render_graph_builder.h"
#include "render_graph_states.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matching [numthreads(64, 1, 1)] in gpu_cull_cs.hlsl.
static constexpr uint32_t kCullThreadGroupSize = 64;

// Root parameter slot indices (must match the global root signature).
static constexpr uint32_t kSlotRootConstants = 0;  // b0 — root constants (unused here)
static constexpr uint32_t kSlotPerFrame      = 1;  // b1 — root CBV (CullConstants)
static constexpr uint32_t kSlotUAVTable      = 4;  // u0..u3 — UAV descriptor table
static constexpr uint32_t kSlotSRVTable      = 5;  // t0..tN — SRV descriptor table (SM5)

// ─────────────────────────────────────────────────────────────────────────────
// AddGpuCullPass
// ─────────────────────────────────────────────────────────────────────────────

void AddGpuCullPass(rg::RgBuilder& builder, const GpuCullPassDesc& desc)
{
    UZ_CHECKF(desc.instanceCount > 0,
              "GpuCullPass: instanceCount must be > 0");
    UZ_CHECKF(desc.pipeline.IsValid(),
              "GpuCullPass: pipeline handle is invalid");

    builder.AddPass("GpuCull_FrustumHiZ", [desc](rg::RgPassBuilder& pb) {
        // No explicit resource tracking — buffers are external persistent
        // resources. The pass manages UAV barriers explicitly after dispatch.

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("GpuCull::Execute");

            // ── Bind pipeline ───────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind CullConstants via root CBV at slot 1 (b1) ──────────
            cmd->SetRootCBV(kSlotPerFrame,
                            desc.cullCBAlloc.bufferHandle,
                            desc.cullCBAlloc.offsetBytes,
                            reinterpret_cast<const uint8_t*>(desc.cullCBAlloc.cpuPtr),
                            desc.cullCBAlloc.sizeBytes);

            // ── Bind SRV descriptor table at slot 5 (t0: bounding spheres) ──
            // SM5: explicit descriptor table binding. Bindless uses
            // ResourceDescriptorHeap directly from the shader.
            if (!desc.bindless) {
                cmd->SetDescriptorTable(kSlotSRVTable, desc.boundingSphereSrvIndex);
            }

            // ── Bind UAV descriptor table at slot 4 (u0+u1) ─────────────
            cmd->SetDescriptorTable(kSlotUAVTable, desc.visibleIndexUavIndex);

            // ── Dispatch: ceil(instanceCount / 64) x 1 x 1 ─────────────
            const uint32_t groupsX = (desc.instanceCount + kCullThreadGroupSize - 1) / kCullThreadGroupSize;
            cmd->Dispatch(groupsX, 1, 1);

            // ── UAV barriers on output buffers ──────────────────────────
            // Ensure writes are complete before any subsequent pass reads.
            RHIBarrierDesc barriers[2] = {};
            barriers[0].buffer       = desc.visibleIndexBuffer;
            barriers[0].stateBefore  = RHIResourceState::UnorderedAccess;
            barriers[0].stateAfter   = RHIResourceState::ShaderResource;
            barriers[1].buffer       = desc.indirectArgsBuffer;
            barriers[1].stateBefore  = RHIResourceState::UnorderedAccess;
            barriers[1].stateAfter   = RHIResourceState::IndirectArgument;
            cmd->ResourceBarrier(barriers, 2);
            desc.states->Set(desc.indirectArgsBuffer, RHIResourceState::IndirectArgument);

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "GpuCullPass: first execution — {} instances, bindless={}",
                       desc.instanceCount, desc.bindless);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
