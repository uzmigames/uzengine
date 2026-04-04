/// UzEngine SVGF Denoiser Pass — Implementation.

#include "svgf_pass.h"

#include "pipeline_library.h"
#include "cb_ring_allocator.h"
#include "root_slots.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

SVGFPass::~SVGFPass() {
    DestroyResources();
}

bool SVGFPass::CreateResources(IRHIDevice* device, uint32_t width, uint32_t height) {
    if (!device || width == 0 || height == 0) return false;

    m_device = device;
    m_width  = width;
    m_height = height;

    auto createTexSrvUav = [this](uint32_t w, uint32_t h, RHIFormat fmt,
                                   const char* name,
                                   RHITextureHandle& outTex,
                                   uint32_t& outSrv, uint32_t& outUav) -> bool {
        RHITextureDesc desc{};
        desc.width     = w;
        desc.height    = h;
        desc.format    = fmt;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        desc.debugName = name;
        outTex = m_device->CreateTexture(desc);
        if (!outTex.IsValid()) return false;
        outSrv = m_device->GetTextureSRVIndex(outTex);
        outUav = m_device->GetTextureUAVIndex(outTex);
        return true;
    };

    auto createTexSrvCopy = [this](uint32_t w, uint32_t h, RHIFormat fmt,
                                    const char* name,
                                    RHITextureHandle& outTex,
                                    uint32_t& outSrv) -> bool {
        RHITextureDesc desc{};
        desc.width     = w;
        desc.height    = h;
        desc.format    = fmt;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        desc.debugName = name;
        outTex = m_device->CreateTexture(desc);
        if (!outTex.IsValid()) return false;
        outSrv = m_device->GetTextureSRVIndex(outTex);
        return true;
    };

    if (!createTexSrvUav(width, height, RHIFormat::RGBA16Float,
                         "SVGF_ColorA", m_colorA, m_colorASrv, m_colorAUav))
        return false;
    if (!createTexSrvUav(width, height, RHIFormat::RGBA16Float,
                         "SVGF_ColorB", m_colorB, m_colorBSrv, m_colorBUav))
        return false;
    if (!createTexSrvUav(width, height, RHIFormat::RG16Float,
                         "SVGF_MomentsA", m_momentsA, m_momentsASrv, m_momentsAUav))
        return false;
    if (!createTexSrvUav(width, height, RHIFormat::RG16Float,
                         "SVGF_MomentsB", m_momentsB, m_momentsBSrv, m_momentsBUav))
        return false;
    if (!createTexSrvCopy(width, height, RHIFormat::RGBA16Float,
                          "SVGF_HistoryColor", m_historyColor, m_historyColorSrv))
        return false;
    if (!createTexSrvCopy(width, height, RHIFormat::RG16Float,
                          "SVGF_HistoryMoments", m_historyMoments, m_historyMomentsSrv))
        return false;

    m_initialized = true;
    m_hasHistory  = false;
    m_resultSrv   = m_colorASrv;

    UZ_LOG(Renderer, Info, "SVGFPass: initialized ({}x{}, {} a-trous iterations)",
           width, height, kAtrousIterations);
    return true;
}

void SVGFPass::DestroyResources() {
    if (!m_device) return;
    auto destroy = [this](RHITextureHandle& tex) {
        if (tex.IsValid()) { m_device->DestroyTexture(tex); tex = {}; }
    };
    destroy(m_colorA);    destroy(m_colorB);
    destroy(m_momentsA);  destroy(m_momentsB);
    destroy(m_historyColor);
    destroy(m_historyMoments);
    m_initialized = false;
    m_hasHistory  = false;
}

