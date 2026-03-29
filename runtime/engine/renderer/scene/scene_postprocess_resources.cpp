/// UzEngine Scene Renderer — Post-process resource helpers (Hi-Z, DFS, Color Grading,
/// FXAA, DOF, GPU Occlusion Culling buffers).

#include "scene_renderer.h"

#include "rhi.h"
#include "log.h"
#include "shadows/sdf_atlas_manager.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Hi-Z Pyramid Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

/// Helper that computes the number of mip levels for a given resolution.
static uint32_t ComputeMipCount(uint32_t width, uint32_t height) {
    uint32_t mips = 0;
    uint32_t w = width;
    uint32_t h = height;
    while (w > 1 || h > 1) {
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        ++mips;
        if (mips >= kHiZMaxMipLevels) break;
    }
    return mips;
}

void SceneRenderer::CreateHiZPyramid(uint32_t width, uint32_t height) {
    // Destroy any existing Hi-Z mips.
    DestroyHiZPyramid();

    m_hizMipCount = ComputeMipCount(width, height);
    if (m_hizMipCount == 0) return;

    uint32_t mipW = width;
    uint32_t mipH = height;

    for (uint32_t i = 0; i < m_hizMipCount; ++i) {
        mipW = (mipW > 1) ? (mipW >> 1) : 1;
        mipH = (mipH > 1) ? (mipH >> 1) : 1;

        RHITextureDesc texDesc{};
        texDesc.width     = mipW;
        texDesc.height    = mipH;
        texDesc.format    = RHIFormat::R32Float;
        texDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        texDesc.debugName = "HiZ_Mip";  // Device may append index for debug.

        RHITextureHandle tex = m_device->CreateTexture(texDesc);
        if (!tex.IsValid()) {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create Hi-Z mip {} ({}x{})", i, mipW, mipH);
            // Partial creation — clean up and disable.
            DestroyHiZPyramid();
            return;
        }

        m_hizMipLevels[i].texture  = tex;
        m_hizMipLevels[i].srvIndex = m_device->GetTextureSRVIndex(tex);
        m_hizMipLevels[i].uavIndex = m_device->GetTextureUAVIndex(tex);
        m_hizMipLevels[i].width    = mipW;
        m_hizMipLevels[i].height   = mipH;
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: Hi-Z pyramid created ({} mips, {}x{} -> {}x{})",
           m_hizMipCount, width, height,
           m_hizMipLevels[m_hizMipCount - 1].width,
           m_hizMipLevels[m_hizMipCount - 1].height);
}

