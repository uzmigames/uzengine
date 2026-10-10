/// UzEngine Local Light Shadow Atlas — Implementation.
///
/// AllocateShadowDepthTargets for local lights — atlas tile allocation

#include "local_light_shadow_atlas.h"

#include "rhi.h"
#include "log.h"
#include "cvar.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(Shadow);

namespace uz {

bool LocalLightShadowAtlas::Init(IRHIDevice* device) {
    if (!device) return false;

    // Layout from the CVars registered in shadow_renderer.cpp (defaults = the kLocalShadow*
    // constants; until 2026-10-05 they were registered and never read). The tile count is capped by
    // the constant-buffer array (kMaxLocalLightShadows).
    const auto& cvars = CVarRegistry::Get();
    m_atlasSize = static_cast<uint32_t>(std::clamp(cvars.GetInt("r.Shadow.LocalAtlasSize", static_cast<int>(kLocalShadowAtlasSize)), 256, 16384));
    m_tileSize  = static_cast<uint32_t>(std::clamp(cvars.GetInt("r.Shadow.LocalTileSize", static_cast<int>(kLocalShadowTileSize)), 64, static_cast<int>(m_atlasSize)));
    m_columns   = std::max(1u, m_atlasSize / m_tileSize);
    m_maxTiles  = std::min(m_columns * m_columns, kMaxLocalShadowTiles);

    RHITextureDesc desc{};
    desc.width     = m_atlasSize;
    desc.height    = m_atlasSize;
    desc.format    = RHIFormat::D32Float;
    desc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    desc.debugName = "LocalLightShadowAtlas";

    m_texture  = device->CreateTexture(desc);
    m_srvIndex = device->GetTextureSRVIndex(m_texture);

    if (!m_texture.IsValid()) {
        UZ_LOG(Shadow, Error, "LocalLightShadowAtlas: failed to create {}x{} D32Float texture",
               m_atlasSize, m_atlasSize);
        return false;
    }

    m_nextTileIndex = 0;
    UZ_LOG(Shadow, Info,
           "LocalLightShadowAtlas: created {}x{} D32Float (srv={}, tileSize={}, maxTiles={})",
           m_atlasSize, m_atlasSize, m_srvIndex,
           m_tileSize, m_maxTiles);
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
    if (m_nextTileIndex >= m_maxTiles) {
        return false; // Atlas full
    }

    const uint32_t tileIndex = m_nextTileIndex++;

    const uint32_t col = tileIndex % m_columns;
    const uint32_t row = tileIndex / m_columns;

    outTile.viewportX    = col * m_tileSize;
    outTile.viewportY    = row * m_tileSize;
    outTile.viewportSize = m_tileSize;

    const float kInvAtlasSize = 1.0f / static_cast<float>(m_atlasSize);
    outTile.uvOffsetX = static_cast<float>(outTile.viewportX) * kInvAtlasSize;
    outTile.uvOffsetY = static_cast<float>(outTile.viewportY) * kInvAtlasSize;
    outTile.uvScale   = static_cast<float>(m_tileSize) * kInvAtlasSize;

    return true;
}

} // namespace uz
