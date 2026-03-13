# Application Lifecycle System

> **Status**: Done (Milestone 2.0)
> **Source**: `runtime/engine/app/`
> **Key Files**: `engine_loop.h/.cpp`, `subsystem_registry.h/.cpp`, `subsystem.h`, `time_manager.h/.cpp`, `module_registry.h/.cpp`

## Overview

The application lifecycle system orchestrates the complete engine initialization, per-frame execution, and shutdown. It replaces manual boilerplate with a structured framework where subsystems self-register, declare dependencies, and participate in ordered initialization and per-frame ticking.

### What's Managed

- **Initialization**: Three phases (PreInit → Init → Start) with automatic dependency resolution via topological sort
- **Per-Frame Ticking**: Four tick groups (PrePhysics, PostPhysics, Update, PostUpdate) enabling correct execution order
- **Shutdown**: Strict reverse-order cleanup guaranteeing no dangling references
- **Module Registration**: Static self-registration pattern for subsystems and plugins
- **Time Management**: Scaled delta time, fixed-timestep accumulation, pause/resume, and time scale control

### Key Problems Solved

1. **Boilerplate Elimination**: Every demo no longer needs 200+ lines of init/loop/shutdown code
2. **Dependency Management**: Subsystems declare dependencies; the registry ensures correct init order
3. **Physics Determinism**: Fixed-timestep physics with variable rendering via accumulator pattern
4. **GPU Resource Leaks**: Reverse-order shutdown guarantees proper cleanup
5. **Subsystem Hot-Reload**: Subsystems can be reloaded without full engine restart (when game DLL reloads)

## Core Concepts

### Subsystem Interface

Every engine system (renderer, audio, physics, input, etc.) implements `ISubsystem`, declaring:

```cpp
class ISubsystem {
public:
    /// Human-readable name for logging and dependency resolution
    virtual const char* GetName() const = 0;

    /// When this subsystem should initialize (PreInit/Init/Start)
    virtual InitPhase GetInitPhase() const = 0;

    /// When this subsystem should tick each frame (PrePhysics/PostPhysics/Update/PostUpdate/None)
    virtual TickGroup GetTickGroup() const { return TickGroup::None; }

    /// Names of subsystems that must initialize before this one
    virtual std::vector<const char*> GetDependencies() const { return {}; }

    /// Called once during initialization
    virtual bool Init() = 0;

    /// Called once during shutdown (in reverse order)
    virtual void Shutdown() = 0;

    /// Called every frame for subsystems with a tick group
    virtual void Tick(float deltaTime) { }
};
```

### Initialization Phases

Subsystems initialize in three ordered phases, enabling hardware-aware setup:

| Phase | Purpose | GPU | When |
|-------|---------|-----|------|
| **PreInit** | CPU-only setup | ❌ | First: logging, config, paths, VFS |
| **Init** | GPU initialization | ✅ | Second: window, RHI, renderer |
| **Start** | World setup | ✅ | Third: scene loading, entity spawning |

Within each phase, subsystems initialize in **dependency order** determined by Kahn's topological sort algorithm. If a circular dependency is detected, initialization fails with a clear error message.

### Tick Groups

Per-frame execution is organized into tick groups, enabling correct ordering of physics, logic, and rendering:

| Group | Frequency | When | Use For |
|-------|-----------|------|---------|
| **PrePhysics** | Fixed (each physics step) | Before physics | Character controller input, animation sampling |
| **PostPhysics** | Fixed (each physics step) | After physics | Physics queries, constraint resolution, callbacks |
| **Update** | Variable (once per frame) | Main game logic | Behavior ticks, AI, scripting, animation |
| **PostUpdate** | Variable (once per frame) | Cleanup | Deferred events, scheduled callbacks |
| **None** | Never | — | Init-only subsystems (default) |

### Time Management

The `TimeManager` subsystem decouples physics (fixed timestep) from rendering (variable framerate):

- **Scaled Delta Time**: `dt = clamp(rawDelta, 0, kMaxDeltaTime) * timeScale`
- **Fixed Timestep Accumulation**: Physics steps are determined by consuming fixed-timestep chunks from accumulated time
- **Time Scale Control**: Supports pause (0.0), slow-motion (0.5), and normal (1.0) without affecting frame-limiting
- **Delta Clamping**: Prevents "spiral of death" after debugger pauses or long hitches

