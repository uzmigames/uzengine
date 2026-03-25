/// VFX Distortion Accumulation Pass -- Implementation
///
/// Registers and executes a graphics pass that draws distortion-emitting
/// particles into an RGBA16F target. Emitters are sorted back-to-front
/// for correct additive blending of overlapping distortion sources.
///
/// @ref Vlachos 2015 "Advanced VR Rendering" -- screen-space distortion technique

#include "distortion_pass.h"

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

/// Root parameter slot for distortion PS constants (b0).
static constexpr uint32_t kSlotDistortionConstants = 0;

/// Root parameter slot for per-emitter billboard constants (b1).
static constexpr uint32_t kSlotPerEmitter = 1;

/// Root parameter slot for SRV descriptor table (t0..tN).
static constexpr uint32_t kSlotSRVTable = 5;

/// Stride of a single indirect draw argument structure in bytes.
/// Matches `sizeof(RHIDrawIndexedIndirectArgs)` (20 bytes).
static constexpr uint32_t kIndirectDrawArgsStride = 20;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Sorts distortion emitters back-to-front by distance to camera.
///
/// Emitters farther from the camera are drawn first, ensuring correct
/// additive blending when distortion sources overlap.
///
/// # Arguments
/// * `emitters` -- Mutable reference to the emitter array. Sorted in-place.
static void SortEmittersBackToFront(std::vector<DistortionEmitterData>& emitters)
{
    std::sort(emitters.begin(), emitters.end(),
              [](const DistortionEmitterData& a, const DistortionEmitterData& b) {
                  return a.distanceToCamera > b.distanceToCamera;
              });
}

/// Draws a single emitter's distortion particles using an indirect draw call.
///
/// Binds the per-emitter distortion and billboard constant buffers, sets the
/// SRV descriptor table, and issues a DrawIndexedIndirect call using the
/// emitter's indirect draw args buffer.
///
/// # Arguments
/// * `cmd` -- Command list to record into.
/// * `emitter` -- Per-emitter distortion rendering data.
static void DrawDistortionEmitter(IRHICommandList* cmd,
                                  const DistortionEmitterData& emitter)
{
    const auto& gpuBuf = *emitter.buffer;

    // Bind b0: distortion PS constants (strength, normal map SRV index).
    if (emitter.distortionCB.cpuPtr) {
        cmd->SetRootCBV(kSlotDistortionConstants,
                        emitter.distortionCB.bufferHandle,
                        emitter.distortionCB.offsetBytes,
                        reinterpret_cast<const uint8_t*>(emitter.distortionCB.cpuPtr),
                        emitter.distortionCB.sizeBytes);
    }

    // Bind b1: per-emitter billboard constants (view/proj, viewport).
    cmd->SetRootCBV(kSlotPerEmitter,
                    emitter.billboardCB.bufferHandle,
                    emitter.billboardCB.offsetBytes,
                    reinterpret_cast<const uint8_t*>(emitter.billboardCB.cpuPtr),
                    emitter.billboardCB.sizeBytes);

    // Bind SRV table: particle attributes + normal map texture.
    cmd->SetDescriptorTable(kSlotSRVTable, emitter.readSetSrvIndex);

    // Issue indirect draw. The args buffer was filled by the simulation pass
    // and is in IndirectArgument state.
    cmd->DrawIndexedIndirect(gpuBuf.indirectDrawArgs,
                             0,                      // argsOffset
                             1,                      // drawCount (1 draw per emitter)
                             kIndirectDrawArgsStride);
}

// ---------------------------------------------------------------------------
// AddDistortionAccumulationPass
// ---------------------------------------------------------------------------

void AddDistortionAccumulationPass(rg::RgBuilder& builder,
                                   const DistortionPassDesc& desc)
{
    for (size_t i = 0; i < desc.emitters.size(); ++i) {
        UZ_CHECKF(desc.emitters[i].buffer != nullptr,
                  "DistortionAccumulationPass: emitter[{}] has null GPUParticleBuffer", i);
        UZ_CHECKF(desc.emitters[i].buffer->IsValid(),
                  "DistortionAccumulationPass: emitter[{}] GPUParticleBuffer is not allocated", i);
    }

    // Copy desc so we can sort emitters inside the lambda.
    auto passDesc = desc;

    builder.AddPass("VFX_DistortionAccumulate", [passDesc](rg::RgPassBuilder& pb) mutable {
        // Declare render graph resource dependencies.
        // Distortion target: we write accumulated UV offsets.
        if (passDesc.distortionTarget.IsValid()) {
            pb.Write(passDesc.distortionTarget, RHIResourceState::RenderTarget);
        }

        // Depth target: read-only depth test (no depth writes).
        if (passDesc.depthTarget.IsValid()) {
            pb.Read(passDesc.depthTarget, RHIResourceState::DepthStencilRead);
        }

        // Declare read-set attribute buffers and shared buffers for automatic
        // barrier tracking. The graph will insert UAV->SRV transitions for
        // the read set and UAV->IndirectArgument for the indirect draw args.
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
                pb.Read(emitter.rgAliveCount,       RHIResourceState::ShaderResource);
                pb.Read(emitter.rgIndirectDrawArgs, RHIResourceState::IndirectArgument);
            }
            if (emitter.rgSortIndices.IsValid()) {
                pb.Read(emitter.rgSortIndices, RHIResourceState::ShaderResource);
            }
        }

        // Sort emitters back-to-front during setup (before execution).
        SortEmittersBackToFront(passDesc.emitters);

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("VFX::DistortionAccumulate");

            if (passDesc.emitters.empty()) return;

            // Draw each emitter (already sorted back-to-front).
            // Per-emitter pipeline binding allows different emitters to use
            // different distortion PSOs (e.g., different blend states).
            for (const auto& emitter : passDesc.emitters) {
                if (!emitter.pipeline.IsValid()) {
                    UZ_LOG(VFX, Warning,
                           "DistortionAccumulationPass: emitter with invalid pipeline -- skipping");
                    continue;
                }
                cmd->SetPipeline(emitter.pipeline);
                DrawDistortionEmitter(cmd, emitter);
            }

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "DistortionAccumulationPass: first execution -- {} emitters",
                       passDesc.emitters.size());
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
