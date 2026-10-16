/// UzEngine TSR Upscaler — Implementation
///
/// Three-pass compute dispatch: dilate velocity, reject shading, update history.
/// Each pass uses a different variant of `tsr_resolve_cs.hlsl` compiled with
/// a preprocessor define selecting the sub-pass. Constant buffer layouts match
/// the shader's `TsrDilateCB`, `TsrRejectCB`, and `TsrResolveCB` at slot b2.
///

#include "tsr_upscaler.h"

#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_types.h"
#include "pipeline_library.h"
#include "cb_ring_allocator.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root signature slot for per-object / per-pass constant buffer (b2).
/// Matches the binding used by TaaUpscaler and all other post-process passes.
static constexpr uint32_t kSlotPerObject = 2;

/// Root signature slot for the output UAV descriptor table (u0).
static constexpr uint32_t kSlotOutputUav = 4;

/// Thread group size matching tsr_resolve_cs.hlsl [numthreads(8, 8, 1)].
static constexpr uint32_t kThreadGroupSize = 8;

/// Controls the theoretic blend factor: lower values = more responsive but
/// noisier, higher values = smoother but more ghosting. Clamped to [8, 32].
static constexpr float kMaxHistorySampleCount = 16.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TsrUpscaler::~TsrUpscaler() {
    Shutdown();
}

void TsrUpscaler::Init(IRHIDevice* device, PipelineLibrary* pipelines, CBRingAllocator* cbRing) {
    m_device    = device;
    m_pipelines = pipelines;
    m_cbRing    = cbRing;
    UZ_LOG(Renderer, Info, "TsrUpscaler initialized (3-pass resolve: dilate + reject + history)");
}

void TsrUpscaler::Shutdown() {
    DestroyIntermediateResources();
    m_device    = nullptr;
    m_pipelines = nullptr;
    m_cbRing    = nullptr;
}

