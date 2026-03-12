# Application Lifecycle — Bootstrap

> **Status**: Planned (Milestone 2.0)
> **Source**: `engine/core/lifecycle/bootstrap.h`, `bootstrap.cpp`
> **Depends on**: Config, paths, logging, RHI, platform window
> **References**: UE5 `FEngineLoop::PreInit/Init`, Godot `Main::setup/start`

## Purpose

The bootstrap sequence is the ordered initialization of all engine subsystems. It occurs in three phases that mirror hardware requirements: PreInit (pure CPU), Init (GPU+RHI), and Start (world gameplay). Each phase has strict dependency guarantees.

## Bootstrap Architecture

```
┌─────────────────────────────────────────────────────────┐
│ Engine Bootstrap Lifecycle                              │
├─────────────────────────────────────────────────────────┤
│                                                         │
│ Phase 0 — PreInit (no GPU)                              │
│ ├── Initialize logging subsystem                        │
│ ├── Load engine configuration (.uzproj)                 │
│ ├── Initialize memory management                        │
│ ├── Mount Virtual Filesystem (res:// paths)             │
│ ├── Fire: OnPreInit event                               │
│ ├── Load plugin modules at CORE phase                   │
│ └── → Ready for CPU initialization                      │
│                                                         │
│ Phase 1 — Init (GPU ready)                              │
│ ├── Create platform window                              │
│ ├── Initialize RHI device (DX12/Vulkan/Metal)           │
│ ├── Create swap chain and command queues                │
│ ├── Initialize renderer (shader pipeline, globals)      │
│ ├── Fire: OnPostInit event                              │
│ ├── Load plugin modules at RHI phase                    │
│ ├── Load plugin modules at ENGINE phase                 │
│ └── → Ready for gameplay                                │
│                                                         │
│ Phase 2 — Start (world live)                            │
│ ├── Create World instance                               │
│ ├── Load scene from disk or default                     │
│ ├── Spawn initial entities                              │
│ ├── Call BeginPlay on all components                    │
│ ├── Fire: OnWorldBeginPlay event                        │
│ ├── Load plugin modules at SCENE phase                  │
│ └── → Gameplay is running                               │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## Phase 0 — PreInit

Initializes CPU-side systems without touching GPU or platform.

### Initialization Order

```cpp
void Engine::Bootstrap(const BootstrapConfig& config) {
    // 1. Logging — earliest, so all subsystems can log
    LogSystem::Initialize();
    UZ_LOG(Engine, Info, "Bootstrap starting: PreInit phase");

    // 2. Configuration — loaded before paths, memory
    ConfigSystem::Load(config.config_path);
    RenderConfig render_cfg = ConfigSystem::GetRender();
    PhysicsConfig physics_cfg = ConfigSystem::GetPhysics();

    // 3. Memory — before any subsystem allocations
    MemorySystem::Initialize();
    Allocator* engine_alloc = MemorySystem::GetEngineAllocator();

    // 4. Paths — resolve res://, exe dir, cache dir
    PathSystem::Initialize(config.exe_dir);

    // 5. Virtual filesystem — mount archive, loose files
    VirtualFS::Mount("res://", "game/assets");  // .uzpak or directory

    // 6. Fire PreInit event — plugins can initialize early
    EventSystem::Fire<OnPreInitEvent>();

    // 7. Load CORE plugins
    PluginManager::LoadPluginsAtPhase(PluginPhase::CORE);

    UZ_LOG(Engine, Info, "PreInit complete");
    BootstrapPhase1(config);
}
```

### Why This Order

- **Logging first**: All subsequent systems can log errors/warnings
- **Config before paths**: Paths are configured via config file
- **Memory before subsystems**: All allocations must use engine allocators
- **VirtualFS before plugins**: Plugins may load assets
- **Fire OnPreInit last**: Plugins executing at this point see fully initialized prerequisites

## Phase 1 — Init

Initializes GPU and RHI. At this point, rendering commands can be recorded.

### Initialization Order

```cpp
void Engine::BootstrapPhase1(const BootstrapConfig& config) {
    UZ_LOG(Engine, Info, "Bootstrap starting: Init phase");

    // 1. Platform window
    PlatformWindow* window = PlatformWindow::Create({
        .width = 1920,
        .height = 1080,
        .title = "UzEngine",
        .vsync = ConfigSystem::GetRender().vsync_enabled,
    });
    UZ_CHECK(window, "Failed to create platform window");

    // 2. RHI device — GPU driver initialization
    RHIDeviceDesc device_desc = {
        .backend = DX12,  // or Vulkan, Metal
        .adapter_index = config.gpu_adapter,
        .enable_validation = config.gpu_validation,
        .enable_breadcrumbs = config.gpu_breadcrumbs,
    };
    IRHIDevice* rhi_device = CreateRHIDevice(device_desc);
    UZ_CHECK(rhi_device, "Failed to create RHI device");
    UZ_LOG(Engine, Info, "RHI device created: sm={}", rhi_device->GetShaderModel());

    // 3. Swap chain
    IRHISwapChain* swap_chain = rhi_device->CreateSwapChain({
        .window = window,
        .format = RHIFormat::RGBA8_Unorm_Srgb,
        .backbuffer_count = 2,
        .tearing_enabled = true,
    });
    UZ_CHECK(swap_chain, "Failed to create swap chain");

    // 4. Renderer — global resources, shader pipeline
    RendererSystem::Initialize(rhi_device, swap_chain);

    // 5. Fire OnPostInit event — plugins see live RHI
    EventSystem::Fire<OnPostInitEvent>();

    // 6. Load RHI and ENGINE plugins
    PluginManager::LoadPluginsAtPhase(PluginPhase::RHI);
    PluginManager::LoadPluginsAtPhase(PluginPhase::ENGINE);

    UZ_LOG(Engine, Info, "Init complete");
    BootstrapPhase2(config, window, rhi_device, swap_chain);
}
```

### Device Creation Error Handling

If device creation fails (unsupported GPU, old driver, out of VRAM), the engine falls back gracefully:

```cpp
RHIDeviceDesc desc = {
    .backend = DX12,
    .shader_model = ShaderModel::SM6_6,
};

