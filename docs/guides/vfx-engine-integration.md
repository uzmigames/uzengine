<!-- doc-meta
title: VFX Engine Integration Guide
description: Migrating VFX demos from DemoApplication to Application (ECS-driven)
author: cpp-core-expert
date: 2026-04-03
-->

# VFX Engine Integration Guide

How to migrate a `DemoApplication`-based VFX demo to the engine's `Application`
framework, using ECS components instead of direct GPU particle management.

## Architecture Comparison

| Aspect            | DemoApplication (legacy)         | Application (engine)           |
|-------------------|----------------------------------|--------------------------------|
| Base class        | `DemoApplication`                | `uz::Application`              |
| GPU access        | Direct `IRHIDevice`, cmdList     | Hidden behind `SceneRenderer`  |
| VFX particles     | Manual `VFXParticleSystem`       | ECS `VfxEmitterComponent`      |
| Camera            | Manual `FlyCameraController`     | Owned by `SceneRenderer`       |
| Rendering         | Manual draw calls per pass       | Automatic `RenderFrame()`      |
| Window/fences     | Managed by `DemoApplication`     | Managed by `Application::Run`  |
| VFX system        | Manual dispatch calls            | Auto-synced in `RenderFrame()` |

## Step-by-Step Migration

### 1. Create Application subclass

```cpp
#include "engine/app/application.h"
#include "engine/ecs/components.h"
#include "engine/ecs/core/world.h"

class VFXViewportApp final : public uz::Application {
    ApplicationConfig Configure() override;
    void OnStart() override;
    void OnUpdate(float dt) override;
    void OnStop() override;
};

UZ_APPLICATION_MAIN(VFXViewportApp)
```

### 2. Configure window and camera

```cpp
ApplicationConfig VFXViewportApp::Configure() {
    ApplicationConfig cfg;
    cfg.title = "VFX Viewport";
    cfg.width = 1280;
    cfg.height = 720;
    cfg.cameraMoveSpeed = 1500.f;
    cfg.cameraPositionZ = 800.f;
    return cfg;
}
```

### 3. Create emitter entities in OnStart

The `SceneRenderer` owns a `VFXSystem` and automatically syncs
`VfxEmitterComponent` entities to GPU `ParticleEmitter` instances each
frame via `SyncVfxEmitters()`. No manual GPU buffer management needed.

```cpp
void VFXViewportApp::OnStart() {
    World& world = GetWorld();
    Entity entity = world.CreateEntity();

    auto& scene = world.AddComponent<SceneComponent>(entity);
    scene.SetPosition({0.f, 0.f, 0.f});

    auto& emitter = world.AddComponent<VfxEmitterComponent>(entity);
    emitter.maxParticles  = 10000;
    emitter.emissionRate  = 300.0f;
    emitter.lifetime      = 3.0f;
    emitter.gravity       = {0.0f, 0.0f, -981.0f};
    emitter.spawnVelocity = {0.0f, 0.0f, 500.0f};
    emitter.emitting      = true;
}
```

### 4. Handle input and animation in OnUpdate

The fly camera is updated automatically by `SceneRenderer`. Only handle
demo-specific input (toggling emitters, animating positions, quit gesture).

```cpp
void VFXViewportApp::OnUpdate(float dt) {
    InputSystem& input = GetInput();

    // Escape-hold-to-quit (1 second)
    if (input.IsKeyDown(Key::Escape)) { /* accumulate timer, Quit() */ }

    // Animate emitter position via SceneComponent
    auto& scene = world.GetComponent<SceneComponent>(emitterEntity);
    scene.SetPosition({radius * cos(time), radius * sin(time), height});
}
```

### 5. Cleanup in OnStop

Entities are automatically destroyed when the `World` shuts down after
`OnStop()` returns. The `SceneRenderer` unregisters emitters from the
`VFXSystem` during its own shutdown. No manual cleanup is typically needed.

```cpp
void VFXViewportApp::OnStop() {
    m_emitterEntities.clear(); // Just clear our handle vector
}
```

### 6. Build configuration

Set `use_demo_application = false` in `build.zig` since Application-based
demos do not use `DemoApplication`:

```zig
var demo = common.createDemo(b, .{
    .name = "vfx_viewport_app",
    .sources = &.{ "main.cpp", "vfx_viewport_app.cpp" },
    .use_demo_application = false,
});
```

## Key Differences from Legacy Approach

### No direct GPU access

`Application` hides `IRHIDevice`, `IRHICommandList`, and all fence management.
VFX particles are created as ECS components, not by calling GPU APIs directly.

### Automatic VFX system integration

`SceneRenderer::RenderFrame()` calls `SyncVfxEmitters()` which detects new
`VfxEmitterComponent` instances, creates corresponding `ParticleEmitter` GPU
resources, registers them with `VFXSystem`, and ticks the system each frame.

### Asset-driven emitters

Set `VfxEmitterComponent::assetPath` to a `.uzfx` file to load the emitter
definition from VFS instead of configuring inline parameters. The
`SyncVfxEmitters()` pass uses `UzfxLoader` to instantiate the emitter.

## Reference Files

| File                                       | Purpose                            |
|--------------------------------------------|------------------------------------|
| `demos/vfx_viewport_app/`                  | Engine Application VFX demo        |
| `demos/vfx_viewport/`                      | Legacy DemoApplication VFX demo    |
| `engine/app/application.h`                 | Application base class             |
| `engine/ecs/environment/vfx_emitter_component.h` | ECS particle emitter component |
| `engine/renderer/scene/scene_vfx_sync.cpp` | Auto-sync ECS to GPU emitters      |
| `engine/vfx/vfx_system.h`                  | Central VFX manager                |
