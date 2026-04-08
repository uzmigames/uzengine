# Spec: Application Layer

## Overview

`uz::Application` is the top-level lifecycle owner for UzEngine games and demos.
It encapsulates platform init, RHI device, swapchain, SceneRenderer, ECS World, and VFS
so that subclasses only contain game logic.

---

## Scenarios

### Lifecycle — Basic Init and Shutdown

**Given** the engine is uninitialized
**When** `Application::Run(argc, argv)` is called
**Then** the engine initializes in order: PlatformWindow → IRHIDevice → IRHISwapChain → VFS → World → SceneRenderer → `OnStart()`

**Given** `Application::Run()` is executing the frame loop
**When** `Application::Quit()` is called
**Then** the current frame completes, `OnStop()` is called once, all GPU resources are released, and `Run()` returns 0

**Given** an exception or critical error during `SceneRenderer::Init()`
**When** `Application::Run()` is called
**Then** `OnStart()` is NOT called, all partially initialized resources are released, and `Run()` returns non-zero

---

### Virtual Interface — Game Logic Isolation

**Given** a class that extends `Application` and overrides `OnStart()`, `OnUpdate(dt)`, `OnStop()`
**When** `Application::Run()` is called
**Then** `OnStart()` is called exactly once after all engine systems are ready, `OnUpdate(dt)` is called every frame with a non-negative delta time, and `OnStop()` is called exactly once before shutdown

**Given** a class that extends `Application` and does NOT override `OnStart()`/`OnUpdate()`/`OnStop()`
**When** `Application::Run()` is called
**Then** the engine runs a blank scene with the default camera and exits cleanly when the window is closed

---

### Entry Point Macro

**Given** a source file that defines `UZ_APPLICATION_MAIN(PBRTestApp)`
**When** the demo is compiled
**Then** a `main(int argc, char** argv)` function is emitted that creates a `PBRTestApp` instance and calls `app.Run(argc, argv)`

**Given** a demo that uses `UZ_APPLICATION_MAIN`
**When** the demo executable is run with `--screenshot path.png --frames 5`
**Then** the Application captures the screenshot after frame 5 and exits

---

### ApplicationConfig

**Given** `ApplicationConfig` is not overridden by `Configure()`
**When** `Application::Run()` is called
**Then** the window is created with title "UzEngine", size 1280×720, DX12 backend, deferred render path, and VFS root pointing to the exe directory's `assets/` folder

**Given** `Configure()` returns `ApplicationConfig { .title="PBR Test", .width=1920, .height=1080 }`
**When** `Application::Run()` is called
**Then** the window is created with the specified title and size

---

### SceneRenderer Integration

**Given** a game calls `GetWorld().Add<StaticMeshComponent>(entity, "meshes/cube.uzmesh")` in `OnStart()`
**When** the first `OnUpdate()` frame renders
**Then** `SceneRenderer::SyncWorldToDrawList()` finds the entity, loads the mesh via VFS, uploads to GPU, and the mesh appears in the rendered output

**Given** `ApplicationConfig::renderPath = RenderPath::Deferred`
**When** `Application::Run()` is called
**Then** `SceneRenderer` uses the deferred render path (GBuffer → lighting pass) without any demo code touching the render graph

**Given** the window is resized by the user
**When** the next frame begins
**Then** `SceneRenderer::Resize(newWidth, newHeight)` is called automatically, the GBuffer is recreated, and the camera aspect ratio is updated

---

### Scene Loading

**Given** `Application::LoadScene("scenes/demo.uzscene")` is called from `OnStart()`
**When** the first frame renders
**Then** all entities defined in the .uzscene file are present in World with their correct components

**Given** `Application::LoadScene()` is called while the frame loop is running
**When** the next frame begins
**Then** the current World is cleared, the new scene is loaded, and `SceneRenderer` syncs the updated World

---

### Lua API — uz.app

**Given** a Lua script calls `uz.app.quit()`
**When** the current frame's `OnUpdate` callback returns
**Then** `Application::Quit()` is signaled and the main loop exits on the next iteration

**Given** a Lua script calls `uz.app.load_scene("scenes/level2.uzscene")`
**When** the next frame begins
**Then** the scene is loaded into World and rendered

**Given** a Lua script calls `uz.app.set_render_path("forward")`
**When** the next frame renders
**Then** `SceneRenderer::SetRenderPath(RenderPath::Forward)` is applied and the deferred GBuffer pass is skipped

**Given** a Lua script calls `uz.app.set_debug_view("normal")`
**When** the next frame renders
**Then** `SceneRenderer::SetDebugViewMode(DebugViewMode::Normal)` is applied and the debug view pass outputs the world-space normal buffer

---

### Demo Migration — Code Size Contract

**Given** the `pbr_test` demo is migrated to `uz::Application`
**When** the migration is complete
**Then** `pbr_test_demo.cpp` contains fewer than 150 lines and zero render graph, GBuffer, shadow atlas, or constant buffer construction code

**Given** any migrated demo
**When** built and run with `--screenshot`
**Then** the visual output is pixel-equivalent (PSNR > 40 dB) to the pre-migration screenshot

---

## File Locations

| File | Purpose |
|------|---------|
| `runtime/engine/app/app_config.h` | ApplicationConfig struct |
| `runtime/engine/app/application.h` | Public interface |
| `runtime/engine/app/application.cpp` | Implementation |
| `runtime/engine/renderer/scene_renderer.cpp` | SceneRenderer implementation |
| `runtime/engine/scripting/api/core/lua_app_api.cpp` | Lua uz.app bindings |
| `docs/specs/engine/application-layer.md` | User-facing spec |
