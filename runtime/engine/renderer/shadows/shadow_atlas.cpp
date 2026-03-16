/// UzEngine Shadow Atlas — Implementation.

#include "shadow_atlas.h"

#include "shadow_pass.h"
#include "rhi.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

bool ShadowAtlas::Init(IRHIDevice* device, uint32_t totalCascades) {
    if (!device) return false;

    const uint32_t cols = GetAtlasColumns(totalCascades);
    const uint32_t rows = GetAtlasRows(totalCascades);
    const uint32_t atlasWidth  = kShadowResolution * cols;
    const uint32_t atlasHeight = kShadowResolution * rows;

    RHITextureDesc desc{};
    desc.width     = atlasWidth;
    desc.height    = atlasHeight;
    desc.format    = RHIFormat::D32Float;
    desc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    desc.debugName = "ShadowAtlas";

    m_texture  = device->CreateTexture(desc);
    m_srvIndex = device->GetTextureSRVIndex(m_texture);

    if (!m_texture.IsValid()) {
        UZ_LOG(Shadow, Error, "ShadowAtlas: failed to create {}x{} D32Float texture",
               atlasWidth, atlasHeight);
        return false;
    }

    m_cascadeCount = totalCascades;
    UZ_LOG(Shadow, Info, "ShadowAtlas: created {}x{} D32Float (srv={}, cascades={}, grid={}x{})",
           atlasWidth, atlasHeight, m_srvIndex, totalCascades, cols, rows);
    return true;
}

void ShadowAtlas::Shutdown(IRHIDevice* device) {
    if (device && m_texture.IsValid()) {
        device->DestroyTexture(m_texture);
    }
    m_texture  = RHITextureHandle{};
    m_srvIndex = UINT32_MAX;
    m_cascadeCount = 0;
}

bool ShadowAtlas::Resize(IRHIDevice* device, uint32_t totalCascades) {
    if (totalCascades == m_cascadeCount && m_texture.IsValid()) {
        return true; // No change needed
    }

    // Cascade count changed — recreate the atlas with new dimensions
    Shutdown(device);
    return Init(device, totalCascades);
}

} // namespace uz
