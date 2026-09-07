/// UzEngine Radiance Cache — Implementation.
///
/// Manages a 16^3 world-space probe grid for diffuse GI via L2 SH.
/// Each frame, dispatches a compute shader that traces rays from probes
/// through the global distance field and accumulates radiance into SH.
///
/// @ref Lumen GI — world-space probe clipmap layout with SH radiance storage.
/// Probe world offsets computed per-frame to follow camera movement.

#include "radiance_cache.h"

#include "global_distance_field.h"
#include "cb_ring_allocator.h"
#include "constant_buffers.h"
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_RadianceCache    = UZ_CVAR_BOOL("r.RadianceCache", false, "SDF radiance cache GI");
static auto* cv_RCGridExtent     = UZ_CVAR_FLOAT("r.GI.RadianceCache.GridExtent", 5000.f, "Probe grid half-extent (cm)", 100.f, 50000.f);
static auto* cv_RCDiagnostic = UZ_CVAR_BOOL("r.GI.RadianceCache.Diagnostic", false, "Read back and log the actual SH probe buffer once after frame 120");
static auto* cv_RCRays = UZ_CVAR_INT("r.GI.RadianceCache.RaysPerProbe", 32,
    "Sphere tracing rays per updated irradiance probe", 4, 64);
static auto* cv_RCProbeBudget = UZ_CVAR_INT("r.GI.RadianceCache.ProbesPerFrame", 1024,
    "Irradiance probes updated per frame after initialization", 64, 4096);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Destruction
// ─────────────────────────────────────────────────────────────────────────────