**Example Frame**:
```
Frame Start (60 FPS):
├─ Time: BeginFrame() computes deltaTime = 16.67ms
├─ Physics: Loop while accumulated_time >= 1/60s (typically 1 iteration)
│  ├─ PrePhysics tick group fires
│  ├─ Physics tick group fires
│  └─ PostPhysics tick group fires
├─ Update: Loop fires once (variable rate)
│  ├─ Update tick group fires
│  └─ PostUpdate tick group fires
└─ Render: Render and present
```

At 30 FPS, the frame takes 33.34ms, so the physics loop would execute 2 iterations.

### Module Registration

Subsystems are registered via a macro in their `.cpp` file, enabling static self-discovery:

```cpp
// In my_subsystem.cpp
class MySubsystem final : public ISubsystem { /* ... */ };

// Register for automatic discovery and initialization
UZ_REGISTER_MODULE(MySubsystem);
```

At startup, all `UZ_REGISTER_MODULE` callbacks execute, adding subsystems to the registry before `InitAll()` is called. This pattern eliminates explicit registration code in `main()`.

## Architecture Diagrams

### Initialization Flow

```
EngineLoop::Run(argc, argv)
    │
    ├─ ParseCommandLine() → EngineConfig
    │
    ├─ Initialize()
    │   ├─ ModuleRegistry::FireAll() ← runs UZ_REGISTER_MODULE callbacks
    │   │   └─ Each module: registry.Register(subsystem)
    │   │
    │   └─ SubsystemRegistry::InitAll()
    │       ├─ Kahn's topological sort by dependencies
    │       │
    │       ├─ Phase PreInit subsystems (in topo order)
    │       │   ├─ Logging
    │       │   ├─ Config
    │       │   ├─ Paths
    │       │   └─ TimeManager
    │       │
    │       ├─ Phase Init subsystems (in topo order)
    │       │   ├─ Window
    │       │   ├─ RHI Device
    │       │   └─ Renderer
    │       │
    │       └─ Phase Start subsystems (in topo order)
    │           └─ World
    │
    ├─ MainLoop()
    │   └─ Each frame:
    │       ├─ TimeManager::BeginFrame()
    │       ├─ InputSystem::Poll()
    │       ├─ Fixed-step loop (0-N iterations per frame)
    │       │   ├─ SubsystemRegistry::RunTickGroup(PrePhysics, dt)
    │       │   ├─ PhysicsSystem::Step(dt)
    │       │   └─ SubsystemRegistry::RunTickGroup(PostPhysics, dt)
    │       ├─ SubsystemRegistry::RunTickGroup(Update, dt)
    │       ├─ SubsystemRegistry::RunTickGroup(PostUpdate, dt)
    │       ├─ Renderer::Render()
    │       └─ SwapChain::Present()
    │
    └─ Shutdown()
        └─ SubsystemRegistry::ShutdownAll() (reverse init order)
```

### Subsystem Dependency Graph Example

```
            Logging ◄─────┐
              │            │
         Config ──────────┤
              │            │
          Paths ───────────┤
              │            │
           VFS ────────────┤────┐
                           │    │
                        Window   │
                           │    │
                          RHI   │
                           │    │
                        Renderer ◄──┐
                           │         │
                        Physics ─────┤
                           │         │
                        World ◄──────┘
```

If any subsystem declares a dependency that doesn't exist, or if a circular dependency is detected, `InitAll()` fails and logs which subsystems are involved.

## Files Documentation

### engine_loop.h / engine_loop.cpp

**Purpose**: Main application driver

**Key Classes**:
- `EngineConfig` — Configuration struct with CLI defaults
- `EngineLoop` — Owns `SubsystemRegistry` and `TimeManager`
- `ParseCommandLine()` — CLI argument parser

**Typical Usage**:
```cpp
int main(int argc, char* argv[]) {
    uz::EngineLoop loop;

    // Optional: register subsystems before Run()
    // loop.RegisterSubsystem(std::make_unique<MySubsystem>());

    // Run parses CLI, initializes, runs main loop, and shuts down
    int exit_code = loop.Run(argc, argv);
    return exit_code;
}
```

