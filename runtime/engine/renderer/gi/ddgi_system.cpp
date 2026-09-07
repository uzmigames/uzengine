/// UzEngine DDGISystem — Implementation.
///
/// Allocates two 2D atlas textures sized for `kTotalProbes` probe tiles and
/// registers their bindless SRV/UAV slots. Resource layout matches the DDGI
/// compute shader and the deferred lighting Chebyshev sampler.

#include "ddgi_system.h"

#include "radiance_cache.h"     // RadianceCacheTraceConstants — shared CB layout
#include "cb_ring_allocator.h"
#include "log.h"
#include "rhi.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

DDGISystem::~DDGISystem() {
    DestroyResources();
}

bool DDGISystem::CreateResources(IRHIDevice* device) {
    if (!device) {
        UZ_LOG(Renderer, Error, "DDGISystem::CreateResources: null device");
        return false;
    }
    if (m_initialized) {
        DestroyResources();
    }

    m_device = device;

    // ── Irradiance atlas (RGBA16F) ───────────────────────────────────
    // Layout: width = probesPerAxis * probesPerAxis * (size + 2)
    //         height = probesPerAxis * (size + 2)
    // The +2 is the 1-texel border on each side that gives the bilinear
    // sampler a wrap-around at octahedral edges.
    {
        RHITextureDesc desc{};
        desc.width     = GetIrradianceAtlasWidth();
        desc.height    = GetIrradianceAtlasHeight();
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "DDGI_IrradianceAtlas";

        m_irradAtlas = m_device->CreateTexture(desc);
        if (!m_irradAtlas.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "DDGISystem: failed to create irradiance atlas {}x{}",
                   desc.width, desc.height);
            return false;
        }
        m_irradSrv = m_device->GetTextureSRVIndex(m_irradAtlas);
        m_irradUav = m_device->GetTextureUAVIndex(m_irradAtlas);
    }

    // ── Depth atlas (RG16F: mean, mean²) ─────────────────────────────
    {
        RHITextureDesc desc{};
        desc.width     = GetDepthAtlasWidth();
        desc.height    = GetDepthAtlasHeight();
        desc.format    = RHIFormat::RG16Float;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "DDGI_DepthAtlas";

        m_depthAtlas = m_device->CreateTexture(desc);
        if (!m_depthAtlas.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "DDGISystem: failed to create depth atlas {}x{}",
                   desc.width, desc.height);
            m_device->DestroyTexture(m_irradAtlas);
            m_irradAtlas = {};
            m_irradSrv   = UINT32_MAX;
            m_irradUav   = UINT32_MAX;
            return false;
        }
        m_depthSrv = m_device->GetTextureSRVIndex(m_depthAtlas);
        m_depthUav = m_device->GetTextureUAVIndex(m_depthAtlas);
    }

    UZ_LOG(Renderer, Info,
           "DDGISystem: allocated irrad {}x{} (SRV={}, UAV={}), depth {}x{} (SRV={}, UAV={})",
           GetIrradianceAtlasWidth(),  GetIrradianceAtlasHeight(),
           m_irradSrv, m_irradUav,
           GetDepthAtlasWidth(),       GetDepthAtlasHeight(),
           m_depthSrv, m_depthUav);

    m_initialized = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// DispatchUpdate
// ─────────────────────────────────────────────────────────────────────────