RadianceCache::~RadianceCache() {
    DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateResources
// ─────────────────────────────────────────────────────────────────────────────

bool RadianceCache::CreateResources(IRHIDevice* device, float worldExtent) {
    if (!device) {
        UZ_LOG(Renderer, Error, "RadianceCache::CreateResources: null device");
        return false;
    }

    DestroyResources();

    m_device      = device;
    m_gridExtent  = worldExtent;
    m_probeSpacing = (worldExtent * 2.0f) / static_cast<float>(kProbesPerAxis - 1);
    m_gridCenter  = Vec3{0.0f, 0.0f, 0.0f};
    m_gridOrigin  = Vec3{-worldExtent, -worldExtent, -worldExtent};

    // ── Create StructuredBuffer for SH coefficients ────────────────────────
    // kTotalProbes * kSHFloat4PerProbe * sizeof(float4) = 4096 * 7 * 16 = 458752 bytes
    const uint32_t totalFloat4s = kTotalProbes * kSHFloat4PerProbe;
    const uint32_t bufferSize   = totalFloat4s * 4 * sizeof(float); // float4 = 16 bytes

    RHIBufferDesc bufDesc{};
    bufDesc.sizeBytes = bufferSize;
    bufDesc.stride    = 4 * sizeof(float); // sizeof(float4) = 16
    bufDesc.usage     = RHIBufferUsage::StructuredBuffer
                      | RHIBufferUsage::UnorderedAccess
                      | RHIBufferUsage::CopySrc | RHIBufferUsage::CopyDst;
    bufDesc.debugName = "RadianceCache_SH";

    m_shBuffer = device->CreateBuffer(bufDesc);
    if (!m_shBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to create SH buffer ({} bytes)", bufferSize);
        return false;
    }

    m_shBufferSrv = device->GetBufferSRVIndex(m_shBuffer);
    m_shBufferUav = device->GetBufferUAVIndex(m_shBuffer);

    bufDesc.debugName = "RadianceCache_SH_History";
    m_historyShBuffer = device->CreateBuffer(bufDesc);
    if (!m_historyShBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to create history SH buffer ({} bytes)", bufferSize);
        DestroyResources();
        return false;
    }
    m_historyShBufferSrv = device->GetBufferSRVIndex(m_historyShBuffer);
    m_historyShBufferUav = device->GetBufferUAVIndex(m_historyShBuffer);

    if (cv_RCDiagnostic->value) {
        bufDesc.sizeBytes = kTotalProbes * 2 * 4 * sizeof(float);
        bufDesc.debugName = "RadianceCache_TraceDiagnostics";
        m_diagnosticStats = device->CreateBuffer(bufDesc);
        if (m_diagnosticStats.IsValid())
            m_diagnosticStatsUav = device->GetBufferUAVIndex(m_diagnosticStats);
    }

    UZ_LOG(Renderer, Info,
           "RadianceCache: created SH buffer ({} probes, {} float4s, {} KB, SRV={}, UAV={})",
           kTotalProbes, totalFloat4s, bufferSize / 1024, m_shBufferSrv, m_shBufferUav);

    m_initialized = true;
    m_dirty       = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyResources
// ─────────────────────────────────────────────────────────────────────────────

void RadianceCache::DestroyResources() {
    if (m_device && m_diagnosticStats.IsValid()) {
        m_device->DestroyBuffer(m_diagnosticStats);
    }
    if (m_device && m_diagnosticReadback.IsValid()) {
        m_device->DestroyBuffer(m_diagnosticReadback);
    }
    if (m_device && m_shBuffer.IsValid()) {
        m_device->DestroyBuffer(m_shBuffer);
    }
    if (m_device && m_historyShBuffer.IsValid()) {
        m_device->DestroyBuffer(m_historyShBuffer);
    }

    m_device = nullptr;
    m_shBuffer = RHIBufferHandle{};
    m_shBufferSrv = UINT32_MAX;
    m_shBufferUav = UINT32_MAX;
    m_historyShBuffer = {};
    m_historyShBufferSrv = UINT32_MAX;
    m_historyShBufferUav = UINT32_MAX;
    m_shBufferState = RHIResourceState::Common;
    m_historyShBufferState = RHIResourceState::Common;
    m_gridCenter = Vec3{};
    m_gridOrigin = Vec3{};
    m_gridExtent = 0.0f;
    m_probeSpacing = 0.0f;
    m_frameCounter = 0;
    m_probeUpdateCursor = 0;
    m_dirty = true;
    m_firstWritePending = true;
    m_initialized = false;
    m_diagnosticReadback = {};
    m_diagnosticStats = {};
    m_diagnosticStatsUav = UINT32_MAX;
    m_diagnosticStatsState = RHIResourceState::Common;
    m_diagnosticCopyFrame = UINT32_MAX;
    m_diagnosticComplete = false;
    m_diagnosticOrigin = {};
    m_diagnosticSpacing = 0.0f;
}

void RadianceCache::QueueDiagnosticReadback(IRHICommandList* cmdList, uint32_t frameIndex) {
    if (!cv_RCDiagnostic->value || m_diagnosticComplete || m_diagnosticReadback.IsValid() || frameIndex < 120) return;
    RHIBufferDesc desc{};
    const uint32_t shBytes = kTotalProbes * kSHFloat4PerProbe * 4 * sizeof(float);
    const uint32_t statsBytes = m_diagnosticStats.IsValid() ? kTotalProbes * 2 * 4 * sizeof(float) : 0;
    desc.sizeBytes = shBytes + statsBytes;
    desc.memory = RHIMemoryType::Readback;
    desc.debugName = "RadianceCache_DiagnosticReadback";
    m_diagnosticReadback = m_device->CreateBuffer(desc);
    if (!m_diagnosticReadback.IsValid()) {
        UZ_LOG(Renderer, Error, "RC diagnostic: readback allocation failed");
        m_diagnosticComplete = true;
        return;
    }
    RHIBarrierDesc barrier{};
    barrier.buffer = m_shBuffer;
    barrier.stateBefore = RHIResourceState::ShaderResource;
    barrier.stateAfter = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);
    cmdList->CopyBuffer(m_shBuffer, 0, m_diagnosticReadback, 0, shBytes);
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);
    if (statsBytes != 0) {
        barrier.buffer = m_diagnosticStats;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);
        cmdList->CopyBuffer(m_diagnosticStats, 0, m_diagnosticReadback, shBytes, statsBytes);
        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }
    m_diagnosticCopyFrame = frameIndex;
    m_diagnosticOrigin = m_gridOrigin;
    m_diagnosticSpacing = m_probeSpacing;
    UZ_LOG(Renderer, Info, "RC diagnostic: queued SH snapshot frame={} SRV={} UAV={} bytes={} origin=({},{},{}) spacing={}",
           frameIndex, m_shBufferSrv, m_shBufferUav, desc.sizeBytes, m_gridOrigin.x, m_gridOrigin.y, m_gridOrigin.z, m_probeSpacing);
}

