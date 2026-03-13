# Application Lifecycle — Subsystem Registry

> **Status**: Planned (Milestone 2.0)
> **Source**: `engine/core/lifecycle/subsystem_registry.h`, `subsystem_base.h`
> **Depends on**: Bootstrap, tick manager, event system
> **References**: UE5 `ISubsystem` / `FSubsystemCollection`, Godot `Node`

## Purpose

The subsystem registry provides a plugin architecture for extending engine functionality without modifying core code. Subsystems are named, lifetime-managed services with ordered initialization, dependency resolution, and hot-reload support.

## ISubsystem Interface

Every engine extension inherits from `ISubsystem`:

```cpp
/// Base class for all engine subsystems.
/// Subsystems are singleton services with ordered lifecycle.
class ISubsystem {
public:
    virtual ~ISubsystem() = default;

    /// Called when subsystem is registered but before Init()
    /// Use for declaring dependencies via GetSubsystem<>()
    virtual void OnPreInit() {}

    /// Subsystem fully initialized, dependencies available
    /// Initialize GPU resources, file I/O, networking here
    virtual void OnInit() {}

    /// Called each frame in designated tick group
    /// Override to register tick callbacks, or leave empty
    virtual void OnRegisterTicks(class TickManager* tm) {}

    /// Called before world loads, still rendering
    virtual void OnWorldBeginPlay() {}

    /// Called when world ends, before world destroy
    virtual void OnWorldEndPlay() {}

    /// Called before subsystem is destroyed
    /// Clean up GPU resources, close files, etc.
    virtual void OnShutdown() {}

    /// Human-readable subsystem name for debugging
    virtual std::string GetName() const = 0;

    /// Called on hot-reload (if supported)
    /// Save state, then be prepared to reinitialize
    virtual void OnHotReload() {}
};
```

## Dependency Declaration

Subsystems declare dependencies explicitly:

```cpp
class PhysicsSubsystem : public ISubsystem {
public:
    void OnPreInit() override {
        // Request dependencies
        config_system = GetSubsystem<ConfigSubsystem>();
        time_manager = GetSubsystem<TimeManager>();
        // These are guaranteed to exist (initialized earlier)
    }

    void OnInit() override {
        // config_system, time_manager are valid
        physics_world = jolt::CreatePhysicsSystem(...);
    }

private:
    ConfigSubsystem* config_system = nullptr;
    TimeManager* time_manager = nullptr;
    jolt::PhysicsSystem* physics_world = nullptr;
};
```

### Dependency Resolution

The registry initializes subsystems in dependency order:

```
ConfigSubsystem (no deps)
    ├── PathSystem (depends on Config)
    ├── LoggingSystem (depends on Config)
    │   ├── PhysicsSubsystem (depends on Config, Logging, TimeManager)
    │   ├── AudioSubsystem (depends on Config, Logging)
    │   └── RendererSubsystem (depends on Config, Logging, RHI)
```

If circular dependencies are detected at init time, `OnPreInit()` fails with a clear error message — never deadlock.

## Subsystem Registration

Subsystems are registered via a macro in their `.cpp` file:

```cpp
// In physics_subsystem.cpp
class PhysicsSubsystem : public ISubsystem {
    std::string GetName() const override { return "Physics"; }
    // ...
};

// Register this subsystem for automatic discovery
UZ_REGISTER_SUBSYSTEM(PhysicsSubsystem);
```

The registry automatically discovers all `UZ_REGISTER_SUBSYSTEM` declarations and initializes them during bootstrap.

## Subsystem Lifecycle

```
Bootstrap Phase 0 (PreInit)
    │
    └─ SubsystemRegistry::InitializeAll()
       │
       ├─ Topological sort by dependencies
       │
       ├─ For each subsystem in order:
       │   ├─ Call OnPreInit()
       │   │   └─ Subsystem requests dependencies via GetSubsystem<T>()
       │   ├─ Call OnInit()
       │   │   └─ Subsystem initializes resources
       │   └─ Store in registry
       │
       └─ Fire OnSubsystemInitialized event

Bootstrap Phase 1 (Init)
    │
    └─ For each subsystem: Call OnRegisterTicks(tm)
       │   └─ Subsystem registers tick callbacks

Bootstrap Phase 2 (Start)
    │
    └─ World created
       │
       └─ For each subsystem: Call OnWorldBeginPlay()

Game Loop (per frame)
    │
    └─ TickManager fires each tick group
       └─ Subsystem tick callbacks execute

World End
    │
    └─ For each subsystem (reverse order): Call OnWorldEndPlay()

Shutdown
    │
    └─ SubsystemRegistry::ShutdownAll()
       └─ For each subsystem (reverse init order): Call OnShutdown()
```

## Example: Audio Subsystem

