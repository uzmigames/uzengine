/// UzEngine Auto-Exposure — Implementation.
///
/// Compiles compute shaders from the VFS (exposure_histogram_cs.hlsl,
/// exposure_average_cs.hlsl), creates GPU buffers for histogram and
/// exposure state, and dispatches compute work each frame.
///
/// # Buffer Strategy
/// - Histogram: Default-heap UAV+SRV buffer, cleared each frame via copy
///   from a zero-filled upload buffer.
/// - Exposure: Default-heap UAV buffer, persistent across frames. A readback
///   buffer mirrors the exposure value for CPU-side consumption.
/// - Zero: Upload-heap buffer filled with zeros at init time, used as the
///   clear source for the histogram buffer each frame.

#include "auto_exposure.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/shader_manager.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"

#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

AutoExposure::~AutoExposure() {
    if (m_initialized) {
        Shutdown();
    }
}

bool AutoExposure::Init(IRHIDevice* device, ShaderManager* shaderMgr) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "AutoExposure::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "AutoExposure::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "AutoExposure::Init failed: shaderMgr is null");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;

    // ── Create histogram buffer (64 x uint32 = 256 bytes, UAV + SRV) ────
    // Created as StructuredBuffer<uint> — HLSL must use RWStructuredBuffer<uint>.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = kHistogramBytes;
        desc.stride    = sizeof(uint32_t);
        desc.usage     = RHIBufferUsage::UnorderedAccess
                       | RHIBufferUsage::StructuredBuffer
                       | RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "AutoExposure_Histogram";

        m_histogramBuffer = m_device->CreateBuffer(desc);
        if (!m_histogramBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "AutoExposure: failed to create histogram buffer");
            return false;
        }

        m_histogramUavIndex = m_device->GetBufferUAVIndex(m_histogramBuffer);
        m_histogramSrvIndex = m_device->GetBufferSRVIndex(m_histogramBuffer);

        if (m_histogramUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "AutoExposure: histogram buffer has no UAV descriptor");
            Shutdown();
            return false;
        }
        if (m_histogramSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "AutoExposure: histogram buffer has no SRV descriptor");
            Shutdown();
            return false;
        }
    }

    // ── Create exposure buffer (2 x float = 8 bytes, UAV) ───────────────
    // Created as StructuredBuffer<float> — HLSL uses RWStructuredBuffer<float>.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = kExposureBytes;
        desc.stride    = sizeof(float);
        desc.usage     = RHIBufferUsage::UnorderedAccess
                       | RHIBufferUsage::StructuredBuffer
                       | RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Default;
        desc.debugName = "AutoExposure_Exposure";

        m_exposureBuffer = m_device->CreateBuffer(desc);
        if (!m_exposureBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "AutoExposure: failed to create exposure buffer");
            Shutdown();
            return false;
        }

        m_exposureUavIndex = m_device->GetBufferUAVIndex(m_exposureBuffer);
        if (m_exposureUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "AutoExposure: exposure buffer has no UAV descriptor");
            Shutdown();
            return false;
        }
    }

    // ── Create zero buffer for histogram clearing ───────────────────────
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = kHistogramBytes;
        desc.stride    = 0;
        desc.usage     = RHIBufferUsage::CopySrc;
        desc.memory    = RHIMemoryType::Upload;
        desc.debugName = "AutoExposure_ZeroClear";

        m_zeroBuffer = m_device->CreateBuffer(desc);
        if (!m_zeroBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "AutoExposure: failed to create zero buffer");
            Shutdown();
            return false;
        }

        // Fill with zeros (upload heap is persistently mapped)
        void* mapped = m_device->MapBuffer(m_zeroBuffer);
        if (mapped) {
            std::memset(mapped, 0, kHistogramBytes);
            m_device->UnmapBuffer(m_zeroBuffer);
        }
    }

    // ── Create readback buffer for CPU exposure retrieval ────────────────
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = kExposureBytes;
        desc.stride    = 0;
        desc.usage     = RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Readback;
        desc.debugName = "AutoExposure_Readback";

        m_readbackBuffer = m_device->CreateBuffer(desc);
        if (!m_readbackBuffer.IsValid()) {
            UZ_LOG(Renderer, Error, "AutoExposure: failed to create readback buffer");
            Shutdown();
            return false;
        }
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "AutoExposure initialized (histogram UAV={}, SRV={}, exposure UAV={})",
           m_histogramUavIndex, m_histogramSrvIndex, m_exposureUavIndex);
    return true;
}

