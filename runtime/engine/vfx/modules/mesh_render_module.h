#pragma once

/// VFX Mesh Render Module -- instanced 3D mesh particle rendering configuration.
///
/// Controls per-particle mesh rendering properties: which mesh and material to use,
/// per-instance scale, shadow casting, and how particles orient themselves relative
/// to their velocity or world axes.
///
/// This module replaces billboard quads with full 3D meshes (e.g., debris chunks,
/// leaves, sparks). A compute shader (particle_mesh_instance_cs.hlsl) builds a
/// RWStructuredBuffer<float4x4> of per-instance transforms, which is then consumed
/// by a DrawIndexedInstancedIndirect call in the particle mesh render pass.
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
///
/// # Design
/// Follows the same module pattern as SpriteRenderModule: the CPU module stores
/// configuration and emits an HLSL snippet; the actual rendering is performed by
/// a dedicated render pass (AddParticleMeshRenderPass).

#include "vfx_module.h"

#include "engine/math/vector3.h"

#include <string>
#include <string_view>
#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MeshRotationMode -- particle orientation strategy
// ─────────────────────────────────────────────────────────────────────────────

/// Determines how a mesh particle's rotation is computed each frame.
///
/// | Mode            | Behaviour                                               |
/// |-----------------|----------------------------------------------------------|
/// | None            | Identity orientation -- mesh always faces the same way.  |
/// | VelocityAligned | Forward axis (+Z) aligns with the velocity vector.       |
/// | Random          | Deterministic random rotation derived from particle seed.|
enum class MeshRotationMode : uint8_t {
    None,            ///< No rotation — all particles share identity orientation.
    VelocityAligned, ///< Forward (+Z) tracks velocity; up = world Y with fallback.
    Random,          ///< Per-particle pseudo-random rotation (seed = particle index).
};

/// Converts a MeshRotationMode to its string representation.
///
/// Used for serialization and logging.
///
/// # Returns
/// - A null-terminated string literal (static lifetime).
/// - `"Unknown"` for unrecognised enum values.
[[nodiscard]] const char* MeshRotationModeToString(MeshRotationMode mode) noexcept;

/// Converts a string to MeshRotationMode.
///
/// Case-sensitive. Recognised values: `"None"`, `"VelocityAligned"`, `"Random"`.
///
/// # Returns
/// The matching `MeshRotationMode`.
///
/// # Errors
/// Returns `MeshRotationMode::None` for unrecognised strings and logs a warning.
[[nodiscard]] MeshRotationMode MeshRotationModeFromString(std::string_view str);

// ─────────────────────────────────────────────────────────────────────────────
// MeshRenderModuleConfig -- all knobs for instanced mesh particle rendering
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the instanced mesh particle render module.
///
/// Separating config from the module class allows inexpensive copying for
/// snapshot-style serialization and editor undo/redo without cloning the
/// full module object.
///
/// # Ownership
/// `meshPath` and `materialPath` are VFS-relative asset paths. The module
/// does not own the assets -- the engine's asset system manages lifetimes.
struct MeshRenderModuleConfig {
    /// VFS-relative path to the mesh asset (e.g., `"meshes/debris_chunk.uzmesh"`).
    ///
    /// Must be a non-empty, valid VFS path before the emitter first ticks.
    std::string meshPath;

    /// VFS-relative path to the material asset (e.g., `"materials/debris.uzmat"`).
    ///
    /// The material must support instanced rendering via an instance transform buffer.
    std::string materialPath;

    /// Uniform scale applied to every mesh instance on top of the particle's size.
    ///
    /// Each component is applied independently, enabling non-uniform scaling.
    /// Default (1, 1, 1) preserves the mesh's original dimensions.
    Vec3 meshScale{1.0f, 1.0f, 1.0f};

    /// Whether mesh particles cast shadows into the scene shadow maps.
    ///
    /// Enabling shadow casting for dense particle systems is expensive.
    /// Disable for short-lived cosmetic debris.
    bool castShadow = false;

