/// UzEngine Shadow Atlas — Cascade Shadow Map Depth Texture Management
///
/// Owns the 2x2 cascade shadow atlas depth texture. Handles creation,
/// destruction, and provides the texture handle + SRV index to the
/// shadow pass and shadow sampling shaders.
///
/// Extracted from SceneRenderer to give the shadow subsystem full ownership
/// of its GPU resources.

#pragma once

#include "rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Per-cascade shadow map resolution in texels (width = height).
/// The full atlas is `kShadowResolution * 2` in each dimension (2x2 layout).
static constexpr uint32_t kShadowResolution = 2048;

/// Full shadow atlas size = 2x cascade resolution (2x2 grid of cascades).
static constexpr uint32_t kShadowAtlasSize = kShadowResolution * 2;

// ─────────────────────────────────────────────────────────────────────────────
// ShadowAtlas
// ─────────────────────────────────────────────────────────────────────────────

/// Manages the cascaded shadow map depth atlas texture.
///
/// The atlas is a single D32Float depth texture containing 4 cascades in a
/// 2x2 grid layout. Each cascade tile is `kShadowResolution x kShadowResolution`.
///
/// # Ownership
/// ShadowAtlas owns the GPU texture. Call `Init()` to create and `Shutdown()`
/// to destroy. The texture is NOT automatically destroyed on object destruction
/// — the caller must explicitly call `Shutdown()` before the RHI device is released.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.
class ShadowAtlas {
public:
    /// Creates the shadow atlas depth texture on the GPU.
    ///
    /// # Arguments
    /// * `device` — RHI device for texture creation.
    ///
    /// # Returns
    /// True if creation succeeded, false on failure.
    bool Init(IRHIDevice* device);

    /// Destroys the shadow atlas depth texture and resets state.
    void Shutdown(IRHIDevice* device);

    /// Returns true if the atlas texture has been created and is valid.
    [[nodiscard]] bool IsValid() const { return m_texture.IsValid(); }

    /// Returns the physical texture handle for render pass attachment.
    [[nodiscard]] RHITextureHandle GetTexture() const { return m_texture; }

    /// Returns the SRV heap index for shader sampling.
    [[nodiscard]] uint32_t GetSrvIndex() const { return m_srvIndex; }

    /// Returns the per-cascade resolution in texels.
    [[nodiscard]] static constexpr uint32_t GetResolution() { return kShadowResolution; }

private:
    RHITextureHandle m_texture;
    uint32_t m_srvIndex = UINT32_MAX;
};

} // namespace uz
