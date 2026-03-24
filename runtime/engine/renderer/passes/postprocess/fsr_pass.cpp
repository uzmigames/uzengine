/// UzEngine FSR 1.0 Pass — Implementation.
///
/// Compiles 2 compute shaders (EASU, RCAS) and dispatches them for spatial
/// upscaling. EASU performs edge-adaptive upscaling from low-res to high-res.
/// RCAS optionally sharpens the EASU output.
///
/// @ref AMD 2021 "FidelityFX Super Resolution" — GPUOpen published algorithm

#include "fsr_pass.h"

#include "engine/rhi/rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_FSR          = UZ_CVAR_BOOL("r.FSR", false, "FSR 1.0 spatial upscaling");
static auto* cv_FSRSharpness = UZ_CVAR_FLOAT("r.FSR.Sharpness", 0.2f,
    "FSR RCAS sharpness (0=max sharp, 2=soft)", 0.f, 2.f);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Float-as-uint bit packing (matches AMD AU1_AF1)
// ─────────────────────────────────────────────────────────────────────────────

/// Reinterpret a float as a uint32_t bit pattern.
/// Used to pack float constants into the uint4 root constant layout
/// that FSR shaders expect.
static uint32_t FloatAsUint(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// ─────────────────────────────────────────────────────────────────────────────
// FsrRcasCon — Pack RCAS Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref AMD ffx_fsr1.h — FsrRcasCon
///
/// Packs the sharpness attenuation into con0.x as a float-as-uint.
/// The shader reads this and uses it to scale the adaptive sharpening weight.
///
/// Sharpness mapping:
///   0.0 -> attenuation = exp2(0) = 1.0 -> maximum sharpening
///   0.2 -> attenuation = exp2(-0.2) = 0.87 -> AMD recommended default
///   2.0 -> attenuation = exp2(-2) = 0.25 -> minimal sharpening
void FsrRcasCon(uint32_t con0[4], float sharpness)
{
    // Clamp sharpness to valid range.
    sharpness = (sharpness < 0.0f) ? 0.0f : sharpness;
    sharpness = (sharpness > 2.0f) ? 2.0f : sharpness;

    // Pack exp2(-sharpness) as the attenuation factor.
    con0[0] = FloatAsUint(std::exp2f(-sharpness));
    con0[1] = 0;
    con0[2] = 0;
    con0[3] = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Root Constant Structures — must match HLSL cbuffer layouts
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for fsr_easu_cs.hlsl (8 DWORDs).
/// The shader computes all sampling parameters from the resolution values.
struct FSREasuRootConstants {
    uint32_t inputSrvIndex;  // DWORD 0: Input texture SRV index.
    uint32_t outputUavIndex; // DWORD 1: Output texture UAV index.
    uint32_t outputWidth;    // DWORD 2: Output width.
    uint32_t outputHeight;   // DWORD 3: Output height.
    uint32_t inputWidth;     // DWORD 4: Input width.
    uint32_t inputHeight;    // DWORD 5: Input height.
    uint32_t _pad0;          // DWORD 6: Padding.
    uint32_t _pad1;          // DWORD 7: Padding.
};

static_assert(sizeof(FSREasuRootConstants) == 32,
    "FSREasuRootConstants must be 8 DWORDs (32 bytes)");

/// Root constants for fsr_rcas_cs.hlsl (8 DWORDs).
struct FSRRcasRootConstants {
    uint32_t con0[4];        // DWORD 0-3: RCAS constant 0.
    uint32_t inputSrvIndex;  // DWORD 4:   Input texture SRV index.
    uint32_t outputUavIndex; // DWORD 5:   Output texture UAV index.
    uint32_t outputWidth;    // DWORD 6:   Output width.
    uint32_t outputHeight;   // DWORD 7:   Output height.
};

static_assert(sizeof(FSRRcasRootConstants) == 32,
    "FSRRcasRootConstants must be 8 DWORDs (32 bytes)");

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

FSRPass::~FSRPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool FSRPass::Init(IRHIDevice* device, ShaderManager* shaderMgr) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "FSRPass::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "FSRPass::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "FSRPass::Init failed: shaderMgr is null");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;
    m_initialized = true;

    UZ_LOG(Renderer, Info, "FSRPass initialized");
    return true;
}

