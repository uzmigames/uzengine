/// UzEngine SMAA Pass — Implementation.
///
/// Compiles 3 graphics pipelines (fullscreen_vs + edge/blend/resolve PS),
/// creates lookup textures from precomputed C arrays (AreaTex.h, SearchTex.h),
/// and dispatches the full 3-pass SMAA pipeline each frame.
///
/// @ref Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing"

#include "smaa_pass.h"

#include "engine/rhi/rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "upload_context.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"
#include "root_slots.h"
#include "cb_ring_allocator.h"

// SMAA precomputed lookup texture data (C arrays).
// These are large (~180KB total) so only included in this .cpp file.
// The include path "thirdparty/smaa" is in the build system's thirdparty_include_dirs.
#include "Textures/AreaTex.h"
#include "Textures/SearchTex.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_SMAA = UZ_CVAR_BOOL("r.SMAA", false, "SMAA anti-aliasing");

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SMAA RT Metrics constant buffer — matches cbuffer SMAAConstants : register(b2)
// ─────────────────────────────────────────────────────────────────────────────

/// Root CBV data for SMAA shaders (4 floats = 16 bytes, padded to 256 for CBV alignment).
struct SMAAConstantsCB {
    float invWidth;   ///< 1.0f / width
    float invHeight;  ///< 1.0f / height
    float width;      ///< viewport width
    float height;     ///< viewport height
};

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

SMAAPass::~SMAAPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool SMAAPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                    uint32_t width, uint32_t height)
{
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "SMAAPass::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "SMAAPass::Init failed: device is null");
        return false;
    }
    if (!shaderMgr) {
        UZ_LOG(Renderer, Error, "SMAAPass::Init failed: shaderMgr is null");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;
    m_width     = width;
    m_height    = height;

    // Create precomputed SMAA lookup textures (area + search).
    if (!CreateLookupTextures()) {
        UZ_LOG(Renderer, Error, "SMAAPass::Init failed: lookup texture creation failed");
        return false;
    }

    // Create intermediate render targets (edges + blend weights).
    if (!CreateIntermediateTextures(width, height)) {
        UZ_LOG(Renderer, Error, "SMAAPass::Init failed: intermediate texture creation failed");
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "SMAAPass initialized ({}x{}, area={}x{}, search={}x{})",
           width, height, 160, 560, 64, 16);
    return true;
}

void SMAAPass::Shutdown() {
    DestroyIntermediateTextures();

    if (m_device) {
        // Lookup textures
        if (m_areaTex.IsValid())   m_device->DestroyTexture(m_areaTex);
        if (m_searchTex.IsValid()) m_device->DestroyTexture(m_searchTex);

        // Pipelines
        if (m_edgePipeline.IsValid())    m_device->DestroyPipeline(m_edgePipeline);
        if (m_blendPipeline.IsValid())   m_device->DestroyPipeline(m_blendPipeline);
        if (m_resolvePipeline.IsValid()) m_device->DestroyPipeline(m_resolvePipeline);

        // Shaders
        if (m_fullscreenVS.IsValid()) m_device->DestroyShader(m_fullscreenVS);
        if (m_edgePS.IsValid())       m_device->DestroyShader(m_edgePS);
        if (m_blendPS.IsValid())      m_device->DestroyShader(m_blendPS);
        if (m_resolvePS.IsValid())    m_device->DestroyShader(m_resolvePS);
    }

    m_areaTex   = RHITextureHandle::Invalid();
    m_searchTex = RHITextureHandle::Invalid();
    m_areaSrvIdx   = UINT32_MAX;
    m_searchSrvIdx = UINT32_MAX;

    m_edgePipeline    = RHIPipelineHandle::Invalid();
    m_blendPipeline   = RHIPipelineHandle::Invalid();
    m_resolvePipeline = RHIPipelineHandle::Invalid();

    m_fullscreenVS = RHIShaderHandle::Invalid();
    m_edgePS       = RHIShaderHandle::Invalid();
    m_blendPS      = RHIShaderHandle::Invalid();
    m_resolvePS    = RHIShaderHandle::Invalid();

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "SMAAPass shut down");
}

