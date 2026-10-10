/// UzEngine Scene Renderer — Post-process passes: TAA / temporal upscaler, SSR composite
/// and copy-prev-colour, bloom, tonemap (straight to the backbuffer), region-dump readback.

#include "scene_renderer.h"
#include "region_dump_layout.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"

#include "camera.h"
#include "units.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <string>

// ── CVars ────────────────────────────────────────────────────────────────
// r.SSR CVar is declared in frame_lighting.cpp (single source of truth)
static auto* cv_SSRComposite    = UZ_CVAR_FLOAT("r.SSR.Composite", 0.0f, "Legacy additive post-TAA SSR composite (block B.5: the lighting blends SSR before the BRDF; 1 double-counts, kept as the A/B control)", 0.0f, 1.0f);
static auto* cv_MotionBlur      = UZ_CVAR_BOOL("r.MotionBlur", true, "Motion blur (inert: pass not wired in the current post chain)");
static auto* cv_DOF             = UZ_CVAR_BOOL("r.DOF", false, "Depth of field (inert: pass not wired in the current post chain)");
static auto* cv_CAS             = UZ_CVAR_BOOL("r.CAS", true, "Contrast adaptive sharpening (inert: pass not wired in the current post chain)");
static auto* cv_TonemapOp       = UZ_CVAR_INT("r.TonemapOperator", 1,
    "Tonemap: 0=None, 1=Hill ACES, 2=Narkowicz ACES, 3=Reinhard, 4=Hable", 0, 4);
static auto* cv_DOFFocusDist    = UZ_CVAR_FLOAT("r.DOF.FocusDistance", 0.f, "DOF focus distance (cm) (inert: DOF pass not wired)", 0.f, 100000.f);
static auto* cv_DOFAperture     = UZ_CVAR_FLOAT("r.DOF.Aperture", 4.f, "DOF f-stop number (inert: DOF pass not wired)", 0.5f, 32.f);
static auto* cv_DOFFocalRegion  = UZ_CVAR_FLOAT("r.DOF.FocalRegion", 0.f, "In-focus zone width (cm) (inert: DOF pass not wired)", 0.f, 10000.f);
static auto* cv_DOFNearTrans    = UZ_CVAR_FLOAT("r.DOF.NearTransition", 300.f, "Near transition ramp (cm) (inert: DOF pass not wired)", 0.f, 10000.f);
static auto* cv_DOFFarTrans     = UZ_CVAR_FLOAT("r.DOF.FarTransition", 500.f, "Far transition ramp (cm) (inert: DOF pass not wired)", 0.f, 50000.f);
static auto* cv_DOFRingCount    = UZ_CVAR_INT("r.DOF.RingCount", 5, "DOF bokeh concentric ring count (inert: DOF pass not wired)", 3, 8);
static auto* cv_DOFCoCSkip      = UZ_CVAR_FLOAT("r.DOF.CoCSkipThreshold", 0.5f, "DOF CoC radius skip threshold (px) (inert: DOF pass not wired)", 0.1f, 3.f);
static auto* cv_MotionBlurMaxR  = UZ_CVAR_FLOAT("r.MotionBlur.MaxRadius", 5.0f, "Max motion blur radius (% viewport) (inert: pass not wired)", 0.1f, 20.f);
static auto* cv_MotionBlurFPS   = UZ_CVAR_INT("r.MotionBlur.TargetFPS", 30, "Target FPS for velocity scaling (inert: pass not wired)", 24, 120);
static auto* cv_FXAAThreshold   = UZ_CVAR_FLOAT("r.FXAA.EdgeThreshold", 0.166f, "FXAA edge detection threshold (inert: pass not wired)", 0.063f, 0.333f);
static auto* cv_TonemapChromaAb = UZ_CVAR_FLOAT("r.Tonemap.ChromaAbScale", 0.007f, "Chromatic aberration intensity scale (inert: not read by the live tonemap)", 0.f, 0.02f);
static auto* cv_POMHeightRatio    = UZ_CVAR_FLOAT("r.Material.POM.HeightRatio", 0.05f, "POM perceived height ratio", 0.01f, 0.2f);
static auto* cv_POMShadowSteps   = UZ_CVAR_INT("r.Material.POM.ShadowSteps", 6, "POM self-shadow ray steps", 4, 16);
static auto* cv_POMRefinement    = UZ_CVAR_INT("r.Material.POM.RefinementSteps", 1, "POM binary search refinement", 1, 3);