void TsrUpscaler::OnResize(uint32_t /*displayWidth*/, uint32_t /*displayHeight*/) {
    // Intermediate textures are at render resolution, not display resolution.
    // They are recreated lazily in Execute() when the render resolution changes.
    // Explicitly destroy here so the next Execute() creates fresh resources.
    DestroyIntermediateResources();
    m_renderWidth  = 0;
    m_renderHeight = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Intermediate Resource Management
// ─────────────────────────────────────────────────────────────────────────────

void TsrUpscaler::CreateIntermediateResources() {
    // Dilated velocity — RG16F at render resolution.
    // Pass 1 writes dilated velocity per pixel after a 3x3 closest-depth search.
    // Pass 2 reads it to reproject history. Pass 3 reads it for final blend.
    {
        RHITextureDesc desc{};
        desc.width     = m_renderWidth;
        desc.height    = m_renderHeight;
        desc.format    = RHIFormat::RG16Float;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        desc.debugName = "TSR.DilatedVelocity";

        m_dilatedVelocity    = m_device->CreateTexture(desc);
        m_dilatedVelocitySrv = m_device->GetTextureSRVIndex(m_dilatedVelocity);
        m_dilatedVelocityUav = m_device->GetTextureUAVIndex(m_dilatedVelocity);

        UZ_LOG(Renderer, Info, "TSR: created DilatedVelocity {}x{} RG16F (SRV={}, UAV={})",
               m_renderWidth, m_renderHeight, m_dilatedVelocitySrv, m_dilatedVelocityUav);
    }

    // Rejection mask — R16F at render resolution.
    // Pass 2 writes a per-pixel rejection weight based on luminance difference
    // between the current frame and reprojected history. Pass 3 reads it to
    // modulate the blend factor (rejected pixels use spatial AA fallback).
    {
        RHITextureDesc desc{};
        desc.width     = m_renderWidth;
        desc.height    = m_renderHeight;
        desc.format    = RHIFormat::R16Float;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        desc.debugName = "TSR.RejectionMask";

        m_rejectionMask    = m_device->CreateTexture(desc);
        m_rejectionMaskSrv = m_device->GetTextureSRVIndex(m_rejectionMask);
        m_rejectionMaskUav = m_device->GetTextureUAVIndex(m_rejectionMask);

        UZ_LOG(Renderer, Info, "TSR: created RejectionMask {}x{} R16F (SRV={}, UAV={})",
               m_renderWidth, m_renderHeight, m_rejectionMaskSrv, m_rejectionMaskUav);
    }
}

void TsrUpscaler::DestroyIntermediateResources() {
    if (!m_device) return;

    if (m_dilatedVelocity.IsValid()) {
        m_device->DestroyTexture(m_dilatedVelocity);
        m_dilatedVelocity    = RHITextureHandle::Invalid();
        m_dilatedVelocitySrv = UINT32_MAX;
        m_dilatedVelocityUav = UINT32_MAX;
    }

    if (m_rejectionMask.IsValid()) {
        m_device->DestroyTexture(m_rejectionMask);
        m_rejectionMask    = RHITextureHandle::Invalid();
        m_rejectionMaskSrv = UINT32_MAX;
        m_rejectionMaskUav = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

bool TsrUpscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("TSR::Resolve");

    // Lazily recreate intermediates if render resolution changed.
    const auto renderW = static_cast<uint32_t>(input.renderWidth);
    const auto renderH = static_cast<uint32_t>(input.renderHeight);

    if (renderW != m_renderWidth || renderH != m_renderHeight) {
        DestroyIntermediateResources();
        m_renderWidth  = renderW;
        m_renderHeight = renderH;
        CreateIntermediateResources();
        m_firstDispatch = true;
    }

    // Compute TheoricBlendFactor — controls history accumulation speed.
    // Higher screen percentages (larger rendered image) accumulate more samples
    // per output pixel, so the blend factor is adjusted by the resolution ratio.
    const float resFracSq = (input.renderWidth * input.renderHeight)
                          / (input.outputWidth * input.outputHeight);
    const float theoricBlendFactor = 1.0f / (1.0f + kMaxHistorySampleCount / resFracSq);

    const float texelSizeX  = 1.0f / input.renderWidth;
    const float texelSizeY  = 1.0f / input.renderHeight;
    const float outTexelSzX = 1.0f / input.outputWidth;
    const float outTexelSzY = 1.0f / input.outputHeight;

    const uint32_t renderGroupsX = (renderW + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t renderGroupsY = (renderH + kThreadGroupSize - 1) / kThreadGroupSize;

    // ── Pass 1: Dilate Velocity ─────────────────────────────────────────────
    //
    // For each pixel, searches a 3x3 neighborhood for the closest depth sample
    // (reversed-Z: largest value is closest). Writes the velocity from that
    // closest sample into the dilated velocity texture. This prevents thin
    // geometry (e.g., hair, fences) from losing motion information.
    //
    {
        UZ_PROFILE_SCOPE("TSR::DilateVelocity");

        auto pso = m_pipelines->GetTsrDilateVelocity();
        if (!pso.IsValid()) {
            UZ_LOG(Renderer, Error, "TSR: DilateVelocity pipeline not available, skipping TSR");
            return false;
        }

        // Transition both intermediate textures to UAV for writing.
        // dilatedVelocity: written by pass 1 as the primary output.
        // rejectionMask: reused as closestDepth scratch storage in pass 1.
        // Both start in Common state (first frame) or SRV state (subsequent frames).
        // D3D12 Common state implicitly promotes to UAV, but we transition
        // explicitly from SRV for correctness on subsequent frames.
        RHIBarrierDesc barriers[2]{};
        barriers[0].texture     = m_dilatedVelocity;
        barriers[0].buffer      = RHIBufferHandle::Invalid();
        barriers[0].stateBefore = m_firstDispatch ? RHIResourceState::Common : RHIResourceState::ShaderResource;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_rejectionMask;
        barriers[1].buffer      = RHIBufferHandle::Invalid();
        barriers[1].stateBefore = m_firstDispatch ? RHIResourceState::Common : RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(barriers, 2);

        cmd->SetPipeline(pso);

        // CB layout must match TsrDilateCB in tsr_resolve_cs.hlsl exactly.
        // 8 fields = 32 bytes, padded to 256-byte alignment by CBRingAllocator.
        struct TsrDilateCB {
            uint32_t depthSrvIndex;          // Depth buffer SRV (R32F, reversed-Z)
            uint32_t velocitySrvIndex;       // Velocity buffer SRV (RG16F, pixels)
            float    texelSizeX;             // 1.0 / renderWidth
            float    texelSizeY;             // 1.0 / renderHeight
            float    outputTexelSizeX;       // 1.0 / outputWidth
            float    outputTexelSizeY;       // 1.0 / outputHeight
            uint32_t dilatedVelocityUav;     // Output: dilated velocity (RG16F, UV-space)
            uint32_t closestDepthUav;        // Output: closest depth (repurpose rejection mask)
        };

        TsrDilateCB cb{};
        cb.depthSrvIndex      = input.depthSrv;
        cb.velocitySrvIndex   = input.velocitySrv;
        cb.texelSizeX         = texelSizeX;
        cb.texelSizeY         = texelSizeY;
        cb.outputTexelSizeX   = outTexelSzX;
        cb.outputTexelSizeY   = outTexelSzY;
        cb.dilatedVelocityUav = m_dilatedVelocityUav;
        cb.closestDepthUav    = m_rejectionMaskUav;  // Reuse rejection mask as temp storage

        auto alloc = m_cbRing->Allocate(sizeof(TsrDilateCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(TsrDilateCB));
        cmd->SetRootCBV(kSlotPerObject, alloc.bufferHandle, alloc.offsetBytes,
                        alloc.cpuPtr, sizeof(TsrDilateCB));

        // Bind dilated velocity UAV at u0 for pass 1 output.
        cmd->SetDescriptorTable(kSlotOutputUav, m_dilatedVelocityUav);

        cmd->Dispatch(renderGroupsX, renderGroupsY, 1);
    }

    // ── Pass 2: Reject Shading ──────────────────────────────────────────────
    //
    // Reprojects the previous frame's history using dilated velocity from pass 1,
    // then compares luminance between the current frame and reprojected history.
    // Pixels with significant luminance difference (disocclusion, lighting change)
    // get a high rejection weight, causing pass 3 to rely more on spatial AA
    // for those pixels instead of temporal accumulation.
    //
    {
        UZ_PROFILE_SCOPE("TSR::RejectShading");

        auto pso = m_pipelines->GetTsrRejectShading();
        if (!pso.IsValid()) {
            UZ_LOG(Renderer, Error, "TSR: RejectShading pipeline not available, skipping TSR");
            return false;
        }

        // Transition dilatedVelocity: UAV → SRV (pass 2 reads it).
        // Rejection mask stays in UAV state — pass 1 wrote closestDepth scratch data
        // which pass 2 completely overwrites with rejection weights. No read dependency
        // between pass 1 and pass 2 for the rejection mask, so no barrier needed.
        RHIBarrierDesc barriers[1]{};
        barriers[0].texture     = m_dilatedVelocity;
        barriers[0].buffer      = RHIBufferHandle::Invalid();
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(barriers, 1);

        cmd->SetPipeline(pso);

        // CB layout must match TsrRejectCB in tsr_resolve_cs.hlsl exactly.
        // 10 fields + 2 padding = 48 bytes.
        struct TsrRejectCB {
            uint32_t sceneColorSrvIndex;       // HDR scene color SRV
            uint32_t dilatedVelocitySrvIndex;  // From pass 1: dilated velocity
            uint32_t historyReadSrvIndex;      // Previous frame history SRV
            float    theoricBlendFactor;       // 1/(1 + sampleCount/resFracSq)
            float    texelSizeX;               // 1/renderWidth
            float    texelSizeY;               // 1/renderHeight
            float    outputTexelSizeX;         // 1/outputWidth
            float    outputTexelSizeY;         // 1/outputHeight
            uint32_t rejectionMaskUav;         // Output: rejection weight R16F
            uint32_t inputSceneColorUav;       // Preprocessed scene color (pass-through)
            float    pad0;
            float    pad1;
        };

        TsrRejectCB cb{};
        cb.sceneColorSrvIndex      = input.sceneColorSrv;
        cb.dilatedVelocitySrvIndex = m_dilatedVelocitySrv;
        cb.historyReadSrvIndex     = input.historyReadSrv;
        cb.theoricBlendFactor      = theoricBlendFactor;
        cb.texelSizeX              = texelSizeX;
        cb.texelSizeY              = texelSizeY;
        cb.outputTexelSizeX        = outTexelSzX;
        cb.outputTexelSizeY        = outTexelSzY;
        cb.rejectionMaskUav        = m_rejectionMaskUav;
        cb.inputSceneColorUav      = 0;  // Not used in current shader variant
        cb.pad0                    = 0.f;
        cb.pad1                    = 0.f;

        auto alloc = m_cbRing->Allocate(sizeof(TsrRejectCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(TsrRejectCB));
        cmd->SetRootCBV(kSlotPerObject, alloc.bufferHandle, alloc.offsetBytes,
                        alloc.cpuPtr, sizeof(TsrRejectCB));

        // Bind rejection mask UAV at u0 for pass 2 output.
        cmd->SetDescriptorTable(kSlotOutputUav, m_rejectionMaskUav);

        cmd->Dispatch(renderGroupsX, renderGroupsY, 1);
    }

    // ── Pass 3: Update History ──────────────────────────────────────────────
    //
    // Final temporal resolve: reprojects history via dilated velocity, clamps
    // it to the current frame's YCoCg neighborhood, and blends with an HDR-
    // weighted exponential moving average. Rejected pixels (high rejection
    // weight from pass 2) use spatial anti-aliasing instead of temporal
    // accumulation to avoid ghosting.
    //
    // On the first frame (no valid history), the blend factor is 1.0 to write
    // the current frame directly into history.
    //
    // The output is dispatched at display resolution (not render resolution)
    // since the history texture is at full display resolution.
    //
    {
        UZ_PROFILE_SCOPE("TSR::UpdateHistory");

        auto pso = m_pipelines->GetTsrUpdateHistory();
        if (!pso.IsValid()) {
            UZ_LOG(Renderer, Error, "TSR: UpdateHistory pipeline not available, skipping TSR");
            return false;
        }

        // Transition rejectionMask: UAV → SRV (pass 3 reads it).
        RHIBarrierDesc barriers[1]{};
        barriers[0].texture     = m_rejectionMask;
        barriers[0].buffer      = RHIBufferHandle::Invalid();
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(barriers, 1);

        cmd->SetPipeline(pso);

        // CB layout must match TsrResolveCB in tsr_resolve_cs.hlsl exactly.
        // 14 fields = 56 bytes (14 x 4 bytes).
        struct TsrResolveCB {
            float    jitterX;                 // Sub-pixel jitter X in pixels
            float    jitterY;                 // Sub-pixel jitter Y in pixels
            float    texelSizeX;              // 1/renderWidth
            float    texelSizeY;              // 1/renderHeight
            float    theoricBlendFactor;      // From pass 2 (same value)
            uint32_t sceneColorSrvIndex;      // HDR scene color SRV
            uint32_t depthSrvIndex;           // Depth buffer SRV
            uint32_t historyReadSrvIndex;     // Previous history SRV
            uint32_t dilatedVelocitySrvIndex; // From pass 1: dilated velocity
            uint32_t rejectionMaskSrvIndex;   // From pass 2: rejection weight
            float    outputWidth;             // Display resolution width
            float    outputHeight;            // Display resolution height
            float    tsrSharpness;            // RCAS sharpness [0, 1], 0 = disabled
            uint32_t pad;
        };

        TsrResolveCB cb{};
        cb.jitterX                 = input.jitterX;
        cb.jitterY                 = input.jitterY;
        cb.texelSizeX              = texelSizeX;
        cb.texelSizeY              = texelSizeY;
        cb.theoricBlendFactor      = input.firstFrame ? 1.0f : theoricBlendFactor;
        cb.sceneColorSrvIndex      = input.sceneColorSrv;
        cb.depthSrvIndex           = input.depthSrv;
        cb.historyReadSrvIndex     = input.historyReadSrv;
        cb.dilatedVelocitySrvIndex = m_dilatedVelocitySrv;
        cb.rejectionMaskSrvIndex   = m_rejectionMaskSrv;
        cb.outputWidth             = input.outputWidth;
        cb.outputHeight            = input.outputHeight;
        cb.tsrSharpness            = m_sharpness;
        cb.pad                     = 0;

        auto alloc = m_cbRing->Allocate(sizeof(TsrResolveCB));
        std::memcpy(alloc.cpuPtr, &cb, sizeof(TsrResolveCB));
        cmd->SetRootCBV(kSlotPerObject, alloc.bufferHandle, alloc.offsetBytes,
                        alloc.cpuPtr, sizeof(TsrResolveCB));

        // Bind history write UAV at u0 for final output.
        cmd->SetDescriptorTable(kSlotOutputUav, input.historyWriteUav);

        // Dispatch at display resolution — the history texture is at full output size.
        const uint32_t outputW = static_cast<uint32_t>(input.outputWidth);
        const uint32_t outputH = static_cast<uint32_t>(input.outputHeight);
        const uint32_t outputGroupsX = (outputW + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t outputGroupsY = (outputH + kThreadGroupSize - 1) / kThreadGroupSize;

        cmd->Dispatch(outputGroupsX, outputGroupsY, 1);
    }

    // Transition intermediates back to SRV for next frame's pass 1.
    // dilatedVelocity is already in SRV (transitioned for pass 2/3).
    // rejectionMask needs UAV → SRV (pass 2 left it in UAV, pass 3 read it as SRV after transition).
    // Both are now in SRV state after pass 3 barriers, so no extra transition needed here.

    m_firstDispatch = false;
    return true;
}

} // namespace uz
