# Proposal: phase2-physics-materials

## Why

Jolt Physics requires friction and restitution values per shape, but the current `CCollider` component stores only raw floats. There is no way to share material settings between objects, and no way for the audio or particle system to know what surface type was hit (wood, metal, stone) to play the right impact sound or spawn the right sparks.

A `.uzphysmat` file associates a named material with Jolt `PhysicsMaterial` properties (friction, restitution, density) and a `surface_type` tag. `CCollider` references materials by path. The `PhysicsMaterialRegistry` provides fast lookup, and collision events include `surface_type` for audio/particle routing.

## What Changes

- **`src/engine/physics/physics_material.h/cpp`** — `PhysicsMaterial` data struct and `PhysicsMaterialRegistry`
- **`src/engine/assets/physics_material_importer.h/cpp`** — `.uzphysmat` parser
- **`ecs/components.h`** — `CCollider` gains `material_path` field (replaces raw friction/restitution floats)
- **`src/engine/physics/physics_system.cpp`** — shape creation uses `PhysicsMaterialRegistry::get(path)`
- **Collision events** — `CollisionEvent` extended with `surface_type` string from material
- **Tests** — parse, registry lookup, Jolt shape integration, collision event surface type

## Impact

- Affected specs: `docs/specs/physics-system.md` (material section), `docs/specs/data-driven-framework.md` (`.uzphysmat` format)
- Dependencies: `phase3-physics-system` (Jolt physics and CCollider exist), `phase0-data-driven-framework` (format spec)
- Breaking change: INTERNAL — `CCollider` friction/restitution fields replaced by `material_path`; existing `.uzscene` files need migration
- User benefit: reusable physics materials; surface-type-aware impact audio/particles; designer-editable material files
