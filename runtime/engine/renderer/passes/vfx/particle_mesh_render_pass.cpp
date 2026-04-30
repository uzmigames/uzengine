/// VFX Particle Mesh Render Pass -- Implementation
///
/// Registers and executes a graphics pass that draws mesh particle instances
/// via DrawIndexedIndirect. The indirect args buffer is filled by the preceding
/// particle_mesh_instance_cs.hlsl dispatch, so draw counts are determined
/// entirely on the GPU.

#include "particle_mesh_render_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root parameter slot for the per-pass constant buffer (b0).
static constexpr uint32_t kSlotPerPass = 0;

/// Root parameter slot for the SRV descriptor table (t0..tN).
/// t0 = instance transform buffer, t1+ = material textures.
static constexpr uint32_t kSlotSRVTable = 5;

/// Stride of DrawIndexedInstancedIndirectArgs in bytes (5 x uint32 = 20).
static constexpr uint32_t kIndirectDrawArgsStride = 20;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Validates that a batch item has enough data to be submitted.
///
/// A batch item is renderable when it has a valid pipeline AND valid mesh and
/// indirect args buffers. Items missing any of these are silently skipped at
/// draw time, but we log a warning here to catch configuration errors early.
///
/// # Parameters
/// - `item`:  The batch item to validate.
/// - `index`: Batch index for diagnostic logging.
///
/// # Returns
/// `true` when the item should be submitted.
static bool IsBatchItemRenderable(const ParticleMeshBatchItem& item,
                                  size_t index)
{
    if (!item.pipeline.IsValid()) {
        UZ_LOG(VFX, Warning,
               "ParticleMeshRenderPass: batch[{}] has invalid pipeline -- skipping",
               index);
        return false;
    }
    if (!item.meshVertexBuffer.IsValid()) {
        UZ_LOG(VFX, Warning,
               "ParticleMeshRenderPass: batch[{}] has invalid mesh vertex buffer -- skipping",
               index);
        return false;
    }
    if (!item.meshIndexBuffer.IsValid()) {
        UZ_LOG(VFX, Warning,
               "ParticleMeshRenderPass: batch[{}] has invalid mesh index buffer -- skipping",
               index);
        return false;
    }
    if (!item.indirectArgsBuffer.IsValid()) {
        UZ_LOG(VFX, Warning,
               "ParticleMeshRenderPass: batch[{}] has invalid indirect args buffer -- skipping",
               index);
        return false;
    }
    if (item.instanceBufferSrvIndex == UINT32_MAX) {
        UZ_LOG(VFX, Warning,
               "ParticleMeshRenderPass: batch[{}] has unset instanceBufferSrvIndex -- skipping",
               index);
        return false;
    }
    return true;
}

