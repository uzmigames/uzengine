/// UzEngine Scene Renderer — Overlay passes: sky atmosphere, fog, editor grid,
/// stats overlay, screenshot capture, and present transition.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "fly_camera_controller.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "sh_projection.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "editor_grid.h"
#include "editor_input_handler.h"
#include "gizmo_renderer.h"
#include "outline_pass.h"
#include "stats_overlay.h"

#include <cstdio>
#include <cstring>

namespace uz {

void SceneRenderer::RegisterOverlayPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // ── Editor Selection Outline pass ───────────────────────────────────
    // Renders a colored outline around the selected entity. Runs before the
    // overlay pass (grid + stats) so the outline composites under the UI.
    // Uses a two-pass technique: mask (mesh→R8Unorm) + detect (fullscreen edge).
    if (m_editorMode && m_selectedEntity.IsValid() && m_outlinePass && m_shadowDebugMode != 1) {
        // Depth must match display resolution for the mask depth test.
        const bool depthMatchesDisplay = (m_renderWidth == m_width && m_renderHeight == m_height);
        if (depthMatchesDisplay) {
            rgBuilder.AddPass("OutlinePass", [&](rg::RgPassBuilder& pb) {
                pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);
                // Use Write (not Read) for depth — pipeline has depthWrite=false,
                // but D3D12 requires DEPTH_WRITE state for OMSetRenderTargets
                // when no read-only DSV descriptor is created.
                pb.Write(ctx.depthRes, RHIResourceState::DepthStencilWrite);

                pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("OutlinePass");

                    auto bb    = res.GetTexture(ctx.bbRes);
                    auto depth = res.GetTexture(ctx.depthRes);

                    m_outlinePass->Draw(
                        cmd, *m_cbRing, *m_camera,
                        ctx.drawList.GetCommands(), m_meshes,
                        m_selectedEntity.id,
                        ctx.vpWidth, ctx.vpHeight,
                        depth, bb,
                        ctx.bindless,
                        ctx.perFrameAlloc);
                });
            });
        }
    }

    // ── Overlay pass (editor grid + stats overlay) ──────────────────────
    // Sky atmosphere + height fog are now in RegisterLightingPasses
    // (before postprocess/tonemap) to ensure correct HDR pipeline order.
    // Skip overlays when buffer visualization grid is active — grid owns the full screen.
    if (m_shadowDebugMode != 1)
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
            if (m_editorMode) {
                m_editorGrid->Draw(cmd, *m_cbRing, *m_camera,
                                   depthSrv, ctx.vpWidth, ctx.vpHeight);
            }

            // ── Gizmo (transform handles on selected entity) ────────
            if (m_editorMode && m_selectedEntity.IsValid()
                && m_gizmoRenderer && m_gizmoRenderer->IsInitialized()) {
                // Find the selected entity's world position from the draw list.
                Vec3 entityPos{0.f, 0.f, 0.f};
                Quat entityRot = Quat::Identity();
                for (const auto& dc : ctx.drawList.GetCommands()) {
                    if (dc.entityId == m_selectedEntity.id) {
                        entityPos = dc.worldMatrix.getTranslation();
                        entityRot = Quat::FromMatrix(dc.worldMatrix);
                        break;
                    }
                }
                m_gizmoRenderer->Draw(cmd, *m_cbRing, *m_camera,
                                       entityPos, entityRot,
                                       static_cast<GizmoMode>(ctx.gizmoModeRaw),
                                       static_cast<GizmoSpace>(ctx.gizmoSpaceRaw),
                                       ctx.gizmoHoveredAxis,
                                       ctx.gizmoActiveAxis);
            }

            if (m_editorMode && m_statsOverlayInitialized) {
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
                        // Use the active LOD's index count when a LOD override is in effect.
                        const uint32_t activeIndexCount = (dc.lodIndexCount > 0)
                            ? dc.lodIndexCount
                            : m.indexCount;
                        totalVertices  += activeIndexCount;
                        totalTriangles += activeIndexCount / 3;
                    }
                }
                m_statsOverlay->SetDrawCalls(drawCount);
                m_statsOverlay->SetVertexCount(totalVertices);
                m_statsOverlay->SetTriangleCount(totalTriangles);

                // LOD info: report the active LOD of the first multi-LOD mesh.
                for (const auto& dc : ctx.drawList.GetCommands()) {
                    if (dc.meshId < m_meshes.size()) {
                        const GPUMesh& gm = m_meshes[dc.meshId];
                        if (gm.lodCount > 1) {
                            uint32_t activeLOD = 0;
                            if (dc.lodIndexCount > 0) {
                                // Determine which LOD by matching index count
                                for (uint32_t li = 0; li < 3; ++li) {
                                    if (gm.lodInfo[li].indexCount == dc.lodIndexCount) {
                                        activeLOD = li + 1;
                                        break;
                                    }
                                }
                            }
                            m_statsOverlay->SetActiveLOD(activeLOD, gm.lodCount);
                            break;
                        }
                    }
                }

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

    // ── PassInspector: pass grid visualization ──────────────────────────
    // When grid is active, draw an NxM grid of all registered targets.
    // Each cell is a vis fullscreen draw with a constrained viewport.
    if (m_passInspector.IsGridEnabled() && !m_passInspector.IsVisActive()) {
        auto gridTargets = m_passInspector.GetGridTargets();
        const auto& gridCfg = m_passInspector.GetGridConfig();

        if (!gridTargets.empty() && gridCfg.columns > 0 && gridCfg.rows > 0) {
            // Capture all SRV indices, modes, and dimensions by value.
            struct GridCell {
                uint32_t srvIndex;
                uint32_t mode;
                uint32_t texWidth;
                uint32_t texHeight;
            };

            std::vector<GridCell> cells;
            cells.reserve(gridTargets.size());
            for (const auto* t : gridTargets) {
                cells.push_back({t->srvIndex, static_cast<uint32_t>(t->mode), t->width, t->height});
            }

            const uint32_t cols = gridCfg.columns;
            const uint32_t rows = gridCfg.rows;

            rgBuilder.AddPass("PassGrid", [&, cells = std::move(cells), cols, rows](rg::RgPassBuilder& pb) {
                pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);

                pb.SetExecutionCallback([this, &ctx, cells, cols, rows](
                        IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("PassInspector::Grid");

                    auto visPipeline = ctx.pipelines->GetVisFullscreen();
                    if (!visPipeline.IsValid()) return;

                    auto bb = res.GetTexture(ctx.bbRes);

                    RHIRenderPassDesc passDesc{};
                    passDesc.colorAttachmentCount        = 1;
                    passDesc.colorAttachments[0].texture = bb;
                    passDesc.colorAttachments[0].clear   = true;
                    passDesc.colorAttachments[0].clearColor[0] = 0.05f;
                    passDesc.colorAttachments[0].clearColor[1] = 0.05f;
                    passDesc.colorAttachments[0].clearColor[2] = 0.05f;
                    passDesc.colorAttachments[0].clearColor[3] = 1.0f;
                    cmd->BeginRenderPass(passDesc);

                    cmd->SetPipeline(visPipeline);

                    const float totalW = ctx.vpWidth;
                    const float totalH = ctx.vpHeight;
                    const float cellW  = totalW / static_cast<float>(cols);
                    const float cellH  = totalH / static_cast<float>(rows);

                    uint32_t nearBits = 0, farBits = 0;
                    std::memcpy(&nearBits, &ctx.perFrame.nearPlane, sizeof(float));
                    std::memcpy(&farBits,  &ctx.perFrame.farPlane,  sizeof(float));

                    for (uint32_t i = 0; i < static_cast<uint32_t>(cells.size()); ++i) {
                        const uint32_t col = i % cols;
                        const uint32_t row = i / cols;
                        if (row >= rows) break;

                        RHIViewport vp{};
                        vp.x        = static_cast<float>(col) * cellW;
                        vp.y        = static_cast<float>(row) * cellH;
                        vp.width    = cellW;
                        vp.height   = cellH;
                        vp.maxDepth = 1.0f;
                        cmd->SetViewport(vp);

                        RHIScissorRect sc{};
                        sc.left   = static_cast<int32_t>(vp.x);
                        sc.top    = static_cast<int32_t>(vp.y);
                        sc.right  = static_cast<int32_t>(vp.x + cellW);
                        sc.bottom = static_cast<int32_t>(vp.y + cellH);
                        cmd->SetScissor(sc);

                        uint32_t rootConstants[8] = {
                            cells[i].srvIndex,
                            cells[i].mode,
                            cells[i].texWidth,
                            cells[i].texHeight,
                            nearBits,
                            farBits,
                            0,
                            0
                        };
                        cmd->SetRootConstants(kSlotRootConstants, rootConstants, 8);

                        cmd->Draw(3); // fullscreen triangle, clipped by viewport
                    }

                    cmd->EndRenderPass();
                });
            });
        }
    }

    // ── PassInspector: vis fullscreen pass ─────────────────────────────
    // When vis is active, overwrite the backbuffer with the selected target.
    // Runs after all rendering but before present.
    if (m_passInspector.IsVisActive()) {
        const RegisteredTarget* visTarget = m_passInspector.GetVisTarget();
        if (visTarget) {
            // Capture the target info by value for the deferred callback.
            const uint32_t visSrvIndex   = visTarget->srvIndex;
            const uint32_t visMode       = static_cast<uint32_t>(visTarget->mode);
            const uint32_t visTexWidth   = visTarget->width;
            const uint32_t visTexHeight  = visTarget->height;

            rgBuilder.AddPass("VisFullscreen", [&, visSrvIndex, visMode, visTexWidth, visTexHeight](rg::RgPassBuilder& pb) {
                pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);

                pb.SetExecutionCallback([this, &ctx, visSrvIndex, visMode, visTexWidth, visTexHeight](
                        IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("Vis::Fullscreen");

                    auto visPipeline = ctx.pipelines->GetVisFullscreen();
                    if (!visPipeline.IsValid()) return;

                    auto bb = res.GetTexture(ctx.bbRes);

                    RHIRenderPassDesc passDesc{};
                    passDesc.colorAttachmentCount        = 1;
                    passDesc.colorAttachments[0].texture = bb;
                    passDesc.colorAttachments[0].clear   = true;
                    passDesc.colorAttachments[0].clearColor[0] = 0.0f;
                    passDesc.colorAttachments[0].clearColor[1] = 0.0f;
                    passDesc.colorAttachments[0].clearColor[2] = 0.0f;
                    passDesc.colorAttachments[0].clearColor[3] = 1.0f;
                    cmd->BeginRenderPass(passDesc);

                    cmd->SetPipeline(visPipeline);

                    RHIViewport vp{};
                    vp.width    = static_cast<uint32_t>(ctx.vpWidth);
                    vp.height   = static_cast<uint32_t>(ctx.vpHeight);
                    vp.maxDepth = 1.0f;
                    cmd->SetViewport(vp);

                    RHIScissorRect sc{};
                    sc.right  = static_cast<int32_t>(ctx.swapChain->GetWidth());
                    sc.bottom = static_cast<int32_t>(ctx.swapChain->GetHeight());
                    cmd->SetScissor(sc);

                    // Root constants: 8 DWORDs matching VisRootCB in vis_fullscreen_ps.hlsl.
                    uint32_t nearBits = 0, farBits = 0;
                    std::memcpy(&nearBits, &ctx.perFrame.nearPlane, sizeof(float));
                    std::memcpy(&farBits, &ctx.perFrame.farPlane, sizeof(float));

                    uint32_t rootConstants[8] = {
                        visSrvIndex,   // textureSrv
                        visMode,       // displayMode
                        visTexWidth,   // texWidth
                        visTexHeight,  // texHeight
                        nearBits,      // nearPlane (as uint bits)
                        farBits,       // farPlane (as uint bits)
                        0,             // _pad0
                        0              // _pad1
                    };
                    cmd->SetRootConstants(kSlotRootConstants, rootConstants, 8);

                    cmd->Draw(3); // fullscreen triangle

                    cmd->EndRenderPass();
                });
            });
        }
    }

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
