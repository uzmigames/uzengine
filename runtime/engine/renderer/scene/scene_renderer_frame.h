/// UzEngine FrameContext — Per-frame shared state for the render pipeline.
///
/// Extracted from SceneRenderer to reduce header size. This struct lives on the
/// stack inside RenderFrame() and is passed by reference to all sub-functions.

#pragma once

#include "per_frame_params.h"
#include "scene_update_flags.h"
#include "post_process_volume.h"
#include "shadow_data_params.h"
#include "cb_ring_allocator.h"   // CBAllocation
#include "instance_batcher.h"    // InstanceBatch, InstanceData
#include "light_renderer.h"      // LightFrameData
#include "shadow_renderer.h"     // ShadowFrameData
#include "draw_list.h"           // DrawList
#include "render_graph_types.h"  // rg::RgResourceId
#include "vector3.h"             // Vec3 (from engine/math/)
#include "mat4.h"                // Mat4 (from engine/math/)

#include <cstdint>
#include <vector>

namespace uz {

// Forward declarations for pointer/reference members
class IRHICommandList;
class IRHISwapChain;
class World;
class IVFS;
class PipelineLibrary;
class SceneRenderer;

/// Per-frame shared state passed between the RenderFrame sub-functions.
/// Lives on the stack inside RenderFrame() — never persisted across frames.
struct FrameContext {
    // ── Externals (passed into RenderFrame) ─────────────────────────
    IRHICommandList* cmdList = nullptr;
    IRHISwapChain*   swapChain = nullptr;
    World*           world = nullptr;
    IVFS*            vfs = nullptr;
    float            dt = 0.f;
    uint32_t         frameIndex = 0;

    /// Owning SceneRenderer back-pointer.
    ///
    /// Provides per-frame sub-functions (e.g. `RegisterParticlePasses`) access
    /// to renderer-owned asset caches without wrapping every accessor in a
    /// `std::function`. Symmetric with `world` and `vfs`: the FrameContext
    /// already stores all other top-level externals as raw non-owning
    /// pointers, so adding `SceneRenderer*` keeps the struct uniform.
    ///
    /// Currently consumed by the VFX mesh-particle path to resolve
    /// `MeshRenderModuleConfig::meshPath` into a cached `GPUMesh` via
    /// `SceneRenderer::LoadMesh` + `SceneRenderer::GetGPUMesh`. Future
    /// per-frame helpers that need texture / material caches should reuse
    /// this pointer instead of plumbing additional callbacks.
    ///
    /// Always non-null inside `RenderFrame` (set by the populator at
    /// `scene_render_frame.cpp`).
    SceneRenderer*   sceneRenderer = nullptr;

    // ── Resolved during SetupFrameData ──────────────────────────────
    PipelineLibrary* pipelines = nullptr;
    float            vpWidth = 0.f;          ///< Display resolution width (backbuffer).
    float            vpHeight = 0.f;         ///< Display resolution height (backbuffer).
    float            renderWidth = 0.f;      ///< Internal render resolution width (screen percentage applied).
    float            renderHeight = 0.f;     ///< Internal render resolution height (screen percentage applied).
    bool             bindless = false;

    // ── Scene update flags (aggregated component dirty state) ─────
    SceneUpdateFlags updateFlags{};

    // ── CB allocations ──────────────────────────────────────────────
    CBAllocation     perFrameAlloc{};
    CBAllocation     lightingAlloc{};
    CBAllocation     shadowAlloc{};

    // ── Per-frame params (CPU mirror of PerFrameCB) ─────────────────
    PerFrameParams   perFrame{};
    ShadowDataParams shadowDataParams{};

    // ── Shadow data ─────────────────────────────────────────────────
    ShadowFrameData  shadowFrame{};
    uint32_t         totalCascadeCount = 0;
    Vec3             sunDir{};
    Vec3             sunColor{};
    Vec3             ambientColor{};
    LightFrameData   lightData{};
    bool             shadowCacheEnabled = false;

    // ── Draw list + instancing ──────────────────────────────────────
    DrawList                    drawList;
    std::vector<InstanceBatch>  instanceBatches;
    std::vector<InstanceData>   instanceDataVec;
    uint32_t                    instanceBufferSrvIndex = UINT32_MAX;
    bool                        useInstancing = false;

    // ── GPU Occlusion Culling ──────────────────────────────────────
    bool     gpuCullingActive = false;       ///< True when GPU cull pass should run this frame.
    uint32_t boundingSphereSrvIndex = UINT32_MAX; ///< SRV for bounding sphere buffer (this frame's slot).
    uint32_t visibleIndexUavIndex = UINT32_MAX;   ///< UAV for Phase 1 visible index output buffer.
    uint32_t indirectArgsUavIndex = UINT32_MAX;   ///< UAV for Phase 1 indirect draw args buffer.
    RHIBufferHandle visibleIndexBuffer;      ///< Phase 1 visible index buffer handle.
    RHIBufferHandle indirectArgsBuffer;      ///< Phase 1 indirect args buffer handle.
    CBAllocation    gpuCullCBAlloc{};         ///< CB ring allocation for Phase 1 GpuCullConstants.

