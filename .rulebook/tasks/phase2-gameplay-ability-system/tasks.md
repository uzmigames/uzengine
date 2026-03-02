## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-timer-system` is complete — `ctx:schedule_after`, `ctx:cancel_timer` work
- [ ] 1.2 Confirm `phase1-shared-state` is complete — `ctx.shared` for reading mana/stamina
- [ ] 1.3 Confirm `phase3-event-bus` is complete — `EventBus<T>` and Lua string dispatch exist
- [ ] 1.4 Read `docs/specs/event-bus.md` — understand `EventBus::post` and Lua subscription

## 2. AbilitySpec Data Struct

- [ ] 2.1 Create `src/engine/abilities/ability_system.h`
- [ ] 2.2 Define `AbilityCost` struct: `attribute_key: string`, `amount: float` (e.g., `"mana"`, `50.0`)
- [ ] 2.3 Define `AbilitySpec` struct:
  - `name: string` — unique ability name within component
  - `cooldown_s: float` — seconds; 0 = no cooldown
  - `costs: vector<AbilityCost>` — list of attribute costs
  - `tags: vector<string>` — tags for blocking/cancellation (e.g., `"movement"`, `"attack"`)
  - `cancel_on_tags: vector<string>` — abilities with these tags cancel this ability
  - `block_tags: vector<string>` — if entity has active ability with these tags, cannot activate
  - `max_activations: int` — -1 = unlimited; positive = stacking limit
  - `on_activate: int` (Lua function ref) — called on activation
  - `on_end: int` (Lua function ref) — called on completion
  - `on_cancel: int` (Lua function ref) — called on cancellation

## 3. AbilityComponent

- [ ] 3.1 Add `AbilityComponent` to `ecs/components.h`
- [ ] 3.2 Fields: `granted_abilities: map<string, AbilitySpec>`, `active_abilities: map<string, AbilityInstance>`
- [ ] 3.3 Define `AbilityInstance` struct: `spec_name`, `activation_time`, `activation_count`

## 4. AbilitySystem ECS System

- [ ] 4.1 Create `src/engine/abilities/ability_system.cpp` — implements `IEcsSystem`
- [ ] 4.2 Register with `SystemRegistry` in `Update` group
- [ ] 4.3 Implement `AbilitySystem::tick(registry, dt)` — advance cooldowns (decrement timers)
- [ ] 4.4 Implement `AbilitySystem::grant(entity, AbilitySpec)` — adds spec to `AbilityComponent.granted_abilities`
- [ ] 4.5 Implement `AbilitySystem::revoke(entity, name)` — removes spec; cancels if active
- [ ] 4.6 Implement `AbilitySystem::activate(entity, name) -> ActivationResult`:
  - Check: ability granted
  - Check: not blocked by active ability tags (`block_tags`)
  - Check: cooldown elapsed
  - Check: attribute costs met (read from `GameState`)
  - Deduct costs from `GameState`
  - Create `AbilityInstance`, add to `active_abilities`
  - Call Lua `on_activate` via `lua_pcall`
  - Post `AbilityActivatedEvent` to EventBus
- [ ] 4.7 Implement `AbilitySystem::end_ability(entity, name)`:
  - Remove from `active_abilities`
  - Start cooldown timer
  - Call Lua `on_end`
  - Post `AbilityEndedEvent`
- [ ] 4.8 Implement `AbilitySystem::cancel(entity, name)`:
  - Remove from `active_abilities`
  - Start cooldown timer (half duration on cancel)
  - Call Lua `on_cancel`
  - Post `AbilityCancelledEvent`
- [ ] 4.9 Implement tag-based cancellation: when ability activates, cancel all active abilities whose tags appear in new ability's `cancel_on_tags`

## 5. EventBus Integration

- [ ] 5.1 Define `AbilityActivatedEvent`: `entity`, `ability_name`, `activation_count`
- [ ] 5.2 Define `AbilityEndedEvent`: `entity`, `ability_name`
- [ ] 5.3 Define `AbilityCancelledEvent`: `entity`, `ability_name`, `reason: string`
- [ ] 5.4 Define `AbilityFailedEvent`: `entity`, `ability_name`, `reason: string` (`"on_cooldown"`, `"insufficient_cost"`, `"blocked"`)

## 6. Lua API

- [ ] 6.1 Implement `ctx.abilities:grant(spec_table)` — constructs `AbilitySpec` from Lua table, calls `AbilitySystem::grant`
- [ ] 6.2 Implement `ctx.abilities:revoke(name)`
- [ ] 6.3 Implement `ctx.abilities:activate(name) -> bool` — returns false if rejected
- [ ] 6.4 Implement `ctx.abilities:cancel(name)`
- [ ] 6.5 Implement `ctx.abilities:end_ability(name)` — signals ability completed normally
- [ ] 6.6 Implement `ctx.abilities:is_active(name) -> bool`
- [ ] 6.7 Implement `ctx.abilities:cooldown_remaining(name) -> float` — seconds; 0 if ready
- [ ] 6.8 Implement `ctx.abilities:get_active() -> table` — array of active ability names

## 7. Testing

- [ ] 7.1 Unit test: `activate` with insufficient cost → returns false, `AbilityFailedEvent` with `"insufficient_cost"`
- [ ] 7.2 Unit test: `activate` while on cooldown → returns false, `AbilityFailedEvent` with `"on_cooldown"`
- [ ] 7.3 Unit test: `activate` blocked by active ability tag → returns false
- [ ] 7.4 Unit test: successful `activate` → `on_activate` Lua called, cost deducted from `GameState`
- [ ] 7.5 Unit test: `cancel_on_tags` — activating ability B cancels active ability A when A's tags match
- [ ] 7.6 Unit test: cooldown timer starts after `end_ability`; `is_active` false immediately after end
- [ ] 7.7 Integration test: `ctx.abilities:grant({name="fireball", cooldown_s=2, on_activate=fn})` → `activate("fireball")` calls fn

## 8. Documentation

- [ ] 8.1 Create `docs/specs/gameplay-ability-system.md` — full spec: `AbilitySpec` fields, lifecycle diagram, tag system
- [ ] 8.2 Update `docs/guides/lua-scripting-guide.md` — "Abilities" section with grant/activate/cancel patterns
