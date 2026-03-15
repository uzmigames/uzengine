<!-- doc-meta
title: Application Layer Spec
version: 1.0
status: approved
created: 2026-03-15
updated: 2026-03-15
authors: [engine-team]
tags: [architecture, application, lifecycle, demos]
-->

# Application Layer Spec

## Overview

`uz::Application` is the top-level lifecycle owner for UzEngine games and demos.
It encapsulates window creation, RHI device/swapchain, engine infrastructure, `SceneRenderer`,
ECS `World`, and `IVFS` — so that subclasses only contain game logic.

A game developer implements three optional virtual methods and declares a main entry point:

```cpp
class MyGame : public uz::Application {
    uz::Entity cube_;

    void OnStart() override {
        cube_ = GetWorld().CreateEntity();
        GetWorld().Add<StaticMeshComponent>(cube_, "meshes/cube.uzmesh");
    }

    void OnUpdate(float dt) override {
        GetWorld().Get<TransformComponent>(cube_).rotation.y += dt * 45.f;
    }
};

UZ_APPLICATION_MAIN(MyGame)
```

---

## Architecture

```
uz::Application
  ├── ApplicationConfig       — title, resolution, render path, VFS root, max FPS
  ├── PlatformWindow          — created internally
  ├── IRHIDevice              — created internally (DX12 by default)
  ├── IRHISwapChain           — created internally
  ├── IRHICommandList         — created internally (graphics queue)
  ├── IRHIFence[3]            — triple-buffered frame fences
  ├── SceneRenderer           — owns ALL rendering (GBuffer, shadow atlas, render graph)
  ├── World (ECS)             — accessible via GetWorld()
  ├── IVFS                    — accessible via GetVFS()
  └── Frame loop              — fence sync, resize, dt calculation, OnUpdate dispatch
```

Game code NEVER sees:
- `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`
- `GBuffer`, shadow atlas, render graph, PSO cache
- Texture/mesh GPU upload details
- Fence synchronization

---

## Public Interface

### ApplicationConfig

```cpp
struct ApplicationConfig {
    const char*               title            = "UzEngine";
    uint32_t                  width            = 1280;
    uint32_t                  height           = 720;
    bool                      vsync            = false;
    bool                      enableValidation = true;   // debug builds only
    RenderPath                renderPath       = RenderPath::Deferred;
    std::string               vfsRoot;                   // default: exe-dir/assets/
    uint32_t                  maxFrames        = 0;      // 0 = unlimited
    std::string               screenshotPath;
    uint32_t                  screenshotFrame  = 5;
    std::optional<RHIBackend> backend;                   // default: DX12
    std::optional<ShaderModel> shaderModel;              // default: auto-detect
    std::string               allModesOutputPath;
    DebugViewMode             debugViewMode    = DebugViewMode::Lit;
    float                     cameraMoveSpeed  = 10.f;
    float                     cameraPositionX  = 0.f;
    float                     cameraPositionY  = 5.f;
    float                     cameraPositionZ  = 2.f;
    float                     cameraYawDeg     = -90.f;
    float                     cameraPitchDeg   = -20.f;
};
```

### Application Base Class

```cpp
class Application {
public:
    virtual ~Application() = default;

    /// Override to return custom ApplicationConfig before Run() initializes.
    virtual ApplicationConfig Configure() { return {}; }

    /// Called once after all engine systems are initialized.
    virtual void OnStart() {}

    /// Called every frame. dt is clamped to 0.1s.
    virtual void OnUpdate(float dt) {}

    /// Called before shutdown. GPU is still valid.
    virtual void OnStop() {}

    // ── Accessors (valid after Run() calls OnStart) ─────────────────────
    World&          GetWorld();
    IVFS&           GetVFS();
    Camera&         GetCamera();
    InputSystem&    GetInput();
    SceneRenderer&  GetRenderer();

    // ── Control ─────────────────────────────────────────────────────────
    void Quit();
    void LoadScene(std::string_view path);

    // ── Entry point ─────────────────────────────────────────────────────
    int Run(int argc, char** argv);

    // ── CLI utilities (static) ──────────────────────────────────────────
    static int TryComposeGrid(int argc, char* argv[]);
    static int TryDiffImages(int argc, char* argv[]);
};
```

### Entry Point Macro

```cpp
#define UZ_APPLICATION_MAIN(AppClass)                \
    int main(int argc, char** argv) {                \
        if (int r = uz::Application::TryComposeGrid(argc, argv); r >= 0) return r; \
        if (int r = uz::Application::TryDiffImages(argc, argv);  r >= 0) return r; \
        AppClass app;                                \
        return app.Run(argc, argv);                  \
    }
```

---

## Lifecycle

