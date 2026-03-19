/// UzEngine Lens Flare Pass — Implementation.
///
/// Compiles the lensflare_cs.hlsl compute shader, creates an RGBA16F output
/// texture, and dispatches the lens flare pass each frame. The output is
/// composited additively by the tonemap shader.
///
/// # Pipeline
/// 1. Allocate LensFlareCBV via CBRingAllocator, bind at root param 2 (b2).
/// 2. Bind output UAV at descriptor table slot 4 (u0).
/// 3. Dispatch ceil(width/8) x ceil(height/8) thread groups.
/// 4. Transition output from UAV → SRV for tonemap to read.
///
/// @source John Chapman 2013 — ghost-based lens flare technique

#include "lens_flare_pass.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/cb_ring_allocator.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

LensFlarePass::~LensFlarePass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool LensFlarePass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                          uint32_t width, uint32_t height)
{
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "LensFlarePass::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "LensFlarePass::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "LensFlarePass::Init failed: shaderMgr is null");
        return false;
    }
    if (width == 0 || height == 0) {
        UZ_LOG(Renderer, Error, "LensFlarePass::Init failed: zero-size output ({}x{})",
               width, height);
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;
    m_width     = width;
    m_height    = height;

    CreateOutputTexture(width, height);

    if (!m_output.IsValid()) {
        UZ_LOG(Renderer, Error, "LensFlarePass::Init failed: output texture creation failed");
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "LensFlarePass initialized ({}x{}, SRV={}, UAV={})",
           width, height, m_outputSrv, m_outputUav);
    return true;
}

void LensFlarePass::Shutdown() {
    DestroyOutputTexture();

    if (m_device) {
        if (m_pipeline.IsValid()) m_device->DestroyPipeline(m_pipeline);
        if (m_shader.IsValid())   m_device->DestroyShader(m_shader);
    }

    m_pipeline = RHIPipelineHandle::Invalid();
    m_shader   = RHIShaderHandle::Invalid();

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;
    m_initialBarrierDone = false;

    UZ_LOG(Renderer, Info, "LensFlarePass shut down");
}

