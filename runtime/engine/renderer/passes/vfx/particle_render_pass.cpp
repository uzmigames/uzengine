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

    // Skip draw — the billboard VS reads particle positions from structured buffers
    // via ResourceDescriptorHeap[index], but the spawn CS CB layout doesn't match
    // the HLSL layout byte-for-byte yet (RC1 partial fix). Drawing with garbage
    // positions causes device removed. Disabled until CB alignment is verified.
    //
    // const uint32_t vertexCount = emitter.cpuAliveCount * 6;
    // if (vertexCount > 0) {
    //     cmd->Draw(vertexCount, 1, 0, 0);
    // }
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
                  "ParticleRenderPass: emitter has null GPUParticleBuffer");
        UZ_CHECKF(desc.emitters[i].buffer->IsValid(),
                  "ParticleRenderPass: emitter GPUParticleBuffer is not allocated");
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

            // ── Begin render pass with color + depth targets ─────────────
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            if (passDesc.colorTarget.IsValid()) {
                rpDesc.colorAttachments[0].texture = res.GetTexture(passDesc.colorTarget);
            }
            if (passDesc.depthTarget.IsValid()) {
                rpDesc.hasDepth = true;
                rpDesc.depthAttachment.texture = res.GetTexture(passDesc.depthTarget);
            }
            cmd->BeginRenderPass(rpDesc);

            // Set viewport + scissor from the color target dimensions.
            if (passDesc.viewportWidth > 0 && passDesc.viewportHeight > 0) {
                RHIViewport vp{};
                vp.width = static_cast<float>(passDesc.viewportWidth);
                vp.height = static_cast<float>(passDesc.viewportHeight);
                vp.minDepth = 0.0f;
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right = static_cast<int32_t>(passDesc.viewportWidth);
                sc.bottom = static_cast<int32_t>(passDesc.viewportHeight);
                cmd->SetScissor(sc);
            }

            // Bind the billboard graphics pipeline once for all emitters.
            cmd->SetPipeline(passDesc.billboardPipeline);

            // Draw each emitter using CPU-side alive count (not indirect draw).
            // TODO: switch to DrawIndirect once the RHI supports non-indexed indirect draw.
            for (const auto& emitter : passDesc.emitters) {
                DrawEmitterBillboards(cmd, emitter);
            }

            cmd->EndRenderPass();

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "ParticleRenderPass: first execution -- {} emitters, viewport={}x{}",
                       passDesc.emitters.size(), passDesc.viewportWidth, passDesc.viewportHeight);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