void SceneRenderer::DestroyHiZPyramid() {
    for (uint32_t i = 0; i < m_hizMipCount; ++i) {
        if (m_hizMipLevels[i].texture.IsValid()) {
            m_device->DestroyTexture(m_hizMipLevels[i].texture);
            m_hizMipLevels[i] = {};
        }
    }
    m_hizMipCount = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Occlusion Culling Buffer Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateGpuCullBuffers(uint32_t capacity) {
    // Destroy any existing buffers first.
    DestroyGpuCullBuffers();

    if (capacity == 0) return;

    // ── Bounding Sphere Buffers (StructuredBuffer<float4>, Upload heap) ──
    // CPU writes bounding spheres each frame; GPU reads in cull compute shader.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(capacity) * 16;  // sizeof(float4) = 16
        desc.stride    = 16;
        desc.usage     = RHIBufferUsage::StructuredBuffer;
        desc.memory    = RHIMemoryType::Upload;

        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            desc.debugName = (i == 0) ? "BoundingSphereBuffer[0]"
                           : (i == 1) ? "BoundingSphereBuffer[1]"
                                      : "BoundingSphereBuffer[2]";
            m_boundingSphereBuffers[i] = m_device->CreateBuffer(desc);
            if (m_boundingSphereBuffers[i].IsValid()) {
                m_boundingSphereSrvIndices[i] = m_device->GetBufferSRVIndex(m_boundingSphereBuffers[i]);
            }
        }
    }

    // ── Visible Index Buffers (StructuredBuffer<uint> + UAV, Default heap) ──
    // GPU compute writes visible instance indices into this buffer.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(capacity) * 4;  // sizeof(uint) = 4
        desc.stride    = 4;
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory    = RHIMemoryType::Default;

        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            desc.debugName = (i == 0) ? "VisibleIndexBuffer[0]"
                           : (i == 1) ? "VisibleIndexBuffer[1]"
                                      : "VisibleIndexBuffer[2]";
            m_visibleIndexBuffers[i] = m_device->CreateBuffer(desc);
            if (m_visibleIndexBuffers[i].IsValid()) {
                m_visibleIndexUavIndices[i] = m_device->GetBufferUAVIndex(m_visibleIndexBuffers[i]);
            }
        }
    }

    // ── Indirect Args Buffers (ByteAddressBuffer + UAV + IndirectArgs, Default heap) ──
    // 20 bytes = sizeof(RHIDrawIndexedIndirectArgs). GPU compute writes instance count;
    // CPU pre-fills the rest per batch before dispatch.
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = 20;  // DrawIndexedIndirectArgs = 5 x uint32_t
        desc.stride    = 0;   // Raw (byte-address), not structured
        desc.usage     = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Default;

        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            desc.debugName = (i == 0) ? "IndirectArgsBuffer[0]"
                           : (i == 1) ? "IndirectArgsBuffer[1]"
                                      : "IndirectArgsBuffer[2]";
            m_indirectArgsBuffers[i] = m_device->CreateBuffer(desc);
            if (m_indirectArgsBuffers[i].IsValid()) {
                m_indirectArgsUavIndices[i] = m_device->GetBufferUAVIndex(m_indirectArgsBuffers[i]);
            }
        }
    }

    // ── Phase 2 Visible Index Buffers (same layout as Phase 1) ─────────
    // Two-phase occlusion re-tests all instances against the updated HiZ.
    // Newly-visible instances are written to this separate buffer so the
    // GBuffer pass can issue two indirect draws (Phase 1 + Phase 2).
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = static_cast<uint64_t>(capacity) * 4;
        desc.stride    = 4;
        desc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        desc.memory    = RHIMemoryType::Default;

        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            desc.debugName = (i == 0) ? "VisibleIndexBuffer2[0]"
                           : (i == 1) ? "VisibleIndexBuffer2[1]"
                                      : "VisibleIndexBuffer2[2]";
            m_visibleIndexBuffers2[i] = m_device->CreateBuffer(desc);
            if (m_visibleIndexBuffers2[i].IsValid()) {
                m_visibleIndexUavIndices2[i] = m_device->GetBufferUAVIndex(m_visibleIndexBuffers2[i]);
            }
        }
    }

    // ── Phase 2 Indirect Args Buffers (same layout as Phase 1) ───────────
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = 20;
        desc.stride    = 0;
        desc.usage     = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::CopyDst;
        desc.memory    = RHIMemoryType::Default;

        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            desc.debugName = (i == 0) ? "IndirectArgsBuffer2[0]"
                           : (i == 1) ? "IndirectArgsBuffer2[1]"
                                      : "IndirectArgsBuffer2[2]";
            m_indirectArgsBuffers2[i] = m_device->CreateBuffer(desc);
            if (m_indirectArgsBuffers2[i].IsValid()) {
                m_indirectArgsUavIndices2[i] = m_device->GetBufferUAVIndex(m_indirectArgsBuffers2[i]);
            }
        }
    }

    m_gpuCullBufferCapacity = capacity;
    UZ_LOG(Renderer, Info, "SceneRenderer: created GPU cull buffers (capacity={})", capacity);
}

