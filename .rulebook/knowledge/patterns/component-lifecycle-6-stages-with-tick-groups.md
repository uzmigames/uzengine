# Component Lifecycle (6 Stages) with Tick Groups

**Category**: architecture
**Tags**: ecs, lifecycle, tick-groups, components, ordering

## Description

Components follow a 6-stage lifecycle: Register → Initialize → BeginPlay → (Tick) → EndPlay → Uninitialize → Unregister. TickableComponent subclasses receive OnTick(dt) dispatched by TickSystem into 4 groups: PrePhysics (input, animation), PostPhysics (physics queries), Update (game logic, AI — default), PostUpdate (audio sync, UI). Tick can be disabled per-component, throttled via tickInterval, and tracks timeSinceLastTick. TickSystem iterates per-group vectors, skips disabled/invalid entities.

## Example

class AIController : public TickableComponent {\n    TickGroup GetTickGroup() const override { return TickGroup::Update; }\n    void OnTick(float dt) override {\n        // AI decision-making, runs after physics\n    }\n};

## When to Use

When creating components that need per-frame updates. When ordering system execution (e.g., input before physics before AI). When throttling expensive updates (LOD tick interval).

## When NOT to Use

For data-only components (CName, CScript) that are read by systems rather than ticking themselves.
