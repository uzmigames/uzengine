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

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_RadianceCache    = UZ_CVAR_BOOL("r.RadianceCache", false, "SDF radiance cache GI");
static auto* cv_RCGridExtent     = UZ_CVAR_FLOAT("r.GI.RadianceCache.GridExtent", 5000.f, "Probe grid half-extent (cm)", 100.f, 50000.f);

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

    if (m_initialized) {
        DestroyResources();
    }

    m_device      = device;
    m_gridExtent  = worldExtent;
    // N probes per axis span the full grid with probes ON both boundaries:
    // spacing = size / (N - 1). This MUST match the deferred lighting
    // trilinear mapping `cell = (pos - origin) / size * (N - 1)` — the
    // previous `size / N` spacing drifted up to a full cell at the far edge.
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
                      | RHIBufferUsage::UnorderedAccess;
    bufDesc.debugName = "RadianceCache_SH";

    m_shBuffer = device->CreateBuffer(bufDesc);
    if (!m_shBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to create SH buffer ({} bytes)", bufferSize);
        return false;
    }

    m_shBufferSrv = device->GetBufferSRVIndex(m_shBuffer);
    m_shBufferUav = device->GetBufferUAVIndex(m_shBuffer);

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
    if (!m_device) return;

    if (m_shBuffer.IsValid()) {
        m_device->DestroyBuffer(m_shBuffer);
        m_shBuffer    = RHIBufferHandle{};
        m_shBufferSrv = UINT32_MAX;
        m_shBufferUav = UINT32_MAX;
    }

    m_initialized = false;
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
    // movemask extracts sign bits; GreaterOrEqual sets all bits for true lanes.
    if (_mm_movemask_ps(vCmp) & 0x7) {
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
    uint32_t frameIndex,
    const Vec3& sceneAlbedo,
    uint32_t skyCubemapSrv)
{
    if (!m_initialized || !cmdList || !tracePipeline.IsValid() || !cbRing) {
        return;
    }

    if (!gdf.IsInitialized()) {
        return;
    }

    m_frameCounter = frameIndex;

    // Transition SH buffer to UAV for compute read/write
    RHIBarrierDesc toUav{};
    toUav.buffer      = m_shBuffer;
    toUav.stateBefore = RHIResourceState::ShaderResource;
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
    constants.raysPerProbe      = kRaysPerProbe;
    constants.frameIndex        = frameIndex;
    constants.maxTraceDistance   = m_gridExtent * 2.0f; // Trace up to the grid diameter
    constants.temporalBlendAlpha = kTemporalBlendAlpha;
    constants.shBufferUav       = m_shBufferUav;

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
    constants.groundAlbedo  = 0.18f; // Legacy scalar — shader now uses sceneAlbedo below.

    // Surface-cache-lite hit inputs (gi_hit_lighting.hlsli). The albedo is
    // the draw-list material average computed by the caller; the sky cubemap
    // gives directional miss radiance instead of a flat color.
    constants.sceneAlbedoR  = sceneAlbedo.x;
    constants.sceneAlbedoG  = sceneAlbedo.y;
    constants.sceneAlbedoB  = sceneAlbedo.z;
    constants.skyCubemapSrv = skyCubemapSrv;

    // Temporal probe spreading: update kProbesPerFrame probes per frame,
    // wrapping the cursor across the grid. The shader skips probes outside
    // [g_probeOffset, g_probeOffset + g_probeBudget).
    const uint32_t probeBudget =
        std::min<uint32_t>(kProbesPerFrame, kTotalProbes);
    const uint32_t probeOffset = m_probeUpdateCursor % kTotalProbes;
    m_probeUpdateCursor = (m_probeUpdateCursor + probeBudget) % kTotalProbes;

    // Root constants at b0 (root param 0, 8 uint32s):
    //   [0] SH UAV index, [1] SH SRV index, [2] frame index,
    //   [3] probe offset, [4] probe budget, [5..7] padding.
    uint32_t rootConstants[8] = {};
    rootConstants[0] = m_shBufferUav;
    rootConstants[1] = m_shBufferSrv;
    rootConstants[2] = frameIndex;
    rootConstants[3] = probeOffset;
    rootConstants[4] = probeBudget;
    cmdList->SetRootConstants(0, rootConstants, 8);

    // Upload bulk trace constants via CB ring and bind at b2 (root param 2)
    CBAllocation constAlloc = cbRing->Allocate(sizeof(RadianceCacheTraceConstants));
    if (!constAlloc.cpuPtr) {
        UZ_LOG(Renderer, Error, "RadianceCache: failed to allocate trace constants");
        return;
    }
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

    m_dirty = false;
}

} // namespace uz
