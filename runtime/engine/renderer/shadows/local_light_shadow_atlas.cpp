/// UzEngine Local Light Shadow Atlas — Implementation.
///
/// @source UE5 ShadowSetup.cpp:5714-5730 — AllocateShadowDepthTargets for local lights

#include "local_light_shadow_atlas.h"

#include "rhi.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

bool LocalLightShadowAtlas::Init(IRHIDevice* device) {
    if (!device) return false;

    RHITextureDesc desc{};
    desc.width     = kLocalShadowAtlasSize;
    desc.height    = kLocalShadowAtlasSize;
    desc.format    = RHIFormat::D32Float;
    desc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    desc.debugName = "LocalLightShadowAtlas";

    m_texture  = device->CreateTexture(desc);
    m_srvIndex = device->GetTextureSRVIndex(m_texture);

    if (!m_texture.IsValid()) {
        UZ_LOG(Shadow, Error, "LocalLightShadowAtlas: failed to create {}x{} D32Float texture",
               kLocalShadowAtlasSize, kLocalShadowAtlasSize);
        return false;
    }

    m_nextTileIndex = 0;
    UZ_LOG(Shadow, Info,
           "LocalLightShadowAtlas: created {}x{} D32Float (srv={}, tileSize={}, maxTiles={})",
           kLocalShadowAtlasSize, kLocalShadowAtlasSize, m_srvIndex,
           kLocalShadowTileSize, kMaxLocalShadowTiles);
    return true;
}

void LocalLightShadowAtlas::Shutdown(IRHIDevice* device) {
    if (device && m_texture.IsValid()) {
        device->DestroyTexture(m_texture);
    }
    m_texture       = RHITextureHandle{};
    m_srvIndex      = UINT32_MAX;
    m_nextTileIndex = 0;
}

void LocalLightShadowAtlas::Reset() {
    m_nextTileIndex = 0;
}

bool LocalLightShadowAtlas::AllocateTile(ShadowTileInfo& outTile) {
    if (m_nextTileIndex >= kMaxLocalShadowTiles) {
        return false; // Atlas full
    }

    const uint32_t tileIndex = m_nextTileIndex++;

    const uint32_t col = tileIndex % kLocalShadowTileCols;
    const uint32_t row = tileIndex / kLocalShadowTileCols;

    outTile.viewportX    = col * kLocalShadowTileSize;
    outTile.viewportY    = row * kLocalShadowTileSize;
    outTile.viewportSize = kLocalShadowTileSize;

    static constexpr float kInvAtlasSize = 1.0f / static_cast<float>(kLocalShadowAtlasSize);
    outTile.uvOffsetX = static_cast<float>(outTile.viewportX) * kInvAtlasSize;
    outTile.uvOffsetY = static_cast<float>(outTile.viewportY) * kInvAtlasSize;
    outTile.uvScale   = static_cast<float>(kLocalShadowTileSize) * kInvAtlasSize;

    return true;
}

} // namespace uz
