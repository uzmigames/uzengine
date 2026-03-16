/// UzEngine Shadow Atlas — Implementation.

#include "shadow_atlas.h"

#include "rhi.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

bool ShadowAtlas::Init(IRHIDevice* device) {
    if (!device) return false;

    RHITextureDesc desc{};
    desc.width     = kShadowAtlasSize;
    desc.height    = kShadowAtlasSize;
    desc.format    = RHIFormat::D32Float;
    desc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    desc.debugName = "ShadowAtlas";

    m_texture  = device->CreateTexture(desc);
    m_srvIndex = device->GetTextureSRVIndex(m_texture);

    if (!m_texture.IsValid()) {
        UZ_LOG(Shadow, Error, "ShadowAtlas: failed to create {}x{} D32Float texture",
               kShadowAtlasSize, kShadowAtlasSize);
        return false;
    }

    UZ_LOG(Shadow, Info, "ShadowAtlas: created {}x{} D32Float (srv={})",
           kShadowAtlasSize, kShadowAtlasSize, m_srvIndex);
    return true;
}

void ShadowAtlas::Shutdown(IRHIDevice* device) {
    if (device && m_texture.IsValid()) {
        device->DestroyTexture(m_texture);
    }
    m_texture  = RHITextureHandle{};
    m_srvIndex = UINT32_MAX;
}

} // namespace uz
