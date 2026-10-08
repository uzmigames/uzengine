/// UzEngine Sky Cubemap Capture Pass -- Implementation.
///
/// Registers and executes a compute pass that fills a 64x64x6 RGBA16F
/// cubemap with sky radiance via the sky_cubemap_cs.hlsl compute shader.
///
/// Uses root constants (12 DWORDs at slot 0 = b0) instead of a CBV,
/// matching the transmittance LUT and IBL baker patterns.

#include "sky_cubemap_capture_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_SkyCubemapRes = UZ_CVAR_INT("r.Atmosphere.SkyCubemapResolution", 64, "Sky cubemap face resolution", 32, 256);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Dispatch Dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matches [numthreads(8, 8, 1)] in the compute shader.
static constexpr uint32_t kCubeThreadGroupSize = 8;

/// Cubemap face size in texels. Dispatch X = Y = 64 / 8 = 8.
static constexpr uint32_t kCubeFaceSize = 64;

/// Number of cubemap faces. Dispatch Z = 6.
static constexpr uint32_t kCubeFaceCount = 6;

// ─────────────────────────────────────────────────────────────────────────────
// AddSkyCubemapCapturePass
// ─────────────────────────────────────────────────────────────────────────────

void AddSkyCubemapCapturePass(rg::RgBuilder& builder, const SkyCubemapCapturePassDesc& desc)
{
    builder.AddPass("SkyCubemapCapture", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.cubemapTarget, RHIResourceState::UnorderedAccess);
        if (desc.transmittanceLutDep.IsValid()) {
            pb.Read(desc.transmittanceLutDep, RHIResourceState::ShaderResource);
        }
        if (desc.scatteringLut3DDep.IsValid()) {
            pb.Read(desc.scatteringLut3DDep, RHIResourceState::ShaderResource);
        }

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("SkyCubemapCapture::Execute");

            if (!cmd) {
                UZ_LOG(Renderer, Error, "SkyCubemapCapturePass: null command list");
                return;
            }

            if (desc.cubemapUavIndex == UINT32_MAX) {
                UZ_LOG(Renderer, Error,
                       "SkyCubemapCapturePass: cubemapUavIndex is UINT32_MAX — UAV not allocated, skipping dispatch");
                return;
            }

            // ── Bind compute pipeline ──────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Set root constants at slot 0 (b0) -- 12 DWORDs ──────────
            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&desc.rootConstants), 12);

            // ── Bind output UAV at root param 4 (u0 descriptor table) ──
            cmd->SetDescriptorTable(4, desc.cubemapUavIndex);

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                const uint32_t groupsXY = kCubeFaceSize / kCubeThreadGroupSize;
                UZ_LOG(Renderer, Info,
                       "SkyCubemapCapturePass: first execution — "
                       "dispatch=({}, {}, {}), pipeline={}, UAV={}, faceSize={}",
                       groupsXY, groupsXY, kCubeFaceCount,
                       desc.pipeline.index, desc.cubemapUavIndex, kCubeFaceSize);
                loggedOnce = true;
            }

            // ── Dispatch: 64/8=8 groups X, 64/8=8 groups Y, 6 faces Z ──
            cmd->Dispatch(kCubeFaceSize / kCubeThreadGroupSize,
                          kCubeFaceSize / kCubeThreadGroupSize,
                          kCubeFaceCount);
        });
    });
}

} // namespace uz
