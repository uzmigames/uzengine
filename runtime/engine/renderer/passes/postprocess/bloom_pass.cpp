/// UzEngine Bloom Pass — Implementation.
///
/// Compiles 3 compute shaders (threshold, downsample, upsample), creates a
/// 6-level half-res mip chain, and dispatches the full bloom pipeline each frame.
/// The bloom result (mip[0]) is composited by the tonemap shader.
///
/// # Pipeline
/// 1. Threshold: HDR (full-res) → Mip[0] (half-res bright pixels, Karis avg)
/// 2. Downsample x5: Mip[0]→Mip[1]→...→Mip[5] (13-tap Jimenez 2014)
/// 3. Upsample x5: Mip[5]→Mip[4]→...→Mip[0] (9-tap tent + additive blend)
/// 4. Tonemap reads mip[0] and composites bloom before exposure + tonemapping.
///

#include "bloom_pass.h"

#include "engine/rhi/rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_BloomEnabled   = UZ_CVAR_BOOL("r.Bloom", true, "Bloom post-process");
// @ref UE5 Scene.cpp — BloomThreshold=-1 (uniform bloom, no bright extraction)
// @ref UE5 Scene.cpp — BloomIntensity=0.675 (subtle glow, not blowout)
static auto* cv_BloomThreshold = UZ_CVAR_FLOAT("r.Bloom.Threshold", -1.f, "Bloom luminance threshold (-1=uniform/UE5)", -1.f, 100.f);
static auto* cv_BloomIntensity = UZ_CVAR_FLOAT("r.Bloom.Intensity", 0.015f, "Bloom contribution multiplier", 0.f, 10.f);
static auto* cv_BloomRadius    = UZ_CVAR_FLOAT("r.Bloom.Radius", 1.f, "Bloom upsample filter radius", 0.1f, 4.f);
static auto* cv_BloomSizeScale = UZ_CVAR_FLOAT("r.Bloom.SizeScale", 4.f, "Global bloom size scale", 0.1f, 64.f);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Root constant structures — must match HLSL cbuffer layouts at register(b0)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for bloom_threshold_cs.hlsl (8 DWORDs).
struct BloomThresholdConstants {
    uint32_t hdrSrvIndex;
    uint32_t outputUavIndex;
    uint32_t inputWidth;
    uint32_t inputHeight;
    float    bloomThreshold;
    float    exposure;
    float    _pad0;
    float    _pad1;
};
static_assert(sizeof(BloomThresholdConstants) == 32,
    "BloomThresholdConstants must be 8 DWORDs");

/// Root constants for bloom_downsample_cs.hlsl (4 DWORDs).
struct BloomDownsampleConstants {
    uint32_t inputSrvIndex;
    uint32_t outputUavIndex;
    uint32_t outputWidth;
    uint32_t outputHeight;
};
static_assert(sizeof(BloomDownsampleConstants) == 16,
    "BloomDownsampleConstants must be 4 DWORDs");

/// Root constants for bloom_upsample_cs.hlsl (8 DWORDs).
///
struct BloomUpsampleConstants {
    uint32_t lowerMipSrvIndex;
    uint32_t outputUavIndex;
    uint32_t outputWidth;
    uint32_t outputHeight;
    float    bloomRadius;
    float    stageTintR;
    float    stageTintG;
    float    stageTintB;
};
static_assert(sizeof(BloomUpsampleConstants) == 32,
    "BloomUpsampleConstants must be 8 DWORDs");

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

BloomPass::~BloomPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool BloomPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                     uint32_t width, uint32_t height)
{
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "BloomPass::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "BloomPass::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "BloomPass::Init failed: shaderMgr is null");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;
    m_fullWidth  = width;
    m_fullHeight = height;

    CreateMipChain(width, height);

    m_initialized = true;
    UZ_LOG(Renderer, Info, "BloomPass initialized ({}x{}, {} mip levels)",
           width, height, kMipCount);
    return true;
}