**CLI Flags**:
- `--headless` — Run without window or GPU (useful for dedicated servers, tests)
- `--screenshot <path>` — Capture screenshot and save to path
- `--screenshot-frame <N>` — Which frame number to capture
- `--config <path>` — Override config file location
- `--frames <N>` — Auto-exit after N frames (useful for automated testing)

### subsystem_registry.h / subsystem_registry.cpp

**Purpose**: Manages the complete lifecycle of all subsystems

**Key Methods**:
- `Register(subsystem)` — Add a subsystem for management
- `InitAll()` — Initialize all subsystems in dependency order (returns false on circular dep or init failure)
- `ShutdownAll()` — Shut down all subsystems in reverse init order
- `RunTickGroup(group, dt)` — Execute all subsystems in a tick group
- `Find(name)` — Look up a subsystem by name

**Implementation Notes**:
- Uses Kahn's algorithm to detect circular dependencies and compute topological order
- Logs each subsystem's init duration (useful for profiling startup)
- On `InitAll()` failure, automatically rolls back by shutting down already-initialized subsystems
- `RunTickGroup()` iterates subsystems in init order, calling `Tick(dt)` only on those matching the group

### subsystem.h

**Purpose**: Interface that all engine subsystems implement

**Key Types**:
- `InitPhase` enum: `PreInit`, `Init`, `Start`
- `TickGroup` enum: `PrePhysics`, `PostPhysics`, `Update`, `PostUpdate`, `None`
- `ISubsystem` abstract class: Base for all subsystems

**Virtual Methods** (all have default implementations except `GetName()` and `GetInitPhase()`):
- `GetName()` — Must return a static string name
- `GetInitPhase()` — Which phase this subsystem belongs to
- `GetTickGroup()` — Which tick group (or `None`)
- `GetDependencies()` — Names of prerequisite subsystems
- `Init()` — Initialization (must return bool)
- `Shutdown()` — Cleanup
- `Tick(dt)` — Per-frame update

### time_manager.h / time_manager.cpp

**Purpose**: Provides frame timing and fixed-timestep accumulation

**Key Methods**:
- `BeginFrame()` — Call at frame start to compute delta time
- `ConsumeFixedTicks()` — Drain accumulator and return number of fixed ticks to execute
- `GetDeltaTime()` — Scaled delta time this frame
- `GetRawDeltaTime()` — Unscaled wall-clock delta
- `GetTotalTime()` — Total elapsed scaled time since init
- `GetFixedDeltaTime()` — Fixed timestep interval
- `SetFixedDeltaTime(dt)` — Change fixed timestep
- `GetTimeScale()` — Time scale multiplier (1.0 = normal)
- `SetTimeScale(scale)` — Pause (0.0), slow-motion (0.5), or normal (1.0)

**Important Constants**:
- `kMaxDeltaTime = 0.25f` — Delta time is clamped to prevent physics explosions after hitches
- `kDefaultFixedDeltaTime = 1/60` — Default physics timestep (16.67ms)

**How It Works**:
```cpp
// Each frame:
time_manager.BeginFrame();  // Measure wall-clock time, apply timeScale
float dt = time_manager.GetDeltaTime();  // Scaled delta time
accumulated += dt;

// Physics loop (typically 1-2 iterations per frame)
int fixed_ticks = time_manager.ConsumeFixedTicks();
for (int i = 0; i < fixed_ticks; ++i) {
    PhysicsStep(time_manager.GetFixedDeltaTime());
}

// Variable-rate update
GameLogic(dt);
```

### module_registry.h / module_registry.cpp

**Purpose**: Implements the static self-registration pattern

**Key Macro**:
- `UZ_REGISTER_MODULE(ClassName)` — Register a subsystem for auto-discovery

**How It Works**:
The macro defines a module initializer function that runs at startup (before `main()`) and registers the subsystem:

```cpp
// In your subsystem.cpp:
class MySubsystem final : public ISubsystem { /* ... */ };
UZ_REGISTER_MODULE(MySubsystem);

// This expands to:
namespace {
    struct ModuleInitializer_MySubsystem {
        ModuleInitializer_MySubsystem() {
            ModuleRegistry::Register([] {
                return std::make_unique<MySubsystem>();
            });
        }
    };
    static ModuleInitializer_MySubsystem g_init;
}
```

### subsystem_wrappers.h

**Purpose**: Adapter classes wrapping core systems as subsystems

