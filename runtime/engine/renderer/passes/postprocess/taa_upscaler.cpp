/// UzEngine Legacy TAA Upscaler — Implementation
///
/// Dispatches taa_resolve_cs.hlsl as a single compute pass. The constant
/// buffer TaaCB is uploaded via CBRingAllocator and bound at slot b2
/// (kSlotPerObject). Output UAV is bound at descriptor table slot 4 (u0).
///

#include <algorithm>
#include "taa_upscaler.h"

#include "engine/rhi/rhi.h"
#include "pipeline_library.h"
#include "cb_ring_allocator.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

static auto* cv_TAAVarianceClip   = UZ_CVAR_FLOAT("r.TAA.VarianceClipGamma", 1.25f, "Variance clip gamma (wider=ghosting, tighter=flickering)", 0.5f, 3.0f);
static auto* cv_TAAVelocityThresh = UZ_CVAR_FLOAT("r.TAA.VelocityThreshold", 40.0f, "Velocity lerp threshold (pixels)", 5.0f, 100.0f);
static auto* cv_TAAAntiFlicker    = UZ_CVAR_FLOAT("r.TAA.AntiFlickerMax", 0.3f, "Max anti-flicker contribution", 0.0f, 1.0f);
// Was declared in frame_setup.cpp and read by nothing, while this CB field took a hardcoded
// 0.04 — so the documented, range-limited knob on the TAA's blend weight silently did
// nothing. The default is unchanged (0.04), so this restores the control without moving
// shipping behaviour.
static auto* cv_TAACurrentWeight   = UZ_CVAR_FLOAT("r.TAA.CurrentFrameWeight", 0.04f, "TAA current frame blend weight", 0.01f, 0.2f);
static auto* cv_TAASpatialWeight  = UZ_CVAR_FLOAT("r.TAA.SpatialWeightMultiplier", 2.29f, "Gaussian spatial weight exp multiplier", 1.0f, 5.0f);
static auto* cv_TAAExposureScale  = UZ_CVAR_FLOAT("r.TAA.ExposureScale", 1.0f, "HDR exposure scale for TAA weighting", 0.1f, 10.0f);
static auto* cv_TAAHdrWeightExp    = UZ_CVAR_FLOAT("r.TAA.ReconstructionHdrWeightExponent", 1.0f,
    "Candidate correction (followup-14, opt-in): exponent applied to the HDR weight inside the TAA "
    "reconstruction filter. 1 = production (Karis 2014 weighting), 0 = spatial weights only. The "
    "weighting suppresses bright taps, which costs 48 % of a thin bright bar's energy and creates a "
    "jitter-dependent residual on pixels whose own sample never changes (followup-10 H-11/H-12). "
    "Values between 0 and 1 trade that energy back against light bleeding into dark neighbours.",
    0.f, 1.f);
static auto* cv_TAAReconstruction = UZ_CVAR_INT("r.TAA.ReconstructionMode", 0,
    "Diagnostic (followup-10), 0 = production: 0 plus filter with spatial x HDR weights, "
    "1 spatial weights only (no HDR weighting), 2 centre tap only (no reconstruction), "
    "3 full 3x3 sample-position-aware weights.", 0, 3);
static auto* cv_TAAClampFreeze    = UZ_CVAR_INT("r.TAA.ClampFreezeAfterFrame", -1,
    "Diagnostic (ledger H-25): from this absolute frame on, reuse the clamp box captured up "
    "to it instead of recomputing it, so the bounds stop moving while the reconstruction, "
    "exposure and lighting stay unchanged. -1 = off. Screen-space frozen bounds are only "
    "meaningful with a static camera.", -1, 100000);
static auto* cv_TAAClampAverage = UZ_CVAR_INT("r.TAA.ClampFreezeAverageFrames", 1,
    "Diagnostic: average this many final clamp boxes before the freeze frame.", 1, 16);
static auto* cv_TAAHistoryRead = UZ_CVAR_INT("r.TAA.HistoryReadMode", 0,
    "Diagnostic: 0 bicubic history, 1 exact nearest history texel.", 0, 1);