    /// Particle orientation strategy. See MeshRotationMode for details.
    MeshRotationMode rotationMode = MeshRotationMode::VelocityAligned;
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshRenderModule -- IVFXModule for instanced 3D mesh particle rendering
// ─────────────────────────────────────────────────────────────────────────────

/// Instanced 3D mesh rendering module for VFX particle systems.
///
/// Each alive particle is rendered as a small 3D mesh instance (e.g., a debris
/// chunk, leaf, or spark shard) instead of a camera-facing billboard. GPU
/// instancing is used: a compute pass writes one float4x4 per alive particle
/// into a RWStructuredBuffer<float4x4>, which is then bound as an SRV for a
/// DrawIndexedInstancedIndirect graphics pass.
///
/// # HLSL Snippet
/// `GetShaderCode()` returns an HLSL snippet that computes the per-particle
/// transform and writes it into `MeshInstanceTransforms[particleIdx]`. The
/// rotation strategy is selected via a compile-time branch on `rotationMode`.
///
/// # Render Pass Integration
/// The module's HLSL snippet is executed by the instance transform CS
/// (particle_mesh_instance_cs.hlsl), not by the compositor's combined simulation
/// shader. The particle mesh render pass (AddParticleMeshRenderPass) consumes
/// the resulting instance buffer.
///
/// # Parameters
/// | Name          | Type  | Default        | Description                      |
/// |---------------|-------|----------------|----------------------------------|
/// | scale_x       | Float | 1.0            | Per-instance X scale             |
/// | scale_y       | Float | 1.0            | Per-instance Y scale             |
/// | scale_z       | Float | 1.0            | Per-instance Z scale             |
/// | cast_shadow   | Bool  | false          | Shadow casting toggle            |
/// | rotation_mode | Int   | VelocityAligned| See MeshRotationMode             |
///
/// # Thread Safety
/// Not thread-safe. Owned and invoked by VFXSystem on the main/render thread.
class MeshRenderModule final : public IVFXModule {
public:
    /// Default-constructs the module with `MeshRenderModuleConfig` defaults.
    ///
    /// Equivalent to `MeshRenderModule(MeshRenderModuleConfig{})`. Required for
    /// `ModuleRegistry::Register<MeshRenderModule>()` which instantiates modules
    /// via default construction before JSON config is applied at asset load time.
    MeshRenderModule();

    /// Constructs the module with an explicit configuration.
    ///
    /// # Parameters
    /// - `config`: Rendering configuration (mesh path, material, scale, etc.).
    ///   `config.meshPath` and `config.materialPath` must be non-empty.
    explicit MeshRenderModule(const MeshRenderModuleConfig& config);

    ~MeshRenderModule() override = default;

    // ── IVFXModule interface ─────────────────────────────────────────────────

    /// Returns the HLSL snippet that writes per-particle instance transforms.
    ///
    /// The snippet reads from the standard particle attribute SRVs (positions,
    /// velocities, ages, lifespans) and writes a float4x4 instance transform
    /// into `RWStructuredBuffer<float4x4> MeshInstanceTransforms` for each
    /// alive particle. It also increments `MeshInstanceCount[0]` atomically.
    ///
    /// Dead particles (age >= lifespan) are skipped -- no transform is emitted.
    ///
    /// # Returns
    /// Non-empty string view containing valid HLSL. Lifetime matches this module.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns the module's registration name.
    ///
    /// # Returns
    /// `"mesh_render"` — stable identifier used for serialization.
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns parameter definitions for editor UI and constant buffer layout.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// No-op on the CPU side.
    ///
    /// Transform generation is performed by the instance CS dispatch
    /// (particle_mesh_instance_cs.hlsl) rather than this module's Execute hook.
    void Execute(const ExecuteContext& ctx) override;

    // ── Configuration accessors ──────────────────────────────────────────────

    /// Returns the current rendering configuration (read-only).
    [[nodiscard]] const MeshRenderModuleConfig& GetConfig() const noexcept;

    // ── Serialization helpers ────────────────────────────────────────────────

    /// Parses a MeshRenderModuleConfig from a JSON object.
    ///
    /// Recognised fields:
    /// | JSON key        | C++ field            | Type          |
    /// |-----------------|----------------------|---------------|
    /// | `mesh_path`     | `meshPath`           | string        |
    /// | `material_path` | `materialPath`       | string        |
    /// | `mesh_scale`    | `meshScale`          | [x, y, z]     |
    /// | `cast_shadow`   | `castShadow`         | bool          |
    /// | `rotation_mode` | `rotationMode`       | string        |
    ///
    /// Missing fields retain their default values. Unknown fields are ignored.
    ///
    /// # Parameters
    /// - `j`: Parsed JSON object. Must be of object type.
    ///
    /// # Returns
    /// Fully populated `MeshRenderModuleConfig` with defaults for absent fields.
    ///
    /// # Errors
    /// Logs `VFX/Warning` for unrecognised `rotation_mode` strings.
    [[nodiscard]] static MeshRenderModuleConfig ParseConfig(const nlohmann::json& j);

private:
    MeshRenderModuleConfig config_;
};

} // namespace uz
