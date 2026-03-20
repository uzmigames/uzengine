/// UzEngine Local Light Shadow Atlas — Tile-Based Shadow Atlas for Point/Spot Lights
///
/// Manages a single D32Float depth texture used as a tiled shadow atlas for
/// local lights (spot and point). Each shadowed light is assigned one or more
/// tiles in a fixed grid layout.
///
/// # Atlas Layout
/// A single 4096x4096 D32Float texture divided into a 4x4 grid of 1024x1024 tiles.
/// Each spot light shadow uses exactly one tile (1024x1024).
/// Maximum 16 spot light shadows per frame.
///
/// # Allocation Strategy
/// Simple per-frame reset: all tiles are freed at the start of each frame and
/// reallocated from index 0. No persistence across frames — every shadowed
/// light re-renders its shadow map each frame. This matches reference's non-cached
/// local light shadow behavior.
///
/// AllocateShadowDepthTargets for spot/point shadows — atlas tile management
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Atlas texture resolution in texels (width = height).
static constexpr uint32_t kLocalShadowAtlasSize = 4096;

/// Per-tile resolution in texels (width = height).
static constexpr uint32_t kLocalShadowTileSize = 1024;

/// Number of tile columns in the atlas grid.
static constexpr uint32_t kLocalShadowTileCols = kLocalShadowAtlasSize / kLocalShadowTileSize; // 4

/// Number of tile rows in the atlas grid.
static constexpr uint32_t kLocalShadowTileRows = kLocalShadowAtlasSize / kLocalShadowTileSize; // 4

/// Maximum number of tiles available (4x4 = 16).
static constexpr uint32_t kMaxLocalShadowTiles = kLocalShadowTileCols * kLocalShadowTileRows;

// ─────────────────────────────────────────────────────────────────────────────
// TileInfo — describes a single allocated tile in the atlas
// ─────────────────────────────────────────────────────────────────────────────

/// Describes the location and UV mapping of an allocated shadow tile.
///
/// Used by the shadow rendering pass (viewport/scissor setup) and by the
/// lighting shader (UV transformation for shadow sampling).
struct ShadowTileInfo {
    float    uvOffsetX;    ///< UV offset X in atlas [0,1]. E.g., 0.0, 0.25, 0.5, 0.75.
    float    uvOffsetY;    ///< UV offset Y in atlas [0,1].
    float    uvScale;      ///< UV scale (tile size / atlas size). For 1024/4096 = 0.25.
    uint32_t viewportX;    ///< Pixel X offset of the tile in the atlas texture.
    uint32_t viewportY;    ///< Pixel Y offset of the tile in the atlas texture.
    uint32_t viewportSize; ///< Tile size in pixels (width = height = kLocalShadowTileSize).
};

// ─────────────────────────────────────────────────────────────────────────────
// LocalLightShadowAtlas
// ─────────────────────────────────────────────────────────────────────────────

/// Manages a tiled shadow atlas for local (point/spot) light shadows.
///
/// The atlas is a single D32Float depth texture containing up to 16 tiles
/// in a 4x4 grid. Each frame, the allocator is reset and tiles are
/// assigned sequentially to shadowed lights.
///
/// # Ownership
/// Owns the GPU depth texture. Call `Init()` to create and `Shutdown()`
/// to destroy. The texture is NOT automatically destroyed on object destruction
/// — the caller must explicitly call `Shutdown()` before the RHI device is released.
class LocalLightShadowAtlas {
public:
    /// Creates the shadow atlas depth texture on the GPU.
    ///
    /// # Arguments
    /// * `device` — RHI device for texture creation.
    ///
    /// # Returns
    /// True if creation succeeded, false on failure.
    [[nodiscard]] bool Init(IRHIDevice* device);

    /// Destroys the shadow atlas depth texture and resets state.
    void Shutdown(IRHIDevice* device);

    /// Resets the tile allocator for a new frame.
    ///
    /// Must be called at the start of each frame before any `AllocateTile()` calls.
    /// All previously allocated tiles become invalid.
    void Reset();

    /// Allocates a single tile from the atlas.
    ///
    /// Tiles are assigned sequentially (left-to-right, top-to-bottom) from
    /// the 4x4 grid. Returns false if no tiles remain (all 16 allocated).
    ///
    /// # Arguments
    /// * `outTile` — Receives the allocated tile info on success.
    ///
    /// # Returns
    /// True if a tile was successfully allocated, false if the atlas is full.
    [[nodiscard]] bool AllocateTile(ShadowTileInfo& outTile);

    /// Returns true if the atlas texture has been created and is valid.
    [[nodiscard]] bool IsValid() const { return m_texture.IsValid(); }

    /// Returns the physical texture handle for render pass attachment.
    [[nodiscard]] RHITextureHandle GetTexture() const { return m_texture; }

    /// Returns the SRV heap index for shader sampling.
    [[nodiscard]] uint32_t GetSrvIndex() const { return m_srvIndex; }

    /// Returns the number of tiles currently allocated this frame.
    [[nodiscard]] uint32_t GetAllocatedTileCount() const { return m_nextTileIndex; }

private:
    RHITextureHandle m_texture;
    uint32_t m_srvIndex = UINT32_MAX;
    uint32_t m_nextTileIndex = 0; ///< Index of the next tile to allocate (0 to kMaxLocalShadowTiles).
};

} // namespace uz
