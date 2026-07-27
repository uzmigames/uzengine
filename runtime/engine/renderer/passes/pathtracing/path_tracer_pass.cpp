/// UzEngine Path Tracer Pass — Implementation.
///
/// Step 6: progressive accumulation via 2-RT MRT (RGBA32F accum + RGBA16F hdr)
/// with ping-pong history reads. Camera-cut detection lives in the caller
/// (frame_lighting.cpp), which resets sampleCount=0 when the view changes.

#include "path_tracer_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "engine/rhi/rhi.h"
#include "gi/radiance_cache.h"
#include "cb_ring_allocator.h"

#include <cstring>

#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

void AddPathTracerPass(rg::RgBuilder& builder, const PathTracerPassDesc& desc)
{
    builder.AddPass("PathTracer", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.accumCurr,       RHIResourceState::RenderTarget);
        pb.Write(desc.hdrTarget,       RHIResourceState::RenderTarget);
        pb.Read(desc.normalGBuffer,    RHIResourceState::ShaderResource);
        pb.Read(desc.depthGBuffer,     RHIResourceState::ShaderResource);
        pb.Read(desc.albedoGBuffer,    RHIResourceState::ShaderResource);
        pb.Read(desc.positionGBuffer,  RHIResourceState::ShaderResource);
        pb.Read(desc.emissiveGBuffer,  RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("PathTracer::Execute");

            auto accumTex = res.GetTexture(desc.accumCurr);
            auto hdrTex   = res.GetTexture(desc.hdrTarget);

            // On first frame after a reset, clear accum to black so the
            // history blend has a deterministic starting point.
            constexpr float kClearColor[4] = {0.0F, 0.0F, 0.0F, 1.0F};
            if (desc.sampleCount == 0) {
                cmd->ClearRenderTarget(accumTex, kClearColor);
            }
            cmd->ClearRenderTarget(hdrTex, kClearColor);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 2;
            passDesc.colorAttachments[0].texture = accumTex;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.colorAttachments[1].texture = hdrTex;
            passDesc.colorAttachments[1].clear   = false;
            passDesc.hasDepth                    = false;

            cmd->BeginRenderPass(passDesc);

            cmd->SetPipeline(desc.pipeline);

            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            // ShadowDataCB (b4) — cascade matrices + atlas SRV index for CSM shadows.
            if (desc.shadowAlloc.cpuPtr != nullptr) {
                cmd->SetRootCBV(kSlotShadow, desc.shadowAlloc.bufferHandle,
                                desc.shadowAlloc.offsetBytes,
                                desc.shadowAlloc.cpuPtr, desc.shadowAlloc.sizeBytes);
            }

            // Root constants — must match PathTracerRC layout in
            // path_tracer_ps.hlsl.
            if (desc.bindless) {
                const uint32_t constants[10] = {
                    desc.normalSrvIndex,
                    desc.depthSrvIndex,
                    desc.albedoSrvIndex,
                    desc.positionSrvIndex,
                    desc.emissiveSrvIndex,
                    desc.skyCubeSrvIndex,
                    desc.accumPrevSrvIndex,
                    desc.sampleCount,
                    desc.frameIndex,
                    desc.tlasSrvIndex
                };
                cmd->SetRootConstants(kSlotRootConstants, constants, 10);

                // TraceConstants (b2) — GDF + material table + sun/sky for
                // the bounce-hit shading. Built HERE (execute time) because
                // the PerFrameCB cpuPtr is only populated after graph build.
                if (desc.giCbRing != nullptr) {
                    RadianceCacheTraceConstants giConstants{};
                    giConstants.maxTraceDistance = 1e7f;
                    FillGITraceHitConstants(giConstants, desc.gdf,
                                            desc.perFrameAlloc.cpuPtr,
                                            desc.sceneAlbedo,
                                            desc.skyCubemapSrv,
                                            desc.materialTableSrv,
                                            desc.rcProbeShSrv);
                    auto giAlloc = desc.giCbRing->Allocate(sizeof(giConstants));
                    if (giAlloc.cpuPtr != nullptr) {
                        std::memcpy(giAlloc.cpuPtr, &giConstants, sizeof(giConstants));
                        cmd->SetRootCBV(kSlotPerObject, giAlloc.bufferHandle,
                                        giAlloc.offsetBytes, giAlloc.cpuPtr,
                                        sizeof(giConstants));
                    }
                }
            } else {
                const uint32_t constants[2] = { desc.sampleCount, desc.frameIndex };
                cmd->SetRootConstants(kSlotRootConstants, constants, 2);
            }

            RHIViewport vp{};
            vp.width    = desc.viewportWidth;
            vp.height   = desc.viewportHeight;
            vp.maxDepth = 1.0F;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(desc.viewportWidth);
            sc.bottom = static_cast<int32_t>(desc.viewportHeight);
            cmd->SetScissor(sc);

            cmd->Draw(3); // Fullscreen triangle from SV_VertexID

            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "PathTracerPass: first execution — viewport={}x{}, pipeline={}, samples={}",
                       desc.viewportWidth, desc.viewportHeight,
                       desc.pipeline.index, desc.sampleCount);
                loggedOnce = true;
            }

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
