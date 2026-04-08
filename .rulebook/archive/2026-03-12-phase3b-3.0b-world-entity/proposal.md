# Proposal: 3.0b — World + Entity

## Why
Foundation for all gameplay systems. Entity creation, generational handles, and sparse-set storage via EnTT. No rendering dependency — pure simulation. Every gameplay feature (physics, animation, scripting) depends on this.

## What Changes
- EnTT integration: entity CRUD, component add/remove/query
- Generational handles: use-after-destroy detection in debug
- World class: simulation container, entity ownership, subsystem host
- World registered as ISubsystem in EngineLoop

## Impact
- Affected specs: `docs/engine/10-entity-component/`
- Affected code: `runtime/core/ecs/`
- Breaking change: NO (new system)
- User benefit: Entity-component architecture for all gameplay