void LensFlarePass::OnResize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) {
        return;
    }

    if (width == 0 || height == 0) {
        UZ_LOG(Renderer, Warning, "LensFlarePass::OnResize: ignoring zero-size ({}x{})",
               width, height);
        return;
    }

    m_width  = width;
    m_height = height;

    DestroyOutputTexture();
    CreateOutputTexture(width, height);
    m_initialBarrierDone = false;

    UZ_LOG(Renderer, Info, "LensFlarePass resized to {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Output Texture Management
// ─────────────────────────────────────────────────────────────────────────────

void LensFlarePass::CreateOutputTexture(uint32_t width, uint32_t height) {
    RHITextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.format    = RHIFormat::RGBA16Float;
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    desc.debugName = "LensFlare_Output";

    m_output = m_device->CreateTexture(desc);
    if (!m_output.IsValid()) {
        UZ_LOG(Renderer, Error, "LensFlarePass: failed to create output texture ({}x{})",
               width, height);
        return;
    }

    m_outputSrv = m_device->GetTextureSRVIndex(m_output);
    m_outputUav = m_device->GetTextureUAVIndex(m_output);

    UZ_LOG(Renderer, Info, "LensFlarePass: output texture created {}x{} (SRV={}, UAV={})",
           width, height, m_outputSrv, m_outputUav);
}

void LensFlarePass::DestroyOutputTexture() {
    if (m_device && m_output.IsValid()) {
        m_device->DestroyTexture(m_output);
    }
    m_output    = RHITextureHandle{};
    m_outputSrv = UINT32_MAX;
    m_outputUav = UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool LensFlarePass::EnsurePipeline() {
    if (m_pipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::LensflareCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "LensFlarePass: failed to compile lensflare_cs.hlsl");
        return false;
    }

    UZ_LOG(Renderer, Info, "LensFlarePass: shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "CSMain";
    shaderDesc.debugName    = "lensflare_cs";

    m_shader = m_device->CreateShader(shaderDesc);
    if (!m_shader.IsValid()) {
        UZ_LOG(Renderer, Error, "LensFlarePass: failed to create shader (bytecode={} bytes)",
               bytecode.size());
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_shader;
    pipeDesc.debugName     = "LensFlarePass_Compute";

    m_pipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "LensFlarePass: failed to create compute pipeline (shader={})",
               m_shader.index);
        return false;
    }

    UZ_LOG(Renderer, Info, "LensFlarePass: compute pipeline created (shader={}, pipeline={})",
           m_shader.index, m_pipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

void LensFlarePass::Execute(IRHICommandList* cmd, CBRingAllocator& cbRing,
                             uint32_t bloomThresholdSrv,
                             const LensFlareSettings& settings)
{
    UZ_PROFILE_SCOPE("LensFlarePass::Execute");

    if (!settings.enabled) {
        return;
    }

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "LensFlarePass::Execute called before Init()");
        return;
    }

    if (!cmd) {
        UZ_LOG(Renderer, Error, "LensFlarePass::Execute: null command list");
        return;
    }

    // Lens flare uses bindless ResourceDescriptorHeap for bloom threshold reads.
    // On SM5/DX11, bindless is not available — skip silently.
    if (!m_device->IsBindless()) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Warning,
                   "LensFlarePass: skipped — compute lens flare requires bindless (SM6.6+)");
            loggedOnce = true;
        }
        return;
    }

    if (bloomThresholdSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "LensFlarePass::Execute: bloomThresholdSrv is UINT32_MAX — bloom threshold not available");
        return;
    }

    if (m_outputUav == UINT32_MAX || m_outputSrv == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "LensFlarePass::Execute: output descriptor indices invalid (SRV={}, UAV={}) "
               "— CreateOutputTexture() may have failed",
               m_outputSrv, m_outputUav);
        return;
    }

    if (m_width == 0 || m_height == 0) {
        UZ_LOG(Renderer, Error,
               "LensFlarePass::Execute: zero-size output ({}x{}) — aborting", m_width, m_height);
        return;
    }

    if (!EnsurePipeline()) {
        UZ_LOG(Renderer, Error, "LensFlarePass: pipeline compilation failed, skipping");
        return;
    }

    // ── Initial barrier: Common → UAV for first use after texture creation ──
    // DX12 textures start in Common state. Textures do NOT support implicit
    // Common → UAV promotion — an explicit barrier is required.
    if (!m_initialBarrierDone) {
        RHIBarrierDesc initBarrier{};
        initBarrier.texture     = m_output;
        initBarrier.stateBefore = RHIResourceState::Common;
        initBarrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(&initBarrier, 1);

        m_initialBarrierDone = true;
        UZ_LOG(Renderer, Info, "LensFlarePass: initial barrier — Common -> UAV");
    } else {
        // Transition output from SRV (left by previous frame) to UAV for write.
        RHIBarrierDesc barrier{};
        barrier.texture     = m_output;
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // ── Set pipeline ────────────────────────────────────────────────────────
    cmd->SetPipeline(m_pipeline);

    // ── Fill and upload LensFlareCBV at register(b2) via CBRingAllocator ────
    LensFlareCBV cb{};
    cb.bloomThresholdSrv = bloomThresholdSrv;
    cb.intensity         = settings.intensity;
    cb.ghostIntensity    = settings.ghostIntensity;
    cb.haloRadius        = settings.haloRadius;
    cb.haloWidth         = settings.haloWidth;
    cb.chromaticShift    = settings.chromaticShift;
    cb.outputWidth       = static_cast<float>(m_width);
    cb.outputHeight      = static_cast<float>(m_height);

    auto alloc = cbRing.Allocate(sizeof(LensFlareCBV));
    if (!alloc.cpuPtr) {
        UZ_LOG(Renderer, Error, "LensFlarePass: CB ring allocation failed for LensFlareCBV");
        return;
    }
    std::memcpy(alloc.cpuPtr, &cb, sizeof(LensFlareCBV));

    // Bind CBV at root param 2 (register b2) via cross-backend SetRootCBV.
    // DX12 uses gpuAddr; DX11 uses cpuData + dataSize for UpdateSubresource.
    cmd->SetRootCBV(2, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr,
                    sizeof(LensFlareCBV));

    // ── Bind UAV (slot 4) — flare output at u0 ─────────────────────────────
    cmd->SetDescriptorTable(4, m_outputUav);

    // ── Dispatch ────────────────────────────────────────────────────────────
    const uint32_t groupsX = (m_width  + kGroupSize - 1) / kGroupSize;
    const uint32_t groupsY = (m_height + kGroupSize - 1) / kGroupSize;

    // First-frame diagnostic dump
    static bool loggedFirstFrame = false;
    if (!loggedFirstFrame) {
        UZ_LOG(Renderer, Info,
               "LensFlarePass: first execution — {}x{}, bloomSRV={}, outputUAV={}, "
               "intensity={:.3f}, ghostIntensity={:.3f}, haloRadius={:.3f}, "
               "dispatch=({}, {}, 1), pipeline={}",
               m_width, m_height, bloomThresholdSrv, m_outputUav,
               settings.intensity, settings.ghostIntensity, settings.haloRadius,
               groupsX, groupsY, m_pipeline.index);
        loggedFirstFrame = true;
    }

    cmd->Dispatch(groupsX, groupsY, 1);

    // ── Transition output: UAV → SRV for tonemap to read ───────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_output;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmd->ResourceBarrier(&barrier, 1);
    }

    // After execution, m_output is in SRV state with the lens flare result.
    // The tonemap pass reads GetOutputSrv() and adds it to scene color.
}

} // namespace uz
