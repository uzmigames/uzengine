# Entity/Component — Subsystems

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §6
> **Depends on**: World (§10-01)
> **References**: UE5 `UWorldSubsystem`, Godot `MainLoop` services

## Purpose

Per-world scoped services that replace singletons. Subsystems are auto-created with the World, have lifecycle callbacks, and are destroyed when the World is destroyed. They provide game-wide state and services scoped to a single simulation.

## Why Subsystems Instead of Singletons

Singletons cause problems with multiple worlds (editor preview + play-in-editor running simultaneously). A singleton physics engine means both worlds share state — changes in one corrupt the other. Subsystems are **per-world**, so each world has its own physics, audio, rendering, etc.

## API

```cpp
/// Base class for per-world subsystems.
class WorldSubsystem {
public:
    virtual ~WorldSubsystem() = default;

    /// Return false to skip creation for this world (e.g., server-only subsystem).
    virtual bool ShouldCreate(const World& world) const { return true; }

    /// Called after all subsystems are created.
    virtual void Initialize(World& world) {}

    /// Called after ALL subsystems have initialized.
    virtual void PostInitialize() {}

    /// Called when world gameplay starts.
    virtual void OnWorldBeginPlay() {}

    /// Per-frame tick. Only called if CanTick() returns true.
    virtual bool CanTick() const { return false; }
    virtual void Tick(float delta) {}

    /// Called when world gameplay stops.
    virtual void OnWorldEndPlay() {}

    /// Called before destruction. Reverse of Initialize order.
    virtual void Deinitialize() {}

protected:
    /// Ensure another subsystem type initializes before this one.
    template<typename T> T* InitializeDependency();

    World* world_ = nullptr;
};
```

## Dependency Ordering

`InitializeDependency<T>()` ensures subsystem T initializes before the calling subsystem. The engine builds a dependency graph at world creation and topologically sorts subsystem init order.

```cpp
class PhysicsSubsystem : public WorldSubsystem {
    void Initialize(World& world) override {
        // Ensure render subsystem exists first (for debug draw)
        auto* render = InitializeDependency<RenderSubsystem>();
        // ... init Jolt physics world
    }
};
```

## Built-In Subsystems

| Subsystem | Responsibility |
|-----------|---------------|
| `RenderSubsystem` | Collects visible entities, builds draw calls, manages `RenderWorld` |
| `PhysicsSubsystem` | Owns the Jolt physics world, syncs with entities |
| `AudioSubsystem` | Spatial audio, voice pool, bus mixing |
| `InputSubsystem` | Action mapping, device polling |

## Game-Defined Subsystems

Games define their own subsystems for cross-entity state:

```cpp
class PongSubsystem : public WorldSubsystem {
    UZ_SUBSYSTEM(PongSubsystem);
public:
    int scores[2] = {0, 0};
};

// Access from any behavior:
auto* pong = GetWorld().GetSubsystem<PongSubsystem>();
pong->scores[0]++;
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Pattern | `UWorldSubsystem` | `MainLoop` + autoloads | **`WorldSubsystem`** |
| Scope | Per-world | Global (autoloads) or per-tree | **Per-world** |
| Dependencies | `InitializeDependency<T>()` | Manual init order | **`InitializeDependency<T>()`** |
| Lifecycle | Initialize/Deinitialize/Tick | `_ready()`/`_process()` | **Initialize/PostInitialize/BeginPlay/Tick/EndPlay/Deinitialize** |

## Rules

1. **NEVER use singletons for game state** — use `WorldSubsystem` scoped to a world
2. **ALWAYS declare dependencies** via `InitializeDependency<T>()` — don't assume init order
3. **ALWAYS implement `ShouldCreate()`** for optional subsystems (e.g., dedicated server skips render)