/// Draws one particle mesh batch using a pre-filled indirect args buffer.
///
/// Binds:
/// 1. The per-pass CB (view-projection, b0).
/// 2. The SRV descriptor table starting at `instanceBufferSrvIndex` (t0+).
/// 3. The mesh vertex and index buffers.
///
/// Then issues DrawIndexedIndirect -- the GPU reads the instance count directly
/// from the indirect args buffer without any CPU readback.
///
/// # Parameters
/// - `cmd`:  Command list to record into.
/// - `item`: Batch data (CB, buffers, SRV indices).
static void DrawMeshBatch(IRHICommandList* cmd,
                          const ParticleMeshBatchItem& item)
{
    // Bind per-pass root constants at b0 (slot 0 is 32-bit root constants, not CBV).
    if (item.passCB.cpuPtr) {
        const uint32_t numDwords = item.passCB.sizeBytes / sizeof(uint32_t);
        cmd->SetRootConstants(kSlotPerPass,
                              reinterpret_cast<const uint32_t*>(item.passCB.cpuPtr),
                              numDwords, 0);
    }

    // Bind SRV table: t0 = instance transforms, t1+ = material textures (if any).
    // The descriptor table starts at instanceBufferSrvIndex; the shader reads
    // consecutive slots from that base.
    // Bindless: SRV accessed via ResourceDescriptorHeap[index], no descriptor table needed.

    // Bind mesh geometry.
    cmd->SetVertexBuffer(0, item.meshVertexBuffer);
    cmd->SetIndexBuffer(item.meshIndexBuffer, item.meshIndexFormat);

    // Issue the indirect draw. The instance CS has already written the
    // DrawIndexedInstancedIndirectArgs (including InstanceCount) into
    // indirectArgsBuffer. A UAV barrier between the CS and this pass
    // is the caller's responsibility.
    cmd->DrawIndexedIndirect(item.indirectArgsBuffer,
                             0,                      // argsOffset
                             1,                      // drawCount (one draw per batch)
                             kIndirectDrawArgsStride);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddParticleMeshRenderPass
// ─────────────────────────────────────────────────────────────────────────────

void AddParticleMeshRenderPass(rg::RgBuilder& builder,
                               const ParticleMeshPassDesc& desc)
{
    // Validate preconditions. Log warnings per-batch (inside IsBatchItemRenderable)
    // but do not assert -- the pass is still registered so the render graph
    // barrier structure remains correct even when all batches are empty.
    for (size_t i = 0; i < desc.batches.size(); ++i) {
        // IsBatchItemRenderable logs warnings internally; result checked at draw time.
        (void)i;
    }

    // Copy desc so the lambda captures a complete snapshot.
    // desc.batches may contain RHI handles that outlive the caller's local scope
    // (they are GPU resources), but the vectors themselves are value-copied.
    auto passDesc = desc;

    builder.AddPass("VFX_ParticleMeshRender", [passDesc](rg::RgPassBuilder& pb) mutable {
        // ── Declare render graph resource dependencies ───────────────────────

        // HDR color target: read-modify-write so the DAG schedules this pass
        // AFTER whatever pass last wrote the HDR composite (sky / lighting).
        // The previous `Write`-only declaration made the auto-scheduler
        // free to reorder this pass before lighting, blanking the scene
        // because the composite was overwritten by a clear or stale state.
        // Mirrors `particle_render_pass.cpp:145-148`.
        if (passDesc.colorTarget.IsValid()) {
            pb.Read(passDesc.colorTarget, RHIResourceState::RenderTarget);
            pb.Write(passDesc.colorTarget, RHIResourceState::RenderTarget);
        }

        // Depth target depends on transparency:
        // - Opaque: write depth so solid debris occludes correctly.
        // - Transparent: read-only depth test, no depth writes.
        if (passDesc.depthTarget.IsValid()) {
            const auto depthState = passDesc.isTransparent
                                  ? RHIResourceState::DepthStencilRead
                                  : RHIResourceState::DepthStencilWrite;
            if (passDesc.isTransparent) {
                pb.Read(passDesc.depthTarget, depthState);
            } else {
                pb.Write(passDesc.depthTarget, depthState);
            }
        }

        // Per-batch indirect args buffer: the preceding mesh-instance CS
        // wrote it as `UnorderedAccess`. Declaring a Read in
        // `IndirectArgument` state lets the auto-barrier scheduler emit
        // the `UAV → IndirectArgument` transition before the
        // `DrawIndexedIndirect` here. Without this declaration the draw
        // executes against a buffer still in `UAV` state and the debug
        // layer raises a CORRUPTED_PARAMETER error.
        //
        // Per-batch instance transforms + instance colors: same story.
        // The mesh-instance CS wrote both as `UnorderedAccess`. The VS /
        // PS read them via `ResourceDescriptorHeap[]` (bindless SRV path)
        // so they MUST be in `ShaderResource` state at draw time. Without
        // these declarations the render graph leaves the buffers in UAV
        // state, the bindless reads return garbage / NaN values, and the
        // resulting clip-space-spanning triangles blank the HDR composite
        // to black (or to the PS constant when forced for diagnostics).
        // @ref phase0_vfx-mesh-particle-visibility-debug
        for (const auto& batch : passDesc.batches) {
            if (batch.indirectArgsBufferRg.IsValid()) {
                pb.Read(batch.indirectArgsBufferRg,
                        RHIResourceState::IndirectArgument);
            }
            if (batch.instanceTransformsBufferRg.IsValid()) {
                pb.Read(batch.instanceTransformsBufferRg,
                        RHIResourceState::ShaderResource);
            }
            if (batch.instanceColorsBufferRg.IsValid()) {
                pb.Read(batch.instanceColorsBufferRg,
                        RHIResourceState::ShaderResource);
            }
        }

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                           const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("VFX::ParticleMeshRender");

            if (passDesc.batches.empty()) return;

            // ── Bind render targets, viewport, scissor ───────────────────────
            // BeginRenderPass binds HDR color + scene depth. Without it the
            // indirect draw inherits stale RTV/DSV from the previous pass
            // (typically GBuffer slots — wrong format for HDR composite).
            // This was the root cause of the RC3 disable — the same fix
            // landed on the billboard pass in commit c8560ba9 and is
            // mirrored here. Does NOT clear — preserves sky + lit scene.
            // @ref runtime/engine/renderer/passes/vfx/particle_render_pass.cpp:190-220
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture = passDesc.colorTexture;
            rpDesc.colorAttachments[0].clear   = false;
            rpDesc.hasDepth = true;
            rpDesc.depthAttachment.texture = passDesc.depthTexture;
            rpDesc.depthAttachment.clear   = false;
            cmd->BeginRenderPass(rpDesc);

            // Viewport + scissor (DX12 does not inherit them across passes).
            // Use render-resolution units — the HDR composite is authored
            // pre-upsample and must be matched here, not the swapchain size.
            {
                RHIViewport vp{};
                vp.width    = static_cast<float>(passDesc.renderWidth);
                vp.height   = static_cast<float>(passDesc.renderHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);
                RHIScissorRect sr{};
                sr.right  = static_cast<int32_t>(passDesc.renderWidth);
                sr.bottom = static_cast<int32_t>(passDesc.renderHeight);
                cmd->SetScissor(sr);
            }

            uint32_t drawnBatches = 0;

            // Draw each emitter batch. Per-batch pipeline binding allows
            // different emitters to use different meshes or materials.
            for (size_t i = 0; i < passDesc.batches.size(); ++i) {
                const auto& batch = passDesc.batches[i];

                if (!IsBatchItemRenderable(batch, i)) continue;

                // Bind the PSO. When all batches share the same pipeline this
                // redundant bind is elided by the RHI command list's state cache.
                cmd->SetPipeline(batch.pipeline);

                DrawMeshBatch(cmd, batch);
                ++drawnBatches;
            }

            cmd->EndRenderPass();

            // Log first execution for diagnostics and pipeline validation.
            // The "first execution -- N emitters" line is the spec scenario
            // marker that hardware-validation scripts grep for to confirm
            // the pass actually ran (vs. being silently skipped).
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "ParticleMeshRenderPass: first execution -- {} emitters "
                       "({} batches submitted, {} drawn, transparent={})",
                       drawnBatches,
                       passDesc.batches.size(),
                       drawnBatches,
                       passDesc.isTransparent);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