void SVGFPass::OnResize(uint32_t width, uint32_t height) {
    DestroyResources();
    if (m_device) CreateResources(m_device, width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Denoise — full SVGF pipeline dispatch
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SVGFPass::Denoise(IRHICommandList* cmdList, const SVGFRenderParams& params) {
    UZ_PROFILE_FUNCTION();

    if (!m_initialized || !params.pipelines) return UINT32_MAX;

    const uint32_t groupsX = (m_width  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_height + kThreadGroupSize - 1) / kThreadGroupSize;

    // ── Pass 1: Temporal Reprojection ────────────────────────────────────
    {
        UZ_PROFILE_SCOPE("SVGF::Reproject");

        auto pso = params.pipelines->GetSvgfReproject();
        if (!pso.IsValid()) return UINT32_MAX;

        // Transition outputs to UAV
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_colorA;
        barriers[0].stateBefore = RHIResourceState::ShaderResource;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_momentsA;
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(barriers, 2);

        cmdList->SetPipeline(pso);

        struct ReprojectRC {
            uint32_t currentColorSrv;
            uint32_t historyColorSrv;
            uint32_t historyMomentsSrv;
            uint32_t velocitySrv;
            uint32_t depthSrv;
            uint32_t outputColorUav;
            uint32_t outputMomentsUav;
            uint32_t packedDimensions;
        };
        static_assert(sizeof(ReprojectRC) == 32);

        ReprojectRC rc{};
        rc.currentColorSrv   = params.noisyColorSrv;
        rc.historyColorSrv   = m_hasHistory ? m_historyColorSrv : params.noisyColorSrv;
        rc.historyMomentsSrv = m_hasHistory ? m_historyMomentsSrv : m_momentsASrv;
        rc.velocitySrv       = params.velocitySrvIndex;
        rc.depthSrv          = params.depthSrvIndex;
        rc.outputColorUav    = m_colorAUav;
        rc.outputMomentsUav  = m_momentsAUav;
        rc.packedDimensions  = (m_width & 0xFFFF) | ((m_height & 0xFFFF) << 16);

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);
        cmdList->SetDescriptorTable(4, m_colorAUav);
        cmdList->Dispatch(groupsX, groupsY, 1);

        // Transition outputs: UAV → SRV
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[1].stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(barriers, 2);
    }

    // ── Pass 2: Filter Moments ───────────────────────────────────────────
    {
        UZ_PROFILE_SCOPE("SVGF::FilterMoments");

        auto pso = params.pipelines->GetSvgfFilterMoments();
        if (!pso.IsValid()) return m_colorASrv;

        RHIBarrierDesc barrier{};
        barrier.texture     = m_momentsB;
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);

        cmdList->SetPipeline(pso);

        struct FilterMomRC {
            uint32_t momentsSrv;
            uint32_t depthSrv;
            uint32_t normalSrv;
            uint32_t outputMomentsUav;
            uint32_t _pad0;
            uint32_t _pad1;
            uint32_t texWidth;
            uint32_t texHeight;
        };
        static_assert(sizeof(FilterMomRC) == 32);

        FilterMomRC rc{};
        rc.momentsSrv       = m_momentsASrv;
        rc.depthSrv         = params.depthSrvIndex;
        rc.normalSrv        = params.normalSrvIndex;
        rc.outputMomentsUav = m_momentsBUav;
        rc.texWidth         = m_width;
        rc.texHeight        = m_height;

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);
        cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                            params.perFrameOffset, params.perFrameCpuPtr,
                            params.perFrameSize);
        cmdList->SetDescriptorTable(4, m_momentsBUav);
        cmdList->Dispatch(groupsX, groupsY, 1);

        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ── Passes 3-7: À-Trous Wavelet Filter (5 iterations) ───────────────
    {
        auto pso = params.pipelines->GetSvgfAtrous();
        if (!pso.IsValid()) return m_colorASrv;

        // Ping-pong: even iterations read A→write B, odd read B→write A
        const uint32_t srvs[2]  = { m_colorASrv, m_colorBSrv };
        const uint32_t uavs[2]  = { m_colorAUav, m_colorBUav };
        const RHITextureHandle texs[2] = { m_colorA, m_colorB };

        static const uint32_t kStepSizes[5] = {1, 2, 4, 8, 16};

        uint32_t readIdx  = 0; // Start reading from colorA (reproject output)
        uint32_t writeIdx = 1;

        for (uint32_t iter = 0; iter < kAtrousIterations; ++iter) {
            UZ_PROFILE_SCOPE("SVGF::ATrous");

            RHIBarrierDesc barrier{};
            barrier.texture     = texs[writeIdx];
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmdList->ResourceBarrier(&barrier, 1);

            cmdList->SetPipeline(pso);

            struct AtrousRC {
                uint32_t inputColorSrv;
                uint32_t momentsSrv;
                uint32_t depthSrv;
                uint32_t normalSrv;
                uint32_t outputColorUav;
                uint32_t stepSize;
                uint32_t texWidth;
                uint32_t texHeight;
                uint32_t sigmaDepthBits;
                uint32_t sigmaNormalBits;
                uint32_t sigmaLuminanceBits;
            };
            static_assert(sizeof(AtrousRC) == 44);

            static auto* cv_SVGFSigmaD = UZ_CVAR_FLOAT("r.SVGF.SigmaDepth", 1.0f, "SVGF edge-stopping depth sigma", 0.1f, 10.0f);
            static auto* cv_SVGFSigmaN = UZ_CVAR_FLOAT("r.SVGF.SigmaNormal", 128.0f, "SVGF edge-stopping normal sigma", 8.0f, 512.0f);
            static auto* cv_SVGFSigmaL = UZ_CVAR_FLOAT("r.SVGF.SigmaLuminance", 4.0f, "SVGF edge-stopping luminance sigma", 0.5f, 16.0f);

            AtrousRC rc{};
            rc.inputColorSrv  = srvs[readIdx];
            rc.momentsSrv     = m_momentsBSrv;
            rc.depthSrv       = params.depthSrvIndex;
            rc.normalSrv      = params.normalSrvIndex;
            rc.outputColorUav = uavs[writeIdx];
            rc.stepSize       = kStepSizes[iter];
            rc.texWidth       = m_width;
            rc.texHeight      = m_height;
            float sd = cv_SVGFSigmaD->value, sn = cv_SVGFSigmaN->value, sl = cv_SVGFSigmaL->value;
            std::memcpy(&rc.sigmaDepthBits, &sd, 4);
            std::memcpy(&rc.sigmaNormalBits, &sn, 4);
            std::memcpy(&rc.sigmaLuminanceBits, &sl, 4);

            cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 11);
            cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                                params.perFrameOffset, params.perFrameCpuPtr,
                                params.perFrameSize);
            cmdList->SetDescriptorTable(4, uavs[writeIdx]);
            cmdList->Dispatch(groupsX, groupsY, 1);

            barrier.stateBefore = RHIResourceState::UnorderedAccess;
            barrier.stateAfter  = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&barrier, 1);

            // Swap ping-pong
            uint32_t tmp = readIdx;
            readIdx  = writeIdx;
            writeIdx = tmp;
        }

        // Result is in the last written texture (now readIdx after final swap)
        m_resultSrv = srvs[readIdx];
    }

    m_hasHistory = true;
    return m_resultSrv;
}

} // namespace uz
