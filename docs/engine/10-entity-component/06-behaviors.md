# Entity/Component — Behaviors

> **Status**: Planned (Milestone 2.0)
> **Source**: `runtime/engine/ecs/`
> **Depends on**: Component Model (§10-02), Tick Architecture (§10-05)
> **References**: UE5 `AActor` with Blueprint, Godot script-attached `Node`

## Purpose

Behaviors are components with game logic — the primary way developers write gameplay code. Scene composition happens in `.uzscene` files; behaviors are the only C++ code game developers write.

## Behavior Base Class

```cpp
/// A Behavior is a component with lifecycle callbacks.
/// Override only the methods you need. Zero-cost for methods not overridden.
class Behavior {
public:
    virtual ~Behavior() = default;

    virtual void OnBeginPlay() {}
    virtual void OnTick(float delta) {}
    virtual void OnPhysicsTick(float delta) {}
    virtual void OnEndPlay() {}

    Entity GetEntity() const;
    World& GetWorld() const;

    /// Access a component on the owning entity.
    template<typename T> T& Get();
    template<typename T> const T& Get() const;
};
```

## Class Registry — Resolving by Name

For `.uzscene` files to reference behavior types by string name, behaviors must self-register:

```cpp
#define UZ_REGISTER_BEHAVIOR(ClassName) \
    static uz::BehaviorRegistrar<ClassName> _reg_##ClassName(#ClassName)

// In player_controller.cpp:
UZ_REGISTER_BEHAVIOR(FPSCamera);
UZ_REGISTER_BEHAVIOR(PaddleController);
```

The `ClassRegistry` maps string names to factory functions:

```cpp
class ClassRegistry {
public:
    template<typename T>
    static void Register(const char* name);

    /// Create a behavior by name (used by scene loader).
    static Behavior* CreateBehavior(const char* name);

    /// Check if a name is registered.
    static bool IsRegistered(const char* name);
};
```

## Game Code Example — Pong Paddle

Developers write **only behaviors and data components**. No bootstrap code, no `main()`.

```cpp
struct PaddleTag {
    int player_index = 0;
    UZ_COMPONENT(PaddleTag);
};

class PaddleController : public uz::Behavior {
    UZ_BEHAVIOR(PaddleController);
    float speed = 8.0f;  // serialized from .uzscene

public:
    void OnBeginPlay() override {
        SetTickGroup(uz::TickGroup::PrePhysics);
    }

    void OnTick(float dt) override {
        auto& paddle = Get<PaddleTag>();
        auto& transform = Get<uz::TransformComponent>();
        auto& input = uz::Application::GetInput();

        float move = 0.0f;
        if (paddle.player_index == 0) {
            if (input.IsKeyDown(Key::W)) move = 1.0f;
            if (input.IsKeyDown(Key::S)) move = -1.0f;
        } else {
            if (input.IsKeyDown(Key::Up)) move = 1.0f;
            if (input.IsKeyDown(Key::Down)) move = -1.0f;
        }

        transform.position.z += move * speed * dt;
        transform.position.z = uz::Clamp(transform.position.z, -4.0f, 4.0f);
    }
};
```

## Bootstrap Model

The developer **never writes bootstrap code**:

1. `uzengine.exe` reads `game.uzproject`
2. Engine loads `startup_scene` from project file
3. `.uzscene` JSON is deserialized → entities + components created
4. Behavior types resolved by name via `ClassRegistry`
5. `OnBeginPlay()` called on all behaviors
6. Main loop runs until quit

The developer runs: `uzengine --project game.uzproject`

## Rules

1. **ALWAYS register behaviors** with `UZ_REGISTER_BEHAVIOR` — unregistered behaviors can't be referenced from `.uzscene`
2. **ALWAYS separate data (components) from logic (behaviors)** — components are `UZ_COMPONENT` structs, behaviors are `Behavior` subclasses
3. **NEVER write `main()` or bootstrap code** — the engine handles initialization
