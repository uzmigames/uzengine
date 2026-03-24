/// UzEngine Scene Renderer — Temporal resource helpers (TAA, SSR, Motion Blur).

#include "scene_renderer.h"

#include "rhi.h"
#include "log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TAA Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateTaaResources(uint32_t width, uint32_t height) {
    DestroyTaaResources();

    for (int i = 0; i < 2; ++i) {
        RHITextureDesc taaDesc{};
        taaDesc.width     = width;
        taaDesc.height    = height;
        taaDesc.format    = RHIFormat::RGBA16Float;
        taaDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        taaDesc.debugName = (i == 0) ? "TAA_History_0" : "TAA_History_1";

        m_taaHistory[i] = m_device->CreateTexture(taaDesc);
        if (m_taaHistory[i].IsValid()) {
            m_taaHistorySrv[i] = m_device->GetTextureSRVIndex(m_taaHistory[i]);
            m_taaHistoryUav[i] = m_device->GetTextureUAVIndex(m_taaHistory[i]);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create TAA history texture {}", i);
        }
    }

    m_taaHistoryIndex = 0;
    m_prevViewProjValid = false;

    UZ_LOG(Renderer, Info, "SceneRenderer: TAA history textures created ({}x{}, RGBA16F)", width, height);
}

void SceneRenderer::DestroyTaaResources() {
    for (int i = 0; i < 2; ++i) {
        if (m_taaHistory[i].IsValid()) {
            m_device->DestroyTexture(m_taaHistory[i]);
            m_taaHistory[i] = RHITextureHandle{};
            m_taaHistorySrv[i] = UINT32_MAX;
            m_taaHistoryUav[i] = UINT32_MAX;
        }
    }
    m_taaHistoryIndex = 0;
    m_prevViewProjValid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSR Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateSsrResources(uint32_t width, uint32_t height) {
    DestroySsrResources();

    // SSR trace output: RGBA16F full-res, rgb=hit color, a=confidence
    {
        RHITextureDesc traceDesc{};
        traceDesc.width     = width;
        traceDesc.height    = height;
        traceDesc.format    = RHIFormat::RGBA16Float;
        traceDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        traceDesc.debugName = "SSR_TraceOutput";

        m_ssrTraceOutput = m_device->CreateTexture(traceDesc);
        if (m_ssrTraceOutput.IsValid()) {
            m_ssrTraceOutputSrv = m_device->GetTextureSRVIndex(m_ssrTraceOutput);
            m_ssrTraceOutputUav = m_device->GetTextureUAVIndex(m_ssrTraceOutput);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR trace output texture");
        }
    }

    // SSR resolved output: RGBA16F full-res, rgb=Fresnel-weighted reflection, a=blend weight
    {
        RHITextureDesc resolveDesc{};
        resolveDesc.width     = width;
        resolveDesc.height    = height;
        resolveDesc.format    = RHIFormat::RGBA16Float;
        resolveDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        resolveDesc.debugName = "SSR_ResolvedOutput";

        m_ssrResolvedOutput = m_device->CreateTexture(resolveDesc);
        if (m_ssrResolvedOutput.IsValid()) {
            m_ssrResolvedOutputSrv = m_device->GetTextureSRVIndex(m_ssrResolvedOutput);
            m_ssrResolvedOutputUav = m_device->GetTextureUAVIndex(m_ssrResolvedOutput);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR resolved output texture");
        }
    }

    // SSR temporal history: ping-pong RGBA16F pair for temporal accumulation.
    // Follows the same pattern as TAA history textures (m_taaHistory[2]).
    for (int i = 0; i < 2; ++i) {
        RHITextureDesc histDesc{};
        histDesc.width     = width;
        histDesc.height    = height;
        histDesc.format    = RHIFormat::RGBA16Float;
        histDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        histDesc.debugName = (i == 0) ? "SSR_History_0" : "SSR_History_1";

        m_ssrHistory[i] = m_device->CreateTexture(histDesc);
        if (m_ssrHistory[i].IsValid()) {
            m_ssrHistorySrv[i] = m_device->GetTextureSRVIndex(m_ssrHistory[i]);
            m_ssrHistoryUav[i] = m_device->GetTextureUAVIndex(m_ssrHistory[i]);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR history texture {}", i);
        }
    }
    m_ssrHistoryIndex = 0;

    UZ_LOG(Renderer, Info, "SceneRenderer: SSR resources created ({}x{}, RGBA16F, trace SRV={}, resolve SRV={}, hist0 SRV={}, hist1 SRV={})",
           width, height, m_ssrTraceOutputSrv, m_ssrResolvedOutputSrv, m_ssrHistorySrv[0], m_ssrHistorySrv[1]);
}

void SceneRenderer::DestroySsrResources() {
    if (m_ssrTraceOutput.IsValid()) {
        m_device->DestroyTexture(m_ssrTraceOutput);
        m_ssrTraceOutput = RHITextureHandle{};
        m_ssrTraceOutputSrv = UINT32_MAX;
        m_ssrTraceOutputUav = UINT32_MAX;
    }
    if (m_ssrResolvedOutput.IsValid()) {
        m_device->DestroyTexture(m_ssrResolvedOutput);
        m_ssrResolvedOutput = RHITextureHandle{};
        m_ssrResolvedOutputSrv = UINT32_MAX;
        m_ssrResolvedOutputUav = UINT32_MAX;
    }
    for (int i = 0; i < 2; ++i) {
        if (m_ssrHistory[i].IsValid()) {
            m_device->DestroyTexture(m_ssrHistory[i]);
            m_ssrHistory[i] = RHITextureHandle{};
            m_ssrHistorySrv[i] = UINT32_MAX;
            m_ssrHistoryUav[i] = UINT32_MAX;
        }
    }
    m_ssrHistoryIndex = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion Blur Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateMotionBlurResources(uint32_t width, uint32_t height) {
    DestroyMotionBlurResources();

    // ── Main blur output (full resolution) ───────────────────────────────
    RHITextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.format    = RHIFormat::RGBA16Float;
    desc.usage     = RHITextureUsage::ShaderResource
                   | RHITextureUsage::UnorderedAccess;
    desc.debugName = "MotionBlur_Output";

    m_motionBlurOutput = m_device->CreateTexture(desc);
    if (m_motionBlurOutput.IsValid()) {
        m_motionBlurOutputSrv = m_device->GetTextureSRVIndex(m_motionBlurOutput);
        m_motionBlurOutputUav = m_device->GetTextureUAVIndex(m_motionBlurOutput);
        UZ_LOG(Renderer, Info, "SceneRenderer: Motion blur output created ({}x{}, RGBA16F, SRV={}, UAV={})",
               width, height, m_motionBlurOutputSrv, m_motionBlurOutputUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create motion blur output texture");
    }

    // ── Tile max texture (1/16th resolution) ─────────────────────────────
    const uint32_t tileW = (width  + 15) / 16;
    const uint32_t tileH = (height + 15) / 16;

    RHITextureDesc tileDesc{};
    tileDesc.width     = tileW;
    tileDesc.height    = tileH;
    tileDesc.format    = RHIFormat::RG16Float;
    tileDesc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
    tileDesc.debugName = "MotionBlur_TileMax";

    m_motionBlurTileMax = m_device->CreateTexture(tileDesc);
    if (m_motionBlurTileMax.IsValid()) {
        m_motionBlurTileMaxSrv = m_device->GetTextureSRVIndex(m_motionBlurTileMax);
        m_motionBlurTileMaxUav = m_device->GetTextureUAVIndex(m_motionBlurTileMax);
        UZ_LOG(Renderer, Info, "SceneRenderer: Motion blur tile max created ({}x{}, RG16F, SRV={}, UAV={})",
               tileW, tileH, m_motionBlurTileMaxSrv, m_motionBlurTileMaxUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create motion blur tile max texture");
    }

    // ── Neighbor max texture (same tile resolution) ──────────────────────
    RHITextureDesc neighborDesc{};
    neighborDesc.width     = tileW;
    neighborDesc.height    = tileH;
    neighborDesc.format    = RHIFormat::RG16Float;
    neighborDesc.usage     = RHITextureUsage::ShaderResource
                           | RHITextureUsage::UnorderedAccess;
    neighborDesc.debugName = "MotionBlur_NeighborMax";

    m_motionBlurNeighborMax = m_device->CreateTexture(neighborDesc);
    if (m_motionBlurNeighborMax.IsValid()) {
        m_motionBlurNeighborMaxSrv = m_device->GetTextureSRVIndex(m_motionBlurNeighborMax);
        m_motionBlurNeighborMaxUav = m_device->GetTextureUAVIndex(m_motionBlurNeighborMax);
        UZ_LOG(Renderer, Info, "SceneRenderer: Motion blur neighbor max created ({}x{}, RG16F, SRV={}, UAV={})",
               tileW, tileH, m_motionBlurNeighborMaxSrv, m_motionBlurNeighborMaxUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create motion blur neighbor max texture");
    }
}

void SceneRenderer::DestroyMotionBlurResources() {
    if (m_motionBlurOutput.IsValid()) {
        m_device->DestroyTexture(m_motionBlurOutput);
        m_motionBlurOutput = RHITextureHandle{};
        m_motionBlurOutputSrv = UINT32_MAX;
        m_motionBlurOutputUav = UINT32_MAX;
    }
    if (m_motionBlurTileMax.IsValid()) {
        m_device->DestroyTexture(m_motionBlurTileMax);
        m_motionBlurTileMax = RHITextureHandle{};
        m_motionBlurTileMaxSrv = UINT32_MAX;
        m_motionBlurTileMaxUav = UINT32_MAX;
    }
    if (m_motionBlurNeighborMax.IsValid()) {
        m_device->DestroyTexture(m_motionBlurNeighborMax);
        m_motionBlurNeighborMax = RHITextureHandle{};
        m_motionBlurNeighborMaxSrv = UINT32_MAX;
        m_motionBlurNeighborMaxUav = UINT32_MAX;
    }
}

} // namespace uz