void SMAAPass::Resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) {
        return;
    }

    m_width  = width;
    m_height = height;

    DestroyIntermediateTextures();
    if (!CreateIntermediateTextures(width, height)) {
        UZ_LOG(Renderer, Error, "SMAAPass: failed to recreate intermediate textures on resize");
        return;
    }

    UZ_LOG(Renderer, Info, "SMAAPass resized to {}x{}", width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup Texture Creation
// ─────────────────────────────────────────────────────────────────────────────

/// Area texture dimensions and data from precomputed C array.
/// @ref Jimenez 2012 SMAA — AreaTex.h
static constexpr uint32_t kAreaTexWidth  = 160;
static constexpr uint32_t kAreaTexHeight = 560;

/// Search texture dimensions and data from precomputed C array.
/// @ref Jimenez 2012 SMAA — SearchTex.h
static constexpr uint32_t kSearchTexWidth  = 64;
static constexpr uint32_t kSearchTexHeight = 16;

bool SMAAPass::CreateLookupTextures() {
    // ── Area Texture: 160x560, RG8_UNORM ──────────────────────────────
    {
        RHITextureDesc desc{};
        desc.width     = kAreaTexWidth;
        desc.height    = kAreaTexHeight;
        desc.format    = RHIFormat::RG8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        desc.debugName = "SMAA_AreaTex";

        m_areaTex = m_device->CreateTexture(desc);
        if (!m_areaTex.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to create area texture");
            return false;
        }
        m_areaSrvIdx = m_device->GetTextureSRVIndex(m_areaTex);
    }

    // ── Search Texture: 64x16, R8_UNORM ──────────────────────────────
    {
        RHITextureDesc desc{};
        desc.width     = kSearchTexWidth;
        desc.height    = kSearchTexHeight;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        desc.debugName = "SMAA_SearchTex";

        m_searchTex = m_device->CreateTexture(desc);
        if (!m_searchTex.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to create search texture");
            return false;
        }
        m_searchSrvIdx = m_device->GetTextureSRVIndex(m_searchTex);
    }

    UZ_LOG(Renderer, Info, "SMAAPass: created lookup textures (area SRV={}, search SRV={})",
           m_areaSrvIdx, m_searchSrvIdx);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup Texture Upload
// ─────────────────────────────────────────────────────────────────────────────

void SMAAPass::UploadLookupTextures(IRHICommandList* cmdList) {
    if (m_lookupUploaded) return;
    if (!m_initialized) return;

    UZ_PROFILE_SCOPE("SMAA::UploadLookupTextures");

    UploadContext upload(m_device);

    // Area texture: 160x560, RG8_UNORM (2 bytes per texel).
    // @ref Jimenez 2012 SMAA — AreaTex.h: AREATEX_SIZE = 160 * 560 * 2 = 179,200 bytes
    upload.UploadTexture(cmdList, m_areaTex, areaTexBytes, AREATEX_SIZE);

    // Search texture: 64x16, R8_UNORM (1 byte per texel).
    // @ref Jimenez 2012 SMAA — SearchTex.h: SEARCHTEX_SIZE = 64 * 16 = 1,024 bytes
    upload.UploadTexture(cmdList, m_searchTex, searchTexBytes, SEARCHTEX_SIZE);

    // Submit the copy commands, wait for GPU to finish, then release staging.
    cmdList->End();
    cmdList->Execute();
    m_device->WaitIdle();
    upload.ReleaseStaging();
    cmdList->Begin();

    m_lookupUploaded = true;
    UZ_LOG(Renderer, Info, "SMAAPass: uploaded lookup textures (area={} bytes, search={} bytes)",
           AREATEX_SIZE, SEARCHTEX_SIZE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Intermediate Texture Management
// ─────────────────────────────────────────────────────────────────────────────

bool SMAAPass::CreateIntermediateTextures(uint32_t width, uint32_t height) {
    // ── Edges: RG8_UNORM, full resolution ─────────────────────────────
    {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::RG8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
        desc.debugName = "SMAA_Edges";

        m_edgesTex = m_device->CreateTexture(desc);
        if (!m_edgesTex.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to create edges texture ({}x{})", width, height);
            return false;
        }
        m_edgesSrvIdx = m_device->GetTextureSRVIndex(m_edgesTex);
    }

    // ── Blend weights: RGBA8_UNORM, full resolution ───────────────────
    {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::RGBA8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;
        desc.debugName = "SMAA_BlendWeights";

        m_blendTex = m_device->CreateTexture(desc);
        if (!m_blendTex.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to create blend texture ({}x{})", width, height);
            return false;
        }
        m_blendSrvIdx = m_device->GetTextureSRVIndex(m_blendTex);
    }

    UZ_LOG(Renderer, Info, "SMAAPass: created intermediate textures {}x{} (edges SRV={}, blend SRV={})",
           width, height, m_edgesSrvIdx, m_blendSrvIdx);
    return true;
}

void SMAAPass::DestroyIntermediateTextures() {
    if (!m_device) return;

    if (m_edgesTex.IsValid()) {
        m_device->DestroyTexture(m_edgesTex);
        m_edgesTex = RHITextureHandle::Invalid();
        m_edgesSrvIdx = UINT32_MAX;
    }

    if (m_blendTex.IsValid()) {
        m_device->DestroyTexture(m_blendTex);
        m_blendTex = RHITextureHandle::Invalid();
        m_blendSrvIdx = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool SMAAPass::EnsureEdgeDetectionPipeline() {
    if (m_edgePipeline.IsValid()) return true;

    // Fullscreen VS (shared across all SMAA passes)
    if (!m_fullscreenVS.IsValid()) {
        m_fullscreenVS = m_shaderMgr->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        if (!m_fullscreenVS.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to compile FullscreenVS");
            return false;
        }
    }

    // Edge detection PS
    if (!m_edgePS.IsValid()) {
        m_edgePS = m_shaderMgr->CreateShader(EngineShader::SmaaEdgePS, RHIShaderStage::Pixel);
        if (!m_edgePS.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to compile SmaaEdgePS");
            return false;
        }
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader                 = m_fullscreenVS;
    desc.pixelShader                  = m_edgePS;
    desc.topology                     = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode          = RHICullMode::None;
    desc.depthStencil.depthEnable     = false;
    desc.depthStencil.depthWrite      = false;
    desc.renderTargetFormats[0]       = RHIFormat::RG8Unorm;
    desc.renderTargetCount            = 1;
    desc.debugName                    = "SMAA_EdgeDetection";

    m_edgePipeline = m_device->CreateGraphicsPipeline(desc);
    if (!m_edgePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "SMAAPass: failed to create edge detection pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "SMAAPass: created edge detection pipeline");
    return true;
}

bool SMAAPass::EnsureBlendWeightPipeline() {
    if (m_blendPipeline.IsValid()) return true;

    // Fullscreen VS (shared)
    if (!m_fullscreenVS.IsValid()) {
        m_fullscreenVS = m_shaderMgr->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        if (!m_fullscreenVS.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to compile FullscreenVS");
            return false;
        }
    }

    // Blend weight PS
    if (!m_blendPS.IsValid()) {
        m_blendPS = m_shaderMgr->CreateShader(EngineShader::SmaaBlendPS, RHIShaderStage::Pixel);
        if (!m_blendPS.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to compile SmaaBlendPS");
            return false;
        }
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader                 = m_fullscreenVS;
    desc.pixelShader                  = m_blendPS;
    desc.topology                     = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode          = RHICullMode::None;
    desc.depthStencil.depthEnable     = false;
    desc.depthStencil.depthWrite      = false;
    desc.renderTargetFormats[0]       = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount            = 1;
    desc.debugName                    = "SMAA_BlendWeight";

    m_blendPipeline = m_device->CreateGraphicsPipeline(desc);
    if (!m_blendPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "SMAAPass: failed to create blend weight pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "SMAAPass: created blend weight pipeline");
    return true;
}

bool SMAAPass::EnsureNeighborhoodBlendPipeline() {
    if (m_resolvePipeline.IsValid()) return true;

    // Fullscreen VS (shared)
    if (!m_fullscreenVS.IsValid()) {
        m_fullscreenVS = m_shaderMgr->CreateShader(EngineShader::FullscreenVS, RHIShaderStage::Vertex);
        if (!m_fullscreenVS.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to compile FullscreenVS");
            return false;
        }
    }

    // Neighborhood blend PS
    if (!m_resolvePS.IsValid()) {
        m_resolvePS = m_shaderMgr->CreateShader(EngineShader::SmaaResolvePS, RHIShaderStage::Pixel);
        if (!m_resolvePS.IsValid()) {
            UZ_LOG(Renderer, Error, "SMAAPass: failed to compile SmaaResolvePS");
            return false;
        }
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader                 = m_fullscreenVS;
    desc.pixelShader                  = m_resolvePS;
    desc.topology                     = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode          = RHICullMode::None;
    desc.depthStencil.depthEnable     = false;
    desc.depthStencil.depthWrite      = false;
    desc.renderTargetFormats[0]       = RHIFormat::RGBA8Unorm;
    desc.renderTargetCount            = 1;
    desc.debugName                    = "SMAA_NeighborhoodBlend";

    m_resolvePipeline = m_device->CreateGraphicsPipeline(desc);
    if (!m_resolvePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "SMAAPass: failed to create neighborhood blend pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "SMAAPass: created neighborhood blend pipeline");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

void SMAAPass::Execute(IRHICommandList* cmdList,
                       uint32_t inputColorSrvIndex,
                       RHITextureHandle inputColorTexture,
                       RHITextureHandle outputRTHandle,
                       uint32_t width, uint32_t height)
{
    UZ_PROFILE_FUNCTION();

    if (!m_initialized) return;
    if (cv_SMAA->value < 0.5f && !settings.enabled) return;

    // Lazy upload lookup textures on first Execute() if not already uploaded.
    if (!m_lookupUploaded) {
        UploadLookupTextures(cmdList);
    }

    // Lazily create pipelines on first Execute().
    if (!EnsureEdgeDetectionPipeline() ||
        !EnsureBlendWeightPipeline() ||
        !EnsureNeighborhoodBlendPipeline()) {
        UZ_LOG(Renderer, Warning, "SMAAPass::Execute: pipeline creation failed, skipping");
        return;
    }

    // Resize if dimensions changed.
    if (width != m_width || height != m_height) {
        Resize(width, height);
    }

    // SMAA RT metrics: float4(1/w, 1/h, w, h).
    const float fWidth  = static_cast<float>(width);
    const float fHeight = static_cast<float>(height);
    SMAAConstantsCB smaaCB{};
    smaaCB.invWidth  = 1.0f / fWidth;
    smaaCB.invHeight = 1.0f / fHeight;
    smaaCB.width     = fWidth;
    smaaCB.height    = fHeight;

    // Viewport and scissor (shared across all 3 passes).
    RHIViewport vp{};
    vp.width    = width;
    vp.height   = height;
    vp.maxDepth = 1.0f;

    RHIScissorRect sc{};
    sc.right  = static_cast<int32_t>(width);
    sc.bottom = static_cast<int32_t>(height);

    // ── Pass 1: Edge Detection ──────────────────────────────────────────
    // Input: LDR scene color (t0, SRV state)
    // Output: edges texture (RG8, RT state → cleared to black first)
    {
        UZ_PROFILE_SCOPE("SMAA::EdgeDetection");

        // Transition edges texture to render target.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_edgesTex;
            barrier.buffer      = RHIBufferHandle::Invalid();
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::RenderTarget;
            cmdList->ResourceBarrier(&barrier, 1);
        }

        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount          = 1;
        passDesc.colorAttachments[0].texture   = m_edgesTex;
        passDesc.colorAttachments[0].clear     = true;
        passDesc.colorAttachments[0].clearColor[0] = 0.0f;
        passDesc.colorAttachments[0].clearColor[1] = 0.0f;
        passDesc.colorAttachments[0].clearColor[2] = 0.0f;
        passDesc.colorAttachments[0].clearColor[3] = 0.0f;
        cmdList->BeginRenderPass(passDesc);

        cmdList->SetPipeline(m_edgePipeline);
        cmdList->SetViewport(vp);
        cmdList->SetScissor(sc);

        // Bind SMAA RT metrics at b2 (kSlotPerObject) via root constants.
        cmdList->SetRootConstants(kSlotPerObject,
                                  reinterpret_cast<const uint32_t*>(&smaaCB), 4);

        // Bind LDR scene color at t0.
        cmdList->SetFragmentTexture(0, inputColorTexture);

        cmdList->Draw(3); // fullscreen triangle
        cmdList->EndRenderPass();

        // Transition edges to SRV for Pass 2.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_edgesTex;
            barrier.buffer      = RHIBufferHandle::Invalid();
            barrier.stateBefore = RHIResourceState::RenderTarget;
            barrier.stateAfter  = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&barrier, 1);
        }
    }

    // ── Pass 2: Blend Weight Calculation ────────────────────────────────
    // Input: edges (t0), area tex (t1), search tex (t2)
    // Output: blend weights (RGBA8, RT state → cleared to zero)
    {
        UZ_PROFILE_SCOPE("SMAA::BlendWeight");

        // Transition blend weights to render target.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_blendTex;
            barrier.buffer      = RHIBufferHandle::Invalid();
            barrier.stateBefore = RHIResourceState::ShaderResource;
            barrier.stateAfter  = RHIResourceState::RenderTarget;
            cmdList->ResourceBarrier(&barrier, 1);
        }

        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount          = 1;
        passDesc.colorAttachments[0].texture   = m_blendTex;
        passDesc.colorAttachments[0].clear     = true;
        passDesc.colorAttachments[0].clearColor[0] = 0.0f;
        passDesc.colorAttachments[0].clearColor[1] = 0.0f;
        passDesc.colorAttachments[0].clearColor[2] = 0.0f;
        passDesc.colorAttachments[0].clearColor[3] = 0.0f;
        cmdList->BeginRenderPass(passDesc);

        cmdList->SetPipeline(m_blendPipeline);
        cmdList->SetViewport(vp);
        cmdList->SetScissor(sc);

        // Bind SMAA RT metrics at b2.
        cmdList->SetRootConstants(kSlotPerObject,
                                  reinterpret_cast<const uint32_t*>(&smaaCB), 4);

        // Bind textures: edges (t0), area (t1), search (t2).
        cmdList->SetFragmentTexture(0, m_edgesTex);
        cmdList->SetFragmentTexture(1, m_areaTex);
        cmdList->SetFragmentTexture(2, m_searchTex);

        cmdList->Draw(3);
        cmdList->EndRenderPass();

        // Transition blend weights to SRV for Pass 3.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_blendTex;
            barrier.buffer      = RHIBufferHandle::Invalid();
            barrier.stateBefore = RHIResourceState::RenderTarget;
            barrier.stateAfter  = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&barrier, 1);
        }
    }

    // ── Pass 3: Neighborhood Blending ───────────────────────────────────
    // Input: LDR scene color (t0), blend weights (t1)
    // Output: final AA result → outputRTHandle
    {
        UZ_PROFILE_SCOPE("SMAA::NeighborhoodBlend");

        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount          = 1;
        passDesc.colorAttachments[0].texture   = outputRTHandle;
        passDesc.colorAttachments[0].clear     = false;
        cmdList->BeginRenderPass(passDesc);

        cmdList->SetPipeline(m_resolvePipeline);
        cmdList->SetViewport(vp);
        cmdList->SetScissor(sc);

        // Bind SMAA RT metrics at b2.
        cmdList->SetRootConstants(kSlotPerObject,
                                  reinterpret_cast<const uint32_t*>(&smaaCB), 4);

        // Bind textures: scene color (t0), blend weights (t1).
        cmdList->SetFragmentTexture(0, inputColorTexture);
        cmdList->SetFragmentTexture(1, m_blendTex);

        cmdList->Draw(3);
        cmdList->EndRenderPass();
    }
}

} // namespace uz
