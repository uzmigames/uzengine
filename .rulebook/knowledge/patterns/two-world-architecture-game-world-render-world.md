# Two-World Architecture (Game World + Render World)

**Category**: architecture
**Tags**: ecs, renderer, decoupling, proxy, two-world, threading

## Description

UzEngine separates game state from GPU state via two parallel worlds: (1) Game World — EnTT registry with Components driving gameplay logic (SceneComponent, StaticMeshComponent, LightComponent, etc.), (2) Render World — flat RenderProxy pool with POD structs optimized for GPU submission (no virtual dispatch). Single sync point per frame via SyncToRenderWorld() bridges them. Both use generational handles for use-after-free prevention. This enables future multi-threading: game thread mutates World, render thread reads RenderWorld.

## Example

// Game World side:\nauto& mesh = world.AddComponent<StaticMeshComponent>(entity);\nmesh.meshPath = \"meshes/cube.gltf\";\n\n// Sync point (once per frame):\nmesh.SyncToRenderWorld(renderWorld);\n\n// Render World side (hot path):\nfor (const auto& proxy : renderWorld.GetProxies()) {\n    if (proxy.visible) submitDrawCall(proxy);\n}

## When to Use

When adding new renderable component types. When implementing culling, LOD, or visibility systems. When planning multi-threaded rendering.

## When NOT to Use

For non-rendered components (input, AI, scripts) — they only exist in Game World.