**Provided Wrappers**:
- `LoggingSubsystem` — Wraps the logging system
- `PathsSubsystem` — Wraps path resolution
- `ConfigSubsystem` — Wraps configuration loading
- And others for compatibility

These adapters allow existing core systems to participate in the subsystem lifecycle without refactoring them into full subsystems.

## Common Patterns

### Creating a Custom Subsystem

```cpp
// my_subsystem.h
class MySubsystem final : public uz::ISubsystem {
public:
    const char* GetName() const override { return "MySystem"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    uz::TickGroup GetTickGroup() const override { return uz::TickGroup::Update; }

    std::vector<const char*> GetDependencies() const override {
        return {"Config", "Logging"};  // Depends on these subsystems
    }

    bool Init() override;
    void Shutdown() override;
    void Tick(float deltaTime) override;

private:
    ConfigSubsystem* config_ = nullptr;
};

// my_subsystem.cpp
bool MySubsystem::Init() {
    // Retrieve dependencies via registry (if needed at runtime)
    return true;
}

void MySubsystem::Tick(float deltaTime) {
    // Called every frame
}

void MySubsystem::Shutdown() {
    // Clean up resources
}

// Register for auto-discovery
UZ_REGISTER_MODULE(MySubsystem);
```

### Implementing an Init-Only Subsystem (No Ticking)

```cpp
class AssetSubsystem final : public uz::ISubsystem {
public:
    const char* GetName() const override { return "AssetSystem"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }
    // Don't override GetTickGroup() — defaults to TickGroup::None

    bool Init() override { /* Load asset metadata */ return true; }
    void Shutdown() override { /* Flush asset cache */ }
};

UZ_REGISTER_MODULE(AssetSubsystem);
```

### Implementing a Physics-Integrated Subsystem

```cpp
class PhysicsSubsystem final : public uz::ISubsystem {
public:
    const char* GetName() const override { return "Physics"; }
    uz::InitPhase GetInitPhase() const override { return uz::InitPhase::Init; }

    uz::TickGroup GetTickGroup() const override {
        return uz::TickGroup::PostPhysics;  // Tick after physics
    }

    std::vector<const char*> GetDependencies() const override {
        return {"TimeManager", "Renderer"};
    }

    bool Init() override { /* Create Jolt world */ return true; }
    void Shutdown() override { /* Destroy Jolt world */ }
    void Tick(float deltaTime) override {
        // Called in PostPhysics group, after physics step
        // Update skeletal meshes from physics bodies
    }
};

UZ_REGISTER_MODULE(PhysicsSubsystem);
```

### Accessing Subsystems from Game Code

```cpp
// From anywhere in game code, look up a subsystem by name
class MyBehavior {
    void OnBeginPlay() {
        // Access subsystem via registry
        auto* audio = static_cast<AudioSubsystem*>(
            SubsystemRegistry::Find("Audio")
        );
        if (audio) {
            audio->PlaySound("content/audio/footstep.wav");
        }
    }
};
```

## Initialization Order Example

**Registered subsystems**:
- Logging (depends on nothing)
- Config (depends on Logging)
- Paths (depends on Config)
- Window (depends on Paths)
- RHI (depends on Window)
- Physics (depends on RHI, TimeManager)
- Renderer (depends on RHI)

