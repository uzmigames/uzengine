/// UzEngine Scene Renderer — Overlay passes: sky atmosphere, fog, editor grid,
/// stats overlay, screenshot capture, and present transition.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "fps_camera_controller.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "sh_projection.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "editor_grid.h"
#include "stats_overlay.h"

#include <cstdio>
#include <cstring>

namespace uz {

void SceneRenderer::RegisterOverlayPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // ── Overlay pass (editor grid + stats overlay) ──────────────────────
    // Sky atmosphere + height fog are now in RegisterLightingPasses
    // (before postprocess/tonemap) to ensure correct HDR pipeline order.
    // Skip overlays when buffer visualization grid is active — grid owns the full screen.
    if (m_shadowDebugMode == 0)
    rgBuilder.AddPass("OverlayPass", [&](rg::RgPassBuilder& pb) {
        pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);
        // Only attach depth when render res == display res (no screen percentage scaling).
        // When screen percentage < 100, the depth buffer is at render resolution which
        // doesn't match the display-resolution backbuffer — attaching it would cause a
        // D3D12 render target / depth stencil dimension mismatch error.
        const bool attachDepth = (m_renderWidth == m_width && m_renderHeight == m_height);
        if (attachDepth) {
            pb.Write(ctx.depthRes, RHIResourceState::DepthStencilWrite);
        } else {
            // When depth can't be attached (dimension mismatch), read it as SRV
            // so the grid shader can perform manual depth occlusion via bindless.
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        }
        pb.SetExecutionCallback([this, &ctx, attachDepth](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb = res.GetTexture(ctx.bbRes);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            if (attachDepth) {
                auto depth = res.GetTexture(ctx.depthRes);
                passDesc.hasDepth                    = true;
                passDesc.depthAttachment.texture     = depth;
                passDesc.depthAttachment.clear       = false;
            } else {
                passDesc.hasDepth = false;
            }
            cmd->BeginRenderPass(passDesc);

            RHIViewport vp{};
            vp.width    = ctx.vpWidth;
            vp.height   = ctx.vpHeight;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(ctx.swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(ctx.swapChain->GetHeight());
            cmd->SetScissor(sc);

            // When hardware depth is attached (screen percentage == 100%),
            // use UINT32_MAX so the shader uses hardware depth test.
            // Otherwise pass the scene depth SRV for manual shader-based occlusion.
            const uint32_t depthSrv = attachDepth ? UINT32_MAX : m_gbuffer.depthSRV;
            m_editorGrid->Draw(cmd, *m_cbRing, *m_camera,
                               depthSrv, ctx.vpWidth, ctx.vpHeight);

            if (m_statsOverlayInitialized) {
                const uint32_t drawCount = ctx.useInstancing
                    ? static_cast<uint32_t>(ctx.instanceBatches.size())
                    : static_cast<uint32_t>(ctx.drawList.Count());
                // Accumulate exact vertex/triangle counts across all draw commands.
                // This correctly handles scenes with multiple distinct meshes.
                uint32_t totalVertices  = 0;
                uint32_t totalTriangles = 0;
                for (const auto& dc : ctx.drawList.GetCommands()) {
                    if (dc.meshId < m_meshes.size() && m_meshes[dc.meshId].IsValid()) {
                        const GPUMesh& m = m_meshes[dc.meshId];
                        totalVertices  += m.vertexCount;
                        totalTriangles += m.indexCount / 3;
                    }
                }
                m_statsOverlay->SetDrawCalls(drawCount);
                m_statsOverlay->SetVertexCount(totalVertices);
                m_statsOverlay->SetTriangleCount(totalTriangles);

                // Camera position + rotation for artist positioning
                const auto& camPos = m_camera->GetPosition();
                float yawDeg   = m_cameraCtrl->GetYaw()   * (180.0f / 3.14159265f);
                float pitchDeg = m_cameraCtrl->GetPitch() * (180.0f / 3.14159265f);
                m_statsOverlay->SetCameraInfo(camPos.x, camPos.y, camPos.z, yawDeg, pitchDeg);
                m_statsOverlay->SetRenderResolution(m_renderWidth, m_renderHeight, m_screenPercentage);
                m_statsOverlay->SetUpscalerName(m_upscaler ? m_upscaler->GetName() : "None");

                m_statsOverlay->Render(cmd, *m_cbRing, ctx.frameIndex);
            }

            cmd->EndRenderPass();
        });
    });

    // ── Present transition ──────────────────────────────────────────────
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(ctx.bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    // ── IBL bake + SH projection — delegated to atmosphere_renderer ────
    // This is deferred until after compile+execute, stored for post-frame.
    // The actual call happens in RenderFrame after rgBuilder.Execute().
    // We store the atmoCtx for the caller (RenderFrame) to use.
    // Since atmoCtx is stack-local in this function, we need to copy it.
    // Rather than storing it, we call PostFrameAtmosphereBake from RenderFrame
    // after compile+execute. The atmoCtx is rebuilt there.
    //
    // NOTE: The atmosphere post-frame bake and screenshot capture are handled
    // directly in the trimmed RenderFrame() after rgBuilder.Execute(), because
    // they require the render graph to have already executed.
}

} // namespace uz
