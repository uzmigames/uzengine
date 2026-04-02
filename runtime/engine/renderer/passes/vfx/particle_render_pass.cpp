/// VFX Particle Render Pass -- Implementation
///
/// Registers and executes a graphics pass that draws all active emitters
/// as camera-facing billboards using indirect draw calls. Emitters are sorted
/// back-to-front for correct alpha blending order.

#include "particle_render_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"
#include "engine/vfx/core/gpu_particle_buffer.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Root parameter slot for bindless descriptor indices CB (b0).
static constexpr uint32_t kSlotBindlessIndices = 0;

/// Root parameter slot for per-emitter constant buffer (b1).
static constexpr uint32_t kSlotPerEmitter = 1;

/// Root parameter slot for SRV descriptor table (t0..tN).
static constexpr uint32_t kSlotSRVTable = 5;

/// Stride of a single indirect draw argument structure in bytes.
/// Matches `sizeof(RHIDrawIndexedIndirectArgs)` (20 bytes).
static constexpr uint32_t kIndirectDrawArgsStride = 20;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Sorts emitters back-to-front by distance to camera.
///
/// Emitters farther from the camera are drawn first, ensuring correct
/// alpha blending when particles from different emitters overlap.
///
/// # Arguments
/// * `emitters` -- Mutable reference to the emitter array. Sorted in-place.
static void SortEmittersBackToFront(std::vector<EmitterRenderData>& emitters)
{
    std::sort(emitters.begin(), emitters.end(),
              [](const EmitterRenderData& a, const EmitterRenderData& b) {
                  return a.distanceToCamera > b.distanceToCamera;
              });
}

/// Transitions the indirect draw args buffer from IndirectArgument to
/// ShaderResource state if needed for the next pass. This is a defensive
/// barrier -- in most pipelines, the simulation pass already leaves the
/// buffer in IndirectArgument state.
///
/// Note: This is called before the draw to ensure the buffer is readable
/// as an indirect argument by the IA stage.
static void EnsureIndirectArgsReadable(IRHICommandList* cmd,
                                       const GPUParticleBuffer& buffer)
{
    if (!buffer.indirectDrawArgs.IsValid()) return;

    // The simulation pass transitions indirect args to IndirectArgument state.
    // No additional barrier needed here -- the buffer is already in the
    // correct state. This function exists as a hook for future barrier
    // coalescing if the pipeline order changes.
}

/// Draws a single emitter's particles using an indirect draw call.
///
/// Binds the per-emitter constant buffer and SRV descriptor table, then
/// issues a DrawIndexedIndirect call using the emitter's indirect draw
/// args buffer.
///
/// # Arguments
/// * `cmd` -- Command list to record into.
/// * `emitter` -- Per-emitter rendering data.
static void DrawEmitterBillboards(IRHICommandList* cmd,
                                  const EmitterRenderData& emitter)
{
    const auto& gpuBuf = *emitter.buffer;

    // Bind b0: VFXBillboardIndices as root constants (slot 0 is 32-bit root constants, not CBV).
    if (emitter.indicesCB.cpuPtr) {
        const uint32_t numDwords = emitter.indicesCB.sizeBytes / sizeof(uint32_t);
        cmd->SetRootConstants(kSlotBindlessIndices,
                              reinterpret_cast<const uint32_t*>(emitter.indicesCB.cpuPtr),
                              numDwords, 0);
    }

    // Bind per-emitter billboard constants (view/proj matrices, viewport, etc.)
    cmd->SetRootCBV(kSlotPerEmitter,
                    emitter.renderCB.bufferHandle,
                    emitter.renderCB.offsetBytes,
                    reinterpret_cast<const uint8_t*>(emitter.renderCB.cpuPtr),
                    emitter.renderCB.sizeBytes);

    // Bind SRV table: particle attributes (positions, sizes, colors, rotations)
    // + sprite texture + scene depth texture.
    // Bindless: SRV accessed via ResourceDescriptorHeap[index], no descriptor table needed.

    // Issue indirect draw. The args buffer was filled by the simulation pass's
    // indirect args CS and is in IndirectArgument state.
    cmd->DrawIndexedIndirect(gpuBuf.indirectDrawArgs,
                             0,               // argsOffset
                             1,               // drawCount (1 draw per emitter)
                             kIndirectDrawArgsStride);
}

