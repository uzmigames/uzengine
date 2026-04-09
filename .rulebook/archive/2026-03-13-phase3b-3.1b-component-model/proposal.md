# Proposal: 3.1b — Component Model

## Why
Component hierarchy with lifecycle callbacks is the base for all gameplay and rendering components. SceneComponent provides the transform hierarchy that everything (physics, rendering, audio) depends on.

## What Changes
- Component base class: Register → Initialize → BeginPlay → EndPlay → Uninitialize lifecycle
- ActorComponent: owning entity ref, enable/disable
- SceneComponent: local/world transform, parent-child hierarchy, dirty flags
- WorldSubsystem base: InitializeDependency<T> for auto-ordering
- MulticastDelegate + deferred EventQueue

## Impact
- Affected specs: `docs/engine/10-entity-component/02-component-model.md`
- Affected code: `runtime/core/ecs/components/`, `runtime/core/ecs/subsystems/`
- Breaking change: NO (new system)
- User benefit: Structured component lifecycle, transform hierarchy, event system
