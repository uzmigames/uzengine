/// UzEngine Sky Cubemap Capture PS Pass — Implementation.
///
/// Registers and executes a graphics pass that fills a 64x64x6 RGBA16F
/// cubemap with sky radiance via the sky_cubemap_ps.hlsl pixel shader.
///
/// Renders 6 fullscreen triangles, one per cubemap face. Each draw targets
/// a different array slice via per-slice RTV. Root constants are updated
/// per-face to communicate the face index to the pixel shader.

#include "sky_cubemap_capture_ps_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ---------------------------------------------------------------------------
// Cubemap Face Constants
// ---------------------------------------------------------------------------

/// Cubemap face size in texels — must match sky_cubemap_ps.hlsl and the
/// texture allocation in scene_renderer.cpp.
static constexpr uint32_t kCubeFaceSize = 64;

/// Number of cubemap faces.
static constexpr uint32_t kCubeFaceCount = 6;

// ---------------------------------------------------------------------------
// AddSkyCubemapCapturePSPass
// ---------------------------------------------------------------------------

void AddSkyCubemapCapturePSPass(rg::RgBuilder& builder, const SkyCubemapCapturePSPassDesc& desc)
{
    builder.AddPass("SkyCubemapCapturePS", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.cubemapTarget, RHIResourceState::RenderTarget);
        if (desc.transmittanceLut.IsValid()) {
            pb.Read(desc.transmittanceLut, RHIResourceState::ShaderResource);
        }

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("SkyCubemapCapturePS::Execute");

            auto cubemapTex = res.GetTexture(desc.cubemapTarget);

            // Bind the graphics pipeline (fullscreen_vs + sky_cubemap_ps)
            cmd->SetPipeline(desc.pipeline);

            // Bind transmittance LUT at t0 (SM5 register path)
            cmd->SetFragmentTexture(0, desc.transmittanceLutTex);

            // Viewport and scissor for cubemap face resolution
            RHIViewport vp{};
            vp.width    = static_cast<float>(kCubeFaceSize);
            vp.height   = static_cast<float>(kCubeFaceSize);
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(kCubeFaceSize);
            sc.bottom = static_cast<int32_t>(kCubeFaceSize);
            cmd->SetScissor(sc);

            // Mutable copy of root constants — cubeFaceIndex updated per face
            SkyCubemapCapturePSRootConstants rc = desc.rootConstants;

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "SkyCubemapCapturePSPass: first execution — "
                       "{} faces, faceSize={}, pipeline={}, transmittanceLUT valid={}",
                       kCubeFaceCount, kCubeFaceSize,
                       desc.pipeline.index,
                       desc.transmittanceLutTex.IsValid());

                UZ_LOG(Renderer, Info,
                       "SkyCubemapCapturePSPass: rootConstants — "
                       "bottomRadiusKm={:.3f}, topRadiusKm={:.3f}, "
                       "sunDir=({:.4f}, {:.4f}, {:.4f}), sunIllum=({:.3f}, {:.3f}, {:.3f}), "
                       "packedScatterScales=0x{:08X}",
                       desc.rootConstants.bottomRadiusKm,
                       desc.rootConstants.topRadiusKm,
                       desc.rootConstants.sunDirX,
                       desc.rootConstants.sunDirY,
                       desc.rootConstants.sunDirZ,
                       desc.rootConstants.sunIllumX,
                       desc.rootConstants.sunIllumY,
                       desc.rootConstants.sunIllumZ,
                       desc.rootConstants.packedScatterScales);

                if (!desc.transmittanceLutTex.IsValid()) {
                    UZ_LOG(Renderer, Warning,
                           "SkyCubemapCapturePSPass: transmittanceLutTex is invalid — "
                           "LUT may not be baked yet");
                }
                loggedOnce = true;
            }

            // Render each face as a separate render pass targeting one array slice
            for (uint32_t face = 0; face < kCubeFaceCount; ++face) {
                rc.cubeFaceIndex = face;

                // Begin render pass targeting this cubemap face (array slice)
                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments[0].texture    = cubemapTex;
                passDesc.colorAttachments[0].arraySlice = face;
                passDesc.colorAttachments[0].mipLevel   = 0;
                passDesc.colorAttachments[0].clear      = true;
                passDesc.colorAttachments[0].clearColor[0] = 0.0f;
                passDesc.colorAttachments[0].clearColor[1] = 0.0f;
                passDesc.colorAttachments[0].clearColor[2] = 0.0f;
                passDesc.colorAttachments[0].clearColor[3] = 1.0f;
                passDesc.hasDepth = false;

                cmd->BeginRenderPass(passDesc);

                // Re-bind pipeline after BeginRenderPass (some backends reset state)
                cmd->SetPipeline(desc.pipeline);
                cmd->SetFragmentTexture(0, desc.transmittanceLutTex);
                cmd->SetViewport(vp);
                cmd->SetScissor(sc);

                // Set root constants with current face index
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 10);

                // Draw fullscreen triangle
                cmd->Draw(3);

                cmd->EndRenderPass();
            }
        });
    });
}

} // namespace uz
