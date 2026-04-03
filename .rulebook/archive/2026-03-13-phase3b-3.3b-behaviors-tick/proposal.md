# Proposal: 3.3b — Behaviors + Tick

## Why
Game logic framework. Behaviors tick each frame within ordered tick groups, enabling deterministic update ordering (PrePhysics → Physics → PostPhysics → Update → PostUpdate).

## What Changes
- Behavior base class: OnTick(float dt), attachable to entities
- UZ_REGISTER_BEHAVIOR macro: factory registration in ClassRegistry
- TickGroup enum: PrePhysics, Physics, PostPhysics, Update, PostUpdate
- Frame tick loop: EngineLoop calls tick groups each frame

## Impact
- Affected specs: `docs/engine/10-entity-component/`
- Affected code: `runtime/core/ecs/behaviors/`, `runtime/core/ecs/tick/`
- Breaking change: NO (new system)
- User benefit: Structured game logic with deterministic update order
