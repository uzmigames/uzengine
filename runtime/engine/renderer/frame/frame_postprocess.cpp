/// UzEngine Scene Renderer — Post-process passes: bloom, TAA, color grading LUT,
/// motion blur (tile max + neighbor max + gather), DOF, tonemap, FXAA.

#include "scene_renderer.h"

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

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_SSR             = UZ_CVAR_BOOL("r.SSR", true, "Screen-space reflections");
static auto* cv_MotionBlur      = UZ_CVAR_BOOL("r.MotionBlur", true, "Motion blur");
static auto* cv_DOF             = UZ_CVAR_BOOL("r.DOF", false, "Depth of field");
static auto* cv_CAS             = UZ_CVAR_BOOL("r.CAS", true, "Contrast adaptive sharpening");
static auto* cv_DOFFocusDist    = UZ_CVAR_FLOAT("r.DOF.FocusDistance", 0.f, "DOF focus distance (cm)", 0.f, 100000.f);
static auto* cv_DOFAperture     = UZ_CVAR_FLOAT("r.DOF.Aperture", 4.f, "DOF f-stop number", 0.5f, 32.f);
static auto* cv_DOFFocalRegion  = UZ_CVAR_FLOAT("r.DOF.FocalRegion", 0.f, "In-focus zone width (cm)", 0.f, 10000.f);
static auto* cv_DOFNearTrans    = UZ_CVAR_FLOAT("r.DOF.NearTransition", 300.f, "Near transition ramp (cm)", 0.f, 10000.f);
static auto* cv_DOFFarTrans     = UZ_CVAR_FLOAT("r.DOF.FarTransition", 500.f, "Far transition ramp (cm)", 0.f, 50000.f);
static auto* cv_DOFRingCount    = UZ_CVAR_INT("r.DOF.RingCount", 5, "DOF bokeh concentric ring count", 3, 8);
static auto* cv_DOFCoCSkip      = UZ_CVAR_FLOAT("r.DOF.CoCSkipThreshold", 0.5f, "DOF CoC radius skip threshold (px)", 0.1f, 3.f);
static auto* cv_MotionBlurMaxR  = UZ_CVAR_FLOAT("r.MotionBlur.MaxRadius", 5.0f, "Max motion blur radius (% viewport)", 0.1f, 20.f);
static auto* cv_MotionBlurFPS   = UZ_CVAR_INT("r.MotionBlur.TargetFPS", 30, "Target FPS for velocity scaling", 24, 120);
static auto* cv_FXAAThreshold   = UZ_CVAR_FLOAT("r.FXAA.EdgeThreshold", 0.166f, "FXAA edge detection threshold", 0.063f, 0.333f);
static auto* cv_TonemapChromaAb = UZ_CVAR_FLOAT("r.Tonemap.ChromaAbScale", 0.007f, "Chromatic aberration intensity scale", 0.f, 0.02f);
static auto* cv_POMHeightRatio    = UZ_CVAR_FLOAT("r.Material.POM.HeightRatio", 0.05f, "POM perceived height ratio", 0.01f, 0.2f);
static auto* cv_POMShadowSteps   = UZ_CVAR_INT("r.Material.POM.ShadowSteps", 6, "POM self-shadow ray steps", 4, 16);
static auto* cv_POMRefinement    = UZ_CVAR_INT("r.Material.POM.RefinementSteps", 1, "POM binary search refinement", 1, 3);

namespace uz {

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

        // Store unjittered viewProj for next frame's reprojection.
        // MUST be inside the simplified block — the original code after `return`
        // is never reached, so prevViewProj would never be set.
        m_prevViewProj = ctx.unjitteredViewProj;
        m_prevViewProjValid = true;