void FSRPass::Shutdown() {
    if (m_device) {
        if (m_easuPipeline.IsValid()) m_device->DestroyPipeline(m_easuPipeline);
        if (m_rcasPipeline.IsValid()) m_device->DestroyPipeline(m_rcasPipeline);

        if (m_easuShader.IsValid()) m_device->DestroyShader(m_easuShader);
        if (m_rcasShader.IsValid()) m_device->DestroyShader(m_rcasShader);
    }

    m_easuPipeline = RHIPipelineHandle::Invalid();
    m_rcasPipeline = RHIPipelineHandle::Invalid();
    m_easuShader   = RHIShaderHandle::Invalid();
    m_rcasShader   = RHIShaderHandle::Invalid();

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "FSRPass shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool FSRPass::EnsureEasuPipeline() {
    if (m_easuPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::FsrEasuCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "FSRPass: failed to compile fsr_easu_cs.hlsl");
        return false;
    }

    UZ_LOG(Renderer, Info, "FSRPass: EASU shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "fsr_easu_cs";

    m_easuShader = m_device->CreateShader(shaderDesc);
    if (!m_easuShader.IsValid()) {
        UZ_LOG(Renderer, Error, "FSRPass: failed to create EASU shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_easuShader;
    pipeDesc.debugName     = "FSRPass_EASU";

    m_easuPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_easuPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "FSRPass: failed to create EASU pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "FSRPass: created EASU pipeline (shader={}, pipeline={})",
           m_easuShader.index, m_easuPipeline.index);
    return true;
}

bool FSRPass::EnsureRcasPipeline() {
    if (m_rcasPipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::FsrRcasCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "FSRPass: failed to compile fsr_rcas_cs.hlsl");
        return false;
    }

    UZ_LOG(Renderer, Info, "FSRPass: RCAS shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "fsr_rcas_cs";

    m_rcasShader = m_device->CreateShader(shaderDesc);
    if (!m_rcasShader.IsValid()) {
        UZ_LOG(Renderer, Error, "FSRPass: failed to create RCAS shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_rcasShader;
    pipeDesc.debugName     = "FSRPass_RCAS";

    m_rcasPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_rcasPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "FSRPass: failed to create RCAS pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "FSRPass: created RCAS pipeline (shader={}, pipeline={})",
           m_rcasShader.index, m_rcasPipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

void FSRPass::Execute(IRHICommandList* cmdList, const FSRInput& input) {
    UZ_PROFILE_SCOPE("FSRPass::Execute");

    if (!cmdList) {
        UZ_LOG(Renderer, Error, "FSRPass::Execute: null command list");
        return;
    }

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "FSRPass::Execute called before Init()");
        return;
    }

    // FSR requires bindless (ResourceDescriptorHeap).
    if (!m_device->IsBindless()) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Warning, "FSRPass: skipped — FSR requires bindless (SM6.6+)");
            loggedOnce = true;
        }
        return;
    }

    if (input.inputSrvIndex == UINT32_MAX || input.outputUavIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "FSRPass::Execute: invalid SRV/UAV indices");
        return;
    }

    if (input.outputWidth == 0 || input.outputHeight == 0) {
        UZ_LOG(Renderer, Error, "FSRPass::Execute: zero output dimensions");
        return;
    }

    if (input.inputWidth == 0 || input.inputHeight == 0) {
        UZ_LOG(Renderer, Error, "FSRPass::Execute: zero input dimensions");
        return;
    }

    // Ensure pipelines are compiled.
    if (!EnsureEasuPipeline()) {
        UZ_LOG(Renderer, Error, "FSRPass: EASU pipeline compilation failed, skipping FSR");
        return;
    }

    if (input.enableRCAS && !EnsureRcasPipeline()) {
        UZ_LOG(Renderer, Error, "FSRPass: RCAS pipeline compilation failed, skipping RCAS");
        // Continue with EASU only — RCAS failure is non-fatal.
    }

    // First-frame diagnostic log.
    static bool loggedFirstFrame = false;
    if (!loggedFirstFrame) {
        UZ_LOG(Renderer, Info,
               "FSRPass: first execution — input={}x{}, output={}x{}, "
               "RCAS={}, sharpness={:.3f}, inputSRV={}, outputUAV={}",
               input.inputWidth, input.inputHeight,
               input.outputWidth, input.outputHeight,
               input.enableRCAS ? "ON" : "OFF",
               input.sharpness,
               input.inputSrvIndex, input.outputUavIndex);
        loggedFirstFrame = true;
    }

    // ── Pass 1: EASU — Spatial Upscaling ─────────────────────────────────
    {
        UZ_PROFILE_SCOPE("FSRPass::EASU");

        cmdList->SetPipeline(m_easuPipeline);

        FSREasuRootConstants rc{};
        rc.inputSrvIndex  = input.inputSrvIndex;
        rc.outputUavIndex = input.outputUavIndex;
        rc.outputWidth    = input.outputWidth;
        rc.outputHeight   = input.outputHeight;
        rc.inputWidth     = input.inputWidth;
        rc.inputHeight    = input.inputHeight;
        rc._pad0          = 0;
        rc._pad1          = 0;

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

        const uint32_t groupsX = (input.outputWidth  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (input.outputHeight + kGroupSize - 1) / kGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Pass 2: RCAS — Sharpening (optional) ─────────────────────────────
    if (input.enableRCAS && m_rcasPipeline.IsValid()) {
        UZ_PROFILE_SCOPE("FSRPass::RCAS");

        // Barrier: EASU output (UAV) -> SRV for RCAS input.
        // RCAS final output texture -> UAV for write.
        RHIBarrierDesc barriers[2] = {};

        barriers[0].texture     = input.intermediateTexture;
        barriers[0].buffer      = RHIBufferHandle::Invalid();
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;

        barriers[1].texture     = input.outputTexture;
        barriers[1].buffer      = RHIBufferHandle::Invalid();
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;

        cmdList->ResourceBarrier(barriers, 2);

        cmdList->SetPipeline(m_rcasPipeline);

        FSRRcasRootConstants rc{};
        FsrRcasCon(rc.con0, input.sharpness);
        rc.inputSrvIndex  = input.intermediateSrvIndex;
        rc.outputUavIndex = input.finalUavIndex;
        rc.outputWidth    = input.outputWidth;
        rc.outputHeight   = input.outputHeight;

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

        const uint32_t groupsX = (input.outputWidth  + kGroupSize - 1) / kGroupSize;
        const uint32_t groupsY = (input.outputHeight + kGroupSize - 1) / kGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }
}

} // namespace uz
