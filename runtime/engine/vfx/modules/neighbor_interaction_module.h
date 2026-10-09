#pragma once

/// VFX Neighbor Interaction Module -- GPU spatial-hash-based particle-to-particle forces.
///
/// Enables flocking (Boids), swarming, and repulsion effects by evaluating
/// per-particle forces against nearby neighbors using a 3D spatial hash grid.
/// Three configurable force components are supported:
///
/// - **Separation**: Pushes particles apart when closer than `separationRadius`.
/// - **Alignment**: Steers each particle's velocity toward the average velocity
///   of neighbors within `alignmentRadius`.
/// - **Cohesion**: Attracts particles toward the center of mass of neighbors
///   within `cohesionRadius`.
///
/// Forces are masked by `forceModeMask` (bit 0 = separation, bit 1 = alignment,
/// bit 2 = cohesion) so any subset can be enabled independently.
///
/// The 3D spatial hash grid is defined by `NeighborGrid3DConfig`, which specifies
/// the AABB and cell resolution. Pass 1 (scatter) bins each alive particle into
/// its grid cell; Pass 2 (neighbor force) iterates the 3x3x3 cell neighborhood
/// to accumulate forces. Both passes are GPU compute shaders.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// @ref Reynolds 1987 "Flocks, Herds, and Schools: A Distributed Behavioral Model"
///      -- separation, alignment, cohesion rules
/// @ref Green 2010 "Particle Simulation using CUDA" GDC
///      -- GPU spatial hashing pattern for N-body simulation

#include "vfx_module.h"

#include <cstdint>
#include <string>
#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// NeighborGrid3DConfig -- spatial hash grid configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Defines the 3D spatial hash grid used for neighbor queries.
///
/// Total cells = cellsX * cellsY * cellsZ.
/// Total cell-list entries = totalCells * maxPerCell.
/// Memory overhead: totalCells * 4 bytes (counts) + entries * 4 bytes (indices).
///
/// | Field       | Default      | Description                               |
/// |-------------|--------------|-------------------------------------------|
/// | gridMinX/Y/Z| -200,0,-200  | AABB minimum corner (cm)                  |
/// | gridExtentX/Y/Z| 400,400,400 | AABB size per axis (cm)                |
/// | cellsX/Y/Z  | 32           | Grid resolution per axis                  |
/// | maxPerCell   | 8            | Max particles recorded per cell           |
struct NeighborGrid3DConfig {
    float gridMinX    = -200.0f;  ///< AABB min X (cm).
    float gridMinY    = -200.0f;  ///< AABB min Y (cm).
    float gridMinZ    =    0.0f;  ///< AABB min Z (cm, Z-up).

    float gridExtentX =  400.0f;  ///< AABB extent X (cm).
    float gridExtentY =  400.0f;  ///< AABB extent Y (cm).
    float gridExtentZ =  400.0f;  ///< AABB extent Z (cm).

    uint32_t cellsX     = 32;    ///< Grid cells along X.
    uint32_t cellsY     = 32;    ///< Grid cells along Y.
    uint32_t cellsZ     = 32;    ///< Grid cells along Z.
    uint32_t maxPerCell = 8;     ///< Max particles per cell slot.
};

// ─────────────────────────────────────────────────────────────────────────────
// NeighborInteractionModule -- Boids-style particle-to-particle forces
// ─────────────────────────────────────────────────────────────────────────────

/// Boids-style neighbor interaction module for GPU particles.
///
/// Implements `IVFXModule`. Unlike most modules, `GetShaderCode()` returns
/// an empty string because neighbor interaction requires dedicated compute
/// passes (grid scatter + neighbor force) rather than an inline HLSL snippet
/// in the standard update shader.
///
/// # Parameters
/// | Name                | Type  | Default | Description                              |
/// |---------------------|-------|---------|------------------------------------------|
/// | separation_radius   | Float | 10.0    | Distance (cm) below which separation triggers |
/// | separation_strength | Float | 200.0   | Separation force magnitude (cm/s^2)      |
/// | alignment_radius    | Float | 30.0    | Radius (cm) for velocity alignment       |
/// | alignment_strength  | Float | 50.0    | Alignment force magnitude                |
/// | cohesion_radius     | Float | 50.0    | Radius (cm) for center-of-mass cohesion  |
/// | cohesion_strength   | Float | 20.0    | Cohesion force magnitude                 |
/// | force_mode_mask     | Int   | 7       | Bitmask: bit0=sep, bit1=align, bit2=coh  |
///
/// # Example
/// ```cpp
/// NeighborInteractionModule module;
/// module.separationRadius = 15.0f;
/// module.forceModeMask = 0b001; // separation only
/// stack.Push(std::make_unique<NeighborInteractionModule>(module));
/// ```
class NeighborInteractionModule final : public IVFXModule {
public:
    NeighborInteractionModule() = default;
    ~NeighborInteractionModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns `"neighbor_interaction"`.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns an empty string -- this module uses dedicated compute passes,
    /// not an inline HLSL snippet in the standard update shader.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns parameter definitions for Boids force configuration.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// Applies JSON configuration to the module's public fields.
    void ApplyJsonConfig(const nlohmann::json& config) override;

    /// No-op. Neighbor force computation runs entirely on the GPU via
    /// dedicated compute passes orchestrated by the render graph.
    void Execute(const ExecuteContext& ctx) override;

    // -- Configuration (public for editor and serialization) -----------------

    /// Spatial hash grid configuration.
    NeighborGrid3DConfig gridConfig;

    /// Separation distance threshold (cm). Particles closer than this are
    /// pushed apart. Corresponds to the "avoidance" zone in Reynolds' model.
    float separationRadius   = 10.0f;

    /// Separation force magnitude (cm/s^2). Higher values produce stronger
    /// repulsion at close range.
    float separationStrength = 200.0f;

    /// Alignment query radius (cm). Each particle aligns its velocity with
    /// the average velocity of neighbors within this radius.
    float alignmentRadius    = 30.0f;

    /// Alignment force magnitude. Higher values make particles converge on
    /// a common heading more quickly.
    float alignmentStrength  = 50.0f;

    /// Cohesion query radius (cm). Each particle steers toward the center
    /// of mass of neighbors within this radius.
    float cohesionRadius     = 50.0f;

    /// Cohesion force magnitude. Higher values produce tighter flocks.
    float cohesionStrength   = 20.0f;

    /// Bitmask enabling individual force components.
    /// - Bit 0 (0x1): Separation
    /// - Bit 1 (0x2): Alignment
    /// - Bit 2 (0x4): Cohesion
    /// Default 0b111 = all three enabled.
    uint32_t forceModeMask   = 0b111;
};

} // namespace uz
