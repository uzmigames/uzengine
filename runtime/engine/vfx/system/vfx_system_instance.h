/// UzEngine VfxSystemInstance — runtime counterpart of `VfxSystem` asset.
///
/// Owns a collection of child `ParticleEmitter` instances created from a
/// `VfxSystem` asset. Propagates the parent system's world transform to
/// each child emitter and mirrors the parent's `isEmitting` state.
///
/// # Relationship to existing systems
/// - `VfxSystem` (this file's asset counterpart) — describes *what* emitters
///   make up a compound effect.
/// - `VFXSystem` (global runtime singleton in `engine/vfx/vfx_system.h`) —
///   dispatches compute work for ALL registered emitters each frame.
/// - `VfxSystemInstance` — owns the child `ParticleEmitter*` objects AND
///   registers each one with the global `VFXSystem` so they participate in
///   the per-frame simulation.
///
/// # Lifecycle
/// ```cpp
/// VfxSystemInstance inst;
/// inst.Init(*asset, device, budget);
/// // ...each frame:
/// inst.SetWorldTransform({100, 0, 0});
/// inst.Tick(dt);
/// // ...on shutdown:
/// inst.Destroy(device, budget);
/// ```
///
/// @ref UE5 `FNiagaraSystemInstance` — runtime compound effect instance

#pragma once

#include "vector3.h"
#include "vector4.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class ParticleBudget;
struct ParticleEmitter;
struct VfxSystem;

/// Runtime instance of a `VfxSystem` asset. Owns N child `ParticleEmitter`
/// objects, one per enabled emitter reference in the source asset.
class VfxSystemInstance {
public:
    VfxSystemInstance() = default;
    ~VfxSystemInstance() = default;

    VfxSystemInstance(const VfxSystemInstance&)            = delete;
    VfxSystemInstance& operator=(const VfxSystemInstance&) = delete;
    VfxSystemInstance(VfxSystemInstance&&)                 = default;
    VfxSystemInstance& operator=(VfxSystemInstance&&)      = default;

    /// Instantiates one `ParticleEmitter` per enabled ref in `asset`.
    ///
    /// Each child emitter's configuration is produced by resolving the
    /// ref's `emitterPath`:
    /// - `"preset:<Name>"` -> matching `ApplyPreset_<Name>` function from
    ///   `engine/vfx/components/emitter_presets.h`.
    /// - Any other path is currently rejected with a VFX/Warning log; full
    ///   `.uzfx` loading may be integrated later (the single-emitter sync
    ///   path already handles `.uzfx` via `UzfxLoader`).
    ///
    /// # Arguments
    /// - `asset`:  Source system description. Must outlive this call
    ///             (only read during `Init`, not retained).
    /// - `device`: RHI device for child emitter GPU buffer allocation. Required.
    /// - `budget`: Global particle budget. Each child allocates from here.
    ///
    /// # Returns
    /// `true` when at least one child emitter initialised successfully.
    /// `false` when `device` is null, the asset has no enabled emitters,
    /// or every child `ParticleEmitter::Initialize` call failed.
    [[nodiscard]] bool Init(const VfxSystem& asset, IRHIDevice* device,
                             ParticleBudget& budget);

    /// Releases all child emitters' GPU buffers and returns their
    /// allocations to the budget.
    ///
    /// Safe to call when `Init` was never invoked (no-op in that case).
    /// After this call `GetEmitterCount()` returns zero.
    void Destroy(IRHIDevice* device, ParticleBudget& budget);

    /// Advances the instance by `dt` seconds.
    ///
    /// Per child:
    /// - Mirrors `m_emitting` into the child's `isEmitting` flag.
    /// - Increments the child's `timeSinceRegistration` so preset
    ///   `startDelay` countdowns progress.
    ///
    /// Simulation itself (compute dispatch, GPU updates) happens inside
    /// the global `VFXSystem::Tick` — child emitters are already
    /// registered there when this instance was initialised.
    void Tick(float dt) noexcept;

    /// Sets the world-space origin used for local-space child emitters.
    ///
    /// Local-space children (`VfxSystemEmitterRef::localSpace == true`)
    /// have their `worldPosition` overwritten with `position`. World-space
    /// children stay at their current position (typically the origin where
    /// they were instantiated).
    void SetWorldTransform(const Vec3& position) noexcept;