```
Run(argc, argv)
  ├── ParseArgs(argc, argv)           — populate config from CLI
  ├── Configure()                     — subclass override for config
  ├── InitPlatform()                  — window, device, swapchain, fences, cmdlist
  ├── InitVFS()                       — IVFS backed by config.vfsRoot
  ├── InitWorld()                     — ECS World
  ├── SceneRenderer::Init()           — all rendering infrastructure
  ├── cmdList.Begin()
  ├── StatsOverlay::Init()            — requires recording cmdList
  ├── cmdList.End() + Execute() + WaitIdle()
  ├── window.ClearResizedFlag()
  ├── OnStart()                       ← GAME CODE HERE
  │
  └── FRAME LOOP
       ├── PumpMessages()
       ├── [skip if minimized]
       ├── [maxFrames check]
       ├── ResizeDetection()
       │    ├── swapChain.Resize()
       │    ├── SceneRenderer.Resize()
       │    └── camera.SetAspectRatio()
       ├── dt calculation (clamped 0.1s)
       ├── fence wait (oldest frame)
       ├── cmdList.Begin()
       ├── input.BeginFrame()
       ├── cbRing.BeginFrame()
       ├── cameraCtrl.Update()
       ├── OnUpdate(dt)               ← GAME CODE HERE
       ├── SceneRenderer.RenderFrame()
       ├── [screenshot capture]
       ├── cmdList.End() + Execute()
       ├── [screenshot save]
       ├── swapChain.Present()
       └── fence.Signal()
  │
  ├── device.WaitIdle()
  ├── OnStop()                        ← GAME CODE HERE
  ├── SceneRenderer.Shutdown()
  ├── ShutdownInfrastructure()
  └── ShutdownPlatform()
```

---

## SceneRenderer Integration

`SceneRenderer` is owned by `Application` and handles the complete render pipeline:
- GBuffer creation (albedo RGBA8, normal RGBA16F, metallic/roughness RGBA8, depth D32F)
- Shadow atlas (4096×4096 D32F, 4 cascade CSM)
- Fallback textures (1×1 white, flat normal)
- Per-frame constant buffer upload (PerFrameCB, LightingCB, ShadowDataCB)
- Shadow cascade computation from ECS `DirectionalLightComponent`
- DrawList population from `StaticMeshComponent` + `SceneComponent`
- Render graph: shadow → GBuffer/forward → lighting/debug → overlay → present
- Debug view modes (F1-F11)

`Application.OnUpdate()` game code never constructs the render graph, builds DrawLists,
or uploads constant buffers. It only modifies ECS component data.

---

## Lua API

`uz.app` table exposes Application control to Lua scripts:

| Function | Description |
|----------|-------------|
| `uz.app.quit()` | Exits the main loop on the next iteration |
| `uz.app.load_scene(path)` | Loads a .uzscene file into World |
| `uz.app.set_render_path(path)` | `"deferred"` or `"forward"` |
| `uz.app.set_debug_view(mode)` | `"lit"`, `"albedo"`, `"normal"`, `"metallic"`, `"roughness"`, `"ao"`, `"depth"`, `"world_pos"`, `"overview"`, `"lighting"`, `"shadows"` |

---

## Acceptance Scenarios

**Given** the engine is uninitialized
**When** `Application::Run(argc, argv)` is called
**Then** the engine initializes in order: window → device → swapchain → VFS → World → SceneRenderer → `OnStart()`

**Given** a subclass overrides `OnStart()`, `OnUpdate(dt)`, `OnStop()`
**When** `Run()` is called
**Then** `OnStart()` is called exactly once after init, `OnUpdate(dt)` once per frame, `OnStop()` once before shutdown

**Given** a source file defines `UZ_APPLICATION_MAIN(PBRTestApp)`
**When** the binary is built
**Then** a `main()` is emitted automatically; `pbr_test_demo.cpp` contains only game logic

**Given** `ApplicationConfig::renderPath = RenderPath::Deferred`
**When** `Run()` is called
**Then** `SceneRenderer` uses deferred path without demo code touching the render graph

**Given** the window is resized
**When** the next frame begins
**Then** `SceneRenderer::Resize()` is called automatically; GBuffer is recreated; camera aspect updated

**Given** `GetWorld().Add<StaticMeshComponent>(e, "meshes/cube.uzmesh")` in `OnStart()`
**When** the first frame renders
**Then** `SceneRenderer` loads the mesh, uploads to GPU, and renders it

**Given** Lua script calls `uz.app.quit()`
**When** current frame's `OnUpdate` returns
**Then** `Application::Quit()` is signaled and the loop exits on the next iteration

**Given** the pbr_test demo is migrated to `uz::Application`
**When** built and run with `--screenshot`
**Then** `pbr_test_demo.cpp` has fewer than 150 lines and output is pixel-equivalent (PSNR > 40 dB)
