/// UzEngine DDGISystem — Implementation.
///
/// Allocates two 2D atlas textures sized for `kTotalProbes` probe tiles and
/// registers their bindless SRV/UAV slots. Resource layout matches the DDGI
/// compute shader and the deferred lighting Chebyshev sampler.

#include "ddgi_system.h"
#include "global_distance_field.h"
#include <algorithm>

#include "radiance_cache.h"     // RadianceCacheTraceConstants — shared CB layout
#include "cb_ring_allocator.h"
#include "constant_buffers.h"  // PerFrameCB — sun/sky read for probe lighting
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <cmath>
#include <limits>

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

static auto* cv_DdgiDirectionalSky = UZ_CVAR_BOOL("r.GI.DDGI.DirectionalSky", true,
    "Escaping DDGI rays sample the sky cubemap in the ray direction, as the SDF radiance cache does. "
    "0 restores the flat sky colour this path used before followup-01, for an A/B on one binary.");

static auto* cv_DdgiDiagnostic = UZ_CVAR_BOOL("r.GI.DDGI.Diagnostic", false,
    "Read back and log the DDGI writer's per-probe mean ray radiance once after frame 120 (pairs with r.GI.RadianceCache.Diagnostic)");

void DDGISystem::DispatchUpdate(IRHICommandList* cmdList,
                                RHIPipelineHandle ddgiPipeline,
                                RHIPipelineHandle borderPipeline,
                                uint32_t          tlasSrvIndex,
                                CBRingAllocator*  cbRing,
                                uint32_t          frameIndex,
                                const GlobalDistanceField* gdf,
                                const void*       perFrameCpuPtr,
                                uint32_t          vfxEmissiveSrvIndex,
                                uint32_t          vfxEmissiveSampleCount) {
    if (!m_initialized || !cmdList || !ddgiPipeline.IsValid() || !cbRing) {
        return;
    }

    ReadDiagnosticResults(frameIndex);
    if (cv_DdgiDiagnostic->value && !m_diagnosticStats.IsValid() && !m_diagnosticComplete) {
        RHIBufferDesc statsDesc{};
        // Two float4 per texel: (lum, sky fraction, sky-only lum, back-face fraction over the hit
        // rays) and the hit side
        // (mean hit distance, lit fraction, mean albedo luminance, hit count).
        statsDesc.sizeBytes = kTotalProbes * kIrradianceProbeSize * kIrradianceProbeSize * 8 * sizeof(float);
        statsDesc.stride    = 4 * sizeof(float);
        statsDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySrc;
        statsDesc.debugName = "DDGI_TransportDiagnostics";
        m_diagnosticStats = m_device->CreateBuffer(statsDesc);
        if (m_diagnosticStats.IsValid()) m_diagnosticStatsUav = m_device->GetBufferUAVIndex(m_diagnosticStats);
        else m_diagnosticComplete = true;
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
    // followup-01: this was hardcoded to 32 while the SDF radiance-cache path read
    // r.GI.RadianceCache.RaysPerProbe, so the two GI paths could silently disagree on ray
    // count — one of the energy differences that task is about. Read through the registry
    // rather than re-registering the name, which would collide with the cache's own
    // declaration. The default is 32, so this changes nothing until the CVar is moved.
    constants.raysPerProbe      = static_cast<uint32_t>(
        CVarRegistry::Get().GetInt("r.GI.RadianceCache.RaysPerProbe", 32));
    m_lastRaysPerProbe = constants.raysPerProbe;
    constants.frameIndex        = frameIndex;
    constants.maxTraceDistance  = m_probeSpacing
                                * static_cast<float>(kProbesPerAxis) * 2.0f;
    constants.temporalBlendAlpha = 0.12f;
    constants.shBufferUav       = UINT32_MAX;
    constants.groundAlbedo      = 0.18f; // fallback outside every GDF cascade

    // ── Surface attributes: the global distance field ────────────────────
    // Mirrors RadianceCache::UpdateProbes so both probe writers shade the same
    // surfaces the same way; DXR only decides visibility.
    constants.numGdfClipmaps = 0;
    if (gdf && gdf->IsInitialized()) {
        const uint32_t numClipmaps =
            std::min<uint32_t>(GlobalDistanceField::kNumClipmaps, 4u);
        constants.numGdfClipmaps = numClipmaps;
        float*    centers[4] = {&constants.gdfCenter0X, &constants.gdfCenter1X,
                                &constants.gdfCenter2X, &constants.gdfCenter3X};
        float*    extents[4] = {&constants.gdfExtent0, &constants.gdfExtent1,
                                &constants.gdfExtent2, &constants.gdfExtent3};
        uint32_t* srvs[4]    = {&constants.gdfClipmapSrv0, &constants.gdfClipmapSrv1,
                                &constants.gdfClipmapSrv2, &constants.gdfClipmapSrv3};
        for (uint32_t i = 0; i < numClipmaps; ++i) {
            const auto& clip = gdf->GetClipmap(i);
            centers[i][0] = clip.center.x;
            centers[i][1] = clip.center.y;
            centers[i][2] = clip.center.z;
            *extents[i]   = clip.extent;
            *srvs[i]      = clip.srvIndex;
        }
    }

    // ── Lighting environment: the scene's sun and sky ────────────────────
    // Same fields RadianceCache::UpdateProbes reads. The previous hardcoded
    // values (sun +Z at intensity 1, sky 0.6/0.7/0.9) had no relation to the
    // loaded scene, which is why the DXR path's probe energy did not match the
    // SDF path's on Sponza.
    // The sky cubemap the SDF radiance cache uses for escaping rays (radiance_cache.cpp passes the
    // same field). UINT32_MAX leaves the shader on its flat sky colour.
    uint32_t skyRadianceSrv = UINT32_MAX;
    if (perFrameCpuPtr) {
        const auto* pf = static_cast<const PerFrameCB*>(perFrameCpuPtr);
        skyRadianceSrv           = cv_DdgiDirectionalSky->value != 0.0f ? pf->prefilteredSrvIndex
                                                                       : UINT32_MAX;
        m_lastSkyRadianceSrv = skyRadianceSrv;
        constants.skyColorR      = pf->skyLightColorR * pf->skyLightIntensity;
        constants.skyColorG      = pf->skyLightColorG * pf->skyLightIntensity;
        constants.skyColorB      = pf->skyLightColorB * pf->skyLightIntensity;
        constants.lightDirX      = pf->lightDir.x;
        constants.lightDirY      = pf->lightDir.y;
        constants.lightDirZ      = pf->lightDir.z;
        constants.lightColorR    = pf->lightColor.x;
        constants.lightColorG    = pf->lightColor.y;
        constants.lightColorB    = pf->lightColor.z;
        constants.lightIntensity = 1.0f;
        // For the identity log line: after the assignment, not before it.
        m_lastSkyColor[0] = constants.skyColorR; m_lastSkyColor[1] = constants.skyColorG;
        m_lastSkyColor[2] = constants.skyColorB;
        // Below the assignments above, for the reason the sky multiplier is: reading a
        // constant into the identity line before it is written reports a zero that is not one.
        m_lastLightColor[0] = constants.lightColorR; m_lastLightColor[1] = constants.lightColorG;
        m_lastLightColor[2] = constants.lightColorB;
        m_lastLightDir[0] = constants.lightDirX; m_lastLightDir[1] = constants.lightDirY;
        m_lastLightDir[2] = constants.lightDirZ;
        m_lastLightIntensity = constants.lightIntensity;
    } else {
        constants.skyColorR      = 0.0f;
        constants.skyColorG      = 0.0f;
        constants.skyColorB      = 0.0f;
        constants.lightDirX      = 0.0f;
        constants.lightDirY      = 0.0f;
        constants.lightDirZ      = 1.0f;
        constants.lightIntensity = 0.0f;
        constants.lightColorR    = 0.0f;
        constants.lightColorG    = 0.0f;
        constants.lightColorB    = 0.0f;
    }

    // VFX emissive feed — populated by `GatherAndUploadVfxEmissiveSamples`
    // earlier in the frame and threaded through the caller. SRV index
    // UINT32_MAX (default arg) when no emitters were alive or the
    // `r.VFX.DDGIInjection` CVar is off; the in-shader gate
    // (`vfxEmissiveSampleCount > 0 && srv != 0xFFFFFFFF`) short-circuits
    // the accumulation loop in `ddgi_probe_update_cs.hlsl` in that case.
    constants.vfxEmissiveSrvIndex   = vfxEmissiveSrvIndex;
    constants.vfxEmissiveSampleCount = vfxEmissiveSampleCount;
    constants._vfxEmissivePad0      = 0.0f;
    constants._vfxEmissivePad1      = 0.0f;

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

    // Border fill (H-55): the deferred lighting samples each 6x6 / 16x16 tile bilinearly and
    // reaches the 1-texel border for every normal within half a texel of a tile edge. The
    // interior pass never writes that border, so it is filled here from the interior after each
    // pass, behind a UAV barrier, before the transition the lighting reads through.
    auto borderFill = [&](uint32_t atlasUav, uint32_t tileSize, uint32_t mode, uint32_t clear) {
        cmdList->SetPipeline(borderPipeline);
        const uint32_t rc[5] = {atlasUav, kProbesPerAxis, tileSize, mode, clear};
        cmdList->SetRootConstants(0, rc, 5);
        const uint32_t stride = tileSize + 2u;
        const uint32_t w = kProbesPerAxis * kProbesPerAxis * stride;
        const uint32_t h = kProbesPerAxis * stride;
        cmdList->Dispatch((w + 7u) / 8u, (h + 7u) / 8u, 1);
    };
    auto uavSync = [&]() {
        RHIBarrierDesc sync[2]{};
        sync[0].texture     = m_irradAtlas;
        sync[0].stateBefore = RHIResourceState::UnorderedAccess;
        sync[0].stateAfter  = RHIResourceState::UnorderedAccess;
        sync[1].texture     = m_depthAtlas;
        sync[1].stateBefore = RHIResourceState::UnorderedAccess;
        sync[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(sync, 2);
    };
    if (!m_atlasCleared) {
        // Defined initial contents: the RHI has no UAV clear, so the fill shader writes zeros.
        borderFill(m_irradUav, kIrradianceProbeSize, 0u, 1u);
        borderFill(m_depthUav, kDepthProbeSize, 1u, 1u);
        uavSync();
        m_atlasCleared = true;
    }

    auto dispatch = [&](uint32_t atlasMode, uint32_t tileSize) {
        cmdList->SetPipeline(ddgiPipeline);
        // Root constants at b0:
        //   [0] irrad UAV, [1] depth UAV, [2] tlas SRV, [3] atlas mode,
        //   [4] frame index, [5] probes per axis, [6] probe offset,
        //   [7] sky radiance cubemap SRV, [8] diagnostic UAV.
        uint32_t rootConstants[15] = {};
        rootConstants[0] = m_irradUav;
        rootConstants[1] = m_depthUav;
        rootConstants[2] = tlasSrvIndex;
        rootConstants[3] = atlasMode;
        rootConstants[4] = frameIndex;
        rootConstants[5] = kProbesPerAxis;
        rootConstants[6] = 0;          // full grid each frame for now
        rootConstants[7] = skyRadianceSrv;
        rootConstants[8] = (atlasMode == 0u && m_diagnosticStats.IsValid() && !m_diagnosticComplete)
                         ? m_diagnosticStatsUav : UINT32_MAX;
        // Known-radiance diagnostic (followup-01 consumer stage); the CVars belong to the cache.
        {
            const float knownL = CVarRegistry::Get().GetFloat("r.GI.Diagnostic.KnownRadiance", 0.0f);
            std::memcpy(&rootConstants[9], &knownL, sizeof(float));
            rootConstants[10] = CVarRegistry::Get().GetBool("r.GI.Diagnostic.KnownRadianceKeepDepth", false) ? 1u : 0u;
            rootConstants[11] = static_cast<uint32_t>(CVarRegistry::Get().GetInt("r.GI.Diagnostic.KnownRadianceMode", 0));
            const float knownD = CVarRegistry::Get().GetFloat("r.GI.Diagnostic.KnownDistance", 0.0f);
            std::memcpy(&rootConstants[12], &knownD, sizeof(float));
            rootConstants[13] = static_cast<uint32_t>(CVarRegistry::Get().GetInt("r.GI.Diagnostic.KnownDistanceMode", 0));
            rootConstants[14] = CVarRegistry::Get().GetBool("r.GI.Diagnostic.DdgiFarDepth", false) ? 1u : 0u;
        }
        cmdList->SetRootConstants(0, rootConstants, 15);

        const uint32_t stride = tileSize + 2u;
        const uint32_t dispatchX = kProbesPerAxis * kProbesPerAxis * stride;
        const uint32_t dispatchY = kProbesPerAxis * stride;
        const uint32_t groupsX = (dispatchX + 7u) / 8u;
        const uint32_t groupsY = (dispatchY + 7u) / 8u;
        cmdList->Dispatch(groupsX, groupsY, 1);
    };

    // Pass 1: irradiance atlas
    dispatch(0u, kIrradianceProbeSize);
    QueueDiagnosticReadback(cmdList, frameIndex);

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

    // Irradiance border, from the interior just written.
    borderFill(m_irradUav, kIrradianceProbeSize, 0u, 0u);
    uavSync();

    // Pass 2: depth atlas
    dispatch(1u, kDepthProbeSize);

    // Depth border, same rule: the Chebyshev test samples the moments bilinearly too.
    uavSync();
    borderFill(m_depthUav, kDepthProbeSize, 1u, 0u);

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

void DDGISystem::QueueDiagnosticReadback(IRHICommandList* cmdList, uint32_t frameIndex) {
    if (!m_diagnosticStats.IsValid() || m_diagnosticComplete || m_diagnosticReadback.IsValid() || frameIndex < 120) return;
    RHIBufferDesc desc{};
    // Must match the stats buffer exactly (8 floats per texel: two float4 records). It read 2
    // for three changes, so only the first quarter of the probes was ever copied back and the
    // rest arrived zero-filled — which reads exactly like a probe that recorded nothing.
    desc.sizeBytes = kTotalProbes * kIrradianceProbeSize * kIrradianceProbeSize * 8 * sizeof(float);
    desc.memory    = RHIMemoryType::Readback;
    desc.debugName = "DDGI_TransportReadback";
    m_diagnosticReadback = m_device->CreateBuffer(desc);
    if (!m_diagnosticReadback.IsValid()) {
        UZ_LOG(Renderer, Error, "DDGI transport: readback allocation failed");
        m_diagnosticComplete = true;
        return;
    }
    RHIBarrierDesc barrier{};
    barrier.buffer      = m_diagnosticStats;
    barrier.stateBefore = RHIResourceState::UnorderedAccess;
    barrier.stateAfter  = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&barrier, 1);
    cmdList->CopyBuffer(m_diagnosticStats, 0, m_diagnosticReadback, 0, desc.sizeBytes);
    barrier.stateBefore = RHIResourceState::CopySrc;
    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&barrier, 1);
    m_diagnosticCopyFrame = frameIndex;
    UZ_LOG(Renderer, Info, "DDGI transport: queued diagnostic snapshot frame={} origin=({},{},{}) spacing={}",
           frameIndex, m_gridOrigin.x, m_gridOrigin.y, m_gridOrigin.z, m_probeSpacing);
    UZ_LOG(Renderer, Info, "DDGI transport: identity skySrv={} mip=0 swizzle=(x,z,y) rays={} sequence=stratified-frame-probe-rotation skyMultiplier=({},{},{}) lightColor=({},{},{}) lightDir=({},{},{}) lightIntensity={} indirectTerm=none knownRadiance={} keepDepth={} knownMode={} knownDistance={} knownDistanceMode={} farDepth={}",
           m_lastSkyRadianceSrv, m_lastRaysPerProbe, m_lastSkyColor[0], m_lastSkyColor[1], m_lastSkyColor[2],
           m_lastLightColor[0], m_lastLightColor[1], m_lastLightColor[2],
           m_lastLightDir[0], m_lastLightDir[1], m_lastLightDir[2], m_lastLightIntensity,
           CVarRegistry::Get().GetFloat("r.GI.Diagnostic.KnownRadiance", 0.0f),
           CVarRegistry::Get().GetBool("r.GI.Diagnostic.KnownRadianceKeepDepth", false) ? 1 : 0,
           CVarRegistry::Get().GetInt("r.GI.Diagnostic.KnownRadianceMode", 0),
           CVarRegistry::Get().GetFloat("r.GI.Diagnostic.KnownDistance", 0.0f),
           CVarRegistry::Get().GetInt("r.GI.Diagnostic.KnownDistanceMode", 0),
           CVarRegistry::Get().GetBool("r.GI.Diagnostic.DdgiFarDepth", false) ? 1 : 0);
}

void DDGISystem::ReadDiagnosticResults(uint32_t frameIndex) {
    if (!m_diagnosticReadback.IsValid() || m_diagnosticComplete || frameIndex - m_diagnosticCopyFrame < 4) return;
    m_device->WaitIdle();
    m_diagnosticComplete = true;
    const auto* data = static_cast<const float*>(m_device->MapBuffer(m_diagnosticReadback));
    if (!data) {
        UZ_LOG(Renderer, Error, "DDGI transport: readback map failed");
        return;
    }
    constexpr uint32_t kTexels = kIrradianceProbeSize * kIrradianceProbeSize;
    double gridLum = 0.0, gridSky = 0.0; uint32_t gridValid = 0;
    for (uint32_t probe = 0; probe < kTotalProbes; ++probe) {
        double lum = 0.0, sky = 0.0, skyLum = 0.0; uint32_t valid = 0;
        double hitDist = 0.0, hitLit = 0.0, hitAlbedo = 0.0, hitCount = 0.0, hitBack = 0.0; uint32_t hitValid = 0;
        for (uint32_t t = 0; t < kTexels; ++t) {
            const float* rec = data + (probe * kTexels + t) * 8;
            const float l  = rec[0];
            const float s  = rec[1];
            const float sl = rec[2];
            if (!std::isfinite(l) || !std::isfinite(s) || !std::isfinite(sl)) continue;
            lum += l; sky += s; skyLum += sl; ++valid;
            // Hit side: every texel traced the same rays, so these agree across texels; the mean
            // over the valid ones is taken for the same reason the three above are.
            if (std::isfinite(rec[3]) && std::isfinite(rec[4]) && std::isfinite(rec[5]) && std::isfinite(rec[6]) && std::isfinite(rec[7])) {
                hitDist += rec[4]; hitLit += rec[5]; hitAlbedo += rec[6]; hitCount += rec[7]; hitBack += rec[3]; ++hitValid;
            }
        }
        const double invHit = hitValid > 0 ? 1.0 / static_cast<double>(hitValid) : 0.0;
        if (valid == 0) continue;
        gridLum += lum / valid; gridSky += sky / valid; ++gridValid;
        const uint32_t x = probe % kProbesPerAxis;
        const uint32_t y = (probe / kProbesPerAxis) % kProbesPerAxis;
        const uint32_t z = probe / (kProbesPerAxis * kProbesPerAxis);
        // Every probe, one line each, so the per-probe comparison with the SDF writer can pair them
        // by index (both lay probes out x-fastest, then y, then z; DDGI copies the cache's origin
        // and spacing). Same two statistics as "RC transport: probe=..." (followup-01, 2026-09-16).
        UZ_LOG(Renderer, Info, "DDGI transport: probe={} cell=({},{},{}) world=({},{},{}) skyFraction={} meanRadiance={} skyLum={} hitDist={} hitLitFraction={} hitAlbedo={} hitCount={} hitBackFraction={}",
               probe, x, y, z, m_gridOrigin.x + x * m_probeSpacing, m_gridOrigin.y + y * m_probeSpacing,
               m_gridOrigin.z + z * m_probeSpacing, sky / valid, lum / valid, skyLum / valid,
               hitDist * invHit, hitLit * invHit, hitAlbedo * invHit, hitCount * invHit, hitBack * invHit);
    }
    UZ_LOG(Renderer, Info, "DDGI transport: grid snapshotFrame={} readFrame={} probes={} meanRadiance={} skyFraction={}",
           m_diagnosticCopyFrame, frameIndex, gridValid,
           gridValid ? gridLum / gridValid : 0.0, gridValid ? gridSky / gridValid : 0.0);
    m_device->UnmapBuffer(m_diagnosticReadback);
}

void DDGISystem::DestroyResources() {
    if (m_device) {
        if (m_irradAtlas.IsValid()) {
            m_device->DestroyTexture(m_irradAtlas);
        }
        if (m_depthAtlas.IsValid()) {
            m_device->DestroyTexture(m_depthAtlas);
        }
        if (m_diagnosticStats.IsValid()) m_device->DestroyBuffer(m_diagnosticStats);
        if (m_diagnosticReadback.IsValid()) m_device->DestroyBuffer(m_diagnosticReadback);
    }
    m_diagnosticStats = {};
    m_diagnosticReadback = {};
    m_diagnosticStatsUav = UINT32_MAX;
    m_diagnosticCopyFrame = UINT32_MAX;
    m_diagnosticComplete = false;
    m_irradAtlas = {};
    m_depthAtlas = {};
    m_irradSrv = m_depthSrv = UINT32_MAX;
    m_irradUav = m_depthUav = UINT32_MAX;
    m_initialized = false;
}

} // namespace uz