void SceneRenderer::DestroyGpuCullBuffers() {
    for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
        if (m_boundingSphereBuffers[i].IsValid()) {
            m_device->DestroyBuffer(m_boundingSphereBuffers[i]);
            m_boundingSphereBuffers[i] = RHIBufferHandle{};
            m_boundingSphereSrvIndices[i] = UINT32_MAX;
        }
        if (m_visibleIndexBuffers[i].IsValid()) {
            m_device->DestroyBuffer(m_visibleIndexBuffers[i]);
            m_visibleIndexBuffers[i] = RHIBufferHandle{};
            m_visibleIndexUavIndices[i] = UINT32_MAX;
        }
        if (m_indirectArgsBuffers[i].IsValid()) {
            m_device->DestroyBuffer(m_indirectArgsBuffers[i]);
            m_indirectArgsBuffers[i] = RHIBufferHandle{};
            m_indirectArgsUavIndices[i] = UINT32_MAX;
        }
        // Phase 2 buffers
        if (m_visibleIndexBuffers2[i].IsValid()) {
            m_device->DestroyBuffer(m_visibleIndexBuffers2[i]);
            m_visibleIndexBuffers2[i] = RHIBufferHandle{};
            m_visibleIndexUavIndices2[i] = UINT32_MAX;
        }
        if (m_indirectArgsBuffers2[i].IsValid()) {
            m_device->DestroyBuffer(m_indirectArgsBuffers2[i]);
            m_indirectArgsBuffers2[i] = RHIBufferHandle{};
            m_indirectArgsUavIndices2[i] = UINT32_MAX;
        }
    }
    m_gpuCullBufferCapacity = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// DFS Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateDfsResources(uint32_t width, uint32_t height) {
    // Only create DFS resources when DFS is enabled
    if (!m_dfsConfig.enabled) return;

    // Output texture: RG16Float (shadow_factor in R, scene_depth in G).
    // Half-res by default unless fullResolution is set.
    const uint32_t dfsWidth  = m_dfsConfig.fullResolution ? width  : width  / 2;
    const uint32_t dfsHeight = m_dfsConfig.fullResolution ? height : height / 2;

    if (dfsWidth == 0 || dfsHeight == 0) return;

    // ── DFS Output Texture ──────────────────────────────────────────────
    {
        RHITextureDesc dfsDesc{};
        dfsDesc.width     = dfsWidth;
        dfsDesc.height    = dfsHeight;
        dfsDesc.format    = RHIFormat::RG16Float;
        dfsDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
        dfsDesc.debugName = "DFS_Output";

        m_dfsOutputTexture = m_device->CreateTexture(dfsDesc);
        if (m_dfsOutputTexture.IsValid()) {
            m_dfsOutputSrvIndex = m_device->GetTextureSRVIndex(m_dfsOutputTexture);
            m_dfsOutputUavIndex = m_device->GetTextureUAVIndex(m_dfsOutputTexture);
            UZ_LOG(Renderer, Info, "SceneRenderer: DFS output texture created ({}x{}, RG16F, SRV={}, UAV={})",
                   dfsWidth, dfsHeight, m_dfsOutputSrvIndex, m_dfsOutputUavIndex);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create DFS output texture — DFS disabled");
        }
    }

    // ── DFS Culled Indices Buffer ────────────────────────────────────────
    // AppendStructuredBuffer<uint> with capacity for all SDF objects.
    {
        RHIBufferDesc bufDesc{};
        bufDesc.sizeBytes = kMaxSdfObjects * sizeof(uint32_t);
        bufDesc.stride    = sizeof(uint32_t);
        bufDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        bufDesc.debugName = "DFS_CulledIndices";

        m_dfsCulledIndicesBuffer = m_device->CreateBuffer(bufDesc);
        if (m_dfsCulledIndicesBuffer.IsValid()) {
            m_dfsCulledIndicesSrvIndex = m_device->GetBufferSRVIndex(m_dfsCulledIndicesBuffer);
            m_dfsCulledIndicesUavIndex = m_device->GetBufferUAVIndex(m_dfsCulledIndicesBuffer);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create DFS culled indices buffer");
        }
    }

    // ── DFS Indirect Args Buffer ─────────────────────────────────────────
    // RWByteAddressBuffer for DispatchIndirect args (3 x uint32_t = 12 bytes).
    // The cull pass atomically increments the X thread group count.
    {
        RHIBufferDesc argsDesc{};
        argsDesc.sizeBytes = 3 * sizeof(uint32_t);
        argsDesc.stride    = 0; // Raw byte address buffer
        argsDesc.usage     = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::CopyDst;
        argsDesc.debugName = "DFS_IndirectArgs";

        m_dfsIndirectArgsBuffer = m_device->CreateBuffer(argsDesc);
        if (m_dfsIndirectArgsBuffer.IsValid()) {
            m_dfsIndirectArgsUavIndex = m_device->GetBufferUAVIndex(m_dfsIndirectArgsBuffer);
        } else {
            UZ_LOG(Renderer, Warning, "SceneRenderer: failed to create DFS indirect args buffer");
        }
    }
}

