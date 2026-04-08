/// UzEngine Legacy TAA Upscaler — Implementation
///
/// Dispatches taa_resolve_cs.hlsl as a single compute pass. The constant
/// buffer TaaCB is uploaded via CBRingAllocator and bound at slot b2
/// (kSlotPerObject). Output UAV is bound at descriptor table slot 4 (u0).
///

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
static auto* cv_TAASpatialWeight  = UZ_CVAR_FLOAT("r.TAA.SpatialWeightMultiplier", 2.29f, "Gaussian spatial weight exp multiplier", 1.0f, 5.0f);
static auto* cv_TAAExposureScale  = UZ_CVAR_FLOAT("r.TAA.ExposureScale", 1.0f, "HDR exposure scale for TAA weighting", 0.1f, 10.0f);

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

void TaaUpscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("TAA::Resolve");

    auto taaPSO = m_pipelines->GetTaaResolve();
    if (!taaPSO.IsValid()) return;

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
    };

    TaaCB taaCb{};
    taaCb.jitterX      = input.jitterX;
    taaCb.jitterY      = input.jitterY;
    taaCb.texelSizeX   = 1.0f / input.renderWidth;
    taaCb.texelSizeY   = 1.0f / input.renderHeight;

    taaCb.blendWeight  = input.firstFrame ? 1.0f : 0.04f;

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
    taaCb.exposureScale     = cv_TAAExposureScale->value;

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
}

} // namespace uz