void AutoExposure::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Destroy buffers
    if (m_histogramBuffer.IsValid()) {
        m_device->DestroyBuffer(m_histogramBuffer);
        m_histogramBuffer = RHIBufferHandle::Invalid();
    }
    if (m_exposureBuffer.IsValid()) {
        m_device->DestroyBuffer(m_exposureBuffer);
        m_exposureBuffer = RHIBufferHandle::Invalid();
    }
    if (m_zeroBuffer.IsValid()) {
        m_device->DestroyBuffer(m_zeroBuffer);
        m_zeroBuffer = RHIBufferHandle::Invalid();
    }
    if (m_readbackBuffer.IsValid()) {
        m_device->DestroyBuffer(m_readbackBuffer);
        m_readbackBuffer = RHIBufferHandle::Invalid();
    }

    // Destroy pipelines
    if (m_histogramPipeline.IsValid()) {
        m_device->DestroyPipeline(m_histogramPipeline);
        m_histogramPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_averagePipeline.IsValid()) {
        m_device->DestroyPipeline(m_averagePipeline);
        m_averagePipeline = RHIPipelineHandle::Invalid();
    }

    // Destroy shaders
    if (m_histogramCS.IsValid()) {
        m_device->DestroyShader(m_histogramCS);
        m_histogramCS = RHIShaderHandle::Invalid();
    }
    if (m_averageCS.IsValid()) {
        m_device->DestroyShader(m_averageCS);
        m_averageCS = RHIShaderHandle::Invalid();
    }

    m_histogramUavIndex = UINT32_MAX;
    m_histogramSrvIndex = UINT32_MAX;
    m_exposureUavIndex  = UINT32_MAX;
    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "AutoExposure shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool AutoExposure::EnsureHistogramPipeline() {
    if (m_histogramPipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ExposureHistogramCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "AutoExposure: failed to compile exposure_histogram_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "exposure_histogram_cs";

    m_histogramCS = m_device->CreateShader(shaderDesc);
    if (!m_histogramCS.IsValid()) {
        UZ_LOG(Renderer, Error, "AutoExposure: failed to create histogram shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_histogramCS;
    pipeDesc.debugName     = "AutoExposure_Histogram";

    m_histogramPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_histogramPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "AutoExposure: failed to create histogram pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "AutoExposure: created histogram pipeline");
    return true;
}

bool AutoExposure::EnsureAveragePipeline() {
    if (m_averagePipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::ExposureAverageCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "AutoExposure: failed to compile exposure_average_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "exposure_average_cs";

    m_averageCS = m_device->CreateShader(shaderDesc);
    if (!m_averageCS.IsValid()) {
        UZ_LOG(Renderer, Error, "AutoExposure: failed to create average shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_averageCS;
    pipeDesc.debugName     = "AutoExposure_Average";

    m_averagePipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_averagePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "AutoExposure: failed to create average pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "AutoExposure: created average pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

float AutoExposure::Execute(IRHICommandList* cmdList, uint32_t hdrSrvIndex,
                            uint32_t width, uint32_t height, float dt)
{
    UZ_PROFILE_SCOPE("AutoExposure::Execute");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "AutoExposure::Execute called before Init()");
        return 1.0f;
    }

    // ── Bypass paths (no GPU work) ──────────────────────────────────────

    if (!settings.enabled) {
        // No auto-exposure — use bias as direct exposure multiplier
        m_lastExposure = std::pow(2.0f, settings.exposureBias);
        return m_lastExposure;
    }

    if (settings.manualExposure > 0.0f) {
        // Fixed manual exposure: treat manualExposure as target luminance
        // UE5: exposure = 0.18 * pow(2, bias) / manualLuminance
        float biasLinear = std::pow(2.0f, settings.exposureBias);
        m_lastExposure = 0.18f * biasLinear / settings.manualExposure;
        return m_lastExposure;
    }

    // ── Ensure pipelines are compiled ───────────────────────────────────

    if (!EnsureHistogramPipeline() || !EnsureAveragePipeline()) {
        return m_lastExposure;
    }

    // ── Step 1: Clear histogram buffer to zero ──────────────────────────
    // Copy from the pre-zeroed upload buffer to the histogram default buffer.
    {
        // Transition histogram to CopyDst for the clear copy
        RHIBarrierDesc barrier{};
        barrier.buffer      = m_histogramBuffer;
        barrier.texture     = RHITextureHandle::Invalid();
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::CopyDst;
        cmdList->ResourceBarrier(&barrier, 1);

        cmdList->CopyBuffer(m_zeroBuffer, 0,
                            m_histogramBuffer, 0,
                            kHistogramBytes);

        // Transition back to UAV for compute write
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Step 2: Histogram pass ──────────────────────────────────────────
    {
        cmdList->SetPipeline(m_histogramPipeline);

        HistogramRootConstants rc{};
        rc.histLogMin   = settings.histLogMin;
        rc.histLogMax   = settings.histLogMax;
        rc.screenWidth  = width;
        rc.screenHeight = height;
        rc.hdrSrvIndex  = hdrSrvIndex;

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 5);

        // Bind histogram UAV at root param 4 (u0 descriptor table)
        cmdList->SetDescriptorTable(4, m_histogramUavIndex);

        // Dispatch: ceil(width/16) x ceil(height/16) x 1
        const uint32_t groupsX = (width  + kHistGroupSize - 1) / kHistGroupSize;
        const uint32_t groupsY = (height + kHistGroupSize - 1) / kHistGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Step 3: UAV barrier (histogram write → read) ────────────────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer      = m_histogramBuffer;
        barrier.texture     = RHITextureHandle::Invalid();
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Step 4: Average/adaptation pass ─────────────────────────────────
    {
        cmdList->SetPipeline(m_averagePipeline);

        AverageRootConstants rc{};
        rc.histLogMin      = settings.histLogMin;
        rc.histLogMax      = settings.histLogMax;
        rc.histogramSrvIdx = m_histogramSrvIndex;
        rc.padding         = 0;
        rc.exposureBias    = settings.exposureBias;
        rc.minBrightness   = settings.minBrightness;
        rc.maxBrightness   = settings.maxBrightness;
        rc.deltaTime       = dt;

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

        // Bind exposure UAV at root param 4 (u0 descriptor table)
        cmdList->SetDescriptorTable(4, m_exposureUavIndex);

        // Single thread group — this shader runs as a single thread
        cmdList->Dispatch(1, 1, 1);
    }

    // ── Step 5: Copy exposure to readback buffer for CPU access ─────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer      = m_exposureBuffer;
        barrier.texture     = RHITextureHandle::Invalid();
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);

        cmdList->CopyBuffer(m_exposureBuffer, 0,
                            m_readbackBuffer, 0,
                            kExposureBytes);

        // Transition exposure back to UAV for next frame
        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Step 6: Transition histogram back to UAV for next frame ─────────
    {
        RHIBarrierDesc barrier{};
        barrier.buffer      = m_histogramBuffer;
        barrier.texture     = RHITextureHandle::Invalid();
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Step 7: Read back exposure from previous frame ──────────────────
    // The readback data is from the PREVIOUS frame's copy (one frame latency).
    // This is standard practice — avoids GPU stalls from synchronous readback.
    {
        const void* mapped = m_device->MapBuffer(m_readbackBuffer);
        if (mapped) {
            float exposureData[2];
            std::memcpy(exposureData, mapped, sizeof(exposureData));
            m_device->UnmapBuffer(m_readbackBuffer);

            // exposureData[0] = currentEV, exposureData[1] = exposure multiplier
            const float readbackExposure = exposureData[1];
            if (std::isfinite(readbackExposure) && readbackExposure > 0.0f) {
                m_lastExposure = readbackExposure;
            }
        }
    }

    return m_lastExposure;
}

} // namespace uz