void DDGISystem::DispatchUpdate(IRHICommandList* cmdList,
                                RHIPipelineHandle ddgiPipeline,
                                uint32_t          tlasSrvIndex,
                                CBRingAllocator*  cbRing,
                                uint32_t          frameIndex) {
    if (!m_initialized || !cmdList || !ddgiPipeline.IsValid() || !cbRing) {
        return;
    }

    // Build the trace constants — same struct as the SDF radiance cache so
    // both shaders share the b2 CB layout.
    RadianceCacheTraceConstants constants{};
    constants.gridOriginX       = m_gridOrigin.x;
    constants.gridOriginY       = m_gridOrigin.y;
    constants.gridOriginZ       = m_gridOrigin.z;
    constants.probeSpacing      = m_probeSpacing;
    constants.probesPerAxis     = kProbesPerAxis;
    constants.totalProbes       = kTotalProbes;
    constants.raysPerProbe      = 32;
    constants.frameIndex        = frameIndex;
    constants.maxTraceDistance  = m_probeSpacing
                                * static_cast<float>(kProbesPerAxis) * 2.0f;
    constants.temporalBlendAlpha = 0.12f;
    constants.shBufferUav       = UINT32_MAX;
    constants.numGdfClipmaps    = 0;
    constants.skyColorR         = 0.6f;
    constants.skyColorG         = 0.7f;
    constants.skyColorB         = 0.9f;
    constants.lightDirX         = 0.0f;
    constants.lightDirY         = 0.0f;
    constants.lightDirZ         = 1.0f;
    constants.lightIntensity    = 1.0f;
    constants.lightColorR       = 1.0f;
    constants.lightColorG       = 1.0f;
    constants.lightColorB       = 1.0f;
    constants.groundAlbedo      = 0.18f;

    // Bind the pipeline once for both passes.
    cmdList->SetPipeline(ddgiPipeline);

    // Upload trace constants once and reuse for both atlas dispatches.
    CBAllocation constAlloc = cbRing->Allocate(sizeof(RadianceCacheTraceConstants));
    if (!constAlloc.cpuPtr) return;
    std::memcpy(constAlloc.cpuPtr, &constants, sizeof(constants));
    cmdList->SetRootCBV(2, constAlloc.bufferHandle,
                        constAlloc.offsetBytes,
                        constAlloc.cpuPtr, sizeof(constants));

    // Transition both atlases to UAV state. First dispatch comes from
    // Common (initial CreateTexture state); subsequent frames come back
    // from ShaderResource after deferred lighting reads them.
    const RHIResourceState beforeState = m_firstDispatch
        ? RHIResourceState::Common
        : RHIResourceState::ShaderResource;
    m_firstDispatch = false;

    RHIBarrierDesc toUav[2]{};
    toUav[0].texture     = m_irradAtlas;
    toUav[0].stateBefore = beforeState;
    toUav[0].stateAfter  = RHIResourceState::UnorderedAccess;
    toUav[1].texture     = m_depthAtlas;
    toUav[1].stateBefore = beforeState;
    toUav[1].stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(toUav, 2);

    auto dispatch = [&](uint32_t atlasMode, uint32_t tileSize) {
        // Root constants at b0:
        //   [0] irrad UAV, [1] depth UAV, [2] tlas SRV, [3] atlas mode,
        //   [4] frame index, [5] probes per axis, [6] probe offset,
        //   [7] probe budget.
        uint32_t rootConstants[8] = {};
        rootConstants[0] = m_irradUav;
        rootConstants[1] = m_depthUav;
        rootConstants[2] = tlasSrvIndex;
        rootConstants[3] = atlasMode;
        rootConstants[4] = frameIndex;
        rootConstants[5] = kProbesPerAxis;
        rootConstants[6] = 0;          // full grid each frame for now
        rootConstants[7] = kTotalProbes;
        cmdList->SetRootConstants(0, rootConstants, 8);

        const uint32_t stride = tileSize + 2u;
        const uint32_t dispatchX = kProbesPerAxis * kProbesPerAxis * stride;
        const uint32_t dispatchY = kProbesPerAxis * stride;
        const uint32_t groupsX = (dispatchX + 7u) / 8u;
        const uint32_t groupsY = (dispatchY + 7u) / 8u;
        cmdList->Dispatch(groupsX, groupsY, 1);
    };

    // Pass 1: irradiance atlas
    dispatch(0u, kIrradianceProbeSize);

    // UAV barrier between the two passes — both write to the same atlases
    // (the depth pass touches the depth atlas exclusively but we play it
    // safe across the irrad UAV as well).
    RHIBarrierDesc uavBarrier[2]{};
    uavBarrier[0].texture     = m_irradAtlas;
    uavBarrier[0].stateBefore = RHIResourceState::UnorderedAccess;
    uavBarrier[0].stateAfter  = RHIResourceState::UnorderedAccess;
    uavBarrier[1].texture     = m_depthAtlas;
    uavBarrier[1].stateBefore = RHIResourceState::UnorderedAccess;
    uavBarrier[1].stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(uavBarrier, 2);

    // Pass 2: depth atlas
    dispatch(1u, kDepthProbeSize);

    // Transition back to SRV for deferred lighting reads.
    RHIBarrierDesc toSrv[2]{};
    toSrv[0].texture     = m_irradAtlas;
    toSrv[0].stateBefore = RHIResourceState::UnorderedAccess;
    toSrv[0].stateAfter  = RHIResourceState::ShaderResource;
    toSrv[1].texture     = m_depthAtlas;
    toSrv[1].stateBefore = RHIResourceState::UnorderedAccess;
    toSrv[1].stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(toSrv, 2);

    m_firstUpdateComplete = true;
}

void DDGISystem::DestroyResources() {
    if (m_device) {
        if (m_irradAtlas.IsValid()) {
            m_device->DestroyTexture(m_irradAtlas);
        }
        if (m_depthAtlas.IsValid()) {
            m_device->DestroyTexture(m_depthAtlas);
        }
    }
    m_irradAtlas = {};
    m_depthAtlas = {};
    m_irradSrv = m_depthSrv = UINT32_MAX;
    m_irradUav = m_depthUav = UINT32_MAX;
    m_initialized = false;
}

} // namespace uz