namespace uz {

namespace {

/// Region dump file header (version 2, little-endian). Followed by `rectCount`
/// RegionFileRect, then `rectCount` RegionFileStamps, then the raw dump buffer
/// (each rect's per-pixel records followed by its 4 GPU-written footer float4).
/// See shaders/runtime/common/region_dump.hlsli for the record layout.
/// Version 3 appends a camera block (unjittered view-projection, the previous frame's, and the
/// camera position) right after this header, so an analysis can track a world point across
/// frames instead of comparing screen pixels while the camera moves. Version 2 has no block.
struct RegionFileCamera {
    float viewProj[16];        ///< Unjittered view-projection of this frame.
    float prevViewProj[16];    ///< The previous frame's, for reprojection.
    float cameraPos[3];
    float pad;
};
static_assert(sizeof(RegionFileCamera) == 144, "region dump camera block must stay 144 bytes");

struct RegionFileHeader {
    uint32_t magic;            ///< 'UZR2' (0x32525A55).
    uint32_t version;          ///< 3 (2 = no camera block).
    uint32_t frame;            ///< Frame index of the render this file holds.
    uint32_t rectCount;
    uint32_t fields;           ///< Field-group bitmask.
    uint32_t recordsPerPixel;
    uint32_t flags;            ///< RegionFileFlag bits.
    uint32_t renderWidth;      ///< CPU-side render resolution of that frame.
    uint32_t renderHeight;
    uint32_t outputWidth;      ///< CPU-side output (viewport) resolution of that frame.
    uint32_t outputHeight;
    uint32_t jitterIndex;      ///< Halton index of that frame (0 = unjittered).
    float    jitterX;          ///< Sub-pixel jitter in render pixels.
    float    jitterY;
    float    exposure;         ///< Exposure that frame (auto-exposure x r.Exposure).
    /// Format 4: record count of each field group, 6 bits per group, in field-bit order
    /// (lighting, taa, output, taa_extra, lighting_extra). The reader derives every channel
    /// offset from this instead of from constants of its own, so a capture can always be read
    /// with the layout it was written with. Zero in formats 2 and 3.
    uint32_t groupRecords;
};
static_assert(sizeof(RegionFileHeader) == 64, "region dump header must stay 64 bytes");

struct RegionFileRect {
    char     name[16];
    uint32_t x, y, w, h;
};
static_assert(sizeof(RegionFileRect) == 32, "region dump rect entry must stay 32 bytes");

struct RegionFileStamps {
    uint32_t lightingStamp;    ///< Frame the deferred lighting pass stamped (UINT32_MAX = never written).
    uint32_t taaStamp;         ///< Frame the TAA resolve stamped.
    uint32_t stampFlags;       ///< Per-rect subset of RegionFileFlag.
    uint32_t baseOffset;       ///< float4 offset of this rect inside the data block.
};

enum RegionFileFlag : uint32_t {
    kRegionFlagTaaActive        = 1u << 0,
    kRegionFlagLightingMismatch = 1u << 1,  ///< Lighting stamp != the frame the CPU read back.
    kRegionFlagTaaMismatch      = 1u << 2,  ///< TAA stamp != the frame the CPU read back.
    kRegionFlagLightingMissing  = 1u << 3,  ///< Lighting never wrote this rect (pass skipped this frame).
    kRegionFlagTaaMissing       = 1u << 4,  ///< TAA resolve never wrote this rect.
    kRegionFlagResolutionSplit  = 1u << 5,  ///< Lighting (render) and TAA (output) domains differ.
    kRegionFlagGatherMismatch   = 1u << 6,  ///< Gather stamp != the frame — stale records from a skipped pass.
    kRegionFlagGatherMissing    = 1u << 7,  ///< The gather group was requested but its passes never wrote it.
};

}  // namespace

void SceneRenderer::SetRegionDump(std::string dir, std::vector<SceneRenderer::RegionRect> rects,
                                  uint32_t firstFrame, uint32_t lastFrame, uint32_t fields) {
    if (dir.empty() || rects.empty() || fields == 0) return;
    rects.erase(std::remove_if(rects.begin(), rects.end(),
                               [](const RegionRect& r) { return r.w == 0 || r.h == 0; }),
                rects.end());
    if (rects.empty()) return;
    m_regionDumpDir = std::move(dir);
    m_regionRects = std::move(rects);
    m_regionFirstFrame = firstFrame; m_regionLastFrame = lastFrame;
    m_regionFields = fields;
    m_regionDumpRequested = true;
}

void SceneRenderer::WriteRegionDumpFile(uint32_t slot) {
    const uint32_t frame = m_regionReadbackFrame[slot];
    const void* mapped = m_device->MapBuffer(m_regionReadback[slot]);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "RegionDump: readback map failed for frame {}", frame);
        return;
    }
    const auto* words = static_cast<const uint32_t*>(mapped);
    const uint32_t records = static_cast<uint32_t>(m_regionBufferBytes / 16ull);

    std::vector<RegionFileRect> rects(m_regionRects.size());
    std::vector<RegionFileStamps> stamps(m_regionRects.size());
    uint32_t flags = m_regionReadbackTaa[slot] ? kRegionFlagTaaActive : 0u;
    uint32_t gpuRenderW = 0, gpuRenderH = 0, gpuOutputW = 0, gpuOutputH = 0;
    for (size_t i = 0; i < m_regionRects.size(); ++i) {
        const auto& r = m_regionRects[i];
        std::memset(&rects[i], 0, sizeof(RegionFileRect));
        std::snprintf(rects[i].name, sizeof(rects[i].name), "%s", r.name.c_str());
        rects[i].x = r.x; rects[i].y = r.y; rects[i].w = r.w; rects[i].h = r.h;

        const uint32_t footer = m_regionRectBase[i] + r.w * r.h * m_regionRecordsPerPixel;
        uint32_t rectFlags = 0;
        uint32_t lightingStamp = UINT32_MAX, taaStamp = UINT32_MAX;
        if (footer + 4u <= records) {
            const uint32_t* lig = words + size_t(footer) * 4u;
            const uint32_t* taa = words + (size_t(footer) + 1u) * 4u;
            const float ligValid = *reinterpret_cast<const float*>(lig + 3);
            const float taaValid = *reinterpret_cast<const float*>(taa + 3);
            if ((m_regionFields & 1u) != 0u) {
                if (ligValid != 1.0f) { rectFlags |= kRegionFlagLightingMissing; }
                else {
                    lightingStamp = lig[0];
                    gpuRenderW = static_cast<uint32_t>(*reinterpret_cast<const float*>(lig + 1));
                    gpuRenderH = static_cast<uint32_t>(*reinterpret_cast<const float*>(lig + 2));
                    if (lightingStamp != frame) rectFlags |= kRegionFlagLightingMismatch;
                }
            }
            if ((m_regionFields & 6u) != 0u) {
                if (taaValid != 1.0f) { rectFlags |= kRegionFlagTaaMissing; }
                else {
                    taaStamp = taa[0];
                    gpuOutputW = static_cast<uint32_t>(*reinterpret_cast<const float*>(taa + 1));
                    gpuOutputH = static_cast<uint32_t>(*reinterpret_cast<const float*>(taa + 2));
                    if (taaStamp != frame) rectFlags |= kRegionFlagTaaMismatch;
                }
            }
            // The gather group, when it was asked for. The dump buffer is not cleared between
            // frames, so a pass that stopped running leaves its last records in place; the stamp is
            // what tells them apart from records written this frame.
            if ((m_regionFields & 32u) != 0u) {
                const uint32_t* gat = words + (size_t(footer) + 2u) * 4u;
                const float gatValid = *reinterpret_cast<const float*>(gat + 3);
                if (gatValid != 1.0f) { rectFlags |= kRegionFlagGatherMissing; }
                else if (gat[0] != frame) { rectFlags |= kRegionFlagGatherMismatch; }
            }
            if (gpuRenderW != 0 && gpuOutputW != 0 && (gpuRenderW != gpuOutputW || gpuRenderH != gpuOutputH))
                rectFlags |= kRegionFlagResolutionSplit;
        } else {
            rectFlags |= kRegionFlagLightingMissing | kRegionFlagTaaMissing;
        }
        stamps[i].lightingStamp = lightingStamp;
        stamps[i].taaStamp = taaStamp;
        stamps[i].stampFlags = rectFlags;
        stamps[i].baseOffset = m_regionRectBase[i];
        flags |= rectFlags;
    }

    constexpr uint32_t kInvalidMask = kRegionFlagLightingMismatch | kRegionFlagTaaMismatch
                                    | kRegionFlagLightingMissing | kRegionFlagTaaMissing
                                    | kRegionFlagGatherMismatch | kRegionFlagGatherMissing;
    if ((flags & kInvalidMask) != 0u) {
        ++m_regionStampMismatches;
        if (m_regionStampMismatches <= 8) {
            UZ_LOG(Renderer, Error,
                   "RegionDump: frame {} invalid (flags=0x{:x}) — GPU stamps disagree with the read-back frame",
                   frame, flags);
        }
    }

    RegionFileHeader header{};
    header.magic = 0x32525A55u;   // 'UZR2'
    header.version = 5;
    header.frame = frame;
    header.rectCount = static_cast<uint32_t>(m_regionRects.size());
    header.fields = m_regionFields;
    header.recordsPerPixel = m_regionRecordsPerPixel;
    // Packed from region_dump_layout.h (LIGHTING_EXTRA 13 records since 2026-09-18).
    header.groupRecords = RegionGroupRecordsPacked(m_regionFields);
    header.flags = flags;
    header.renderWidth = gpuRenderW ? gpuRenderW : m_regionReadbackRes[slot][0];
    header.renderHeight = gpuRenderH ? gpuRenderH : m_regionReadbackRes[slot][1];
    header.outputWidth = gpuOutputW ? gpuOutputW : m_regionReadbackRes[slot][2];
    header.outputHeight = gpuOutputH ? gpuOutputH : m_regionReadbackRes[slot][3];
    header.jitterIndex = m_regionReadbackJitterIdx[slot];
    header.jitterX = m_regionReadbackJitter[slot][0];
    header.jitterY = m_regionReadbackJitter[slot][1];
    header.exposure = m_regionReadbackExposure[slot];

    RegionFileCamera camera{};
    std::memcpy(camera.viewProj, &m_regionReadbackViewProj[slot], sizeof(camera.viewProj));
    std::memcpy(camera.prevViewProj, &m_regionReadbackPrevViewProj[slot], sizeof(camera.prevViewProj));
    camera.cameraPos[0] = m_regionReadbackCamera[slot].x;
    camera.cameraPos[1] = m_regionReadbackCamera[slot].y;
    camera.cameraPos[2] = m_regionReadbackCamera[slot].z;

    const std::string path = m_regionDumpDir + "/region_" + std::to_string(frame) + ".bin";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(&header, sizeof(header), 1, f);
        std::fwrite(&camera, sizeof(camera), 1, f);
        std::fwrite(rects.data(), sizeof(RegionFileRect), rects.size(), f);
        std::fwrite(stamps.data(), sizeof(RegionFileStamps), stamps.size(), f);
        std::fwrite(mapped, 1, static_cast<size_t>(m_regionBufferBytes), f);
        std::fclose(f);
        ++m_regionFilesWritten;
    } else {
        UZ_LOG(Renderer, Error, "RegionDump: cannot open '{}' for writing", path);
    }
    m_device->UnmapBuffer(m_regionReadback[slot]);
}

