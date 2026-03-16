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
static constexpr uint32_t kShadowResolution = 2048;

// ─────────────────────────────────────────────────────────────────────────────
// ShadowAtlas
// ─────────────────────────────────────────────────────────────────────────────

/// Manages the cascaded shadow map depth atlas texture.
///
/// The atlas is a single D32Float depth texture containing cascades in a dynamic
/// NxM grid layout. The grid dimensions depend on the total cascade count
/// (see `GetAtlasColumns()`). Each cascade tile is `kShadowResolution x kShadowResolution`.
///
/// # Ownership
/// ShadowAtlas owns the GPU texture. Call `Init()` to create and `Shutdown()`
/// to destroy. The texture is NOT automatically destroyed on object destruction
/// — the caller must explicitly call `Shutdown()` before the RHI device is released.
///
/// # Resizing
/// If the cascade count changes (e.g., far shadows enabled at runtime), call
/// `Resize()` to recreate the atlas with the new dimensions. Alternatively,
/// `Shutdown()` + `Init()` achieves the same result.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.
class ShadowAtlas {
public:
    /// Creates the shadow atlas depth texture on the GPU.
    ///
    /// # Arguments
    /// * `device` — RHI device for texture creation.
    /// * `totalCascades` — Total number of cascades (main + far). Determines
    ///   atlas grid dimensions. Default: 4 (backward compatible 2x2 layout).
    ///
    /// # Returns
    /// True if creation succeeded, false on failure.
    bool Init(IRHIDevice* device, uint32_t totalCascades = 4);

    /// Destroys the shadow atlas depth texture and resets state.
    void Shutdown(IRHIDevice* device);

    /// Recreates the atlas if the cascade count changed since last Init().
    ///
    /// No-op if the cascade count matches the current allocation. Otherwise,
    /// destroys the old texture and creates a new one with the correct dimensions.
    ///
    /// # Returns
    /// True if the atlas is valid after the call (either unchanged or recreated).
    bool Resize(IRHIDevice* device, uint32_t totalCascades);

    /// Returns true if the atlas texture has been created and is valid.
    [[nodiscard]] bool IsValid() const { return m_texture.IsValid(); }

    /// Returns the physical texture handle for render pass attachment.
    [[nodiscard]] RHITextureHandle GetTexture() const { return m_texture; }

    /// Returns the SRV heap index for shader sampling.
    [[nodiscard]] uint32_t GetSrvIndex() const { return m_srvIndex; }

    /// Returns the per-cascade resolution in texels.
    [[nodiscard]] static constexpr uint32_t GetResolution() { return kShadowResolution; }

    /// Returns the cascade count the atlas was created for.
    [[nodiscard]] uint32_t GetCascadeCount() const { return m_cascadeCount; }

private:
    RHITextureHandle m_texture;
    uint32_t m_srvIndex = UINT32_MAX;
    uint32_t m_cascadeCount = 0;
};

} // namespace uz
