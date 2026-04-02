/// VFX Trail Render Pass — Implementation
///
/// Registers and executes a graphics pass that draws ribbon trails as
/// camera-facing quad strips. Items are sorted back-to-front by sort order
/// and distance to camera, then drawn with one DrawIndexed call each.

#include "trail_render_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root parameter slot for the per-trail constant buffer (b1).
static constexpr uint32_t kSlotPerTrail = 1;

/// Root parameter slot for the SRV descriptor table (t0..t1).
/// t0 = vertex buffer structured SRV, t1 = index buffer SRV.
static constexpr uint32_t kSlotSRVTable = 5;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Sorts trail render items for correct transparency rendering.
///
/// Primary sort key: `sortOrder` ascending (lower sort orders in front).
/// Secondary sort key: `distanceToCamera` descending (farther items first).
///
/// This two-level sort ensures that art-directed layer ordering is always
/// respected, while trails at the same layer are blended back-to-front.
///
/// # Parameters
/// - `items`: Mutable reference to the item array. Sorted in-place.
static void SortItemsBackToFront(std::vector<TrailPassRenderItem>& items)
{
    std::sort(items.begin(), items.end(),
              [](const TrailPassRenderItem& a, const TrailPassRenderItem& b) {
                  if (a.sortOrder != b.sortOrder) {
                      // Lower sortOrder is drawn FIRST (rendered beneath higher layers).
                      return a.sortOrder < b.sortOrder;
                  }
                  // Same layer: farther items are drawn first (back-to-front).
                  return a.distanceToCamera > b.distanceToCamera;
              });
}

/// Draws a single trail's ribbon geometry with its pre-bound pipeline.
///
/// Binds the per-trail constant buffer to root slot b1, the SRV descriptor
/// table for vertex/index buffers to root slot t0..t1, then issues one
/// `DrawIndexed` call for the full ribbon mesh.
///
/// # Arguments
/// * `cmd`  -- Command list to record into.
/// * `item` -- Per-trail render data (constant buffer, SRV indices, draw counts).
static void DrawTrailRibbon(IRHICommandList* cmd, const TrailPassRenderItem& item)
{
    // Bind per-trail view-projection and parameter constants (b1).
    cmd->SetRootCBV(kSlotPerTrail,
                    item.constantBuffer.bufferHandle,
                    item.constantBuffer.offsetBytes,
                    reinterpret_cast<const uint8_t*>(item.constantBuffer.cpuPtr),
                    item.constantBuffer.sizeBytes);

    // Bind SRV table: t0 = vertex buffer structured SRV, t1 = index buffer SRV.
    // Bindless: SRV accessed via ResourceDescriptorHeap[index], no descriptor table needed.

    // Draw the ribbon quad strip (one DrawIndexed per trail).
    cmd->DrawIndexed(item.indexCount,
                     /*instanceCount=*/1,
                     /*startIndex=*/0,
                     /*baseVertex=*/0,
                     /*startInstance=*/0);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddTrailRenderPass
// ─────────────────────────────────────────────────────────────────────────────

void AddTrailRenderPass(rg::RgBuilder& builder, const TrailPassDesc& desc)
{
    // Copy desc so we own the items inside the lambda capture.
    // Sorting happens at setup time (before Execute) so the execution
    // callback sees a stable, sorted view.
    auto passDesc = desc;

    // Sort now, during pass registration, before the lambda is captured.
    // This is intentional: the sorted order must be determined by the
    // CPU-side distance values sampled this frame, not at GPU execution time.
    SortItemsBackToFront(passDesc.items);

    builder.AddPass("VFX_TrailRender", [passDesc](rg::RgPassBuilder& pb) mutable {
        // Declare render graph resource dependencies.

        // Color buffer: we write to it (additive/alpha-blend transparent phase).
        if (passDesc.colorTarget.IsValid()) {
            pb.Write(passDesc.colorTarget, RHIResourceState::RenderTarget);
        }

        // Depth buffer: read-only depth testing. No depth writes — trails are
        // transparent and must not occlude subsequent transparent draws.
        if (passDesc.depthTarget.IsValid()) {
            pb.Read(passDesc.depthTarget, RHIResourceState::DepthStencilRead);
        }

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                           const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("VFX::TrailRender");

            if (passDesc.items.empty()) return;

            uint32_t drawnCount = 0;
            RHIPipelineHandle boundPipeline{};  // Track last bound PSO to avoid redundant binds.

            for (const auto& item : passDesc.items) {
                // Skip degenerate items with no geometry.
                if (item.indexCount == 0 || item.vertexCount == 0) continue;

                UZ_CHECKF(item.pipeline.IsValid(),
                          "TrailRenderPass: item has invalid pipeline handle");

                // Bind the pipeline only when it changes to minimise state churn.
                // Most frames all trails share the same PSO, so this saves one
                // SetPipeline call per trail after the first.
                if (item.pipeline != boundPipeline) {
                    cmd->SetPipeline(item.pipeline);
                    boundPipeline = item.pipeline;
                }

                DrawTrailRibbon(cmd, item);
                ++drawnCount;
            }

            // Log first execution for diagnostics; subsequent frames are silent.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "TrailRenderPass: first execution -- {} items submitted, {} drawn",
                       passDesc.items.size(),
                       drawnCount);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