    /// Enables or disables new-particle spawning across all child emitters.
    /// Existing particles continue to simulate until their natural death.
    void SetEmitting(bool emitting) noexcept { m_emitting = emitting; }

    /// Raw access to child emitters (for registering with the global
    /// `VFXSystem` and for the renderer to iterate during draw setup).
    ///
    /// Returned pointers are owned by this instance — do **not** delete
    /// or move them. Indices are stable for the lifetime of the instance.
    [[nodiscard]] std::vector<ParticleEmitter*> GetEmitters() const;

    /// Number of successfully initialised child emitters.
    [[nodiscard]] size_t GetEmitterCount() const noexcept { return m_emitters.size(); }

    /// Whether new particles are currently being spawned.
    [[nodiscard]] bool IsEmitting() const noexcept { return m_emitting; }

    /// Current world-space origin applied to local-space children.
    [[nodiscard]] Vec3 GetWorldPosition() const noexcept { return m_worldPosition; }

    // ── Named user parameters (7.2.0 system-level parameter store) ──────────
    //
    // Writes land in the System scope of the internal attribute store. Values
    // survive across frames and hot-reload. Readers bind to them via the
    // canonical `System.<name>` key (see docs/engine/25-vfx/20-namespaced-attributes.md).
    //
    // # Example
    // ```cpp
    // inst.SetFloatParameter("intensity", 2.0f);
    // const float intensity = inst.GetFloatParameter("intensity", 1.0f);
    // ```

    /// Sets a float parameter on the System scope.
    void SetFloatParameter(const std::string& name, float value);

    /// Sets a Vec3 parameter on the System scope (stored as Vec4 with w=0).
    void SetVec3Parameter(const std::string& name, const Vec3& value);

    /// Sets a Vec4 parameter on the System scope (canonical colour/gradient format).
    void SetVec4Parameter(const std::string& name, const Vec4& value);

    /// Reads a float parameter; returns `defaultValue` if the parameter has
    /// never been set.
    [[nodiscard]] float GetFloatParameter(const std::string& name,
                                          float defaultValue = 0.0f) const;

    /// Reads a Vec3 parameter; returns `defaultValue` on miss.
    [[nodiscard]] Vec3 GetVec3Parameter(const std::string& name,
                                        const Vec3& defaultValue = {}) const;

    /// Reads a Vec4 parameter; returns `defaultValue` on miss.
    [[nodiscard]] Vec4 GetVec4Parameter(const std::string& name,
                                        const Vec4& defaultValue = {}) const;

    /// Enumerates parameter names currently set on the System scope.
    /// Insertion order preserved (stable for GPU CB layout).
    [[nodiscard]] std::vector<std::string> GetParameterNames() const;

    /// Returns true if `name` has been set at least once and not cleared.
    [[nodiscard]] bool HasParameter(const std::string& name) const;

    /// Drops a single parameter. Returns true if it existed.
    bool RemoveParameter(const std::string& name);

    /// Drops every System-scope parameter. Leaves Particles/Emitter scopes
    /// untouched (they're currently unused at system level but reserved for
    /// future per-system particle defaults).
    void ClearParameters();

    /// Direct access to the underlying attribute store — intended for the
    /// render graph and the HLSL CB generator. Most callers should prefer
    /// the `Set*/Get*Parameter` accessors above.
    [[nodiscard]] VfxAttributeStore&       GetAttributeStore()       noexcept { return m_attributes; }
    [[nodiscard]] const VfxAttributeStore& GetAttributeStore() const noexcept { return m_attributes; }

private:
    /// Child emitters in declaration order from the source asset.
    /// Parallel to `m_localSpace`.
    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;

    /// Per-child local-space flag. When true the child follows
    /// `m_worldPosition`; when false it stays at origin.
    std::vector<bool> m_localSpace;

    /// World-space origin; propagated to local-space children each frame.
    Vec3 m_worldPosition{0.0f, 0.0f, 0.0f};

    /// Master emission flag; mirrored into every child during `Tick`.
    bool m_emitting = true;

    /// Scoped attribute store. Only the `System` scope is populated today
    /// by `Set*Parameter`; per-emitter and per-particle scopes are reserved
    /// for future expansions (7.1.1 runtime migration, 7.12 particle-read DI).
    VfxAttributeStore m_attributes;
};

} // namespace uz
