#pragma once

/// VFX Advection Velocity Field Data Interface.
///
/// Samples a 3D velocity texture (Texture3D<float3>) and *overrides* each
/// particle's velocity with the sampled value (blended by a tightness
/// factor). Contrast with the Vector Field DI (phase 4.15), which *adds*
/// a force to the current velocity — advection drives particles along the
/// field, force-fields push them.
///
/// Common use cases:
///   - Painted smoke flow following an author-defined path.
///   - Wind tunnels where particles must follow the tunnel geometry.
///   - Stylised magic plume trails tracing a hand-authored curve.
///
/// # Fields
///
/// The velocity field is a `Texture3D<float3>` covering a world-space
/// AABB. `SampleAdvection(worldPos)` remaps the world position into [0,1]³
/// UV coordinates and samples with trilinear filtering. Out-of-bounds
/// samples return `(0, 0, 0)`.
///
/// # Tightness blend
///
/// The update CS blends current velocity with the advected velocity:
///
/// ```
/// float3 v = lerp(particle.velocity, sampleVel, tightness)
/// ```
///
/// - `tightness = 0` — pure physics (field has no effect).
/// - `tightness = 0.5` — half-mix (loose follow).
/// - `tightness = 1` — hard follow (velocity is fully taken from field).
///
/// Authors typically tune tightness based on desired path-follow rigour.
///
/// # Thread safety
/// Pure CPU helpers are safe to call from any thread. The bindless SRV
/// index is immutable after registration so the GPU-side reads are
/// race-free.
///
/// @ref UE5 UNiagaraDataInterfaceVelocityGrid
/// @ref docs/engine/25-vfx/38-advection-field.md

#include "vector3.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for one advection-field data interface.
struct VfxAdvectionFieldConfig {
    /// Bindless SRV index of the Texture3D<float3> velocity field.
    /// `0xFFFFFFFF` disables sampling (returns zero velocity).
    uint32_t fieldSrvIndex = 0xFFFFFFFFu;

    /// World-space AABB that the field covers. Positions outside this
    /// box read as zero velocity (no extrapolation beyond the painted
    /// region).
    Vec3 worldMin{0, 0, 0};
    Vec3 worldMax{0, 0, 0};

    /// Tightness blend factor in [0, 1]. See header docs.
    float tightness = 0.5f;

    /// Returns true when the config describes a usable field (SRV bound
    /// and AABB is non-degenerate).
    [[nodiscard]] bool IsEnabled() const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// World-to-field remap
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a world-space position into the field's [0,1]³ UV space.
///
/// Returns `(u, v, w)` in the returned `Vec3` (re-using the Vec3 type for
/// convenience). Out-of-bounds positions return negative or >1 components
/// so the caller can detect and return zero without branching.
///
/// The result is NOT clamped — callers can trilinear-sample with Clamp
/// addressing to get edge extrapolation or use an in-range guard to zero
/// out-of-field samples.
[[nodiscard]] Vec3 VfxAdvectionWorldToField(const VfxAdvectionFieldConfig& cfg,
                                              const Vec3& worldPos) noexcept;

/// Returns true when `uvw` is inside the [0,1]³ field box.
[[nodiscard]] bool VfxAdvectionIsInside(const Vec3& uvw) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Tightness blend
// ─────────────────────────────────────────────────────────────────────────────

/// Blends the current particle velocity with the advected sample.
///
/// Tightness is clamped to [0, 1] so out-of-range config doesn't produce
/// unstable output. Out-of-field positions (detected via
/// `VfxAdvectionIsInside`) fall through to the current velocity so
/// particles that exit the painted region keep their inertia.
[[nodiscard]] Vec3 VfxAdvectionBlendVelocity(const Vec3& currentVel,
                                               const Vec3& advectedVel,
                                               bool insideField,
                                               float tightness) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet emitter
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the HLSL snippet that declares `SampleAdvection(worldPos)` and
/// performs the velocity blend at the update-CS dispatch site. Emitted
/// once per emitter permutation that uses an advection DI.
[[nodiscard]] const char* VfxAdvectionEmitHLSLSnippet() noexcept;

} // namespace uz
