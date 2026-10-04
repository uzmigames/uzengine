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

/// Creates the r.TAA.ClampFreezeAfterFrame storage on demand, and releases it when the CVar is
/// turned off. The diagnostic is off by default, and an unused diagnostic must not cost 31.6 MiB
/// of video memory or a write per pixel per frame.
void SceneRenderer::EnsureTaaClampBoxResources(bool wanted) {
    const bool have = m_taaClampBox[0].IsValid() && m_taaClampBox[1].IsValid();
    if (wanted == have) return;
    if (!wanted) {
        for (int i = 0; i < 2; ++i) {
            if (m_taaClampBox[i].IsValid()) {
                m_device->DestroyTexture(m_taaClampBox[i]);
                m_taaClampBox[i] = RHITextureHandle{};
                m_taaClampBoxSrv[i] = UINT32_MAX;
                m_taaClampBoxUav[i] = UINT32_MAX;
            }
        }
        m_taaClampBoxInitialised = false;
        return;
    }
    for (int i = 0; i < 2; ++i) {
        RHITextureDesc boxDesc{};
        boxDesc.width     = m_width;
        boxDesc.height    = m_height;
        boxDesc.format    = RHIFormat::RGBA16Float;
        boxDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        boxDesc.debugName = (i == 0) ? "TAA_ClampBox_Min" : "TAA_ClampBox_Max";
        m_taaClampBox[i] = m_device->CreateTexture(boxDesc);
        if (m_taaClampBox[i].IsValid()) {
            m_taaClampBoxSrv[i] = m_device->GetTextureSRVIndex(m_taaClampBox[i]);
            m_taaClampBoxUav[i] = m_device->GetTextureUAVIndex(m_taaClampBox[i]);
        }
    }
    m_taaClampBoxInitialised = false;
    UZ_LOG(Renderer, Info, "TAA clamp-box diagnostic storage created ({}x{})", m_width, m_height);
}

/// Loads the component-replay buffer, or releases it when the path is cleared.
///
/// The file is a flat float4 array: element 0 is the rectangle (x, y, w, h), element 1 the frame
/// range (firstFrame, frameCount, 0, 0), then frameCount * h * w entries of (specular.rgb, ao).
/// Everything the shader needs travels in the buffer, because only one 4-byte slot was left in
/// PerFrameCB. Diagnostic, off by default; nothing is allocated while it is off.
void SceneRenderer::EnsureComponentReplay(const std::string& path) {
    if (path == m_componentReplayLoaded) return;
    if (m_componentReplay.IsValid()) {
        m_device->DestroyBuffer(m_componentReplay);
        m_componentReplay = RHIBufferHandle{};
        m_componentReplaySrv = UINT32_MAX;
    }
    m_componentReplayLoaded = path;
    if (path.empty()) return;

    FILE* fh = std::fopen(path.c_str(), "rb");
    if (!fh) {
        UZ_LOG(Renderer, Error, "ComponentReplay: cannot open '{}'", path);
        m_componentReplayLoaded.clear();
        return;
    }
    std::fseek(fh, 0, SEEK_END);
    const long bytes = std::ftell(fh);
    std::fseek(fh, 0, SEEK_SET);
    std::vector<float> data(static_cast<size_t>(bytes > 0 ? bytes : 0) / sizeof(float));
    const size_t got = data.empty() ? 0 : std::fread(data.data(), sizeof(float), data.size(), fh);
    std::fclose(fh);
    if (data.size() < 8 || got != data.size()) {
        UZ_LOG(Renderer, Error, "ComponentReplay: '{}' is truncated or empty", path);
        m_componentReplayLoaded.clear();
        return;
    }
    const uint32_t rw = static_cast<uint32_t>(data[2]);
    const uint32_t rh = static_cast<uint32_t>(data[3]);
    const uint32_t frames = static_cast<uint32_t>(data[5]);
    const size_t expect = 8 + static_cast<size_t>(frames) * rh * rw * 4;
    if (data.size() != expect) {
        UZ_LOG(Renderer, Error,
               "ComponentReplay: '{}' holds {} floats but its header describes {}",
               path, data.size(), expect);
        m_componentReplayLoaded.clear();
        return;
    }

    RHIBufferDesc desc{};
    desc.sizeBytes = data.size() * sizeof(float);
    desc.stride    = sizeof(float) * 4;
    desc.usage     = RHIBufferUsage::StructuredBuffer;
    desc.memory    = RHIMemoryType::Upload;
    desc.debugName = "GI_ComponentReplay";
    m_componentReplay = m_device->CreateBuffer(desc);
    if (!m_componentReplay.IsValid()) {
        UZ_LOG(Renderer, Error, "ComponentReplay: buffer creation failed");
        m_componentReplayLoaded.clear();
        return;
    }
    if (void* mapped = m_device->MapBuffer(m_componentReplay)) {
        std::memcpy(mapped, data.data(), desc.sizeBytes);
        m_device->UnmapBuffer(m_componentReplay);
    }
    m_componentReplaySrv = m_device->GetBufferSRVIndex(m_componentReplay);
    UZ_LOG(Renderer, Info, "ComponentReplay: '{}' loaded ({}x{}, {} frames from {}), srv={}",
           path, rw, rh, frames, static_cast<uint32_t>(data[4]), m_componentReplaySrv);
}