        // ── Bloom pass (if enabled) ─────────────────────────────────────
        if (ctx.bloomEnabled && m_bloomPass && m_bloomPass->IsInitialized()) {
            rgBuilder.AddPass("Bloom", [&](rg::RgPassBuilder& pb) {
                pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
                pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
                    UZ_PROFILE_SCOPE("Bloom::Execute");
                    m_bloomPass->Execute(cmd, m_hdrSceneColorSrv,
                                         static_cast<uint32_t>(ctx.renderWidth),
                                         static_cast<uint32_t>(ctx.renderHeight),
                                         ctx.perFrame.exposure);
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

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount        = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear   = false;
                cmd->BeginRenderPass(passDesc);

                cmd->SetPipeline(ctx.pipelines->GetTonemap());

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

                uint32_t indices[5] = { capturedTonemapSrv, bloomSrvIdx, bloomIntensityBits, 0, 0 };
                cmd->SetRootConstants(kSlotRootConstants, indices, 5);

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
        return; // Skip full pipeline (CAS, FXAA, motion blur, DOF)
    }

    // ═══════ ORIGINAL CODE BELOW (skipped by return above) ═══════

    // All locals (pipelines, bindless, vpWidth, etc.) are accessed via ctx.*
    // to avoid dangling references in deferred execution callbacks.

    // Post-process passes below (bloom, tonemap, CAS, FXAA, etc.)

    // ── Bloom pass (compute: threshold + downsample + upsample) ─────────
    // Runs after all HDR rendering is complete. Reads HDR scene color,
    // extracts bright pixels, and builds a multi-scale bloom mip chain.
    // The bloom result (mip[0]) is composited by the tonemap shader.
    //
    // Only enabled when:
    // 1. BloomPass is initialized (DX12 compute available)
    // 2. HDR scene color has content from a previous frame (first frame skipped)
    // 3. Bloom is enabled in settings
    // 4. Buffer debug grid is not active
    if (ctx.bloomEnabled) {
        rgBuilder.AddPass("Bloom", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("Bloom::Execute");

                m_bloomPass->Execute(cmd, m_hdrSceneColorSrv,
                                     static_cast<uint32_t>(ctx.renderWidth), static_cast<uint32_t>(ctx.renderHeight),
                                     ctx.perFrame.exposure);
            });
        });
    }

    // ── Lens Flare pass (after bloom, only when looking toward the sun) ──
    // Project sun direction to screen space. If the sun is on-screen,
    // dispatch the lens flare shader. Ghosts reflect through the sun's
    // screen position, not through arbitrary bright pixels.
    if (m_lensFlareEnabled && m_lensFlarePass && m_lensFlarePass->IsInitialized()
        && ctx.bloomEnabled && m_bloomPass && m_bloomPass->GetBloomResultSrvIndex() != UINT32_MAX
        && m_shadowDebugMode != 1 && ctx.bindless) {

        // Project sun position to clip space (sun is at infinity → use direction)
        const Vec3& camPos = {ctx.perFrame.cameraPos.x, ctx.perFrame.cameraPos.y, ctx.perFrame.cameraPos.z};
        const Vec3 sunWorldPos = {
            camPos.x + ctx.sunDir.x * uz::Units::kDefaultFarPlaneCm,
            camPos.y + ctx.sunDir.y * uz::Units::kDefaultFarPlaneCm,
            camPos.z + ctx.sunDir.z * uz::Units::kDefaultFarPlaneCm
        };
        const Vec4 sunClip = ctx.unjitteredViewProj * Vec4{sunWorldPos.x, sunWorldPos.y, sunWorldPos.z, 1.f};

        // Check if sun is in front of camera (w > 0) and on-screen
        const bool sunVisible = (sunClip.w > 0.01f);
        float sunScreenX = 0.5f, sunScreenY = 0.5f;
        if (sunVisible) {
            sunScreenX = (sunClip.x / sunClip.w) *  0.5f + 0.5f;
            sunScreenY = (sunClip.y / sunClip.w) * -0.5f + 0.5f; // Y flip for D3D
        }
        // Sun must be roughly on screen (allow some margin for ghost tails)
        const bool sunOnScreen = sunVisible
            && sunScreenX > -0.5f && sunScreenX < 1.5f
            && sunScreenY > -0.5f && sunScreenY < 1.5f;

        if (sunOnScreen) {
            const float capturedSunX = sunScreenX;
            const float capturedSunY = sunScreenY;

            rgBuilder.AddPass("LensFlare", [&, capturedSunX, capturedSunY](rg::RgPassBuilder& pb) {
                pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

                pb.SetExecutionCallback([this, &ctx, capturedSunX, capturedSunY](IRHICommandList* cmd, const rg::RgPassResources&) {
                    UZ_PROFILE_SCOPE("LensFlare::Execute");

                    LensFlareSettings settings;
                    settings.enabled        = true;
                    settings.intensity      = m_lensFlareIntensity;
                    settings.ghostIntensity = m_lensFlareGhostIntensity;
                    settings.haloRadius     = m_lensFlareHaloRadius;
                    settings.haloWidth      = 0.04f;
                    settings.chromaticShift = m_lensFlareChromaticShift;
                    settings.sunScreenX     = capturedSunX;
                    settings.sunScreenY     = capturedSunY;

                    m_lensFlarePass->Execute(cmd, *m_cbRing,
                                              m_bloomPass->GetBloomResultSrvIndex(),
                                              settings);
                });
            });
        }
    }

    // ── Temporal Upscaler pass (TAA or TSR via ITemporalUpscaler) ──────
    // Temporal upscaler pass (TAA or TSR) — anti-aliasing + optional upscaling.
    //
    // Delegates to the active upscaler (TaaUpscaler or TsrUpscaler).
    // The caller manages history texture barriers and ping-pong, the upscaler
    // handles its own internal dispatches and intermediate resource transitions.
    if (ctx.taaActive && m_shadowDebugMode != 1 && m_upscaler) {
        const uint32_t writeIdx = ctx.taaWriteIdx;
        const uint32_t readIdx  = ctx.taaReadIdx;
        const float jitterX = ctx.jitterX;
        const float jitterY = ctx.jitterY;

        const char* passName = m_upscaler->GetName();
        rgBuilder.AddPass(passName, [&, writeIdx, readIdx, jitterX, jitterY](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx, writeIdx, readIdx, jitterX, jitterY](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("Upscaler::Execute");

                // Transition TAA history textures:
                // - Write target: SRV → UAV (will be written by upscaler)
                // - Read target → SRV (sampled by upscaler)
                {
                    RHIBarrierDesc barriers[2]{};
                    barriers[0].texture     = m_taaHistory[writeIdx];
                    barriers[0].buffer      = RHIBufferHandle::Invalid();
                    barriers[0].stateBefore = RHIResourceState::ShaderResource;
                    barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;

                    barriers[1].texture     = m_taaHistory[readIdx];
                    barriers[1].buffer      = RHIBufferHandle::Invalid();
                    barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
                    barriers[1].stateAfter  = RHIResourceState::ShaderResource;

                    cmd->ResourceBarrier(barriers, 2);
                }

                // Fill UpscalerInput and dispatch
                UpscalerInput input{};
                input.sceneColorSrv    = m_hdrSceneColorSrv;
                input.depthSrv         = m_gbuffer.depthSRV;
                input.velocitySrv      = m_gbuffer.velocitySRV;
                input.historyReadSrv   = m_taaHistorySrv[readIdx];
                input.historyWriteUav  = m_taaHistoryUav[writeIdx];
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
                input.firstFrame   = !m_prevViewProjValid;
                input.exposure     = ctx.perFrame.exposure;

                m_upscaler->Execute(cmd, input);

                // Transition write target back to SRV for tonemap to read.
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

        // Tonemap reads from the upscaler output instead of raw HDR scene color.
        ctx.taaSrvForTonemap = m_taaHistorySrv[writeIdx];

        // Flip ping-pong index for next frame.
        m_taaHistoryIndex = 1 - m_taaHistoryIndex;
    }

    // Update the SRV routing chain after TAA.
    ctx.motionBlurSrvForTonemap = ctx.taaSrvForTonemap;

    // Store unjittered viewProj for next frame's reprojection.
    // This runs every frame (not just when TAA is active) because the velocity
    // buffer pass and motion blur also need the previous frame's viewProj.
    m_prevViewProj = ctx.unjitteredViewProj;
    m_prevViewProjValid = true;

    // Save current frame's HiZ pyramid state for next frame's Phase 1 occlusion cull.
    // Phase 1 of two-phase culling tests against the previous frame's HiZ to identify
    // objects that are "definitely visible" before the depth prepass runs.
    m_prevHizMipCount = m_hizMipCount;
    if (m_hizMipCount > 0) {
        m_prevHizWidth  = m_hizMipLevels[0].width;
        m_prevHizHeight = m_hizMipLevels[0].height;
        for (uint32_t i = 0; i < m_hizMipCount && i < kHiZMaxMipLevels; ++i) {
            m_prevHizMipSrvIndices[i] = m_hizMipLevels[i].srvIndex;
        }
    }

    // ── Color Grading LUT generation (compute) ─────────────────────────
    // 3D LUT (32^3) for combined color grading + tonemap. Dispatch: 8x8 groups.
    // Generated once when dirty, sampled by tonemap shader via LUT SRV.
    if (m_colorGradingLutDirty && m_colorGradingLut.IsValid()
        && ctx.bindless && m_shadowDebugMode != 1) {
        rgBuilder.AddPass("ColorGradingLUT", [&](rg::RgPassBuilder& pb) {
            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("ColorGradingLUT::Generate");
                auto lutPSO = ctx.pipelines->GetColorGradingLut();
                if (!lutPSO.IsValid()) return;

                // Transition to UAV
                RHIBarrierDesc b{};
                b.texture = m_colorGradingLut;
                b.buffer = RHIBufferHandle::Invalid();
                b.stateBefore = RHIResourceState::ShaderResource;
                b.stateAfter = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&b, 1);

                cmd->SetPipeline(lutPSO);

                // Build LUT generation params (reference defaults).
                // This struct matches the LutGenParams cbuffer in color_grading_lut_cs.hlsl.
                // Too large for root constants, so uploaded via CB ring as root CBV.
                struct LutGenParams {
                    float whiteTemp, whiteTint;
                    float colorSaturation[4], colorContrast[4], colorGamma[4], colorGain[4], colorOffset[4];
                    float colorSaturationShadows[4], colorContrastShadows[4], colorGammaShadows[4], colorGainShadows[4], colorOffsetShadows[4];
                    float colorSaturationMidtones[4], colorContrastMidtones[4], colorGammaMidtones[4], colorGainMidtones[4], colorOffsetMidtones[4];
                    float colorSaturationHighlights[4], colorContrastHighlights[4], colorGammaHighlights[4], colorGainHighlights[4], colorOffsetHighlights[4];
                    float ccShadowsMax, ccHighlightsMin, ccHighlightsMax;
                    float filmSlope, filmToe, filmShoulder, filmBlackClip, filmWhiteClip;
                    float blueCorrection, expandGamut, toneCurveAmount;
                    float mappingPolynomial[3];
                    float colorScale[3];
                    float overlayColor[4];
                    float inverseGammaY, lutSize;
                    float outputExtentInverse[2];
                };
                LutGenParams p{};
                p.whiteTemp = 6500.0f; p.whiteTint = 0.0f;
                const float o[4] = {1,1,1,1}, z[4] = {0,0,0,0};
                std::memcpy(p.colorSaturation,o,16); std::memcpy(p.colorContrast,o,16);
                std::memcpy(p.colorGamma,o,16); std::memcpy(p.colorGain,o,16); std::memcpy(p.colorOffset,z,16);
                std::memcpy(p.colorSaturationShadows,o,16); std::memcpy(p.colorContrastShadows,o,16);
                std::memcpy(p.colorGammaShadows,o,16); std::memcpy(p.colorGainShadows,o,16); std::memcpy(p.colorOffsetShadows,z,16);
                std::memcpy(p.colorSaturationMidtones,o,16); std::memcpy(p.colorContrastMidtones,o,16);
                std::memcpy(p.colorGammaMidtones,o,16); std::memcpy(p.colorGainMidtones,o,16); std::memcpy(p.colorOffsetMidtones,z,16);
                std::memcpy(p.colorSaturationHighlights,o,16); std::memcpy(p.colorContrastHighlights,o,16);
                std::memcpy(p.colorGammaHighlights,o,16); std::memcpy(p.colorGainHighlights,o,16); std::memcpy(p.colorOffsetHighlights,z,16);
                p.ccShadowsMax = 0.09f; p.ccHighlightsMin = 0.5f; p.ccHighlightsMax = 1.0f;
                p.filmSlope = ctx.perFrame.toneCurveSlope; p.filmToe = ctx.perFrame.toneCurveToe;
                p.filmShoulder = ctx.perFrame.toneCurveShoulder; p.filmBlackClip = ctx.perFrame.toneCurveBlackClip;
                p.filmWhiteClip = ctx.perFrame.toneCurveWhiteClip;
                p.blueCorrection = 0.6f; p.expandGamut = 1.0f; p.toneCurveAmount = 1.0f;
                p.mappingPolynomial[0] = 0; p.mappingPolynomial[1] = 1; p.mappingPolynomial[2] = 0;
                p.colorScale[0] = 1; p.colorScale[1] = 1; p.colorScale[2] = 1;
                p.overlayColor[0] = 0; p.overlayColor[1] = 0; p.overlayColor[2] = 0; p.overlayColor[3] = 0;
                p.inverseGammaY = 1.0f;
                p.lutSize = static_cast<float>(kColorGradingLutSize);
                p.outputExtentInverse[0] = 1.0f / p.lutSize;
                p.outputExtentInverse[1] = 1.0f / p.lutSize;

                auto lutCb = m_cbRing->Allocate(sizeof(p));
                std::memcpy(lutCb.cpuPtr, &p, sizeof(p));
                cmd->SetRootCBV(2, lutCb.bufferHandle, lutCb.offsetBytes, lutCb.cpuPtr, lutCb.sizeBytes);
                cmd->SetDescriptorTable(4, m_colorGradingLutUav);
                cmd->Dispatch(kColorGradingLutSize / 8, kColorGradingLutSize / 8, kColorGradingLutSize / 8);

                // Transition back to SRV
                b.stateBefore = RHIResourceState::UnorderedAccess;
                b.stateAfter = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&b, 1);
            });
        });
        m_colorGradingLutDirty = false;
    }

    // ── Motion Blur passes (tile max → neighbor max → per-pixel gather) ─
    // Motion blur: tile max → neighbor max → per-pixel gather.
    // MotionBlurAmount=0.5, MotionBlurMax=5.0 (standard defaults).
    if (ctx.motionBlurActive) {
        // ── Compute velocity scale on CPU ────────────────────────────────
        // Velocity scale = (targetDt / realDt) * motionBlurAmount * 0.5
        // This normalizes blur to a target framerate so faster/slower frames look consistent.
        const float clampedDt = std::max(ctx.dt, 0.001f);
        const float targetDt = 1.0f / static_cast<float>(std::max(m_motionBlurTargetFPS, 1));
        const float motionBlurTimeScale = targetDt / clampedDt;
        const float motionBlurScale = motionBlurTimeScale * m_motionBlurAmount * 0.5f;
        // Motion blur operates at render resolution (pre-upscale effect).
        const float maxBlurPx = m_motionBlurMax * 0.01f * static_cast<float>(static_cast<uint32_t>(ctx.renderWidth));

        // Tile size = 16x16 pixels for velocity flattening (standard tile size).
        const uint32_t tileCountX = (static_cast<uint32_t>(ctx.renderWidth)  + 15) / 16;
        const uint32_t tileCountY = (static_cast<uint32_t>(ctx.renderHeight) + 15) / 16;

        // ── Pass 1: Tile Max (16x16 tiles, find max velocity per tile) ───
        // Pass 1: Find max velocity per 16x16 tile.
        rgBuilder.AddPass("MotionBlur_TileMax", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
            pb.SetExecutionCallback([this, &ctx, tileCountX, tileCountY](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("MotionBlur::TileMax");
                auto pso = ctx.pipelines->GetMotionBlurTileMax();
                if (!pso.IsValid()) return;

                RHIBarrierDesc b{}; b.buffer = RHIBufferHandle::Invalid();
                b.texture = m_motionBlurTileMax;
                b.stateBefore = RHIResourceState::ShaderResource;
                b.stateAfter = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&b, 1);

                cmd->SetPipeline(pso);
                struct { uint32_t velSrv, outUav, tcX, tcY; } rc;
                rc.velSrv = m_gbuffer.velocitySRV;
                rc.outUav = m_motionBlurTileMaxUav;
                rc.tcX    = tileCountX;
                rc.tcY    = tileCountY;
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);
                // Dispatch one thread group per tile (each group is 16x16 threads).
                cmd->Dispatch(tileCountX, tileCountY, 1);

                b.stateBefore = RHIResourceState::UnorderedAccess;
                b.stateAfter = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&b, 1);
            });
        });

        // ── Pass 2: Neighbor Max (3x3 tile neighborhood max) ─────────────
        // Pass 2: 3x3 neighbor max of tile velocities for scatter-as-gather.
        rgBuilder.AddPass("MotionBlur_NeighborMax", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
            pb.SetExecutionCallback([this, &ctx, tileCountX, tileCountY](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("MotionBlur::NeighborMax");
                auto pso = ctx.pipelines->GetMotionBlurNeighborMax();
                if (!pso.IsValid()) return;

                RHIBarrierDesc b{}; b.buffer = RHIBufferHandle::Invalid();
                b.texture = m_motionBlurNeighborMax;
                b.stateBefore = RHIResourceState::ShaderResource;
                b.stateAfter = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&b, 1);

                cmd->SetPipeline(pso);
                struct { uint32_t tileSrv, outUav, tcX, tcY; } rc;
                rc.tileSrv = m_motionBlurTileMaxSrv;
                rc.outUav  = m_motionBlurNeighborMaxUav;
                rc.tcX     = tileCountX;
                rc.tcY     = tileCountY;
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);
                cmd->Dispatch((tileCountX + 7) / 8, (tileCountY + 7) / 8, 1);

                b.stateBefore = RHIResourceState::UnorderedAccess;
                b.stateAfter = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&b, 1);
            });
        });

        // ── Pass 3: Per-Pixel Gather Blur ────────────────────────────────
        // Pass 3: Per-pixel gather blur using neighbor max for early-out.
        rgBuilder.AddPass("MotionBlur", [&, motionBlurScale, maxBlurPx](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
            pb.SetExecutionCallback([this, &ctx, motionBlurScale, maxBlurPx](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("MotionBlur::Execute");
                auto mbPSO = ctx.pipelines->GetMotionBlur();
                if (!mbPSO.IsValid()) return;

                RHIBarrierDesc b{}; b.texture = m_motionBlurOutput; b.buffer = RHIBufferHandle::Invalid();
                b.stateBefore = RHIResourceState::ShaderResource; b.stateAfter = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&b, 1);

                cmd->SetPipeline(mbPSO);
                const uint32_t vpW = static_cast<uint32_t>(ctx.renderWidth);
                const uint32_t vpH = static_cast<uint32_t>(ctx.renderHeight);
                /// Uses neighbor max for per-tile early-out (skip tiles with zero motion).
                struct MBConstants { uint32_t scene, vel, depth, dimsPacked; float scale, maxPx; uint32_t uav, neighborMax; } c;
                c.scene = ctx.taaSrvForTonemap; c.vel = m_gbuffer.velocitySRV; c.depth = m_gbuffer.depthSRV;
                c.dimsPacked = (vpW & 0xFFFFu) | (vpH << 16u);
                c.scale = motionBlurScale; c.maxPx = maxBlurPx;
                c.uav = m_motionBlurOutputUav;
                c.neighborMax = m_motionBlurNeighborMaxSrv;
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&c), 8);
                cmd->Dispatch((static_cast<uint32_t>(ctx.renderWidth)+7)/8, (static_cast<uint32_t>(ctx.renderHeight)+7)/8, 1);

                b.stateBefore = RHIResourceState::UnorderedAccess; b.stateAfter = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&b, 1);
            });
        });
        ctx.motionBlurSrvForTonemap = m_motionBlurOutputSrv;
    }

    // ── DOF passes (disabled by default, Equivalent: DepthOfFieldFocalDistance=0) ─
    ctx.postDofSrvForTonemap = ctx.motionBlurSrvForTonemap;
    if (ctx.dofActive) {
        rgBuilder.AddPass("DOF_Setup", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("DOF::Setup");
                auto pso = ctx.pipelines->GetDofSetup(); if (!pso.IsValid()) return;
                RHIBarrierDesc b{}; b.texture = m_dofCocTexture; b.buffer = RHIBufferHandle::Invalid();
                b.stateBefore = RHIResourceState::ShaderResource; b.stateAfter = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&b, 1);
                cmd->SetPipeline(pso);
                // Physical CoC model: thin-lens approximation with APS-C sensor.
                // SensorWidth = 24.576mm (APS-C standard).
                const float sensorW = 24.576f;
                const float fovRad = m_camera->GetFovY();
                const float fMM = sensorW / (2.0f * std::tan(fovRad * 0.5f));
                const float dMM = m_dofFocusDistance * 10.0f;
                const float cocInf = (fMM*fMM) / (m_dofAperture * (dMM - fMM)) * static_cast<float>(static_cast<uint32_t>(ctx.renderWidth)) / sensorW * 0.5f;

                // Pre-compute transition reciprocals for the focal region model.
                // The focal region creates an in-focus dead zone centered on FocusDistance.
                // NearTransition/FarTransition control the ramp width.
                // Focal region model defaults: FocalRegion=0, NearTransition=300cm, FarTransition=500cm.
                //
                // When FocalRegion > 0, the focus distance for near/far transitions
                // is shifted by half the focal region. The transition reciprocal encodes
                // both the focal region offset and the transition width:
                //   effectiveNearDistance = focusDistance - focalRegion/2
                //   effectiveFarDistance  = focusDistance + focalRegion/2
                // But since the shader computes |depth - focusDistance| * transitionRcp,
                // we adjust the reciprocal to account for the focal region:
                //   nearTransitionRcp = 1.0 / max(nearTransition + focalRegion/2, 0.001)
                //   farTransitionRcp  = 1.0 / max(farTransition + focalRegion/2, 0.001)
                // This way, depths within focalRegion/2 of focusDistance produce CoC ~ 0.
                const float halfFocalRegion = m_dofFocalRegion * 0.5f;
                const float nearTransRcp = 1.0f / std::max(m_dofNearTransition + halfFocalRegion, 0.001f);
                const float farTransRcp  = 1.0f / std::max(m_dofFarTransition + halfFocalRegion, 0.001f);

                struct C { uint32_t dSrv,oUav,w,h; float fd,cinf,nearRcp,farRcp; } c;
                c.dSrv=m_gbuffer.depthSRV; c.oUav=m_dofCocUav; c.w=static_cast<uint32_t>(ctx.renderWidth); c.h=static_cast<uint32_t>(ctx.renderHeight);
                c.fd=m_dofFocusDistance; c.cinf=cocInf;
                c.nearRcp=nearTransRcp; c.farRcp=farTransRcp;
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&c), 8);
                cmd->Dispatch((static_cast<uint32_t>(ctx.renderWidth)+7)/8, (static_cast<uint32_t>(ctx.renderHeight)+7)/8, 1);
                b.stateBefore = RHIResourceState::UnorderedAccess; b.stateAfter = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&b, 1);
            });
        });
        rgBuilder.AddPass("DOF_Blur", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);
            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("DOF::Blur");
                auto pso = ctx.pipelines->GetDofBlur(); if (!pso.IsValid()) return;
                RHIBarrierDesc b{}; b.texture = m_dofOutputTexture; b.buffer = RHIBufferHandle::Invalid();
                b.stateBefore = RHIResourceState::ShaderResource; b.stateAfter = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&b, 1);
                cmd->SetPipeline(pso);
                const float maxCoc = 0.025f * static_cast<float>(static_cast<uint32_t>(ctx.renderWidth));
                struct C { uint32_t sSrv,cSrv,oUav,w,h; float bScale,maxR; uint32_t pad; } c;
                c.sSrv=ctx.motionBlurSrvForTonemap; c.cSrv=m_dofCocSrv; c.oUav=m_dofOutputUav;
                c.w=static_cast<uint32_t>(ctx.renderWidth); c.h=static_cast<uint32_t>(ctx.renderHeight); c.bScale=1.0f; c.maxR=maxCoc; c.pad=0;
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&c), 8);
                cmd->Dispatch((static_cast<uint32_t>(ctx.renderWidth)+7)/8, (static_cast<uint32_t>(ctx.renderHeight)+7)/8, 1);
                b.stateBefore = RHIResourceState::UnorderedAccess; b.stateAfter = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&b, 1);
            });
        });
        ctx.postDofSrvForTonemap = m_dofOutputSrv;
    }

    // ── LDR intermediate import ─────────────────────────────────────────
    // Always import the LDR intermediate — tonemap writes here, then either
    // CAS/FXAA or a raw CopyTexture forwards the result to the backbuffer.
    // This guarantees a stable, engine-owned texture for H.264 readback
    // (never read back from the swapchain backbuffer — it rotates after Present).
    const bool needsLdrIntermediate = false; // SHADER DEBUG: tonemap writes directly to backbuffer
    ctx.fxaaLdrRes = rgBuilder.ImportTexture("LDR_Intermediate",
                                              m_fxaaLdrIntermediate,
                                              RHIResourceState::ShaderResource);

    // ── Tonemap pass (HDR → LDR) ────────────────────────────────────────
    // Reads HDR scene color (RGBA16F) + optional bloom result, applies
    // bloom composite + auto-exposure + ACES filmic tonemap + sRGB gamma,
    // writes LDR to intermediate (if CAS/FXAA active) or backbuffer.
    // This is the ONLY place tonemapping happens in the pipeline.
    rgBuilder.AddPass("Tonemap", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

        // Tonemap writes to LDR intermediate when CAS or FXAA needs it,
        // or directly to the backbuffer when neither is active.
        const auto tonemapOutputRes = needsLdrIntermediate ? ctx.fxaaLdrRes : ctx.bbRes;
        pb.Write(tonemapOutputRes, RHIResourceState::RenderTarget);

        pb.SetExecutionCallback([this, &ctx, tonemapOutputRes](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("Tonemap::Execute");

            auto tonemapTarget = res.GetTexture(tonemapOutputRes);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = tonemapTarget;
            passDesc.colorAttachments[0].clear   = false;
            cmd->BeginRenderPass(passDesc);

            cmd->SetPipeline(ctx.pipelines->GetTonemap());

            RHIViewport vp{};
            vp.width    = static_cast<uint32_t>(ctx.vpWidth);
            vp.height   = static_cast<uint32_t>(ctx.vpHeight);
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(ctx.swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(ctx.swapChain->GetHeight());
            cmd->SetScissor(sc);

            // Root constants: HDR scene color SRV + bloom SRV + bloom intensity
            // Bloom SRV index = 0 means no bloom (shader checks for this)
            uint32_t bloomSrvIdx = 0;
            float bloomIntensity = 0.0f;
            if (ctx.bloomEnabled && m_bloomPass->GetBloomResultSrvIndex() != UINT32_MAX) {
                bloomSrvIdx = m_bloomPass->GetBloomResultSrvIndex();
                bloomIntensity = m_bloomPass->settings.intensity;
            }
            uint32_t bloomIntensityBits = 0;
            std::memcpy(&bloomIntensityBits, &bloomIntensity, sizeof(float));

            // Lens flare SRV (additive contribution, 0 = disabled)
            uint32_t lensFlareSrvIdx = 0;
            if (m_lensFlarePass && m_lensFlarePass->IsInitialized()
                && m_lensFlarePass->GetOutputSrv() != UINT32_MAX) {
                lensFlareSrvIdx = m_lensFlarePass->GetOutputSrv();
            }

            // Color grading LUT SRV (0 = no LUT, shader bypasses LUT sampling)
            const uint32_t lutSrvIdx = (m_colorGradingLut.IsValid() && m_colorGradingLutSrv != UINT32_MAX)
                                         ? m_colorGradingLutSrv : 0u;

            // Read from the final post-process output (DOF > motion blur > TAA > raw HDR).
            uint32_t indices[5] = { ctx.postDofSrvForTonemap, bloomSrvIdx, bloomIntensityBits, lensFlareSrvIdx, lutSrvIdx };
            cmd->SetRootConstants(kSlotRootConstants, indices, 5);

            // SM5/SM6.0: bind all textures via SetFragmentTexture.
            // CRITICAL: every slot the shader reads MUST be explicitly bound —
            // stale descriptors from previous passes persist in the DX12 staging
            // block. If t1 (bloom) is not bound, it contains the transmittance LUT
            // from the sky atmosphere pass, which gets ADDED to every pixel and
            // produces the orange/pink sky bug.
            if (!ctx.bindless) {
                cmd->SetFragmentTexture(0, m_hdrSceneColor);
                // t1: bloom result — MUST always be bound. Use black fallback when
                // bloom is not available to prevent stale descriptor reads.
                if (ctx.bloomEnabled && m_bloomPass
                    && m_bloomPass->GetBloomResultTexture().IsValid()) {
                    cmd->SetFragmentTexture(1, m_bloomPass->GetBloomResultTexture());
                } else {
                    cmd->SetFragmentTexture(1, m_fallbackBlack);
                }
                if (m_colorGradingLut.IsValid()) {
                    cmd->SetFragmentTexture(2, m_colorGradingLut);
                }
            }

            // PerFrameCB at b1 — contains exposure from auto-exposure
            cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                            ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                            ctx.perFrameAlloc.sizeBytes);

            cmd->Draw(3); // fullscreen triangle

            cmd->EndRenderPass();
        });
    });

    // ── CAS pass (LDR sharpening → backbuffer) ─────────────────────────
    // Contrast Adaptive Sharpening runs after tonemap on LDR content.
    // Reads from the LDR intermediate and writes to the backbuffer.
    // When CAS is active, it replaces FXAA (both achieve edge enhancement;
    // running both would over-process the image).
    //
    // @source AMD FidelityFX CAS — ffx_cas.h
    if (ctx.casActive) {
        rgBuilder.AddPass("CAS_Sharpen", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.fxaaLdrRes, RHIResourceState::ShaderResource);
            pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("CAS::Execute");

                auto bb = res.GetTexture(ctx.bbRes);

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount        = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear   = false;
                cmd->BeginRenderPass(passDesc);

                cmd->SetPipeline(ctx.pipelines->GetCasSharpen());

                // Root constants at b0: LDR intermediate SRV index
                uint32_t casIndices[4] = { m_fxaaLdrIntermediateSrv, 0, 0, 0 };
                cmd->SetRootConstants(kSlotRootConstants, casIndices, 4);

                // CASParams CB at b2 (kSlotPerObject)
                struct CASParamsCB {
                    float invScreenWidth;    ///< 1.0 / viewport width
                    float invScreenHeight;   ///< 1.0 / viewport height
                    float sharpenStrength;   ///< CAS sharpening intensity [0.0-1.0]
                    float _pad0;
                };
                CASParamsCB casParams{};
                casParams.invScreenWidth  = 1.0f / ctx.vpWidth;
                casParams.invScreenHeight = 1.0f / ctx.vpHeight;
                casParams.sharpenStrength = m_casSharpenStrength;
                casParams._pad0           = 0.0f;

                CBAllocation casAlloc = m_cbRing->Allocate(sizeof(CASParamsCB));
                std::memcpy(casAlloc.cpuPtr, &casParams, sizeof(CASParamsCB));
                cmd->SetRootCBV(kSlotPerObject, casAlloc.bufferHandle,
                                casAlloc.offsetBytes, casAlloc.cpuPtr,
                                sizeof(CASParamsCB));

                RHIViewport vp{};
                vp.width    = static_cast<uint32_t>(ctx.vpWidth);
                vp.height   = static_cast<uint32_t>(ctx.vpHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right  = static_cast<int32_t>(ctx.swapChain->GetWidth());
                sc.bottom = static_cast<int32_t>(ctx.swapChain->GetHeight());
                cmd->SetScissor(sc);

                cmd->Draw(3); // fullscreen triangle

                cmd->EndRenderPass();
            });
        });
    }

    // ── FXAA pass (LDR → backbuffer) ────────────────────────────────────
    // Reads the LDR tonemap output from the intermediate texture and
    // writes the anti-aliased result to the backbuffer. Only runs when
    // TAA is disabled (FXAA and TAA are mutually exclusive in reference implementations) AND
    // CAS is not active (CAS replaces FXAA when enabled).
    //
    // FXAA 3.11 — Lottes 2011, "Fast Approximate Anti-Aliasing" (NVIDIA).
    if (ctx.fxaaActive && !ctx.casActive) {
        rgBuilder.AddPass("FXAA", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.fxaaLdrRes, RHIResourceState::ShaderResource);
            pb.Write(ctx.bbRes, RHIResourceState::RenderTarget);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("FXAA::Execute");

                auto bb = res.GetTexture(ctx.bbRes);

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount        = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear   = false;
                cmd->BeginRenderPass(passDesc);

                cmd->SetPipeline(ctx.pipelines->GetFxaa());

                // Root constants at b0: LDR intermediate SRV index
                uint32_t fxaaIndices[4] = { m_fxaaLdrIntermediateSrv, 0, 0, 0 };
                cmd->SetRootConstants(kSlotRootConstants, fxaaIndices, 4);

                // FXAAParams CB at b2 (kSlotPerObject)
                // FXAA quality parameters (standard defaults for quality preset 29).
                struct FXAAParamsCB {
                    float invScreenWidth;   ///< 1.0 / viewport width
                    float invScreenHeight;  ///< 1.0 / viewport height
                    float edgeThreshold;    ///< Local contrast threshold (0.166 = reference default)
                    float blurStrength;     ///< Unused by FXAA 3.11, kept for cbuffer alignment
                };
                FXAAParamsCB fxaaParams{};
                fxaaParams.invScreenWidth  = 1.0f / ctx.vpWidth;
                fxaaParams.invScreenHeight = 1.0f / ctx.vpHeight;
                fxaaParams.edgeThreshold   = 0.166f;  // Standard FXAA edge detection threshold.
                fxaaParams.blurStrength    = 0.0f;

                CBAllocation fxaaAlloc = m_cbRing->Allocate(sizeof(FXAAParamsCB));
                std::memcpy(fxaaAlloc.cpuPtr, &fxaaParams, sizeof(FXAAParamsCB));
                cmd->SetRootCBV(kSlotPerObject, fxaaAlloc.bufferHandle,
                                fxaaAlloc.offsetBytes, fxaaAlloc.cpuPtr,
                                sizeof(FXAAParamsCB));

                RHIViewport vp{};
                vp.width    = static_cast<uint32_t>(ctx.vpWidth);
                vp.height   = static_cast<uint32_t>(ctx.vpHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right  = static_cast<int32_t>(ctx.swapChain->GetWidth());
                sc.bottom = static_cast<int32_t>(ctx.swapChain->GetHeight());
                cmd->SetScissor(sc);

                cmd->Draw(3); // fullscreen triangle

                cmd->EndRenderPass();
            });
        });
    }

    // ── LDR → Backbuffer copy (when neither CAS nor FXAA writes to bb) ──
    // Both CAS and FXAA read from LDR intermediate and write to the backbuffer.
    // When neither is active, the backbuffer is untouched — copy the intermediate
    // to the backbuffer via a GPU copy. Both textures are RGBA8Unorm (same format),
    // so CopyTexture is a valid, zero-overhead GPU memcpy.
    if (!ctx.casActive && !ctx.fxaaActive) {
        rgBuilder.AddPass("LDR_Blit", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.fxaaLdrRes, RHIResourceState::CopySrc);
            pb.Write(ctx.bbRes, RHIResourceState::CopyDst);

            pb.SetExecutionCallback([&ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("LDR_Blit::Execute");
                auto ldr = res.GetTexture(ctx.fxaaLdrRes);
                auto bb  = res.GetTexture(ctx.bbRes);
                cmd->CopyTexture(ldr, bb);
            });
        });
        m_ldrFinalState = RHIResourceState::CopySrc;
    } else {
        m_ldrFinalState = RHIResourceState::ShaderResource;
    }

    // ── PassInspector: register post-process targets ─────────────────────
    {
        const uint32_t rw = static_cast<uint32_t>(ctx.renderWidth);
        const uint32_t rh = static_cast<uint32_t>(ctx.renderHeight);

        // Bloom result
        if (ctx.bloomEnabled && m_bloomPass && m_bloomPass->GetBloomResultSrvIndex() != UINT32_MAX) {
            m_passInspector.RegisterTarget("bloom.result", m_bloomPass->GetBloomResultTexture(),
                                            m_bloomPass->GetBloomResultSrvIndex(), VisDisplayMode::HDR, rw, rh);
        }

        // Motion blur output
        if (ctx.motionBlurActive && m_motionBlurOutputSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("motionblur.result", m_motionBlurOutput, m_motionBlurOutputSrv,
                                            VisDisplayMode::Color, rw, rh);
        }

        // DOF output
        if (ctx.dofActive && m_dofOutputSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("dof.result", m_dofOutputTexture, m_dofOutputSrv,
                                            VisDisplayMode::Color, rw, rh);
        }
        if (ctx.dofActive && m_dofCocSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("dof.coc", m_dofCocTexture, m_dofCocSrv,
                                            VisDisplayMode::Grayscale, rw, rh);
        }

        // LDR intermediate (after tonemap, before FXAA/CAS)
        if (needsLdrIntermediate && m_fxaaLdrIntermediateSrv != UINT32_MAX) {
            const uint32_t vpW = static_cast<uint32_t>(ctx.vpWidth);
            const uint32_t vpH = static_cast<uint32_t>(ctx.vpHeight);
            m_passInspector.RegisterTarget("final.ldr", m_fxaaLdrIntermediate, m_fxaaLdrIntermediateSrv,
                                            VisDisplayMode::Color, vpW, vpH);
        }

        // Color grading 3D LUT (32x32x32 RGBA8 — first slice shown as 2D)
        if (m_colorGradingLut.IsValid() && m_colorGradingLutSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("postprocess.color_lut", m_colorGradingLut, m_colorGradingLutSrv,
                                            VisDisplayMode::Color, 32, 32);
        }
    }
}

} // namespace uz