void SceneRenderer::RegisterPostProcessPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // Simplified pipeline — TAA + Bloom + Tonemap direct to backbuffer.
    // No CAS, no FXAA, no motion blur, no DOF.
    {
        // ── TAA resolve (jitter removal + temporal accumulation) ─────────
        uint32_t tonemapInputSrv = m_hdrSceneColorSrv;

        if (ctx.taaActive && m_upscaler && m_shadowDebugMode != 1) {
            const uint32_t writeIdx = ctx.taaWriteIdx;
            const uint32_t readIdx  = ctx.taaReadIdx;
            const float jitterX = ctx.jitterX;
            const float jitterY = ctx.jitterY;
            const bool firstFrame = !m_prevViewProjValid;

            rgBuilder.AddPass(m_upscaler->GetName(), [&, writeIdx, readIdx, jitterX, jitterY, firstFrame](rg::RgPassBuilder& pb) {
                pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

                pb.SetExecutionCallback([this, &ctx, writeIdx, readIdx, jitterX, jitterY, firstFrame](IRHICommandList* cmd, const rg::RgPassResources&) {
                    UZ_PROFILE_SCOPE("Upscaler::Execute");

                    // History barrier states depend on whether this is the first frame.
                    // First frame: textures are in Common state (never written).
                    // Subsequent frames: write target is SRV (read by tonemap last frame),
                    //                    read target is UAV (written by upscaler last frame) → need SRV.
                    // The clamp-box diagnostic allocates only while it is enabled.
                    const int clampFreezeFrame =
                        CVarRegistry::Get().GetInt("r.TAA.ClampFreezeAfterFrame", -1);
                    EnsureTaaClampBoxResources(clampFreezeFrame >= 0);
                    const bool clampBoxReady =
                        m_taaClampBox[0].IsValid() && m_taaClampBox[1].IsValid();
                    const bool clampBoxRead =
                        clampBoxReady && m_taaClampBoxInitialised
                        && static_cast<int>(ctx.frameIndex) > clampFreezeFrame;
                    if (clampBoxReady) {
                        // Written as a UAV until the freeze frame, read as an SRV after it.
                        RHIBarrierDesc boxBarriers[2]{};
                        for (int i = 0; i < 2; ++i) {
                            boxBarriers[i].texture     = m_taaClampBox[i];
                            boxBarriers[i].buffer      = RHIBufferHandle::Invalid();
                            boxBarriers[i].stateBefore =
                                !m_taaClampBoxInitialised ? RHIResourceState::Common
                                : (clampBoxRead ? RHIResourceState::UnorderedAccess
                                                : RHIResourceState::ShaderResource);
                            boxBarriers[i].stateAfter  = clampBoxRead
                                ? RHIResourceState::ShaderResource
                                : RHIResourceState::UnorderedAccess;
                        }
                        cmd->ResourceBarrier(boxBarriers, 2);
                        m_taaClampBoxInitialised = true;
                    }

                    {
                        RHIBarrierDesc barriers[2]{};
                        barriers[0].texture     = m_taaHistory[writeIdx];
                        barriers[0].buffer      = RHIBufferHandle::Invalid();
                        barriers[0].stateBefore = firstFrame ? RHIResourceState::Common : RHIResourceState::ShaderResource;
                        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;

                        barriers[1].texture     = m_taaHistory[readIdx];
                        barriers[1].buffer      = RHIBufferHandle::Invalid();
                        barriers[1].stateBefore = firstFrame ? RHIResourceState::Common : RHIResourceState::UnorderedAccess;
                        barriers[1].stateAfter  = RHIResourceState::ShaderResource;

                        cmd->ResourceBarrier(barriers, firstFrame ? 1 : 2);
                    }

                    UpscalerInput input{};
                    input.sceneColorSrv       = m_hdrSceneColorSrv;
                    input.depthSrv            = m_gbuffer.depthSRV;
                    input.velocitySrv         = m_gbuffer.velocitySRV;
                    input.historyReadSrv      = m_taaHistorySrv[readIdx];
                    input.clampBoxSrv[0]      = clampBoxRead ? m_taaClampBoxSrv[0] : UINT32_MAX;
                    input.clampBoxSrv[1]      = clampBoxRead ? m_taaClampBoxSrv[1] : UINT32_MAX;
                    input.clampBoxUav[0]      = clampBoxRead ? UINT32_MAX : m_taaClampBoxUav[0];
                    input.clampBoxUav[1]      = clampBoxRead ? UINT32_MAX : m_taaClampBoxUav[1];
                    input.clampFreezeEnabled  = clampBoxReady ? 1u : 0u;
                    input.frameIndex          = ctx.frameIndex;
                    input.historyWriteUav     = m_taaHistoryUav[writeIdx];
                    input.sceneColorTexture   = m_hdrSceneColor;
                    input.depthTexture        = m_gbuffer.depth;
                    input.velocityTexture     = m_gbuffer.velocity;
                    input.historyReadTexture  = m_taaHistory[readIdx];
                    input.historyWriteTexture = m_taaHistory[writeIdx];
                    input.jitterX      = jitterX;
                    input.jitterY      = jitterY;
                    input.renderWidth  = ctx.renderWidth;
                    input.renderHeight = ctx.renderHeight;
                    input.outputWidth  = ctx.vpWidth;
                    input.outputHeight = ctx.vpHeight;
                    input.firstFrame   = firstFrame;
                    input.exposure     = ctx.perFrame.exposure;

                    // Region dump (diagnostics): the lighting pass wrote its records earlier this
                    // frame; order the resolve's writes after them with a UAV barrier.
                    const bool regionFrame = m_regionDumpActive && ctx.frameIndex >= m_regionFirstFrame && ctx.frameIndex <= m_regionLastFrame;
                    if (regionFrame) {
                        input.debugRegionUav    = m_regionDumpUav;
                        input.debugRegionSrv    = m_regionRectSrv;
                        input.debugRegionCount  = static_cast<uint32_t>(m_regionRects.size());
                        input.debugRegionStamp  = ctx.frameIndex;
                        input.debugRegionFields = m_regionFields;
                        RHIBarrierDesc uav{}; uav.buffer = m_regionDumpBuffer;
                        uav.stateBefore = RHIResourceState::UnorderedAccess; uav.stateAfter = RHIResourceState::UnorderedAccess;
                        cmd->ResourceBarrier(&uav, 1);
                    }

                    m_upscaler->Execute(cmd, input);


                    // Write target → SRV for tonemap to read
                    {
                        RHIBarrierDesc barrier{};
                        barrier.texture     = m_taaHistory[writeIdx];
                        barrier.buffer      = RHIBufferHandle::Invalid();
                        barrier.stateBefore = RHIResourceState::UnorderedAccess;
                        barrier.stateAfter  = RHIResourceState::ShaderResource;
                        cmd->ResourceBarrier(&barrier, 1);
                    }
                });
            });

            // Tonemap reads from TAA output (de-jittered + temporally stable)
            tonemapInputSrv = m_taaHistorySrv[writeIdx];

            // Flip ping-pong for next frame
            m_taaHistoryIndex = 1 - m_taaHistoryIndex;
        }

        // Region dump (diagnostics): the previous frame's matrix, taken before it is
        // overwritten just below, so a capture can reproject a world point into the frame
        // that came before it.
        const Mat4 regionPrevViewProj = m_prevViewProj;

        // Store unjittered viewProj for next frame's reprojection.
        // MUST be inside the simplified block — the original code after `return`
        // is never reached, so prevViewProj would never be set.
        m_prevViewProj = ctx.unjitteredViewProj;
        m_prevViewProjValid = true;

        // ── SSR Post-TAA Composite ──────────────────────────────────────
        // Apply SSR AFTER TAA to prevent TAA neighborhood contamination.
        // The deferred lighting pass outputs IBL-only specular; SSR is
        // composited here onto the TAA-resolved (or raw HDR) image.
        // Block B.5: the lighting pass blends SSR before the BRDF over the SDF or IBL layer; this
        // additive composite double-counts it and runs only when r.SSR.Composite is set (A/B control).
        if (ctx.ssrActive && m_ssrHasPrevColor && cv_SSRComposite->value >= 0.5f
            && m_ssrResolvedOutput.IsValid()
            && m_ssrResolvedOutputSrv != UINT32_MAX) {

            // Determine the target: TAA output if TAA active, else raw HDR
            const uint32_t compositeTargetUav = ctx.taaActive
                ? m_taaHistoryUav[1 - m_taaHistoryIndex]  // TAA just-written
                : UINT32_MAX;  // will use raw HDR fallback

            if (compositeTargetUav != UINT32_MAX || !ctx.taaActive) {
                rgBuilder.AddPass("SSR_Composite", [&, compositeTargetUav, tonemapInputSrv](rg::RgPassBuilder& pb) {
                    pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);   // block B.5: after TAA, before the tonemap
                    pb.SetExecutionCallback([this, &ctx, compositeTargetUav, tonemapInputSrv](IRHICommandList* cmd, const rg::RgPassResources&) {
                        UZ_PROFILE_SCOPE("SSR::Composite");

                        auto ssrCompositePSO = ctx.pipelines->GetSsrComposite();
                        if (!ssrCompositePSO.IsValid() || cv_SSRComposite->value < 0.5f) return;

                        // Determine which texture to composite onto
                        RHITextureHandle targetTex = ctx.taaActive
                            ? m_taaHistory[1 - m_taaHistoryIndex]  // TAA wrote here
                            : m_hdrSceneColor;
                        uint32_t targetUav = ctx.taaActive
                            ? m_taaHistoryUav[1 - m_taaHistoryIndex]
                            : UINT32_MAX;  // need HDR UAV — skip if not available

                        if (targetUav == UINT32_MAX) return;  // no UAV for raw HDR

                        // TAA output is in SRV state — transition to UAV for R/W
                        {
                            RHIBarrierDesc barrier{};
                            barrier.texture     = targetTex;
                            barrier.stateBefore = RHIResourceState::ShaderResource;
                            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                            cmd->ResourceBarrier(&barrier, 1);
                        }

                        cmd->SetPipeline(ssrCompositePSO);

                        struct SsrCompositeConstants {
                            uint32_t ssrSrvIdx, normalSrvIdx, depthSrvIdx, reserved;
                            uint32_t outputWidth, outputHeight;
                            float intensity, pad;
                        };

                        SsrCompositeConstants rc{};
                        rc.ssrSrvIdx    = m_ssrResolvedOutputSrv;
                        rc.normalSrvIdx = m_gbuffer.normalSRV;
                        rc.depthSrvIdx  = m_gbuffer.depthSRV;
                        rc.outputWidth  = static_cast<uint32_t>(ctx.vpWidth);
                        rc.outputHeight = static_cast<uint32_t>(ctx.vpHeight);
                        rc.intensity    = 1.0f;

                        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);
                        cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                        ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                        sizeof(PerFrameCB));
                        cmd->SetDescriptorTable(4, targetUav);

                        const uint32_t groupsX = (rc.outputWidth + 7) / 8;
                        const uint32_t groupsY = (rc.outputHeight + 7) / 8;
                        cmd->Dispatch(groupsX, groupsY, 1);

                        // Back to SRV for tonemap/bloom
                        {
                            RHIBarrierDesc barrier{};
                            barrier.texture     = targetTex;
                            barrier.stateBefore = RHIResourceState::UnorderedAccess;
                            barrier.stateAfter  = RHIResourceState::ShaderResource;
                            cmd->ResourceBarrier(&barrier, 1);
                        }
                    });
                });
            }
        }

        // ── SSR CopyPrevColor (post-TAA, post-composite) ────────────────
        // Copy the TAA-resolved scene color for next frame's SSR trace input.
        // MUST run AFTER TAA (not before) to avoid barrier transitions on
        // hdrSceneColor that cause micro-pixel TAA variation → stipple noise.
        // @ref UE5 — reads PrevViewInfo.TemporalAAHistory (prev TAA output)
        if (m_ssrEnabled && m_ssrPrevSceneColor.IsValid()) {
            // Source: TAA output if available, else raw HDR
            const RHITextureHandle copySource = ctx.taaActive
                ? m_taaHistory[1 - m_taaHistoryIndex]
                : m_hdrSceneColor;
            const auto copySourceState = ctx.taaActive
                ? RHIResourceState::ShaderResource  // TAA output is in SRV
                : RHIResourceState::RenderTarget;   // HDR is in RT

            rgBuilder.AddPass("SSR_CopyPrevColor", [&, copySource, copySourceState](rg::RgPassBuilder& pb) {
                // Block B.5: declared so the graph schedules the copy after TAA (it declared nothing and ran
                // first in the frame, copying the TAA history in the wrong state: every SSR hit was black).
                pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
                pb.SetExecutionCallback([this, copySource, copySourceState](IRHICommandList* cmd, const rg::RgPassResources&) {
                    UZ_PROFILE_SCOPE("SSR::CopyPrevColor");

                    RHIBarrierDesc barriers[2]{};
                    barriers[0].texture     = copySource;
                    barriers[0].stateBefore = copySourceState;
                    barriers[0].stateAfter  = RHIResourceState::CopySrc;
                    barriers[1].texture     = m_ssrPrevSceneColor;
                    barriers[1].stateBefore = RHIResourceState::ShaderResource;
                    barriers[1].stateAfter  = RHIResourceState::CopyDst;
                    cmd->ResourceBarrier(barriers, 2);

                    cmd->CopyTexture(copySource, m_ssrPrevSceneColor);

                    RHIBarrierDesc restore[2]{};
                    restore[0].texture     = copySource;
                    restore[0].stateBefore = RHIResourceState::CopySrc;
                    restore[0].stateAfter  = copySourceState;
                    restore[1].texture     = m_ssrPrevSceneColor;
                    restore[1].stateBefore = RHIResourceState::CopyDst;
                    restore[1].stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(restore, 2);
                });
            });

            m_ssrHasPrevColor = true;
            m_ssrPrevExposure = ctx.perFrame.exposure;
        }

        // ── Bloom pass (if enabled) ─────────────────────────────────────
        // Bloom is built from the image the tonemapper displays: the TAA
        // output when TAA ran (de-jittered, display resolution) or the HDR
        // scene colour otherwise (render resolution — the two differ when
        // ScreenPercentage != 100). Reading the jittered HDR while displaying
        // the TAA output was a real mismatch, but it was measured NOT to be
        // the close-up wobble of 2026-09-08 (r.Bloom=0 left it unchanged);
        // that cause was the jitter matrix — see frame/taa_jitter.h.
        if (m_showFlags.Get(ShowFlag::Bloom) && ctx.bloomEnabled && m_bloomPass && m_bloomPass->IsInitialized()) {
            const uint32_t bloomInputSrv = tonemapInputSrv;
            const bool bloomFromDisplayRes = (bloomInputSrv != m_hdrSceneColorSrv);
            const uint32_t bloomW = static_cast<uint32_t>(bloomFromDisplayRes ? ctx.vpWidth  : ctx.renderWidth);
            const uint32_t bloomH = static_cast<uint32_t>(bloomFromDisplayRes ? ctx.vpHeight : ctx.renderHeight);
            rgBuilder.AddPass("Bloom", [&, bloomInputSrv, bloomW, bloomH](rg::RgPassBuilder& pb) {
                pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
                pb.SetExecutionCallback([this, &ctx, bloomInputSrv, bloomW, bloomH](IRHICommandList* cmd, const rg::RgPassResources&) {
                    UZ_PROFILE_SCOPE("Bloom::Execute");
                    m_bloomPass->Execute(cmd, bloomInputSrv, bloomW, bloomH, ctx.perFrame.exposure);
                });
            });
        }

        // ── Tonemap pass (direct to backbuffer) ─────────────────────────
        const uint32_t capturedTonemapSrv = tonemapInputSrv;
        rgBuilder.AddPass("Tonemap", [&, capturedTonemapSrv](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
            pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);

            pb.SetExecutionCallback([this, &ctx, capturedTonemapSrv](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("Tonemap::Execute");

                auto bb = res.GetTexture(ctx.bbRes);

                // Validate the pipeline BEFORE opening the render pass: an early
                // return after BeginRenderPass left the pass open.
                const RHIPipelineHandle tonemapPipeline = ctx.pipelines->GetTonemap();
                if (!tonemapPipeline.IsValid()) return;

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount        = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear   = false;
                cmd->BeginRenderPass(passDesc);
                cmd->SetPipeline(tonemapPipeline);

                RHIViewport vp{};
                vp.width    = static_cast<uint32_t>(ctx.vpWidth);
                vp.height   = static_cast<uint32_t>(ctx.vpHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right  = static_cast<int32_t>(ctx.swapChain->GetWidth());
                sc.bottom = static_cast<int32_t>(ctx.swapChain->GetHeight());
                cmd->SetScissor(sc);

                uint32_t bloomSrvIdx = 0;
                float bloomIntensity = 0.0f;
                if (ctx.bloomEnabled && m_bloomPass && m_bloomPass->GetBloomResultSrvIndex() != UINT32_MAX) {
                    bloomSrvIdx = m_bloomPass->GetBloomResultSrvIndex();
                    bloomIntensity = m_bloomPass->settings.intensity;
                }
                uint32_t bloomIntensityBits = 0;
                std::memcpy(&bloomIntensityBits, &bloomIntensity, sizeof(float));

                uint32_t indices[6] = { capturedTonemapSrv, bloomSrvIdx, bloomIntensityBits, 0, 0,
                                        static_cast<uint32_t>(cv_TonemapOp->value) };
                cmd->SetRootConstants(kSlotRootConstants, indices, 6);

                static bool loggedOnce = false;
                if (!loggedOnce) {
                    UZ_LOG(Renderer, Info, "Tonemap: hdrSRV={}, taaActive={}, bloomEnabled={}",
                           capturedTonemapSrv, ctx.taaActive, ctx.bloomEnabled);
                    loggedOnce = true;
                }

                if (!ctx.bindless) {
                    cmd->SetFragmentTexture(0, m_hdrSceneColor);
                    if (m_bloomPass && m_bloomPass->GetBloomResultTexture().IsValid())
                        cmd->SetFragmentTexture(1, m_bloomPass->GetBloomResultTexture());
                }

                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                ctx.perFrameAlloc.sizeBytes);

                cmd->Draw(3);
                cmd->EndRenderPass();
            });
        });
        // ── Region dump readback (diagnostics) ──────────────────────────
        // Runs at the end of the frame and independently of TAA: with `r.TAA=0` the resolve
        // never writes its records, and the file must still be produced *and flagged*, so the
        // analysis rejects it instead of silently reading records from an older frame.
        if (m_regionDumpActive && ctx.frameIndex >= m_regionFirstFrame && ctx.frameIndex <= m_regionLastFrame) {
            rgBuilder.AddPass("RegionDumpReadback", [&](rg::RgPassBuilder& pb) {
                pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
                pb.SetExecutionCallback([this, &ctx, regionPrevViewProj](IRHICommandList* cmd, const rg::RgPassResources&) {
                    UZ_PROFILE_SCOPE("RegionDump::Readback");
                    // Copy this frame's records into the readback of slot frame%3, then write the
                    // file of the frame that used this slot three frames ago: the main loop waits
                    // on that frame's fence before reusing the slot, so its GPU work is complete.
                    // The GPU frame stamps inside the data are compared against the frame the CPU
                    // thinks it is writing — a mismatch marks the file invalid instead of shipping
                    // a stale readback as if it were a real frame.
                    const uint32_t slot = ctx.frameIndex % 3u;
                    if (m_regionReadbackFrame[slot] != UINT32_MAX) {
                        WriteRegionDumpFile(slot);
                    }
                    RHIBarrierDesc toCopy{}; toCopy.buffer = m_regionDumpBuffer;
                    toCopy.stateBefore = RHIResourceState::UnorderedAccess; toCopy.stateAfter = RHIResourceState::CopySrc;
                    cmd->ResourceBarrier(&toCopy, 1);
                    cmd->CopyBuffer(m_regionDumpBuffer, 0, m_regionReadback[slot], 0, m_regionBufferBytes);
                    toCopy.stateBefore = RHIResourceState::CopySrc; toCopy.stateAfter = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&toCopy, 1);
                    m_regionReadbackFrame[slot] = ctx.frameIndex;
                    m_regionReadbackJitter[slot][0] = ctx.jitterX;
                    m_regionReadbackJitter[slot][1] = ctx.jitterY;
                    m_regionReadbackExposure[slot] = ctx.perFrame.exposure;
                    m_regionReadbackJitterIdx[slot] = ctx.jitterIndex;
                    m_regionReadbackRes[slot][0] = static_cast<uint32_t>(ctx.renderWidth);
                    m_regionReadbackRes[slot][1] = static_cast<uint32_t>(ctx.renderHeight);
                    m_regionReadbackRes[slot][2] = static_cast<uint32_t>(ctx.vpWidth);
                    m_regionReadbackRes[slot][3] = static_cast<uint32_t>(ctx.vpHeight);
                    m_regionReadbackTaa[slot] = ctx.taaActive;
                    // Motion compensation (followup-13) needs the camera of the frame that
                    // produced the records, and the matrix of the frame before it.
                    m_regionReadbackViewProj[slot] = ctx.unjitteredViewProj;
                    m_regionReadbackPrevViewProj[slot] = regionPrevViewProj;
                    m_regionReadbackCamera[slot] = ctx.perFrame.cameraPos.xyz();
                });
            });
        }

        return; // Skip full pipeline (CAS, FXAA, motion blur, DOF)
    }

    // The full pipeline that used to follow (second bloom/tonemap, lens flare,
    // colour-grading LUT, motion blur, DOF, CAS, FXAA, LDR blit) was dead code
    // behind the unconditional return above and was removed on 2026-09-08
    // (phase0_sponza-followup-08). It had not run since the shader rewrite
    // bd4db199 (2026-03-31) put the return here; the SponzaTest reference has the
    // same return. Its CVars stay registered but have no effect.
    //
    // Re-wiring those passes is not enough to bring them back: their shaders are
    // still the rewrite's scaffolds. motion_blur_cs, motionblur_tilemax_cs,
    // motionblur_neighbormax_cs, dof_setup_cs, dof_blur_cs, lensflare_cs and
    // color_grading_lut_cs are one-line no-op kernels; cas_sharpen_ps and fxaa_ps
    // copy their input unchanged. Wired as before (the C++ is in 7eea6b95^), the
    // tonemap would display the never-written motion blur output (r.MotionBlur is
    // on by default) and replace every colour with a sample of the never-written
    // LUT. Implement a pass's shader before registering the pass.

}

} // namespace uz

// Note for anyone changing the region-dump layout: this build does not treat headers as
// dependencies, so editing only `region_dump_layout.h` can leave the binary stale while
// `zig build` still reports success. `scripts/rendering/test_binary_layout.py` catches that by
// requiring the compiled engine to write the layout the sources declare.

// build-cache: PerFrameCB gained sdfSampleRotation (headers are not tracked as dependencies)

// build-cache: GATHER grew to 31 records (cascade-at-hit and clipmap 0 at +29..+30)