```cpp
// audio_subsystem.h
class AudioSubsystem : public ISubsystem {
public:
    /// Play a 2D sound (no spatial audio)
    void PlaySound2D(std::string_view path, float volume = 1.0f);

    /// Get current active voice count
    int32_t GetVoiceCount() const;

protected:
    void OnPreInit() override;
    void OnInit() override;
    void OnRegisterTicks(TickManager* tm) override;
    void OnShutdown() override;

    std::string GetName() const override { return "Audio"; }

private:
    ConfigSubsystem* config = nullptr;
    IXAudio2* xaudio = nullptr;
    IXAudio2MasteringVoice* master_voice = nullptr;
};

// audio_subsystem.cpp
void AudioSubsystem::OnPreInit() {
    config = GetSubsystem<ConfigSubsystem>();
    UZ_CHECK(config, "ConfigSubsystem required");
}

void AudioSubsystem::OnInit() {
    AudioConfig cfg = config->GetAudio();
    HRESULT hr = XAudio2Create(&xaudio, 0);
    UZ_CHECK(SUCCEEDED(hr), "XAudio2Create failed: {}", hr);

    hr = xaudio->CreateMasteringVoice(&master_voice);
    UZ_CHECK(SUCCEEDED(hr), "CreateMasteringVoice failed: {}", hr);

    UZ_LOG(Audio, Info, "AudioSubsystem initialized");
}

void AudioSubsystem::OnRegisterTicks(TickManager* tm) {
    // Audio doesn't need per-frame ticking
    // (audio playback is managed by XAudio2 internally)
}

void AudioSubsystem::OnShutdown() {
    if (master_voice) {
        master_voice->DestroyVoice();
        master_voice = nullptr;
    }
    if (xaudio) {
        xaudio->Release();
        xaudio = nullptr;
    }
}

UZ_REGISTER_SUBSYSTEM(AudioSubsystem);
```

## Getting Subsystems

Game code retrieves subsystems by type:

```cpp
// From a Behavior
void MyBehavior::OnBeginPlay() {
    auto* audio = GetSubsystem<AudioSubsystem>();
    if (audio) {
        audio->PlaySound2D("content/audio/impact.wav");
    }
}

// From a world callback
void OnPlayerDeath(World* world) {
    auto* physics = GetSubsystem<PhysicsSubsystem>();
    physics->SetGravity({0, -9.81f, 0});
}
```

Template specialization guarantees type safety at compile time.

## Hot-Reload Support

Subsystems can opt-in to hot-reload (e.g., when game code DLL reloads):

```cpp
class ConfigSubsystem : public ISubsystem {
public:
    void OnHotReload() override {
        // Save current state
        saved_render_cfg = render_config;
        saved_physics_cfg = physics_config;
    }

    void OnInit() override {
        // Reload from disk
        LoadConfigs();

        // Restore overridden values
        if (!saved_render_cfg.empty()) {
            render_config = saved_render_cfg;
        }
    }

private:
    RenderConfig saved_render_cfg;
    PhysicsConfig saved_physics_cfg;
};
```

## Event Hooks

The subsystem registry fires events at key lifecycle points:

```cpp
// Listen for subsystem init
EventSystem::Subscribe<OnSubsystemInitializedEvent>([](auto& event) {
    UZ_LOG(Engine, Info, "Subsystem initialized: {}", event.subsystem->GetName());
});

// Listen for subsystem shutdown
EventSystem::Subscribe<OnSubsystemShutdownEvent>([](auto& event) {
    UZ_LOG(Engine, Info, "Subsystem shutdown: {}", event.subsystem->GetName());
});
```

## Registry API

```cpp
class SubsystemRegistry {
public:
    /// Initialize all registered subsystems in dependency order
    static bool InitializeAll();

    /// Retrieve subsystem by type
    template <typename T>
    static T* GetSubsystem() {
        return Get().GetSubsystemInternal<T>();
    }

    /// Shutdown all subsystems in reverse order
    static void ShutdownAll();

    /// Check if subsystem is initialized
    template <typename T>
    static bool HasSubsystem() {
        return Get().HasSubsystemInternal<T>();
    }

    /// Trigger hot-reload for all subsystems
    static void OnHotReload();
};
```

## Engine Comparison

| Aspect | UE5 | Godot | **UzEngine** |
|--------|-----|-------|------------|
| Extension mechanism | `ISubsystem` (engine) + plugins | `Node` tree + GDExtension | **ISubsystem** (registry pattern) |
| Dependency resolution | Manual via GetSubsystem<T>() | Parent-child node graph | **Automatic topological sort** |
| Lifecycle hooks | OnInitialize/OnDeinitialize | `_ready/_exit_tree` | **OnPreInit/OnInit/OnShutdown** |
| Hot-reload | Reloadable via module manager | GDScript instant, GDExtension deferred | **OnHotReload** callback |
| Service discovery | GetSubsystem<T>() template | Node.get_node() path | **GetSubsystem<T>()** template |

## Rules

1. **ALWAYS declare dependencies in OnPreInit()** — never access subsystems elsewhere
2. **NEVER create circular dependencies** — registry detects them at startup and fails
3. **ALWAYS call UZ_REGISTER_SUBSYSTEM()** — subsystems without it won't initialize
4. **NEVER assume subsystem existence** — check `GetSubsystem<T>()` for nullptr
5. **ALWAYS shutdown in reverse order** — registry handles this automatically