static auto* cv_TAAClampMode      = UZ_CVAR_INT("r.TAA.ClampMode", 0,
    "Diagnostic (followup-10), 0 = production: 0 AABB intersected with the variance box, "
    "1 no clamp (history passes through), 2 variance box only, 3 AABB only.", 0, 3);
static auto* cv_TAABlendMode      = UZ_CVAR_INT("r.TAA.BlendMode", 0,
    "Diagnostic (followup-10), 0 = production: 0 velocity lerp + anti-flicker guard, "
    "1 fixed weight r.TAA.CurrentFrameWeight only.", 0, 1);
static auto* cv_TAAPreExpose      = UZ_CVAR_BOOL("r.TAA.PreExposeWeights", false,
    "Multiply the HDR-weighting exposure (Karis 2014 HdrWeight) by the frame's auto-exposure value instead of 1.0. Default off = the shipped look; on = the filter/blend weights see display-referred luminance, so a clipped highlight (Sponza window panes, 8 Y at exposure 67) stops bleeding into neighbouring dark pixels through the jitter-weighted plus filter (2026-09-09 region-dump finding)");
static auto* cv_TAAStableRecon = UZ_CVAR_INT("r.TAA.StableRecon", 1,
    "phase0_sponza-flicker-correction (ON by default, gates passed 2026-10-13): 1 = hand "
    "coverage-stable pixels their own centre tap, removing the jitter-weighted plus filter's "
    "8-periodic leak on static bright regions (pose-W window: bright_std 0.0205->0.0007, energy "
    "0.973->1.000; boundary bit-identical; neutral under motion). 0 = legacy (pre-fix).", 0, 1);
static auto* cv_TAAStableReconTol = UZ_CVAR_FLOAT("r.TAA.StableReconTolerance", 0.02f,
    "Relative-luma tolerance for the stable-reconstruction gate: a pixel is stable when its raw "
    "centre luma is within this fraction of its running mean.", 0.001f, 0.5f);
static auto* cv_TAAStableReconVel = UZ_CVAR_FLOAT("r.TAA.StableReconVelocityPx", 4.0f,
    "The stable-reconstruction gate fades to zero by this screen velocity (pixels/frame).", 0.1f, 40.0f);
static auto* cv_TAAFlipBlend = UZ_CVAR_INT("r.TAA.FlipBlend", 0,
    "phase0_sponza-flicker-correction (opt-in until gates pass; requires r.TAA.StableRecon=1): 1 = "
    "at static neighbour-corroborated coverage-flip pixels (the complement of StableRecon), use the "
    "spatial-only reconstruction + equal blend weights, recovering the rear-door glass energy the "
    "HDR weighting drains (energy ~0.52->target >=0.85). 0 = production.", 0, 1);
static auto* cv_TAAFlipIsolation = UZ_CVAR_FLOAT("r.TAA.FlipBlendIsolationRatio", 2.0f,
    "FlipBlend firefly guard: a centre brighter than this multiple of its brightest neighbour is an "
    "isolated sample (firefly/sub-pixel sparkle) and keeps the Karis path.", 1.0f, 10.0f);
static auto* cv_TAAFlipVelocity = UZ_CVAR_FLOAT("r.TAA.FlipBlendVelocityPx", 4.0f,
    "The FlipBlend gate fades to zero by this screen velocity (pixels/frame).", 0.1f, 40.0f);
static auto* cv_TAAFlipWeight = UZ_CVAR_FLOAT("r.TAA.FlipBlendWeight", -1.0f,
    "FlipBlend optional fixed blend weight at flip pixels (-1 = inherit r.TAA.CurrentFrameWeight); "
    "lower it to slow the settling if boundary flicker rises too much.", -1.0f, 0.2f);

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TaaUpscaler::~TaaUpscaler() {
    Shutdown();
}

void TaaUpscaler::Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) {
    m_device    = device;
    m_pipelines = pipelines;
    m_cbRing    = cbRing;
    UZ_LOG(Renderer, Info, "TaaUpscaler initialized (single-pass resolve)");
}

void TaaUpscaler::Shutdown() {
    // TAA owns no GPU resources — pipeline and CB ring are external.
    m_device    = nullptr;
    m_pipelines = nullptr;
    m_cbRing    = nullptr;
}