    // ── Two-Phase Occlusion Culling ───────────────────────────────────
    // Phase 1 culls against previous frame's HiZ (conservative — may reject
    // newly revealed objects). Phase 2 re-tests all instances against the
    // updated HiZ built from Phase 1 visible geometry.
    bool     twoPhaseOcclusion = false;      ///< True when two-phase culling is active (prevHiZ available).
    uint32_t visibleIndexUavIndex2 = UINT32_MAX;  ///< UAV for Phase 2 visible index output.
    uint32_t indirectArgsUavIndex2 = UINT32_MAX;  ///< UAV for Phase 2 indirect draw args.
    RHIBufferHandle visibleIndexBuffer2;     ///< Phase 2 visible index buffer handle.
    RHIBufferHandle indirectArgsBuffer2;     ///< Phase 2 indirect args buffer handle.
    CBAllocation    gpuCullCBAlloc2{};        ///< CB ring allocation for Phase 2 GpuCullConstants.

    // ── Render graph resource IDs ───────────────────────────────────
    rg::RgResourceId bbRes{};
    rg::RgResourceId hdrRes{};
    rg::RgResourceId depthRes{};
    rg::RgResourceId normalRes{};
    rg::RgResourceId albedoRes{};
    rg::RgResourceId positionRes{};
    rg::RgResourceId emissiveRes{};
    rg::RgResourceId velocityRes{};
    rg::RgResourceId shadowAtlasRes{};
    rg::RgResourceId localShadowAtlasRes{};
    rg::RgResourceId apVolumeRes{};        ///< Aerial perspective volume (imported, Read in deferred, Write in AP compute).
    rg::RgResourceId debugDirectRes{};     ///< Lighting split RT1 (written by deferred lighting, read by the debug view).
    rg::RgResourceId debugIndirectRes{};   ///< Lighting split RT2.
    rg::RgResourceId debugShadowRes{};     ///< Lighting split RT3 (R = shadow, G = AO).

    // ── DBuffer resource IDs (decal system) ─────────────────────────
    rg::RgResourceId dbufferARes{};
    rg::RgResourceId dbufferBRes{};
    rg::RgResourceId dbufferCRes{};

    // ── TAA state ───────────────────────────────────────────────────
    bool     taaActive = false;
    uint32_t jitterIndex = 0;   ///< Halton index used this frame ((frame % r.TAA.JitterSequenceLength) + 1); 0 = no jitter.
    float    jitterX = 0.f;
    float    jitterY = 0.f;
    uint32_t taaWriteIdx = 0;
    uint32_t taaReadIdx = 1;
    Mat4     unjitteredViewProj{};
    Mat4     jitteredViewProj{};    ///< ViewProj with TAA sub-pixel jitter applied.

    // ── Post-process chain SRV routing ──────────────────────────────
    // Each pass in the post-process chain may override these to chain
    // its output as the next pass's input.  The tonemap pass reads
    // the final value.
    uint32_t taaSrvForTonemap = UINT32_MAX;
    uint32_t motionBlurSrvForTonemap = UINT32_MAX;
    uint32_t postDofSrvForTonemap = UINT32_MAX;

    // ── PostProcess Volume blended settings ────────────────────────
    /// Blended post-process settings from all active PostProcessVolumeComponents.
    /// Valid only when hasBlendedPP is true. Subsystems (bloom, DOF, etc.) should
    /// check hasBlendedPP and use these instead of CVar defaults when set.
    PostProcessSettings blendedPP;
    bool hasBlendedPP = false;

    // ── Post-process activation flags ───────────────────────────────
    bool bloomEnabled = false;
    bool ssrActive = false;
    bool motionBlurActive = false;
    bool dofActive = false;
    bool fxaaActive = false;
    bool casActive = false;

    // ── FXAA/CAS render graph resource ──────────────────────────────
    rg::RgResourceId fxaaLdrRes{};

    // ── Editor gizmo state ───────────────────────────────────────────
    // These are set from the EditorInputHandler during frame setup.
    // The overlay pass reads them to choose which gizmo geometry to draw.
    // Using uint8_t to avoid including editor_input_handler.h in this header.
    uint8_t gizmoModeRaw = 0;   ///< GizmoMode enum cast: 0=Translate, 1=Rotate, 2=Scale.
    uint8_t gizmoSpaceRaw = 0;  ///< GizmoSpace enum cast: 0=World, 1=Local.
    int gizmoHoveredAxis = -1;  ///< Hovered gizmo axis (-1=none, 0-6=axis/plane/center).
    int gizmoActiveAxis = -1;   ///< Active (dragging) gizmo axis (same encoding).
};

} // namespace uz