**Topologically sorted order** (as computed by Kahn's algorithm):
1. Logging (phase PreInit)
2. Config (phase PreInit, depends on Logging)
3. Paths (phase PreInit, depends on Config)
4. TimeManager (phase PreInit)
5. Window (phase Init, depends on Paths)
6. RHI (phase Init, depends on Window)
7. Renderer (phase Init, depends on RHI)
8. Physics (phase Init, depends on RHI, TimeManager)

If you add `World (depends on Physics, Renderer)` in phase Start, it initializes after both Physics and Renderer.

## Shutdown Order

Shutdown always follows **strict reverse initialization order**. This guarantees:

1. No subsystem accesses destroyed dependencies
2. Resources are released in correct order (dependent subsystems first)
3. GPU resources are flushed before device destruction

Example (reverse of above):
1. World
2. Physics
3. Renderer
4. RHI
5. Window
6. TimeManager
7. Paths
8. Config
9. Logging

## Rules and Best Practices

### MUST DO

1. ✅ **Declare all dependencies** in `GetDependencies()` — the registry cannot infer them
2. ✅ **Return true from Init() only on success** — returning false triggers rollback
3. ✅ **Implement Shutdown() for cleanup** — GPU resources, file handles, threads
4. ✅ **Use the correct InitPhase** — PreInit for CPU-only, Init for GPU, Start for gameplay
5. ✅ **Register every subsystem** with `UZ_REGISTER_MODULE()` or explicit `RegisterSubsystem()`
6. ✅ **Handle missing dependencies gracefully** — use `SubsystemRegistry::Find()` to check for existence

### MUST NOT DO

1. ❌ **Do NOT use raw new/delete** — subsystems are owned by the registry via `unique_ptr`
2. ❌ **Do NOT call GPU functions in PreInit** — RHI device doesn't exist yet
3. ❌ **Do NOT forget to call Shutdown()** — GPU leaks will result
4. ❌ **Do NOT create circular dependencies** — the registry will detect and fail to init
5. ❌ **Do NOT tick subsystems manually** — the engine loop calls `RunTickGroup()` automatically
6. ❌ **Do NOT assume subsystem availability** — always check `Find()` returns non-null

## Engine Comparison

| Aspect | UE5 | Godot | **UzEngine** |
|--------|-----|-------|------------|
| **Lifecycle model** | `FEngineLoop` (PreInit/Init/Tick/Exit) | `Main::setup/start/iteration/cleanup` | **3-phase + reverse shutdown** |
| **Subsystem interface** | `ISubsystem` with manual registration | `Node` (tree-based) | **ISubsystem with auto-registration** |
| **Dependency resolution** | Manual via `GetSubsystem<T>()` | Parent-child graph | **Automatic topological sort** |
| **Initialization phases** | 4+ module phases | Single start phase | **3 clear phases** (PreInit/Init/Start) |
| **Tick groups** | 6+ (PrePhysics, Physics, PostPhysics, PostUpdateWork, etc.) | `_physics_process`, `_process` | **4 groups** (PrePhysics, PostPhysics, Update, PostUpdate) |
| **Physics timestep** | Fixed (substeps) + variable render | Fixed (configurable Hz) + variable render | **Fixed + variable render** |
| **Plugin system** | Module manager + reloadable | GDExtension init_level | **Module registry + UZ_REGISTER_MODULE** |

## Troubleshooting

### "Circular dependency detected: A → B → C → A"

**Cause**: Subsystems have a circular dependency (A depends on B, B depends on C, C depends on A).

**Fix**: Break the cycle by removing one dependency. Typically, one subsystem should be refactored to not depend on the other.

### "Failed to initialize subsystem: Renderer (returned false)"

**Cause**: The Renderer's `Init()` returned `false`.

**Fix**: Check logs for why the renderer failed (e.g., GPU device creation). The engine will have logged the specific error. Fix the underlying issue (driver, GPU capability, resource file) and try again.

### "Subsystem 'Physics' depends on 'RHI' which does not exist"

**Cause**: Physics declares a dependency on RHI, but no RHI subsystem was registered.

**Fix**: Register the RHI subsystem before calling `InitAll()`. Typically this happens automatically via `UZ_REGISTER_MODULE()`.

### "Missing frame time or FPS drops to 1 FPS"

**Cause**: `TimeManager::BeginFrame()` is not being called each frame, or subsystems are blocking in their `Tick()` methods.

**Fix**: Ensure the engine loop calls `BeginFrame()` and that tick methods return quickly. Use profiling to identify slow subsystems.

### Subsystem is not being initialized

**Cause**: The subsystem wasn't registered (missing `UZ_REGISTER_MODULE()` macro or `RegisterSubsystem()` call).

**Fix**: Add `UZ_REGISTER_MODULE(YourSubsystem);` to the `.cpp` file, or call `loop.RegisterSubsystem(std::make_unique<YourSubsystem>())` before `Run()`.

## See Also

- [Bootstrap Sequence](./02-bootstrap.md) — Detailed initialization phases and timing
- [Tick Architecture](./03-tick-architecture.md) — Per-frame execution and tick groups
- [Subsystem Registry](./04-subsystem-registry.md) — Registry implementation details
- [Game Instance](./05-game-instance.md) — Scene loading, pause/resume, level transitions
- [Demo Migration Guide](./demo-guide.md) — How to update old demos to use EngineLoop
