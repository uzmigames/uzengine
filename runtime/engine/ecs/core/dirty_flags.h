/// UzEngine ECS Dirty Flags — CB update propagation.
///
/// @ref Falcor UpdateFlags — per-component dirty tracking for constant buffer
/// upload optimization. When no flags are set, the CB section for that component
/// can be skipped entirely, saving CPU + GPU upload bandwidth.
///
/// # Usage
/// ```cpp
/// // In component:
/// light.color = {1, 0, 0};
/// light.MarkCBDirty(LightDirtyFlag::Color);
///
/// // In per-frame gather:
/// if (light.IsCBDirty()) {
///     WriteToConstantBuffer(light);
///     light.ClearCBDirty();
/// }
/// ```

#pragma once

#include <cstdint>

namespace uz {

/// Mixin for ECS components that feed constant buffers.
/// Add as a public base or embed as a member.
struct CBDirtyMixin {
    /// Marks specific CB fields as dirty.
    void MarkCBDirty(uint32_t flags) noexcept { m_cbDirtyFlags |= flags; }

    /// Marks all CB fields as dirty (force full re-upload).
    void MarkCBDirtyAll() noexcept { m_cbDirtyFlags = 0xFFFFFFFF; }

    /// Returns true if any CB field needs re-uploading.
    [[nodiscard]] bool IsCBDirty() const noexcept { return m_cbDirtyFlags != 0; }

    /// Returns true if a specific flag is set.
    [[nodiscard]] bool IsCBDirty(uint32_t flag) const noexcept { return (m_cbDirtyFlags & flag) != 0; }

    /// Returns the raw dirty flags bitmask.
    [[nodiscard]] uint32_t GetCBDirtyFlags() const noexcept { return m_cbDirtyFlags; }

    /// Clears all dirty flags. Call after CB data has been written.
    void ClearCBDirty() noexcept { m_cbDirtyFlags = 0; }

protected:
    uint32_t m_cbDirtyFlags = 0xFFFFFFFF; ///< Start fully dirty (first frame must upload).
};

// ─── Per-component dirty flag bits ──────────────────────────────────────

namespace TransformDirty {
    constexpr uint32_t Position = 1 << 0;
    constexpr uint32_t Rotation = 1 << 1;
    constexpr uint32_t Scale    = 1 << 2;
    constexpr uint32_t All      = Position | Rotation | Scale;
}

namespace LightDirty {
    constexpr uint32_t Color     = 1 << 0;
    constexpr uint32_t Intensity = 1 << 1;
    constexpr uint32_t Direction = 1 << 2;
    constexpr uint32_t Enabled   = 1 << 3;
    constexpr uint32_t Radius    = 1 << 4;
    constexpr uint32_t All       = Color | Intensity | Direction | Enabled | Radius;
}

namespace CameraDirty {
    constexpr uint32_t Position   = 1 << 0;
    constexpr uint32_t Projection = 1 << 1;
    constexpr uint32_t FOV        = 1 << 2;
    constexpr uint32_t All        = Position | Projection | FOV;
}

namespace EnvironmentDirty {
    constexpr uint32_t Params = 1 << 0; ///< Whole-component granularity.
    constexpr uint32_t All    = Params;
}

} // namespace uz