void SceneRenderer::DestroyTaaResources() {
    for (int i = 0; i < 2; ++i) {
        if (m_taaHistory[i].IsValid()) {
            m_device->DestroyTexture(m_taaHistory[i]);
            m_taaHistory[i] = RHITextureHandle{};
            m_taaHistorySrv[i] = UINT32_MAX;
            m_taaHistoryUav[i] = UINT32_MAX;
        }
        if (m_taaClampBox[i].IsValid()) {
            m_device->DestroyTexture(m_taaClampBox[i]);
            m_taaClampBox[i] = RHITextureHandle{};
            m_taaClampBoxSrv[i] = UINT32_MAX;
            m_taaClampBoxUav[i] = UINT32_MAX;
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

    // ── Tile classification buffers ─────────────────────────────────────
    // @ref AMD FidelityFX SSSR — ray list + counter/args for indirect dispatch.
    {
        // Ray list: one uint per pixel (worst case all pixels need rays)
        RHIBufferDesc rayListDesc{};
        rayListDesc.sizeBytes = static_cast<uint64_t>(width) * height * sizeof(uint32_t);
        rayListDesc.stride    = sizeof(uint32_t);
        rayListDesc.usage     = RHIBufferUsage::StructuredBuffer
                              | RHIBufferUsage::UnorderedAccess;
        rayListDesc.memory    = RHIMemoryType::Default;
        rayListDesc.debugName = "SSR_RayList";

        m_ssrRayList = m_device->CreateBuffer(rayListDesc);
        if (m_ssrRayList.IsValid()) {
            m_ssrRayListSrv = m_device->GetBufferSRVIndex(m_ssrRayList);
            m_ssrRayListUav = m_device->GetBufferUAVIndex(m_ssrRayList);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR ray list buffer");
        }

        // Ray counter + indirect dispatch args: 16 bytes
        // [0] = rayCount (uint32), [4..15] = DispatchIndirectArgs {X, Y, Z}
        RHIBufferDesc argsDesc{};
        argsDesc.sizeBytes = 16;
        argsDesc.stride    = 0;  // raw/byte-address buffer
        argsDesc.usage     = RHIBufferUsage::UnorderedAccess
                           | RHIBufferUsage::IndirectArgs;
        argsDesc.memory    = RHIMemoryType::Default;
        argsDesc.debugName = "SSR_RayCounterArgs";

        m_ssrRayCounterArgs = m_device->CreateBuffer(argsDesc);
        if (m_ssrRayCounterArgs.IsValid()) {
            m_ssrRayCounterArgsUav = m_device->GetBufferUAVIndex(m_ssrRayCounterArgs);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR ray counter buffer");
        }
    }

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

    // SSR hit distance: R16F full-res — stores screen-space ray t for virtual hit reprojection.
    // @ref AMD FidelityFX SSSR — separate hit distance for temporal reprojection.
    {
        RHITextureDesc hitDesc{};
        hitDesc.width     = width;
        hitDesc.height    = height;
        hitDesc.format    = RHIFormat::R16Float;
        hitDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        hitDesc.debugName = "SSR_HitDistance";

        m_ssrHitDistance = m_device->CreateTexture(hitDesc);
        if (m_ssrHitDistance.IsValid()) {
            m_ssrHitDistanceSrv = m_device->GetTextureSRVIndex(m_ssrHitDistance);
            m_ssrHitDistanceUav = m_device->GetTextureUAVIndex(m_ssrHitDistance);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR hit distance texture");
        }
    }

    // Previous frame HDR scene color copy — SSR trace reads this instead of
    // the live HDR buffer to break the feedback loop (UE5 pattern).
    // @ref UE5 DeferredShadingRenderer.cpp — PrevFrameViewInfo.ScreenSpaceRayTracingInput
    {
        RHITextureDesc prevDesc{};
        prevDesc.width     = width;
        prevDesc.height    = height;
        prevDesc.format    = RHIFormat::RGBA16Float;
        prevDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        prevDesc.debugName = "SSR_PrevSceneColor";

        m_ssrPrevSceneColor = m_device->CreateTexture(prevDesc);
        if (m_ssrPrevSceneColor.IsValid()) {
            m_ssrPrevSceneColorSrv = m_device->GetTextureSRVIndex(m_ssrPrevSceneColor);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create SSR prev scene color texture");
        }
    }
    m_ssrHasPrevColor = false;

    // ── Translucent scene color copy (screen-space refraction) ──────────
    // @ref Three.js transmissionRenderTarget — copy of opaque scene for glass.
    {
        RHITextureDesc transDesc{};
        transDesc.width     = width;
        transDesc.height    = height;
        transDesc.format    = RHIFormat::RGBA16Float;
        transDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        transDesc.debugName = "TranslucentSceneColor";

        m_translucentSceneColor = m_device->CreateTexture(transDesc);
        if (m_translucentSceneColor.IsValid()) {
            m_translucentSceneColorSrv = m_device->GetTextureSRVIndex(m_translucentSceneColor);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create translucent scene color texture");
        }
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: SSR resources created ({}x{}, RGBA16F, trace SRV={}, resolve SRV={}, hist0 SRV={}, hist1 SRV={}, prevColor SRV={})",
           width, height, m_ssrTraceOutputSrv, m_ssrResolvedOutputSrv, m_ssrHistorySrv[0], m_ssrHistorySrv[1], m_ssrPrevSceneColorSrv);
}

void SceneRenderer::DestroySsrResources() {
    if (m_ssrRayList.IsValid()) {
        m_device->DestroyBuffer(m_ssrRayList);
        m_ssrRayList = RHIBufferHandle{};
        m_ssrRayListSrv = UINT32_MAX;
        m_ssrRayListUav = UINT32_MAX;
    }
    if (m_ssrRayCounterArgs.IsValid()) {
        m_device->DestroyBuffer(m_ssrRayCounterArgs);
        m_ssrRayCounterArgs = RHIBufferHandle{};
        m_ssrRayCounterArgsUav = UINT32_MAX;
    }

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

    if (m_ssrHitDistance.IsValid()) {
        m_device->DestroyTexture(m_ssrHitDistance);
        m_ssrHitDistance = RHITextureHandle{};
        m_ssrHitDistanceSrv = UINT32_MAX;
        m_ssrHitDistanceUav = UINT32_MAX;
    }

    if (m_ssrPrevSceneColor.IsValid()) {
        m_device->DestroyTexture(m_ssrPrevSceneColor);
        m_ssrPrevSceneColor = RHITextureHandle{};
        m_ssrPrevSceneColorSrv = UINT32_MAX;
    }
    m_ssrHasPrevColor = false;

    if (m_translucentSceneColor.IsValid()) {
        m_device->DestroyTexture(m_translucentSceneColor);
        m_translucentSceneColor = RHITextureHandle{};
        m_translucentSceneColorSrv = UINT32_MAX;
    }
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
