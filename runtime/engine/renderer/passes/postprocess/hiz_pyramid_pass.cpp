/// UzEngine Hi-Z Pyramid Pass — Implementation
///
/// Registers and executes a compute pass that generates a hierarchical Z-buffer
/// from the depth prepass output. Each mip is produced by a separate compute
/// dispatch that reads the previous mip and writes the min of each 2x2 block.
///
/// @source UE5 HZBOcclusion.cpp — BuildHZB pass
/// @source UE5 HZB.usf:183-293 — HZBBuildCS

#include "hiz_pyramid_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matching [numthreads(8, 8, 1)] in hiz_pyramid_cs.hlsl.
/// @source UE5 HZB.usf:10 — GROUP_TILE_SIZE
static constexpr uint32_t kHiZThreadGroupSize = 8;

// ─────────────────────────────────────────────────────────────────────────────
// HiZ Root Constants — matches cbuffer HiZConstants in hiz_pyramid_cs.hlsl
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the Hi-Z compute shader (8 DWORDs at b0).
///
/// Passed via SetRootConstants at slot 0. The shader uses integer
/// dimensions for Load() addressing rather than UV-space transforms.
///
/// In bindless mode (SM6.6+), g_InputSrvIndex is used to access the input
/// depth texture via ResourceDescriptorHeap[]. In SM5 mode, the input is
/// bound via descriptor table at t0 and the extra fields are ignored.
struct HiZRootConstants {
    uint32_t outputWidth;    ///< Width of the output mip level in texels.
    uint32_t outputHeight;   ///< Height of the output mip level in texels.
    uint32_t inputWidth;     ///< Width of the input mip level in texels (for clamping).
    uint32_t inputHeight;    ///< Height of the input mip level in texels (for clamping).
    uint32_t inputSrvIndex;  ///< SRV index for bindless input depth texture.
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

static_assert(sizeof(HiZRootConstants) == 32,
              "HiZRootConstants must be exactly 8 DWORDs (32 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// AddHiZPyramidPass
// ─────────────────────────────────────────────────────────────────────────────

void AddHiZPyramidPass(rg::RgBuilder& builder, const HiZPyramidPassDesc& desc)
{
    UZ_CHECKF(desc.mipCount > 0 && desc.mipCount <= kHiZMaxMipLevels,
              "HiZPyramid: mipCount must be 1..{}, got {}", kHiZMaxMipLevels, desc.mipCount);

    builder.AddPass("HiZPyramid", [desc](rg::RgPassBuilder& pb) {
        // Declare the depth source as a read dependency so the render graph
        // inserts a DepthStencilWrite -> ShaderResource barrier after the
        // depth prepass.
        pb.Read(desc.depthSource, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("HiZPyramid::Execute");

            cmd->SetPipeline(desc.pipeline);

            // Track the current input SRV index and dimensions. For mip 0,
            // the input is the depth prepass output at full resolution.
            uint32_t inputSrvIndex = desc.depthSourceSrvIndex;
            uint32_t inputWidth    = desc.sourceWidth;
            uint32_t inputHeight   = desc.sourceHeight;

            for (uint32_t mip = 0; mip < desc.mipCount; ++mip) {
                const HiZMipLevel& level = desc.mipLevels[mip];

                // ── Root constants: output/input dimensions + SRV index ───
                HiZRootConstants rootConstants{};
                rootConstants.outputWidth  = level.width;
                rootConstants.outputHeight = level.height;
                rootConstants.inputWidth   = inputWidth;
                rootConstants.inputHeight  = inputHeight;
                rootConstants.inputSrvIndex = inputSrvIndex;
                rootConstants._pad0 = 0;
                rootConstants._pad1 = 0;
                rootConstants._pad2 = 0;
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rootConstants), 8);

                // ── Bind input SRV via descriptor table (SM5 path) or
                //    root constants (bindless path, index already set above)
                if (!desc.bindless) {
                    cmd->SetDescriptorTable(5, inputSrvIndex);
                }

                // ── Bind output UAV at descriptor table slot 4 (u0) ───────
                cmd->SetDescriptorTable(4, level.uavIndex);

                // ── Dispatch: ceil(outW / 8) x ceil(outH / 8) x 1 ────────
                const uint32_t groupsX = (level.width  + kHiZThreadGroupSize - 1) / kHiZThreadGroupSize;
                const uint32_t groupsY = (level.height + kHiZThreadGroupSize - 1) / kHiZThreadGroupSize;
                cmd->Dispatch(groupsX, groupsY, 1);

                // ── UAV barrier to ensure writes complete before the next
                //    dispatch reads this mip as input ──────────────────────
                RHIBarrierDesc barrier{};
                barrier.texture     = level.texture;
                barrier.stateBefore = RHIResourceState::UnorderedAccess;
                barrier.stateAfter  = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&barrier, 1);

                // This mip's SRV becomes the next dispatch's input.
                inputSrvIndex = level.srvIndex;
                inputWidth    = level.width;
                inputHeight   = level.height;
            }

            // Log mip chain once for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "HiZPyramid: {} mip levels generated ({}x{} -> {}x{})",
                       desc.mipCount, desc.sourceWidth, desc.sourceHeight,
                       desc.mipLevels[desc.mipCount - 1].width,
                       desc.mipLevels[desc.mipCount - 1].height);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