void RadianceCache::ReadDiagnosticResults(uint32_t frameIndex) {
    if (!m_diagnosticReadback.IsValid() || m_diagnosticComplete || frameIndex - m_diagnosticCopyFrame < 4) return;
    // The copy belongs to an earlier submitted frame; never wait on this recording command list.
    m_device->WaitIdle();
    m_diagnosticComplete = true;
    const auto* data = static_cast<const float*>(m_device->MapBuffer(m_diagnosticReadback));
    if (!data) {
        UZ_LOG(Renderer, Error, "RC diagnostic: readback map failed");
        return;
    }
    uint32_t finite = 0, invalid = 0, nonzero = 0;
    float minimum = std::numeric_limits<float>::max(), maximum = std::numeric_limits<float>::lowest();
    double dc[3]{}, axisSum[6][3]{};
    uint32_t axisFinite[6][3]{};
    const auto evaluate = [](const float* probe, uint32_t axis, float sign, uint32_t channel) {
        const float* a = probe + channel * 4;
        const float* b = probe + (channel + 3) * 4;
        float value = a[axis] * sign + a[3];
        if (axis == 2) value += b[2];
        if (axis == 0) value += probe[24 + channel];
        if (axis == 1) value -= probe[24 + channel];
        return value;
    };
    for (uint32_t index = 0; index < kTotalProbes; ++index) {
        const float* probe = data + index * 28;
        bool hasEnergy = false;
        for (uint32_t coefficient = 0; coefficient < 27; ++coefficient) {
            const float value = probe[coefficient];
            if (!std::isfinite(value)) { ++invalid; continue; }
            ++finite;
            minimum = std::min(minimum, value); maximum = std::max(maximum, value);
            hasEnergy |= value != 0.0f;
        }
        nonzero += hasEnergy;
        for (uint32_t channel = 0; channel < 3; ++channel) {
            const float constant = probe[channel * 4 + 3];
            if (std::isfinite(constant)) dc[channel] += constant;
            for (uint32_t direction = 0; direction < 6; ++direction) {
                const float value = evaluate(probe, direction / 2, direction % 2 == 0 ? 1.0f : -1.0f, channel);
                if (std::isfinite(value)) { axisSum[direction][channel] += value; ++axisFinite[direction][channel]; }
            }
        }
    }
    UZ_LOG(Renderer, Info, "RC diagnostic: snapshotFrame={} readFrame={} probes={} nonzeroProbes={} finite={} invalid={} min={} max={} meanPolynomialDC=({},{},{})",
           m_diagnosticCopyFrame, frameIndex, kTotalProbes, nonzero, finite, invalid,
           finite ? minimum : 0.0f, finite ? maximum : 0.0f, dc[0] / kTotalProbes, dc[1] / kTotalProbes, dc[2] / kTotalProbes);
    for (uint32_t direction = 0; direction < 6; ++direction) {
        UZ_LOG(Renderer, Info, "RC diagnostic: axis={} sign={} meanIrradiance=({},{},{}) finiteRGB=({},{},{})",
               direction / 2, direction % 2 == 0 ? 1 : -1,
               axisSum[direction][0] / std::max(axisFinite[direction][0], 1u),
               axisSum[direction][1] / std::max(axisFinite[direction][1], 1u),
               axisSum[direction][2] / std::max(axisFinite[direction][2], 1u),
               axisFinite[direction][0], axisFinite[direction][1], axisFinite[direction][2]);
    }
    for (uint32_t z : {7u, 8u}) for (uint32_t y : {7u, 8u}) for (uint32_t x : {7u, 8u}) {
        const uint32_t index = x + y * kProbesPerAxis + z * kProbesPerAxis * kProbesPerAxis;
        const float* probe = data + index * 28;
        UZ_LOG(Renderer, Info, "RC diagnostic: centralProbe={} world=({},{},{}) polynomialDC=({},{},{}) plusZ=({},{},{}) minusZ=({},{},{})",
               index, m_diagnosticOrigin.x + x * m_diagnosticSpacing, m_diagnosticOrigin.y + y * m_diagnosticSpacing,
               m_diagnosticOrigin.z + z * m_diagnosticSpacing, probe[3], probe[7], probe[11],
               evaluate(probe, 2, 1, 0), evaluate(probe, 2, 1, 1), evaluate(probe, 2, 1, 2),
               evaluate(probe, 2, -1, 0), evaluate(probe, 2, -1, 1), evaluate(probe, 2, -1, 2));
        if (m_diagnosticStats.IsValid()) {
            const float* stats = data + kTotalProbes * kSHFloat4PerProbe * 4 + index * 8;
            UZ_LOG(Renderer, Info, "RC transport: probe={} distance={} albedo=({},{},{}) skyFraction={} frontHitFraction={} meanDirect={} meanRadiance={}",
                   index, stats[0], stats[1], stats[2], stats[3], stats[4], stats[5], stats[6], stats[7]);
        }
    }
    m_device->UnmapBuffer(m_diagnosticReadback);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — recenter probe grid on camera
// ─────────────────────────────────────────────────────────────────────────────

void RadianceCache::Update(const Vec3& cameraPos) {
    if (!m_initialized) return;

    using namespace DirectX;

    // Snap camera position to probe spacing grid for stable interpolation coordinates
    const XMVECTOR vCamPos    = cameraPos.toXM();
    const XMVECTOR vSpacing   = XMVectorReplicate(m_probeSpacing);
    const XMVECTOR vInvSpacing = XMVectorReciprocal(vSpacing);
    const XMVECTOR vSnapped   = XMVectorMultiply(
        XMVectorFloor(XMVectorMultiply(vCamPos, vInvSpacing)), vSpacing);

    // Check if grid shifted (any axis moved by >= probeSpacing)
    const XMVECTOR vGridCenter = m_gridCenter.toXM();
    const XMVECTOR vDiff       = XMVectorAbs(XMVectorSubtract(vSnapped, vGridCenter));
    const XMVECTOR vCmp        = XMVectorGreaterOrEqual(vDiff, vSpacing);

    // Check if any of the first 3 components are true (nonzero).
    // GreaterOrEqual sets all bits for true lanes; test components directly
    // so this stays portable (SSE movemask is x86-only).
    if (XMVectorGetX(vCmp) != 0.0f || XMVectorGetY(vCmp) != 0.0f ||
        XMVectorGetZ(vCmp) != 0.0f) {
        const Vec3 snappedCenter(vSnapped);
        m_gridCenter = snappedCenter;
        m_gridOrigin = Vec3(XMVectorSubtract(vSnapped, XMVectorReplicate(m_gridExtent)));
        m_dirty      = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UpdateProbes — dispatch compute shader to trace rays and accumulate SH
// ─────────────────────────────────────────────────────────────────────────────

void RadianceCache::UpdateProbes(
    IRHICommandList* cmdList,
    const GlobalDistanceField& gdf,
    RHIPipelineHandle tracePipeline,
    CBRingAllocator* cbRing,
    RHIBufferHandle perFrameBuffer,
    uint32_t perFrameOffset,
    const void* perFrameCpuPtr,
    uint32_t perFrameSize,
    uint32_t frameIndex)
{
    if (!m_initialized || !cmdList || !tracePipeline.IsValid() || !cbRing) {
        return;
    }

    if (!gdf.IsInitialized()) {
        return;
    }

    ReadDiagnosticResults(frameIndex);

    CBAllocation constAlloc = cbRing->Allocate(sizeof(RadianceCacheTraceConstants));
    if (!constAlloc.cpuPtr) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to allocate trace constants");
        return;
    }

    m_frameCounter = frameIndex;
    const bool resetHistory = m_firstWritePending || m_dirty;

    std::swap(m_shBuffer, m_historyShBuffer);
    std::swap(m_shBufferSrv, m_historyShBufferSrv);
    std::swap(m_shBufferUav, m_historyShBufferUav);
    std::swap(m_shBufferState, m_historyShBufferState);

    // Cross-probe feedback reads an immutable previous frame (Majercik 2019, section 5.3).
    // Preserve probes outside this frame's update budget in the new output buffer.
    if (!resetHistory) {
        RHIBarrierDesc toCopy[2]{};
        toCopy[0].buffer = m_historyShBuffer;
        toCopy[0].stateBefore = m_historyShBufferState;
        toCopy[0].stateAfter = RHIResourceState::CopySrc;
        toCopy[1].buffer = m_shBuffer;
        toCopy[1].stateBefore = m_shBufferState;
        toCopy[1].stateAfter = RHIResourceState::CopyDst;
        cmdList->ResourceBarrier(toCopy, 2);
        cmdList->CopyBuffer(m_historyShBuffer, 0, m_shBuffer, 0,
                            kTotalProbes * kSHFloat4PerProbe * 4 * sizeof(float));
        toCopy[0].stateBefore = RHIResourceState::CopySrc;
        toCopy[0].stateAfter = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(toCopy, 1);
        m_historyShBufferState = RHIResourceState::ShaderResource;
        m_shBufferState = RHIResourceState::CopyDst;
    }

    // Transition SH buffer to UAV for compute read/write
    RHIBarrierDesc toUav{};
    toUav.buffer      = m_shBuffer;
    toUav.stateBefore = m_shBufferState;
    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&toUav, 1);

    // Set the trace pipeline
    cmdList->SetPipeline(tracePipeline);

    // ── Build trace constants ──────────────────────────────────────────────
    RadianceCacheTraceConstants constants{};
    constants.gridOriginX       = m_gridOrigin.x;
    constants.gridOriginY       = m_gridOrigin.y;
    constants.gridOriginZ       = m_gridOrigin.z;
    constants.probeSpacing      = m_probeSpacing;
    constants.probesPerAxis     = kProbesPerAxis;
    constants.totalProbes       = kTotalProbes;
    constants.raysPerProbe      = static_cast<uint32_t>(cv_RCRays->value);
    constants.frameIndex        = frameIndex;
    constants.maxTraceDistance   = m_gridExtent * 2.0f; // Trace up to the grid diameter
    constants.temporalBlendAlpha = kTemporalBlendAlpha;
    constants.shBufferUav       = m_shBufferUav;
    // The SH buffer is created unwritten; the first dispatch must replace
    // rather than blend with undefined contents.
    constants.firstWrite        = resetHistory ? 1u : 0u;

    // Fill GDF clipmap parameters from the global distance field
    const uint32_t numClipmaps = GlobalDistanceField::kNumClipmaps;
    constants.numGdfClipmaps = numClipmaps;

    for (uint32_t i = 0; i < numClipmaps && i < 4; ++i) {
        const auto& clip = gdf.GetClipmap(i);
        float* centerPtr = nullptr;
        float* extentPtr = nullptr;
        uint32_t* srvPtr = nullptr;

        switch (i) {
            case 0:
                centerPtr = &constants.gdfCenter0X;
                extentPtr = &constants.gdfExtent0;
                srvPtr    = &constants.gdfClipmapSrv0;
                break;
            case 1:
                centerPtr = &constants.gdfCenter1X;
                extentPtr = &constants.gdfExtent1;
                srvPtr    = &constants.gdfClipmapSrv1;
                break;
            case 2:
                centerPtr = &constants.gdfCenter2X;
                extentPtr = &constants.gdfExtent2;
                srvPtr    = &constants.gdfClipmapSrv2;
                break;
            case 3:
                centerPtr = &constants.gdfCenter3X;
                extentPtr = &constants.gdfExtent3;
                srvPtr    = &constants.gdfClipmapSrv3;
                break;
        }

        if (centerPtr && extentPtr && srvPtr) {
            centerPtr[0] = clip.center.x;
            centerPtr[1] = clip.center.y;
            centerPtr[2] = clip.center.z;
            *extentPtr    = clip.extent;
            *srvPtr       = clip.srvIndex;
        }
    }

    // Read light/sky data from PerFrameCB for accurate GI bounce
    const auto* pf = static_cast<const PerFrameCB*>(perFrameCpuPtr);

    // Sky color for missed rays — use sky atmosphere color scaled by intensity.
    // No attenuation factor — the probes should capture the full sky radiance.
    constants.skyColorR = pf->skyLightColorR * pf->skyLightIntensity;
    constants.skyColorG = pf->skyLightColorG * pf->skyLightIntensity;
    constants.skyColorB = pf->skyLightColorB * pf->skyLightIntensity;


    // Primary directional light for single-bounce GI at hit points
    constants.lightDirX     = pf->lightDir.x;
    constants.lightDirY     = pf->lightDir.y;
    constants.lightDirZ     = pf->lightDir.z;
    constants.lightColorR   = pf->lightColor.x;
    constants.lightColorG   = pf->lightColor.y;
    constants.lightColorB   = pf->lightColor.z;
    constants.lightIntensity = 1.0f;
    constants.groundAlbedo  = 0.18f; // Average scene albedo (18% grey card)

    // Temporal probe spreading: update kProbesPerFrame probes per frame,
    // wrapping the cursor across the grid. The shader skips probes outside
    // [g_probeOffset, g_probeOffset + g_probeBudget).
    const uint32_t probeBudget = resetHistory ? kTotalProbes
        : std::min<uint32_t>(static_cast<uint32_t>(cv_RCProbeBudget->value), kTotalProbes);
    const uint32_t probeOffset = resetHistory ? 0u : m_probeUpdateCursor;

    // Root constants at b0 (root param 0, 8 uint32s):
    //   [0] SH UAV index, [1] previous-frame SH SRV index, [2] frame index,
    //   [3] probe offset, [4] probe budget, [5] sky radiance SRV, [6..7] padding.
    uint32_t rootConstants[8] = {};
    rootConstants[0] = m_shBufferUav;
    rootConstants[1] = resetHistory ? UINT32_MAX : m_historyShBufferSrv;
    rootConstants[2] = frameIndex;
    rootConstants[3] = probeOffset;
    rootConstants[4] = probeBudget;
    rootConstants[5] = pf->prefilteredSrvIndex;
    if (m_diagnosticStats.IsValid() && !m_diagnosticComplete) {
        RHIBarrierDesc diagnosticBarrier{};
        diagnosticBarrier.buffer = m_diagnosticStats;
        diagnosticBarrier.stateBefore = m_diagnosticStatsState;
        diagnosticBarrier.stateAfter = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&diagnosticBarrier, 1);
        m_diagnosticStatsState = RHIResourceState::UnorderedAccess;
        rootConstants[6] = m_diagnosticStatsUav;
    }
    cmdList->SetRootConstants(0, rootConstants, 8);

    // Upload bulk trace constants via CB ring and bind at b2 (root param 2)
    std::memcpy(constAlloc.cpuPtr, &constants, sizeof(constants));
    cmdList->SetRootCBV(2, constAlloc.bufferHandle,
                        constAlloc.offsetBytes,
                        constAlloc.cpuPtr, sizeof(constants));

    // Dispatch only as many groups as needed for this frame's probe budget.
    // 64 threads per group (matches [numthreads(64,1,1)] in the trace shader).
    const uint32_t groupCount = (probeBudget + 63) / 64;
    cmdList->Dispatch(groupCount, 1, 1);

    // Transition back to SRV for lighting shader reads
    RHIBarrierDesc toSrv{};
    toSrv.buffer      = m_shBuffer;
    toSrv.stateBefore = RHIResourceState::UnorderedAccess;
    toSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toSrv, 1);
    m_shBufferState = RHIResourceState::ShaderResource;

    m_probeUpdateCursor = (probeOffset + probeBudget) % kTotalProbes;
    m_dirty = false;
    m_firstWritePending = false;
    QueueDiagnosticReadback(cmdList, frameIndex);
    if (m_diagnosticCopyFrame == frameIndex) {
        UZ_LOG(Renderer, Info, "RC diagnostic: frame={} pfEnabled={} pfSHSrv={} pfGiIntensity={} skySRV={} skyMultiplier=({},{},{}) lightColor=({},{},{}) lightDir=({},{},{}) probeOffset={} probeBudget={} firstWrite={}",
               frameIndex, pf->radianceCacheEnabled, pf->rcProbeSHBufferSrv, pf->rcGiIntensity, rootConstants[5],
               constants.skyColorR, constants.skyColorG, constants.skyColorB,
               constants.lightColorR, constants.lightColorG, constants.lightColorB,
               constants.lightDirX, constants.lightDirY, constants.lightDirZ, probeOffset, probeBudget, constants.firstWrite);
    }
}

} // namespace uz
