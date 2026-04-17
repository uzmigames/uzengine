/// UzEngine VfxSystemComponent — ECS component for compound VFX systems.
///
/// References a `.uzvfxsys` asset so the scene renderer can create a
/// `VfxSystemInstance` (owning multiple child `ParticleEmitter`s) for the
/// entity. Unlike `VfxEmitterComponent`, a single `VfxSystemComponent`
/// produces *N* GPU emitters from one entity — the compound/Niagara model.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// world.AddComponent<SceneComponent>(entity);   // position / transform
/// auto& sys = world.AddComponent<VfxSystemComponent>(entity);
/// sys.systemAssetPath = "vfx/systems/fireball.uzvfxsys";
/// sys.emitting = true;
/// ```
///
/// # Relationship to `VfxEmitterComponent`
/// Both components can coexist in the same World. The scene renderer
/// iterates each view separately. Use `VfxEmitterComponent` for a single
/// emitter (simple effect), `VfxSystemComponent` for a compound effect
/// that should travel as one unit.
///
/// @ref UE5 `UNiagaraComponent` — scene component referencing a system asset

#pragma once

#include <cstdint>
#include <string>

namespace uz {

/// ECS component referencing a compound VFX system asset (`.uzvfxsys`).
struct VfxSystemComponent {
    /// Path to the `.uzvfxsys` asset (VFS-relative when a VFS is attached,
    /// otherwise disk path). Must be set before the component is first
    /// processed by the scene renderer.
    std::string systemAssetPath;

    /// When true, child emitters spawn new particles.
    /// When false, spawning stops but existing particles keep simulating.
    bool emitting = true;

    /// Set to true to request a full GPU reset on all child emitters
    /// (clears alive particles, re-uploads free lists, resumes spawning).
    /// The scene sync code clears this flag after handling.
    bool restartRequested = false;

    /// Index into `SceneRenderer::m_runtimeSystems` once the instance has
    /// been created. -1 means "not yet instantiated". Managed by the
    /// engine; do not modify from gameplay code.
    int32_t runtimeInstanceIdx = -1;

    // ── Culling state (phase 10.5.0) ─────────────────────────────────────

    /// Mirrors `uz::EmitterCullState` fields (same layout as
    /// `VfxEmitterComponent::cullCulled*`). The scene renderer treats the
    /// compound system's AABB as a single sphere for the frustum test.
    /// @ref engine/vfx/lod/vfx_frustum_cull.h
    bool     cullCulledThisFrame   = false;
    bool     cullCulledPrevFrame   = false;
    uint32_t cullFramesSinceCulled = 0;
};

} // namespace uz