void BloomPass::Shutdown() {
    DestroyMipChain();

    if (m_device) {
        if (m_thresholdPipeline.IsValid())  m_device->DestroyPipeline(m_thresholdPipeline);
        if (m_downsamplePipeline.IsValid()) m_device->DestroyPipeline(m_downsamplePipeline);
        if (m_upsamplePipeline.IsValid())   m_device->DestroyPipeline(m_upsamplePipeline);

        if (m_thresholdCS.IsValid())  m_device->DestroyShader(m_thresholdCS);
        if (m_downsampleCS.IsValid()) m_device->DestroyShader(m_downsampleCS);
        if (m_upsampleCS.IsValid())   m_device->DestroyShader(m_upsampleCS);
    }

    m_thresholdPipeline  = RHIPipelineHandle::Invalid();
    m_downsamplePipeline = RHIPipelineHandle::Invalid();
    m_upsamplePipeline   = RHIPipelineHandle::Invalid();
    m_thresholdCS  = RHIShaderHandle::Invalid();
    m_downsampleCS = RHIShaderHandle::Invalid();
    m_upsampleCS   = RHIShaderHandle::Invalid();

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "BloomPass shut down");
}

void BloomPass::Resize(uint32_t width, uint32_t height) {
    if (width == m_fullWidth && height == m_fullHeight) {
        return;
    }

    m_fullWidth  = width;
    m_fullHeight = height;

    DestroyMipChain();
    CreateMipChain(width, height);

    UZ_LOG(Renderer, Info, "BloomPass resized to {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mip Chain Management
// ─────────────────────────────────────────────────────────────────────────────

void BloomPass::CreateMipChain(uint32_t width, uint32_t height) {
    // Mip 0 = half-res, Mip 1 = quarter-res, ..., Mip 5 = 1/64 res
    uint32_t w = width  / 2;
    uint32_t h = height / 2;

    for (uint32_t i = 0; i < kMipCount; ++i) {
        w = std::max(w, 1u);
        h = std::max(h, 1u);

        m_mipWidths[i]  = w;
        m_mipHeights[i] = h;

        static const char* kMipNames[kMipCount] = {
            "Bloom_Mip0", "Bloom_Mip1", "Bloom_Mip2",
            "Bloom_Mip3", "Bloom_Mip4", "Bloom_Mip5"
        };

        RHITextureDesc desc{};
        desc.width     = w;
        desc.height    = h;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        desc.debugName = kMipNames[i];

        m_mipTextures[i] = m_device->CreateTexture(desc);
        if (!m_mipTextures[i].IsValid()) {
            UZ_LOG(Renderer, Error, "BloomPass: failed to create mip {} texture ({}x{})",
                   i, w, h);
            continue;
        }

        m_mipSrvIndices[i] = m_device->GetTextureSRVIndex(m_mipTextures[i]);
        m_mipUavIndices[i] = m_device->GetTextureUAVIndex(m_mipTextures[i]);

        UZ_LOG(Renderer, Info, "BloomPass: mip {} = {}x{} (SRV={}, UAV={})",
               i, w, h, m_mipSrvIndices[i], m_mipUavIndices[i]);

        w /= 2;
        h /= 2;
    }
}

void BloomPass::DestroyMipChain() {
    if (!m_device) return;

    for (uint32_t i = 0; i < kMipCount; ++i) {
        if (m_mipTextures[i].IsValid()) {
            m_device->DestroyTexture(m_mipTextures[i]);
            m_mipTextures[i]  = RHITextureHandle{};
            m_mipSrvIndices[i] = UINT32_MAX;
            m_mipUavIndices[i] = UINT32_MAX;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool BloomPass::EnsureThresholdPipeline() {
    if (m_thresholdPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::BloomThresholdCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to compile bloom_threshold_cs.hlsl");
        return false;
    }

    UZ_LOG(Renderer, Info, "BloomPass: threshold shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "bloom_threshold_cs";

    m_thresholdCS = m_device->CreateShader(shaderDesc);
    if (!m_thresholdCS.IsValid()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to create threshold shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_thresholdCS;
    pipeDesc.debugName     = "BloomPass_Threshold";

    m_thresholdPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_thresholdPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to create threshold pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "BloomPass: created threshold pipeline (shader={}, pipeline={})",
           m_thresholdCS.index, m_thresholdPipeline.index);
    return true;
}

bool BloomPass::EnsureDownsamplePipeline() {
    if (m_downsamplePipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::BloomDownsampleCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to compile bloom_downsample_cs.hlsl");
        return false;
    }

    UZ_LOG(Renderer, Info, "BloomPass: downsample shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "bloom_downsample_cs";

    m_downsampleCS = m_device->CreateShader(shaderDesc);
    if (!m_downsampleCS.IsValid()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to create downsample shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_downsampleCS;
    pipeDesc.debugName     = "BloomPass_Downsample";

    m_downsamplePipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_downsamplePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to create downsample pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "BloomPass: created downsample pipeline (shader={}, pipeline={})",
           m_downsampleCS.index, m_downsamplePipeline.index);
    return true;
}

bool BloomPass::EnsureUpsamplePipeline() {
    if (m_upsamplePipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::BloomUpsampleCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to compile bloom_upsample_cs.hlsl");
        return false;
    }

    UZ_LOG(Renderer, Info, "BloomPass: upsample shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "bloom_upsample_cs";

    m_upsampleCS = m_device->CreateShader(shaderDesc);
    if (!m_upsampleCS.IsValid()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to create upsample shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_upsampleCS;
    pipeDesc.debugName     = "BloomPass_Upsample";

    m_upsamplePipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_upsamplePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "BloomPass: failed to create upsample pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "BloomPass: created upsample pipeline (shader={}, pipeline={})",
           m_upsampleCS.index, m_upsamplePipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

void BloomPass::Execute(IRHICommandList* cmdList,
                        uint32_t hdrSrvIndex,
                        uint32_t width, uint32_t height,
                        float exposure)
{
    UZ_PROFILE_SCOPE("BloomPass::Execute");

    if (!cmdList) {
        UZ_LOG(Renderer, Error, "BloomPass::Execute: null command list");
        return;
    }

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "BloomPass::Execute called before Init()");
        return;
    }

    // Apply CVar overrides to settings
    settings.enabled   = cv_BloomEnabled->value >= 0.5f;
    settings.threshold = cv_BloomThreshold->value;
    settings.intensity = cv_BloomIntensity->value;
    settings.radius    = cv_BloomRadius->value;
    settings.sizeScale = cv_BloomSizeScale->value;

    if (!settings.enabled) {
        return;
    }

    if (hdrSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "BloomPass::Execute: hdrSrvIndex is UINT32_MAX — HDR SRV not allocated");
        return;
    }

    if (width == 0 || height == 0) {
        UZ_LOG(Renderer, Error, "BloomPass::Execute: zero-size viewport ({}x{}) — aborting", width, height);
        return;
    }

    // Resize mip chain if resolution changed
    if (width != m_fullWidth || height != m_fullHeight) {
        Resize(width, height);
    }

    // Ensure all pipelines are compiled
    if (!EnsureThresholdPipeline() || !EnsureDownsamplePipeline() || !EnsureUpsamplePipeline()) {
        UZ_LOG(Renderer, Error, "BloomPass: pipeline compilation failed, skipping bloom");
        return;
    }

    // First-frame diagnostic dump
    static bool loggedFirstFrame = false;
    if (!loggedFirstFrame) {
        UZ_LOG(Renderer, Info,
               "BloomPass: first execution — {}x{}, hdrSRV={}, exposure={:.4f}, "
               "threshold={:.3f}, radius={:.3f}, mip0={}x{} (SRV={}, UAV={}), "
               "thresholdPipeline={}, downsamplePipeline={}, upsamplePipeline={}",
               width, height, hdrSrvIndex, exposure,
               settings.threshold, settings.radius,
               m_mipWidths[0], m_mipHeights[0], m_mipSrvIndices[0], m_mipUavIndices[0],
               m_thresholdPipeline.index, m_downsamplePipeline.index,
               m_upsamplePipeline.index);
        for (uint32_t i = 0; i < kMipCount; ++i) {
            UZ_LOG(Renderer, Info,
                   "BloomPass:   mip[{}] = {}x{}, SRV={}, UAV={}",
                   i, m_mipWidths[i], m_mipHeights[i], m_mipSrvIndices[i], m_mipUavIndices[i]);
        }
        loggedFirstFrame = true;
    }

    // ── Step 1: Threshold — HDR (full-res) → Mip[0] (half-res) ─────────
    // Transition mip[0] to UAV for compute write
    {
        UZ_PROFILE_SCOPE("BloomPass::Threshold");

        RHIBarrierDesc barrier{};
        barrier.texture     = m_mipTextures[0];
        barrier.buffer      = RHIBufferHandle::Invalid();
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);

        cmdList->SetPipeline(m_thresholdPipeline);

        BloomThresholdConstants rc{};
        rc.hdrSrvIndex    = hdrSrvIndex;
        rc.outputUavIndex = m_mipUavIndices[0];
        rc.inputWidth     = width;
        rc.inputHeight    = height;
        rc.bloomThreshold = settings.threshold;
        rc.exposure       = exposure;


        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

        // On non-bindless backends (DX11/SM5) bind SRV (t0) and UAV (u0) explicitly.
        // Bindless reads via ResourceDescriptorHeap[] using the root constant indices above.
        if (!m_device->IsBindless()) {
            cmdList->SetDescriptorTable(5, hdrSrvIndex);          // t0 — HDR scene color
            cmdList->SetDescriptorTable(4, m_mipUavIndices[0]);   // u0 — mip[0] output
        }

        const uint32_t groupsX = (m_mipWidths[0]  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_mipHeights[0] + kGroupSize - 1) / kGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Step 2: Downsample chain — Mip[0] → Mip[1] → ... → Mip[5] ─────
    for (uint32_t i = 0; i < kMipCount - 1; ++i) {
        UZ_PROFILE_SCOPE("BloomPass::Downsample");

        // Transition source (mip[i]) from UAV to SRV
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_mipTextures[i];
        barriers[0].buffer      = RHIBufferHandle::Invalid();
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;

        // Transition destination (mip[i+1]) to UAV
        barriers[1].texture     = m_mipTextures[i + 1];
        barriers[1].buffer      = RHIBufferHandle::Invalid();
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;

        cmdList->ResourceBarrier(barriers, 2);

        cmdList->SetPipeline(m_downsamplePipeline);

        BloomDownsampleConstants rc{};
        rc.inputSrvIndex  = m_mipSrvIndices[i];
        rc.outputUavIndex = m_mipUavIndices[i + 1];
        rc.outputWidth    = m_mipWidths[i + 1];
        rc.outputHeight   = m_mipHeights[i + 1];

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 4);

        // On non-bindless backends (DX11/SM5) bind SRV (t0) and UAV (u0) explicitly.
        if (!m_device->IsBindless()) {
            cmdList->SetDescriptorTable(5, m_mipSrvIndices[i]);          // t0 — input mip
            cmdList->SetDescriptorTable(4, m_mipUavIndices[i + 1]);      // u0 — output mip
        }

        const uint32_t groupsX = (m_mipWidths[i + 1]  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_mipHeights[i + 1] + kGroupSize - 1) / kGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Step 3: Upsample chain — Mip[5] → Mip[4] → ... → Mip[0] ───────
    // Each level reads: lower-res result (just upsampled) + current downsample level
    // and writes the blended result.
    //
    // First upsample: reads mip[5] (lower) + mip[4] (current), writes mip[4]
    // Second: reads mip[4] (lower) + mip[3] (current), writes mip[3]
    // ...
    // Last: reads mip[1] (lower) + mip[0] (current), writes mip[0]
    for (uint32_t step = 0; step < kMipCount - 1; ++step) {
        UZ_PROFILE_SCOPE("BloomPass::Upsample");

        // lowerIdx is the smaller (deeper) mip that was just upsampled into
        // currentIdx is the downsample result at this resolution
        // outputIdx is where we write = currentIdx (overwrite the downsample data)
        const uint32_t lowerIdx   = kMipCount - 1 - step;  // 5, 4, 3, 2, 1
        const uint32_t currentIdx = lowerIdx - 1;           // 4, 3, 2, 1, 0

        // Transition lower mip to SRV (it was either UAV from downsample or from previous upsample)
        // Transition current mip to UAV (will be overwritten with upsample result)
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_mipTextures[lowerIdx];
        barriers[0].buffer      = RHIBufferHandle::Invalid();
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;

        // For step 0, currentIdx (4) was in UAV from downsample. For subsequent
        // steps, it's been untouched since the downsample wrote it (UAV state).
        // But mip[0] was transitioned to SRV after threshold → downsample read.
        // We transition it from SRV → UAV for the final upsample write.
        //
        // For steps 1+, currentIdx was written by downsample (UAV state),
        // then transitioned to SRV for the next downsample read. So it's in SRV.
        barriers[1].texture     = m_mipTextures[currentIdx];
        barriers[1].buffer      = RHIBufferHandle::Invalid();
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;

        cmdList->ResourceBarrier(barriers, 2);

        cmdList->SetPipeline(m_upsamplePipeline);

        BloomUpsampleConstants rc{};
        rc.lowerMipSrvIndex = m_mipSrvIndices[lowerIdx];
        rc.outputUavIndex   = m_mipUavIndices[currentIdx];
        rc.outputWidth      = m_mipWidths[currentIdx];
        rc.outputHeight     = m_mipHeights[currentIdx];
        rc.bloomRadius      = settings.radius;

        // Map upsample step to stage tint index:
        //   step 0: mip 5->4, uses stageTints[5] (stage 6, 1/64 res)
        //   step 1: mip 4->3, uses stageTints[4] (stage 5, 1/32 res)
        //   step 2: mip 3->2, uses stageTints[3] (stage 4, 1/16 res)
        //   step 3: mip 2->1, uses stageTints[2] (stage 3, 1/8 res)
        //   step 4: mip 1->0, uses stageTints[1] (stage 2, 1/4 res)
        //
        // lowerIdx goes 5,4,3,2,1 — which directly indexes stageTints[].
        // For now, use scalar tint applied equally to all channels (existing BloomSettings stores scalar).
        // Artists can extend BloomSettings with float3 tintColors[6] for colored bloom.
        rc.stageTintR = settings.stageTints[lowerIdx];
        rc.stageTintG = settings.stageTints[lowerIdx];
        rc.stageTintB = settings.stageTints[lowerIdx];

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

        // On non-bindless backends (DX11/SM5) bind SRV (t0) and UAV (u0) explicitly.
        if (!m_device->IsBindless()) {
            cmdList->SetDescriptorTable(5, m_mipSrvIndices[lowerIdx]);    // t0 — lower mip input
            cmdList->SetDescriptorTable(4, m_mipUavIndices[currentIdx]);  // u0 — current mip output
        }

        const uint32_t groupsX = (m_mipWidths[currentIdx]  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (m_mipHeights[currentIdx] + kGroupSize - 1) / kGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Final: Transition bloom mip[0] to SRV for tonemap to read ───────
    // The tonemap shader composites bloom (mip[0]) into the scene color
    // before applying exposure and tonemapping. No separate composite pass needed.
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_mipTextures[0];
        barrier.buffer      = RHIBufferHandle::Invalid();
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // After execution, mip[0] is in SRV state with the final bloom result.
    // The tonemap pass reads mip[0] and adds it to scene color before tonemapping.
}

} // namespace uz
