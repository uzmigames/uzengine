/// UzEngine Transmittance LUT Pass — Implementation.
///
/// Registers and executes a compute pass that fills a 256x64 RGBA16F
/// transmittance LUT via the transmittance_lut_cs.hlsl compute shader.
///
/// Uses root constants (8 DWORDs at slot 0 = b0) instead of a CBV,
/// matching the IBL baker pattern and the global root signature layout.

#include "transmittance_lut_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LUT Dispatch Dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matches [numthreads(8, 8, 1)] in the compute shader.
static constexpr uint32_t kLutThreadGroupSize = 8;

/// LUT width in texels. Dispatch X = 256 / 8 = 32.
static constexpr uint32_t kLutWidth = 256;

/// LUT height in texels. Dispatch Y = 64 / 8 = 8.
static constexpr uint32_t kLutHeight = 64;

// ─────────────────────────────────────────────────────────────────────────────
// AddTransmittanceLutPass
// ─────────────────────────────────────────────────────────────────────────────

void AddTransmittanceLutPass(rg::RgBuilder& builder, const TransmittanceLutPassDesc& desc)
{
    builder.AddPass("TransmittanceLUT", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.lutTarget, RHIResourceState::UnorderedAccess);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("TransmittanceLUT::Execute");

            // ── Bind compute pipeline ──────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Set root constants at slot 0 (b0) — 8 DWORDs ──────────
            // Same pattern as IBL baker: SetRootConstants(0, data, count).
            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&desc.rootConstants), 8);

            // ── Bind output UAV at root param 4 (u0 descriptor table) ─
            cmd->SetDescriptorTable(4, desc.lutUavIndex);

            // ── Dispatch: 256/8=32 groups X, 64/8=8 groups Y ──────────
            cmd->Dispatch(kLutWidth / kLutThreadGroupSize,
                          kLutHeight / kLutThreadGroupSize, 1);
        });
    });
}

} // namespace uz
