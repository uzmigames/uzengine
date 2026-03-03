# Proposal: phase4-destructible-meshes

## Why

Action and shooter games need interactive environmental destruction: walls that shatter, crates that splinter, vehicles that crumple. Procedural fracture at runtime is too expensive; instead, meshes are pre-fractured offline into a `.uzfrac` file containing fragments with precomputed geometry and connectivity data. At runtime, `CDestructible` triggers fracture into Jolt rigid body debris when health reaches zero, with pooled debris management to limit active fragment count.

## What Changes

- **Offline tool** — `uzfrac` tool (separate exe): takes mesh + fracture params, produces `.uzfrac` file
- **`ecs/components.h`** — `CDestructible` component: `frac_path`, `health`, `damage_threshold`, `debris_lifetime_s`
- **`src/engine/physics/destructible_system.h/cpp`** — `DestructibleSystem`: handles damage, triggers fracture
- **`src/engine/physics/debris_pool.h/cpp`** — `DebrisPool`: pools Jolt bodies for fragments (max 256 active)
- **EventBus** — `DestructibleBrokenEvent` for gameplay responses
- **Tests** — damage accumulation, fracture trigger, debris pool limits, event dispatch

## Impact

- Affected specs: `docs/specs/physics-system.md` (destructibles section)
- Dependencies: `phase3-physics-system` (Jolt), `phase3-world-streaming` (debris respawn on chunk reload)
- Breaking change: NO — additive; non-destructible meshes unaffected
- User benefit: interactive destruction without physics performance cost (precomputed fragments, pooled debris)