IRHIDevice* device = CreateRHIDevice(desc);
if (!device) {
    UZ_LOG(Engine, Warning, "SM6.6 device creation failed, trying SM5.0");
    desc.shader_model = ShaderModel::SM5_0;
    device = CreateRHIDevice(desc);
}

if (!device) {
    UZ_LOG(Engine, Error, "All device creation attempts failed");
    return false;  // Cannot proceed, exit gracefully
}
```

## Phase 2 — Start

Creates the world, loads the scene, and begins gameplay.

### Initialization Order

```cpp
void Engine::BootstrapPhase2(const BootstrapConfig& config,
                              PlatformWindow* window,
                              IRHIDevice* rhi_device,
                              IRHISwapChain* swap_chain) {
    UZ_LOG(Engine, Info, "Bootstrap starting: Start phase");

    // 1. Create World
    World* world = World::Create({
        .physics_config = ConfigSystem::GetPhysics(),
        .scene_name = config.initial_scene,
    });
    UZ_CHECK(world, "Failed to create World");

    // 2. Load scene
    if (!config.initial_scene.empty()) {
        bool ok = world->LoadScene(config.initial_scene);
        UZ_CHECK(ok, "Failed to load scene: {}", config.initial_scene);
    }

    // 3. Spawn initial entities (if not from scene)
    if (config.spawn_player_on_boot) {
        Entity player = world->SpawnEntity("Player");
        // ... configure player ...
    }

    // 4. Call BeginPlay on all entities
    world->BeginPlay();

    // 5. Fire OnWorldBeginPlay event
    EventSystem::Fire<OnWorldBeginPlayEvent>();

    // 6. Load SCENE plugins
    PluginManager::LoadPluginsAtPhase(PluginPhase::SCENE);

    UZ_LOG(Engine, Info, "Start complete, entering main loop");

    // 7. Enter main loop
    MainLoop(window, rhi_device, swap_chain, world);
}
```

## Subsystem Dependencies

The bootstrap order ensures no subsystem is used before its dependencies are ready.

```
Logging
  ├── Config
  │   └── Paths
  │       └── VirtualFS
  │
Memory
  └── All subsystems

RHI Device
  └── Renderer
      └── World
          └── Physics, Audio, Input, etc.
```

**Key rule**: Never call `IRHIDevice::CreateBuffer()` until Phase 1 Init is complete. The device is null until then.

## Bootstrap Configuration

The `BootstrapConfig` struct encapsulates all startup options:

```cpp
struct BootstrapConfig {
    /// Path to .uzproject config file
    std::string config_path = "project.uzproject";

    /// Initial scene to load, or empty for blank world
    std::string initial_scene = "";

    /// GPU adapter index (-1 = auto-select highest performance)
    int32_t gpu_adapter = -1;

    /// Enable GPU validation layers (slower, but catches shader bugs)
    bool gpu_validation = false;
    bool gpu_breadcrumbs = false;

    /// Force specific shader model (leave empty for auto-detect)
    std::optional<ShaderModel> force_shader_model = std::nullopt;

    /// Editor mode — hook up InputManager to VSCode extension
    bool editor_mode = false;

    /// Spawn a default player entity if scene is empty
    bool spawn_player_on_boot = false;

    /// Path to the executable directory
    std::string exe_dir = GetExeDir();
};
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| PreInit | `FEngineLoop::PreInit()` — logging, config, modules | `Main::setup()` — time, paths, OS APIs | **Phase 0** — logging, config, VFS, CORE plugins |
| Init | `FEngineLoop::Init()` — window, RHI, renderer | `Main::start()` — scene load, main loop | **Phase 1** — window, RHI, renderer, RHI+ENGINE plugins |
| World Load | After init, within loop | During `start()` before loop | **Phase 2** — world create, scene load, BeginPlay |
| Plugin phases | 6 phases (pre-core to post-engine) | GDExtension init_level (0–7) | **4 phases** (CORE, RHI, ENGINE, SCENE) |

## Rules

1. **ALWAYS initialize in order** — never skip a phase or reorder subsystems
2. **NEVER call RHI functions before Phase 1 Init** — GPU isn't ready
3. **ALWAYS check device creation success** — gracefully fall back to lower shader models
4. **NEVER fire events before phase prerequisites exist** — OnPostInit fires AFTER device creation
5. **ALWAYS load plugins at designated phases** — CORE before RHI, RHI before ENGINE, ENGINE before SCENE