void SceneRenderer::DestroyDfsResources() {
    if (m_dfsOutputTexture.IsValid()) {
        m_device->DestroyTexture(m_dfsOutputTexture);
        m_dfsOutputTexture = RHITextureHandle{};
        m_dfsOutputSrvIndex = UINT32_MAX;
        m_dfsOutputUavIndex = UINT32_MAX;
    }
    if (m_dfsCulledIndicesBuffer.IsValid()) {
        m_device->DestroyBuffer(m_dfsCulledIndicesBuffer);
        m_dfsCulledIndicesBuffer = RHIBufferHandle{};
        m_dfsCulledIndicesSrvIndex = UINT32_MAX;
        m_dfsCulledIndicesUavIndex = UINT32_MAX;
    }
    if (m_dfsIndirectArgsBuffer.IsValid()) {
        m_device->DestroyBuffer(m_dfsIndirectArgsBuffer);
        m_dfsIndirectArgsBuffer = RHIBufferHandle{};
        m_dfsIndirectArgsUavIndex = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Color Grading LUT Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateColorGradingLutResources() {
    DestroyColorGradingLutResources();

    RHITextureDesc lutDesc{};
    lutDesc.width            = kColorGradingLutSize;
    lutDesc.height           = kColorGradingLutSize;
    lutDesc.depthOrArraySize = static_cast<uint16_t>(kColorGradingLutSize);
    lutDesc.is3D             = true;
    lutDesc.format           = RHIFormat::RGBA8Unorm;
    lutDesc.usage            = RHITextureUsage::ShaderResource
                             | RHITextureUsage::UnorderedAccess;
    lutDesc.debugName        = "ColorGradingLUT_3D";

    m_colorGradingLut = m_device->CreateTexture(lutDesc);
    if (m_colorGradingLut.IsValid()) {
        m_colorGradingLutSrv = m_device->GetTextureSRVIndex(m_colorGradingLut);
        m_colorGradingLutUav = m_device->GetTextureUAVIndex(m_colorGradingLut);
        m_colorGradingLutDirty = true;
        UZ_LOG(Renderer, Info, "SceneRenderer: Color grading LUT created ({}x{}x{}, RGBA8, 3D, SRV={}, UAV={})",
               kColorGradingLutSize, kColorGradingLutSize, kColorGradingLutSize,
               m_colorGradingLutSrv, m_colorGradingLutUav);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create color grading LUT texture");
    }
}

void SceneRenderer::DestroyColorGradingLutResources() {
    if (m_colorGradingLut.IsValid()) {
        m_device->DestroyTexture(m_colorGradingLut);
        m_colorGradingLut = RHITextureHandle{};
        m_colorGradingLutSrv = UINT32_MAX;
        m_colorGradingLutUav = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FXAA Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateFxaaResources(uint32_t width, uint32_t height) {
    DestroyFxaaResources();

    RHITextureDesc desc{};
    desc.width     = width;
    desc.height    = height;
    desc.format    = RHIFormat::RGBA8Unorm;   // Must match swapchain format AND tonemap PSO output format.
    desc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget | RHITextureUsage::CopySrc;
    desc.debugName = "FXAA_LdrIntermediate";

    m_fxaaLdrIntermediate = m_device->CreateTexture(desc);
    if (m_fxaaLdrIntermediate.IsValid()) {
        m_fxaaLdrIntermediateSrv = m_device->GetTextureSRVIndex(m_fxaaLdrIntermediate);
        UZ_LOG(Renderer, Info, "SceneRenderer: FXAA LDR intermediate created ({}x{}, RGBA8, SRV={})",
               width, height, m_fxaaLdrIntermediateSrv);
    } else {
        UZ_LOG(Renderer, Error, "SceneRenderer: failed to create FXAA LDR intermediate texture");
    }
}

void SceneRenderer::DestroyFxaaResources() {
    if (m_fxaaLdrIntermediate.IsValid()) {
        m_device->DestroyTexture(m_fxaaLdrIntermediate);
        m_fxaaLdrIntermediate = RHITextureHandle{};
        m_fxaaLdrIntermediateSrv = UINT32_MAX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DOF Resource Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::CreateDofResources(uint32_t width, uint32_t height) {
    DestroyDofResources();

    // CoC texture — R16F signed circle of confusion per pixel
    {
        RHITextureDesc cocDesc{};
        cocDesc.width     = width;
        cocDesc.height    = height;
        cocDesc.format    = RHIFormat::R16Float;
        cocDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        cocDesc.debugName = "DOF_CoC";

        m_dofCocTexture = m_device->CreateTexture(cocDesc);
        if (m_dofCocTexture.IsValid()) {
            m_dofCocSrv = m_device->GetTextureSRVIndex(m_dofCocTexture);
            m_dofCocUav = m_device->GetTextureUAVIndex(m_dofCocTexture);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DOF CoC texture");
        }
    }

    // DOF output — RGBA16F blurred result
    {
        RHITextureDesc outDesc{};
        outDesc.width     = width;
        outDesc.height    = height;
        outDesc.format    = RHIFormat::RGBA16Float;
        outDesc.usage     = RHITextureUsage::ShaderResource
                          | RHITextureUsage::UnorderedAccess;
        outDesc.debugName = "DOF_Output";

        m_dofOutputTexture = m_device->CreateTexture(outDesc);
        if (m_dofOutputTexture.IsValid()) {
            m_dofOutputSrv = m_device->GetTextureSRVIndex(m_dofOutputTexture);
            m_dofOutputUav = m_device->GetTextureUAVIndex(m_dofOutputTexture);
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to create DOF output texture");
        }
    }

    UZ_LOG(Renderer, Info, "SceneRenderer: DOF textures created ({}x{}, CoC SRV={}, Output SRV={})",
           width, height, m_dofCocSrv, m_dofOutputSrv);
}

void SceneRenderer::DestroyDofResources() {
    if (m_dofCocTexture.IsValid()) {
        m_device->DestroyTexture(m_dofCocTexture);
        m_dofCocTexture = RHITextureHandle{};
        m_dofCocSrv = UINT32_MAX;
        m_dofCocUav = UINT32_MAX;
    }
    if (m_dofOutputTexture.IsValid()) {
        m_device->DestroyTexture(m_dofOutputTexture);
        m_dofOutputTexture = RHITextureHandle{};
        m_dofOutputSrv = UINT32_MAX;
        m_dofOutputUav = UINT32_MAX;
    }
}

} // namespace uz
