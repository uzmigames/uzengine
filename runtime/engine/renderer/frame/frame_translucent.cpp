/// UzEngine Scene Renderer — Forward translucent pass.
///
/// Renders materials with blendMode >= Translucent (glass, transparent,
/// additive) in a forward pass AFTER deferred lighting. Copies the HDR
/// scene color for screen-space refraction before drawing.
///
/// @ref Three.js transmissionRenderTarget — copy opaque scene, then draw glass
///   sampling it with refracted screen UVs.
/// @ref UE5 — Translucent objects use a separate forward pass after deferred.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "components.h"

#include "camera.h"
#include "camera_manager.h"
#include "constant_buffers.h"
#include "draw_list.h"
#include "gpu_mesh.h"
#include "per_frame_params.h"
#include "pipeline_library.h"
#include "render_graph_builder.h"
#include "render_graph_states.h"
#include "render_graph_pass.h"
#include "render_graph_resources.h"
#include "root_slots.h"
#include "shader_parameter_binder.h"
#include "gbuffer_per_object_params.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

void SceneRenderer::RegisterTranslucentPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_showFlags.Get(ShowFlag::Translucency)) return;
    UZ_PROFILE_FUNCTION();

    // Check if any draw command has blendMode >= 2 (Translucent/Additive).
    const auto& commands = ctx.drawList.GetCommands();
    bool hasTranslucent = false;
    for (const auto& drawCmd : commands) {
        const uint32_t blendMode = drawCmd.materialFlags & 0x3u;
        if (blendMode >= 2) {
            hasTranslucent = true;
            break;
        }
    }
    if (!hasTranslucent) return;

    // Get the translucent PSO (premultiplied alpha blend, depth read, no depth write).
    RHIPipelineHandle translucentPSO = ctx.pipelines->GetTranslucent();
    if (!translucentPSO.IsValid()) {
        UZ_LOG(Renderer, Warning, "TranslucentPass: no translucent PSO available");
        return;
    }

    // Capture state for the render graph lambdas.
    // Scene sync's opaque keys have no view depth. Sort only this pass's
    // subset, independently of opaque mesh/material batching.
    std::vector<DrawCommand> translucent;
    for (const auto& drawCmd : commands) {
        if ((drawCmd.materialFlags & 0x3u) >= 2u) translucent.push_back(drawCmd);
    }
    const Mat4 view = m_cameraManager->GetActiveCamera().GetView();
    std::stable_sort(translucent.begin(), translucent.end(),
        [&view](const DrawCommand& a, const DrawCommand& b) {
            return (view * (a.worldMatrix * Vec3{})).z
                 > (view * (b.worldMatrix * Vec3{})).z;
        });
    CBRingAllocator* cbRing   = m_cbRing.get();
    const GPUMesh* meshes     = m_meshes.data();
    const uint32_t meshCount  = static_cast<uint32_t>(m_meshes.size());
    const bool bindless       = m_device->IsBindless();
    const float vpW           = ctx.renderWidth;
    const float vpH           = ctx.renderHeight;
    RHITextureHandle depthTex = m_gbuffer.depth;
    RHITextureHandle hdrTex   = m_hdrSceneColor;
    RHITextureHandle transSceneColor = m_translucentSceneColor;
    const uint32_t transSceneColorSrv = m_translucentSceneColorSrv;

    // The shader aliases PerObjectCB's POM fields as glassThickness and
    // sceneColorSrvIndex. Neither the CB layout nor the root block grows.

    // Pre-allocate per-frame CB.
    PerFrameParams perFrameCopy = ctx.perFrame;
    CBAllocation perFrameAlloc = cbRing->Allocate(sizeof(PerFrameParams));
    if (perFrameAlloc.cpuPtr) {
        std::memcpy(perFrameAlloc.cpuPtr, &perFrameCopy, sizeof(PerFrameParams));
    }

    const auto hdrRes   = ctx.hdrRes;
    const auto depthRes = ctx.depthRes;

    // ── Pass 1: Copy HDR scene color for screen-space refraction ─────────
    // @ref Three.js — renders opaque scene first, copies to transmissionRenderTarget,
    //   then draws glass objects sampling from it.
    // @ref UE5 DeferredShadingRenderer — copy opaque scene for translucent refraction.
    // @ref Three.js — transmissionRenderTarget copy before glass objects.
    // Uses manual barriers only (not RG-tracked) — same pattern as SSR prev color copy
    // in frame_lighting.cpp. The hdrTex is NOT declared to the RG for this pass to
    // avoid double-barrier conflicts.
    rgBuilder.AddPass("TranslucentCopyScene", [=](rg::RgPassBuilder& pb) {
        pb.Read(hdrRes, RHIResourceState::RenderTarget);

        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("TranslucentCopyScene::Execute");

            if (!transSceneColor.IsValid() || !hdrTex.IsValid()) return;

            RHIBarrierDesc barriers[1]{};
            barriers[0].texture     = hdrTex;
            barriers[0].stateBefore = RHIResourceState::RenderTarget;
            barriers[0].stateAfter  = RHIResourceState::CopySrc;
            cmd->ResourceBarrier(barriers, 1);
            m_rgStates->Transition(cmd, transSceneColor, RHIResourceState::CopyDst);

            cmd->CopyTexture(hdrTex, transSceneColor);

            RHIBarrierDesc restore[1]{};
            restore[0].texture     = hdrTex;
            restore[0].stateBefore = RHIResourceState::CopySrc;
            restore[0].stateAfter  = RHIResourceState::RenderTarget;
            cmd->ResourceBarrier(restore, 1);
            m_rgStates->Transition(cmd, transSceneColor, RHIResourceState::ShaderResource);
        });
    });

    // ── Pass 2: Forward translucent draw ──────────────────────────────────
    rgBuilder.AddPass("ForwardTranslucent", [=](rg::RgPassBuilder& pb) {
        pb.Write(hdrRes, RHIResourceState::RenderTarget);
        pb.Read(depthRes, RHIResourceState::DepthStencilRead);

        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("ForwardTranslucent::Execute");

            auto hdrTarget = res.GetTexture(hdrRes);

            // Begin render pass — write to HDR target, read depth (no write).
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = hdrTarget;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = depthTex;
            passDesc.depthAttachment.clearDepth = kDepthFar;
            passDesc.depthAttachment.clear      = false;
            cmd->BeginRenderPass(passDesc);

            cmd->SetPipeline(translucentPSO);

            // Bind per-frame CB.
            cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.bufferHandle,
                            perFrameAlloc.offsetBytes,
                            perFrameAlloc.cpuPtr, perFrameAlloc.sizeBytes);

            // Viewport + scissor.
            RHIViewport vp{};
            vp.width    = vpW;
            vp.height   = vpH;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(vpW);
            sc.bottom = static_cast<int32_t>(vpH);
            cmd->SetScissor(sc);

            // Draw loop — only translucent/additive materials (blendMode >= 2).
            const auto& cmds = translucent;
            uint32_t lastBoundMeshId = UINT32_MAX;
            uint32_t drawCallCount = 0;

            for (const auto& drawCmd : cmds) {
                const uint32_t blendMode = drawCmd.materialFlags & 0x3u;
                if (blendMode < 2) continue;

                if (drawCmd.meshId >= meshCount) continue;
                const GPUMesh& mesh = meshes[drawCmd.meshId];
                if (!mesh.IsValid()) continue;

                // Rebind vertex/index buffers only when mesh changes.
                if (drawCmd.meshId != lastBoundMeshId) {
                    if (bindless) {
                        const uint32_t pushData[4] = {
                            mesh.vbSrvIndex,
                            (mesh.vertexStride == 32) ? 1u : 0u,
                            mesh.colorSrvIndex, 0u
                        };
                        cmd->SetRootConstants(kSlotRootConstants, pushData, 4);
                    } else {
                        cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                    }
                    cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                    lastBoundMeshId = drawCmd.meshId;
                }

                // Fill the unchanged per-object CB. The shader aliases the POM
                // slots as glassThickness and sceneColorSrvIndex for glass.
                PerObjectCB perObject{};
                perObject.model        = drawCmd.worldMatrix;
                perObject.normalMatrix = drawCmd.normalMatrix;
                perObject.albedoIndex    = drawCmd.albedoSrvIndex;
                perObject.normalIndex    = drawCmd.normalSrvIndex;
                perObject.roughnessIndex = drawCmd.roughnessSrvIndex;
                perObject.metallicIndex  = drawCmd.metallicSrvIndex;
                perObject.aoIndex        = drawCmd.aoSrvIndex;
                perObject.emissiveColorR    = drawCmd.emissiveColorR;
                perObject.emissiveColorG    = drawCmd.emissiveColorG;
                perObject.emissiveColorB    = drawCmd.emissiveColorB;
                perObject.emissiveIntensity = drawCmd.emissiveIntensity;
                perObject.baseColorTintR      = drawCmd.baseColorTintR;
                perObject.baseColorTintG      = drawCmd.baseColorTintG;
                perObject.baseColorTintB      = drawCmd.baseColorTintB;
                perObject.roughnessMultiplier = drawCmd.roughnessMultiplier;
                perObject.metallicMultiplier  = drawCmd.metallicMultiplier;
                perObject.normalIntensity     = drawCmd.normalIntensity;
                perObject.uvScaleX            = drawCmd.uvScaleX;
                perObject.uvScaleY            = drawCmd.uvScaleY;
                perObject.uvOffsetX           = drawCmd.uvOffsetX;
                perObject.uvOffsetY           = drawCmd.uvOffsetY;
                perObject.aoIntensity         = drawCmd.aoIntensity;
                perObject.opacity             = drawCmd.opacity;
                perObject.alphaClipThreshold  = drawCmd.alphaClipThreshold;
                perObject.opacitySrvIndex     = drawCmd.opacitySrvIndex;
                perObject.materialFlags       = drawCmd.materialFlags;
                perObject.displacementScale   = drawCmd.displacementScale;
                // Inject scene color SRV for screen-space refraction.
                perObject.heightmapSrvIndex   = transSceneColorSrv;

                CBAllocation objAlloc = cbRing->Allocate(sizeof(PerObjectCB));
                if (!objAlloc.cpuPtr) {
                    UZ_LOG(Renderer, Error,
                           "TranslucentPass: CB ring exhausted at draw {}", drawCallCount);
                    break;
                }
                std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle,
                                objAlloc.offsetBytes,
                                objAlloc.cpuPtr, objAlloc.sizeBytes);

                cmd->DrawIndexed(mesh.indexCount, 1, 0, 0, 0);
                ++drawCallCount;
            }

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
