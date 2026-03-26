/// VFX Distortion Resolve Pass -- Implementation
///
/// Registers and executes a fullscreen graphics pass that warps the HDR color
/// buffer using UV offsets from the distortion accumulation texture.
///
/// @ref Vlachos 2015 "Advanced VR Rendering" -- screen-space UV distortion

#include "distortion_resolve_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"
#include "root_slots.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// AddDistortionResolvePass
// ---------------------------------------------------------------------------

void AddDistortionResolvePass(rg::RgBuilder& builder,
                              const DistortionResolveDesc& desc)
{
    UZ_CHECKF(desc.resolvePipeline.IsValid(),
              "DistortionResolvePass: resolve pipeline handle is invalid");

    // Copy desc so the lambda captures a complete snapshot.
    auto passDesc = desc;

    builder.AddPass("VFX_DistortionResolve", [passDesc](rg::RgPassBuilder& pb) {
        // Declare render graph resource dependencies.
        // Color target: we write the warped result.
        if (passDesc.colorTarget.IsValid()) {
            pb.Write(passDesc.colorTarget, RHIResourceState::RenderTarget);
        }

        // Distortion accumulation texture: read as shader resource.
        if (passDesc.distortionSource.IsValid()) {
            pb.Read(passDesc.distortionSource, RHIResourceState::ShaderResource);
        }

        // Pre-distortion HDR color copy: read as shader resource.
        if (passDesc.preDistortionCopy.IsValid()) {
            pb.Read(passDesc.preDistortionCopy, RHIResourceState::ShaderResource);
        }

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("VFX::DistortionResolve");

            // Bind the fullscreen triangle pipeline.
            cmd->SetPipeline(passDesc.resolvePipeline);

            // Set root constants with SRV descriptor indices for the resolve shader.
            // Layout matches DistortionResolveConstants: [colorSrv, distortionSrv, pad, pad].
            uint32_t rootConstants[4] = {
                passDesc.colorTextureSrvIndex,
                passDesc.distortionTextureSrvIndex,
                0, // _pad0
                0  // _pad1
            };
            cmd->SetRootConstants(kSlotRootConstants, rootConstants, 4);

            // Draw fullscreen triangle (3 vertices, no vertex buffer).
            // The vertex shader generates clip-space positions and UVs
            // from SV_VertexID.
            cmd->Draw(3);

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "DistortionResolvePass: first execution -- "
                       "colorSrv={}, distortionSrv={}",
                       passDesc.colorTextureSrvIndex,
                       passDesc.distortionTextureSrvIndex);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
