/// UzEngine Scene Update Flags — per-frame dirty state aggregation.
///
/// @ref Falcor Scene::UpdateFlags — aggregated dirty bitmask
///
/// Computed once per frame by OR-ing all component dirty flags during scene
/// gather. Downstream systems check these flags to skip expensive operations
/// (cascade recompute, CB uploads, etc.) when the relevant data hasn't changed.

#pragma once

#include <cstdint>

namespace uz {

/// Aggregated per-frame dirty state.
///
/// Each bit indicates that at least one component of the corresponding type
/// was modified since the last frame. All bits default to set (first frame
/// must process everything).
struct SceneUpdateFlags {
    uint32_t flags = 0xFFFFFFFF; ///< Start fully dirty.

    // ── Flag bits ────────────────────────────────────────────────────────

    static constexpr uint32_t kCamera       = 1 << 0;  ///< Camera position, projection, or FOV changed.
    static constexpr uint32_t kLights       = 1 << 1;  ///< Any light color, intensity, direction, or radius changed.
    static constexpr uint32_t kTransforms   = 1 << 2;  ///< Any entity position, rotation, or scale changed.
    static constexpr uint32_t kFog          = 1 << 3;  ///< HeightFogComponent parameters changed.
    static constexpr uint32_t kClouds       = 1 << 4;  ///< VolumetricCloudsComponent parameters changed.
    static constexpr uint32_t kPostProcess  = 1 << 5;  ///< PostProcessVolumeComponent parameters changed.
    static constexpr uint32_t kSkyLight     = 1 << 6;  ///< SkyLightComponent parameters changed.
    static constexpr uint32_t kShadows      = 1 << 7;  ///< Shadow-relevant data changed (camera + lights + transforms).
    static constexpr uint32_t kMaterials    = 1 << 8;  ///< Any material property changed.
    static constexpr uint32_t kMeshes       = 1 << 9;  ///< Mesh added/removed/changed.

    static constexpr uint32_t kAll          = 0xFFFFFFFF;

    // ── Helpers ──────────────────────────────────────────────────────────

    void Set(uint32_t f) noexcept { flags |= f; }
    void Clear() noexcept { flags = 0; }
    void ClearFlag(uint32_t f) noexcept { flags &= ~f; }
    [[nodiscard]] bool IsSet(uint32_t f) const noexcept { return (flags & f) != 0; }
    [[nodiscard]] bool IsAnySet() const noexcept { return flags != 0; }

    /// Shadows are dirty when camera, lights, or transforms changed.
    [[nodiscard]] bool AreShadowsDirty() const noexcept {
        return IsSet(kCamera | kLights | kTransforms | kShadows);
    }
};

} // namespace uz
