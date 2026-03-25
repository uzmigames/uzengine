#pragma once

/// Registers all built-in VFX modules with the module registry.
///
/// Called once during VFXSystem::Initialize() to populate the registry
/// with the standard module set: spawn, update, decay, sprite_render,
/// turbulence, constraint, collision, light_render, mesh_render.
///
/// # Parameters
/// - `registry`: Module registry to populate. Must not be null.

namespace uz {

class ModuleRegistry;

/// Registers all built-in VFX modules (spawn, update, decay, etc.).
///
/// Populates the given registry with factory functions for every core
/// VFX module. Called once from VFXSystem::Initialize(). Adding new
/// built-in modules only requires adding a Register<> call here.
///
/// Registered module names:
/// - `"spawn"`        -- particle emission rate/burst logic
/// - `"update"`       -- per-particle physics (gravity, drag, etc.)
/// - `"decay"`        -- particle lifetime management and recycling
/// - `"sprite_render"`-- billboard sprite size and color evaluation
/// - `"turbulence"`   -- noise-based velocity perturbation
/// - `"constraint"`   -- position/velocity boundary constraints
/// - `"collision"`    -- depth-buffer collision detection and response
/// - `"light_render"` -- post-compute transient point light injection
/// - `"mesh_render"`  -- instanced static mesh particle rendering
///
/// # Parameters
/// - `registry`: The module registry to populate. Must be empty or
///   ready to accept new registrations (duplicates log a warning).
///
/// # Side Effects
/// - Logs `VFX/Info` with the total number of registered modules.
void RegisterBuiltinModules(ModuleRegistry& registry);

} // namespace uz
