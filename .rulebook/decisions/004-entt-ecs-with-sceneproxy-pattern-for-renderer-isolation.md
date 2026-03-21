# 4. EnTT ECS with SceneProxy Pattern for Renderer Isolation

**Status**: proposed
**Date**: 2026-03-15

## Context

Game engines need a way to manage entities and components efficiently. The renderer must access entity data without coupling to the scene/gameplay systems. UE5 uses SceneProxy, Unity uses SRP batch culling, Godot uses RenderingServer commands.

## Decision

Use EnTT (header-only C++ ECS) for entity-component management. Adopt UE5's SceneProxy pattern: SceneEnvironment gatherer collects per-frame snapshots of all environment components (sky, lights, fog, post-process). Renderer reads data copies, never accesses scene directly. Singletons (sky, directional light) use first-found semantics. Arrays (point/spot lights) accumulate up to caps.

## Alternatives Considered

- Custom ECS from scratch (high effort, EnTT already battle-tested)
- Unity DOTS-style archetype storage (complex, EnTT supports sparse sets)
- Direct scene access from renderer (tight coupling, threading issues)
- Godot RenderingServer deferred commands (good but adds latency)

## Consequences

Pros: EnTT is header-only, cache-friendly sparse set storage, proven at scale, SceneProxy decouples render from game thread. Cons: EnTT API is C++ template-heavy, SceneProxy requires careful sync points. Key pattern: renderer never calls registry.get<Component>() directly — always goes through gathered EnvironmentState.
