# Proposal: phase2-gameplay-ability-system

## Why

Action RPG, MOBA, and shooter games define character abilities (attack, dodge, cast spell) each with activation conditions, costs (mana/stamina/cooldown), effects, and cancellation rules. Without a shared Gameplay Ability System, every game reimplements this logic ad hoc in Lua scripts — producing fragile, untestable state machines. A `AbilityComponent` with grant/activate/cancel lifecycle, cost verification, and cooldown tracking gives game developers a reusable ability framework with clean Lua API.

## What Changes

- **`src/engine/abilities/ability_system.h/cpp`** — `AbilitySpec`, `AbilityComponent`, `AbilitySystem` ECS system
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.abilities:grant(spec)`, `ctx.abilities:activate(name)`, `ctx.abilities:cancel(name)`
- **EventBus integration** — `AbilityActivatedEvent`, `AbilityEndedEvent`, `AbilityCancelledEvent`
- **Cooldown tracking** — per-ability timer integrated with `phase1-timer-system`
- **Cost verification** — reads from `GameState` (mana, stamina) before activation
- **Tests** — grant/activate/cancel lifecycle, cooldown enforcement, cost rejection, event dispatch

## Impact

- Affected specs: new `docs/specs/gameplay-ability-system.md`
- Dependencies: `phase3-event-bus` (EventBus), `phase1-timer-system` (cooldowns), `phase1-shared-state` (cost attributes)
- Breaking change: NO — additive ECS system; existing entities unaffected
- User benefit: reusable ability framework; AI can author abilities as Lua specs without game-specific knowledge; consistent cooldown/cost UX across all abilities