void TaaUpscaler::OnResize(uint32_t /*displayWidth*/, uint32_t /*displayHeight*/) {
    // TAA has no resolution-dependent intermediate buffers.
    // History textures are managed externally by SceneRenderer.
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

bool TaaUpscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("TAA::Resolve");

    auto taaPSO = m_pipelines->GetTaaResolve();
    if (!taaPSO.IsValid()) return false;

    cmd->SetPipeline(taaPSO);

    // TaaCB layout matches taa_resolve_cs.hlsl cbuffer TaaCB at b2.
    // 12 floats/uints = 48 bytes, padded to 16-byte alignment.
    struct TaaCB {
        float    jitterX, jitterY;
        float    texelSizeX, texelSizeY;
        float    blendWeight;
        uint32_t sceneSrv;
        uint32_t depthSrv;
        uint32_t historySrv;
        uint32_t velocitySrv;
        float    outputWidth;
        float    outputHeight;
        float    varianceClipGamma;
        float    velocityThreshold;
        float    antiFlickerMax;
        float    spatialWeightMul;
        float    exposureScale;
        uint32_t debugRegionUav;    // region dump (diagnostics), UINT32_MAX = off
        uint32_t debugRegionSrv;
        uint32_t debugRegionCount;
        uint32_t debugRegionStamp;
        uint32_t debugRegionFields;
        uint32_t clampBoxSrvMin;      // r.TAA.ClampFreezeAfterFrame storage (0xffffffff = unavailable)
        uint32_t clampBoxSrvMax;
        uint32_t clampBoxUavMin;
        uint32_t clampBoxUavMax;
        uint32_t clampFreezeActive;   // 1 = read the stored box, 0 = compute it and store it
        uint32_t clampFreezeEnabled;  // 0 = diagnostic off; the shader must not touch the storage
        float    hdrWeightExponent;   // r.TAA.ReconstructionHdrWeightExponent (candidate correction)
        uint32_t reconstructionMode;  // r.TAA.ReconstructionMode (diagnostic)
        uint32_t clampMode;           // r.TAA.ClampMode (diagnostic)
        uint32_t blendMode;           // r.TAA.BlendMode (diagnostic)
        uint32_t historyReadMode;
        uint32_t clampAverageFrames;
        uint32_t frameIndex;
        uint32_t clampFreezeFrame;
        uint32_t stableReconEnabled;
        uint32_t stableReconReset;
        float    stableReconTolerance;
        float    stableReconVelocityPx;
        uint32_t flipBlendEnabled;
        float    flipIsolationRatio;
        float    flipVelocityPx;
        float    flipBlendWeight;
    };

    TaaCB taaCb{};
    taaCb.jitterX      = input.jitterX;
    taaCb.jitterY      = input.jitterY;
    taaCb.texelSizeX   = 1.0f / input.renderWidth;
    taaCb.texelSizeY   = 1.0f / input.renderHeight;

    taaCb.blendWeight  = input.firstFrame ? 1.0f : cv_TAACurrentWeight->value;

    taaCb.sceneSrv     = input.sceneColorSrv;
    taaCb.depthSrv     = input.depthSrv;
    taaCb.historySrv   = input.historyReadSrv;
    taaCb.velocitySrv  = input.velocitySrv;
    taaCb.outputWidth  = input.outputWidth;
    taaCb.outputHeight = input.outputHeight;
    taaCb.varianceClipGamma = cv_TAAVarianceClip->value;
    taaCb.velocityThreshold = cv_TAAVelocityThresh->value;
    taaCb.antiFlickerMax    = cv_TAAAntiFlicker->value;
    taaCb.spatialWeightMul  = cv_TAASpatialWeight->value;
    // Karis 2014 pre-exposes the weights; UpscalerInput::exposure carries the frame exposure but was never
    // consumed (weights ran on raw HDR, so an 8 Y highlight had only 9x less weight than a 0.01 Y interior pixel).
    taaCb.exposureScale     = cv_TAAExposureScale->value * (cv_TAAPreExpose->value > 0.5f ? std::max(input.exposure, 1e-4f) : 1.0f);
    taaCb.debugRegionUav    = input.debugRegionUav;
    taaCb.debugRegionSrv    = input.debugRegionSrv;
    taaCb.debugRegionCount  = input.debugRegionCount;
    taaCb.debugRegionStamp  = input.debugRegionStamp;
    taaCb.debugRegionFields = input.debugRegionFields;
    taaCb.clampBoxSrvMin    = input.clampBoxSrv[0];
    taaCb.clampBoxSrvMax    = input.clampBoxSrv[1];
    taaCb.clampBoxUavMin    = input.clampBoxUav[0];
    taaCb.clampBoxUavMax    = input.clampBoxUav[1];
    taaCb.clampFreezeActive =
        (cv_TAAClampFreeze->value >= 0
         && static_cast<int>(input.frameIndex) > cv_TAAClampFreeze->value) ? 1u : 0u;
    taaCb.clampFreezeEnabled = input.clampFreezeEnabled;
    taaCb.hdrWeightExponent  = cv_TAAHdrWeightExp->value;
    taaCb.reconstructionMode = static_cast<uint32_t>(cv_TAAReconstruction->value);
    taaCb.clampMode          = static_cast<uint32_t>(cv_TAAClampMode->value);
    taaCb.blendMode          = static_cast<uint32_t>(cv_TAABlendMode->value);
    taaCb.historyReadMode    = static_cast<uint32_t>(cv_TAAHistoryRead->value);
    taaCb.clampAverageFrames = static_cast<uint32_t>(cv_TAAClampAverage->value);
    taaCb.frameIndex         = input.frameIndex;
    taaCb.clampFreezeFrame   = static_cast<uint32_t>(static_cast<int>(cv_TAAClampFreeze->value));
    taaCb.stableReconEnabled    = static_cast<uint32_t>(cv_TAAStableRecon->value);
    taaCb.stableReconReset      = input.firstFrame ? 1u : 0u;
    taaCb.stableReconTolerance  = cv_TAAStableReconTol->value;
    taaCb.stableReconVelocityPx = cv_TAAStableReconVel->value;
    taaCb.flipBlendEnabled      = static_cast<uint32_t>(cv_TAAFlipBlend->value);
    taaCb.flipIsolationRatio    = cv_TAAFlipIsolation->value;
    taaCb.flipVelocityPx        = cv_TAAFlipVelocity->value;
    taaCb.flipBlendWeight       = cv_TAAFlipWeight->value;

    // Root signature slot 2 = b2 (per-object constant buffer).
    constexpr uint32_t kSlotPerObject = 2;

    auto taaAlloc = m_cbRing->Allocate(sizeof(TaaCB));
    std::memcpy(taaAlloc.cpuPtr, &taaCb, sizeof(TaaCB));
    cmd->SetRootCBV(kSlotPerObject, taaAlloc.bufferHandle, taaAlloc.offsetBytes,
                    taaAlloc.cpuPtr, sizeof(TaaCB));

    // Bind output UAV at descriptor table slot 4 (u0).
    cmd->SetDescriptorTable(4, input.historyWriteUav);

    // On non-bindless backends (DX11/SM5) the shader reads textures via explicit
    // register(tN) declarations rather than ResourceDescriptorHeap[]. Bind them
    // individually: slot 5 covers t0 via the root layout's SRV_Table entry, and
    // SetFragmentTexture binds the remaining slots t1-t3 directly.
    if (m_device && !m_device->IsBindless()) {
        cmd->SetDescriptorTable(5, input.sceneColorSrv);           // t0 — scene color
        cmd->SetFragmentTexture(1, input.depthTexture);            // t1 — depth
        cmd->SetFragmentTexture(2, input.historyReadTexture);      // t2 — history
        cmd->SetFragmentTexture(3, input.velocityTexture);         // t3 — velocity
    }

    // Dispatch 8x8 thread groups covering the full display resolution.
    const uint32_t groupsX = (static_cast<uint32_t>(input.outputWidth)  + 7) / 8;
    const uint32_t groupsY = (static_cast<uint32_t>(input.outputHeight) + 7) / 8;
    cmd->Dispatch(groupsX, groupsY, 1);
    return true;
}

} // namespace uz

// build-cache: TAA blend weight now reads its CVar