// ---------------------------------------------------------------------------
// AddParticleRenderPass
// ---------------------------------------------------------------------------

void AddParticleRenderPass(rg::RgBuilder& builder,
                           const ParticleRenderPassDesc& desc)
{
    UZ_CHECKF(desc.billboardPipeline.IsValid(),
              "ParticleRenderPass: billboard pipeline handle is invalid");

    for (size_t i = 0; i < desc.emitters.size(); ++i) {
        UZ_CHECKF(desc.emitters[i].buffer != nullptr,
                  "ParticleRenderPass: emitter[{}] has null GPUParticleBuffer", i);
        UZ_CHECKF(desc.emitters[i].buffer->IsValid(),
                  "ParticleRenderPass: emitter[{}] GPUParticleBuffer is not allocated", i);
    }

    // Copy desc so we can sort emitters inside the lambda.
    auto passDesc = desc;

    builder.AddPass("VFX_ParticleRender", [passDesc](rg::RgPassBuilder& pb) mutable {
        // Declare render graph resource dependencies.
        // Color target: we write to it (render target blend).
        if (passDesc.colorTarget.IsValid()) {
            pb.Write(passDesc.colorTarget, RHIResourceState::RenderTarget);
        }

        // Depth target: we read for depth testing (no depth write).
        if (passDesc.depthTarget.IsValid()) {
            pb.Read(passDesc.depthTarget, RHIResourceState::DepthStencilRead);
        }

        // Declare read-set attribute buffers and shared buffers for automatic
        // barrier tracking. The graph will insert UAV->SRV transitions for
        // the read set (which was the previous sim's write set) and
        // UAV->IndirectArgument for the indirect draw args.
        for (const auto& emitter : passDesc.emitters) {
            if (emitter.rgReadPositions.IsValid()) {
                pb.Read(emitter.rgReadPositions,  RHIResourceState::ShaderResource);
                pb.Read(emitter.rgReadVelocities, RHIResourceState::ShaderResource);
                pb.Read(emitter.rgReadAges,       RHIResourceState::ShaderResource);
                pb.Read(emitter.rgReadLifespans,  RHIResourceState::ShaderResource);
                pb.Read(emitter.rgReadSizes,      RHIResourceState::ShaderResource);
                pb.Read(emitter.rgReadColors,     RHIResourceState::ShaderResource);
                pb.Read(emitter.rgReadRotations,  RHIResourceState::ShaderResource);
            }
            if (emitter.rgAliveCount.IsValid()) {
                pb.Read(emitter.rgAliveCount,      RHIResourceState::ShaderResource);
                pb.Read(emitter.rgIndirectDrawArgs, RHIResourceState::IndirectArgument);
            }
            if (emitter.rgSortIndices.IsValid()) {
                pb.Read(emitter.rgSortIndices, RHIResourceState::ShaderResource);
            }
        }

        // Sort emitters back-to-front during setup (before execution).
        SortEmittersBackToFront(passDesc.emitters);

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::ParticleRender");

            if (passDesc.emitters.empty()) return;

            // Bind the billboard graphics pipeline once for all emitters.
            // All particle emitters share the same PSO (billboard VS + alpha-blend PS).
            cmd->SetPipeline(passDesc.billboardPipeline);

            // Read-set attribute buffer transitions (UAV->SRV) and indirect
            // draw args transitions (UAV->IndirectArgument) are handled by the
            // render graph's automatic barrier scheduling via the pb.Read()
            // declarations above. No manual barriers needed here.

            // Draw each emitter (already sorted back-to-front).
            for (const auto& emitter : passDesc.emitters) {
                EnsureIndirectArgsReadable(cmd, *emitter.buffer);
                DrawEmitterBillboards(cmd, emitter);
            }

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "ParticleRenderPass: first execution -- {} emitters",
                       passDesc.emitters.size());
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
