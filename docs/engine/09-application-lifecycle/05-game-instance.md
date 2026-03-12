# Application Lifecycle — Game Instance

> **Status**: Planned (Milestone 2.0)
> **Source**: `engine/core/world/game_instance.h`, `world.h`
> **Depends on**: Bootstrap, subsystem registry, physics, renderer
> **References**: UE5 `AGameInstance`, Godot `SceneTree`

## Purpose

The `GameInstance` class manages the lifetime and configuration of the current game session: world creation, scene loading, level transitions, pause/resume state, and graceful shutdown.

## GameInstance Architecture

```
GameInstance (singleton per session)
├── World (current gameplay world)
│   ├── Entities (player, NPCs, props)
│   ├── Physics system
│   ├── Scripting VM
│   └── Audio listeners
├── SaveGameManager
├── InputManager
├── PauseState
├── LevelTransitionController
└── EventSystem hooks
```

## Core Interface

```cpp
/// Central game session controller.
/// Manages world creation, scene loading, level transitions, pause state.
class GameInstance {
public:
    /// Create game instance and enter initial scene
    static GameInstance* Create(const GameInstanceConfig& config);

    /// Get singleton instance
    static GameInstance* Get();

    /// Load a new scene (world transition)
    /// Unloads current world, spawns new entities from scene file
    bool LoadScene(std::string_view scene_path);

    /// Reload current scene (restart)
    bool ReloadScene();

    /// Pause gameplay (not rendering)
    void Pause();
    void Resume();
    bool IsPaused() const { return paused; }

    /// Get the current world
    World* GetWorld() const { return current_world; }

    /// Shutdown game instance and cleanup
    void Shutdown();

    /// Called by main loop, not directly by game code
    void Tick(float dt);

private:
    World* current_world = nullptr;
    std::string current_scene_path;
    bool paused = false;
};
```

## GameInstance Lifecycle

### Creation

```cpp
GameInstanceConfig cfg = {
    .initial_scene = "content/scenes/main_menu.uzscene",
    .physics_config = PhysicsConfig{...},
    .auto_save_enabled = true,
};

GameInstance* game = GameInstance::Create(cfg);
// World is created and initial scene is loaded
// All entities' BeginPlay() has been called
```

### Per-Frame Tick

Called by the main loop:

```cpp
void MainLoop::Run() {
    GameInstance* game = GameInstance::Get();

    while (is_running) {
        input_system->Poll();
        time_manager->Update(GetCurrentTime());

        // GameInstance::Tick processes world updates
        game->Tick(time_manager->DeltaTime());

        // Render
        renderer->Render(game->GetWorld());
        swap_chain->Present();
    }
}
```

Inside `Tick()`:

```cpp
void GameInstance::Tick(float dt) {
    if (paused) {
        // Paused: only UI ticks, no physics/game logic
        ui_system->Tick(dt);
        return;
    }

    World* world = GetWorld();
    if (!world) return;

    // World handles its own entity ticks, physics, etc.
    world->Tick(dt);

    // Update world persistence (autosave, checkpoint)
    if (autosave_enabled) {
        autosave_timer -= dt;
        if (autosave_timer <= 0.0f) {
            SaveGame("autosave");
            autosave_timer = autosave_interval;
        }
    }
}
```

## Scene Loading

When `LoadScene()` is called, the world is transitioned:

```cpp
bool GameInstance::LoadScene(std::string_view scene_path) {
    UZ_LOG(World, Info, "Loading scene: {}", scene_path);

    // 1. EndPlay on all entities in current world
    if (current_world) {
        current_world->EndPlay();
    }

    // 2. Destroy current world
    if (current_world) {
        World::Destroy(current_world);
        current_world = nullptr;
    }

    // 3. Create new world
    current_world = World::Create({...});
    if (!current_world) {
        UZ_LOG(World, Error, "Failed to create world");
        return false;
    }

    // 4. Load scene entities
    bool ok = LoadSceneIntoWorld(scene_path, current_world);
    if (!ok) {
        UZ_LOG(World, Error, "Failed to load scene: {}", scene_path);
        World::Destroy(current_world);
        current_world = nullptr;
        return false;
    }

    // 5. BeginPlay on all entities
    current_world->BeginPlay();
    current_scene_path = scene_path;

    // 6. Fire event
    EventSystem::Fire<OnSceneLoadedEvent>();

    return true;
}
```

## Pause and Resume

Pause stops gameplay without destroying the world:

```cpp
void GameInstance::Pause() {
    if (paused) return;  // Already paused
    paused = true;

    World* world = GetWorld();
    if (world) {
        world->OnPause();
    }

    EventSystem::Fire<OnGamePausedEvent>();
    UZ_LOG(World, Debug, "Game paused");
}

void GameInstance::Resume() {
    if (!paused) return;  // Already running
    paused = false;

    World* world = GetWorld();
    if (world) {
        world->OnResume();
    }

    EventSystem::Fire<OnGameResumedEvent>();
    UZ_LOG(World, Debug, "Game resumed");
}
```

### Pause Effects

- **Physics**: Doesn't step (physics queries still work for UI raycasts)
- **Behaviors/Scripts**: `on_update()` callbacks don't fire
- **Audio**: Sounds pause (by fading volume to zero)
- **Rendering**: Still renders current frame (so UI is visible over world)
- **Input**: UI receives input, gameplay input is ignored

## Save/Load

The `GameInstance` manages save game operations:

```cpp
/// Save current world state to a slot
bool GameInstance::SaveGame(int32_t slot_number) {
    if (!current_world) return false;

    SaveGameData save_data;
    save_data.scene_path = current_scene_path;
    save_data.world_state = current_world->Serialize();
    save_data.timestamp = GetCurrentTimestamp();

    return SaveManager::SaveSlot(slot_number, save_data);
}

/// Load world from a save slot
bool GameInstance::LoadGame(int32_t slot_number) {
    SaveGameData save_data = SaveManager::LoadSlot(slot_number);
    if (save_data.scene_path.empty()) {
        return false;  // Slot empty
    }

    // Load the scene first
    if (!LoadScene(save_data.scene_path)) {
        return false;
    }

    // Then deserialize world state (entities, components, etc.)
    current_world->Deserialize(save_data.world_state);

    EventSystem::Fire<OnGameLoadedEvent>();
    return true;
}
```

## Level Transitions

Level transitions (switching between scenes) can be animated:

```cpp
class LevelTransitionController {
public:
    /// Start transition to new scene
    void TransitionTo(std::string_view scene_path, float fade_duration = 1.0f) {
        target_scene = scene_path;
        fade_progress = 0.0f;
        fading = true;
    }

    void Tick(float dt) {
        if (!fading) return;

        fade_progress += dt / fade_duration;

        // Fade to black
        if (fade_progress < 0.5f) {
            float alpha = fade_progress * 2.0f;
            ui->SetFadeOverlayAlpha(alpha);
        }
        // Load new scene
        else if (fade_progress == 0.5f) {
            GameInstance::Get()->LoadScene(target_scene);
        }
        // Fade from black
        else {
            float alpha = 1.0f - (fade_progress - 0.5f) * 2.0f;
            ui->SetFadeOverlayAlpha(alpha);

            if (fade_progress >= 1.0f) {
                fading = false;
            }
        }
    }

private:
    std::string target_scene;
    float fade_progress = 0.0f;
    bool fading = false;
    float fade_duration = 1.0f;
};
```

## Shutdown

When the game instance is destroyed, it cleans up in reverse order:

```cpp
void GameInstance::Shutdown() {
    UZ_LOG(World, Info, "Shutting down GameInstance");

    // 1. EndPlay on all entities
    if (current_world) {
        current_world->EndPlay();
    }

    // 2. Destroy world
    if (current_world) {
        World::Destroy(current_world);
        current_world = nullptr;
    }

    // 3. Flush pending saves
    SaveManager::FlushToDisk();

    // 4. Fire shutdown event
    EventSystem::Fire<OnGameInstanceShutdownEvent>();
}
```

## Events

GameInstance fires several events for editor/UI integration:

| Event | When Fired | Use Case |
|-------|-----------|----------|
| `OnSceneLoadedEvent` | Scene loaded, BeginPlay done | Update UI scene tree |
| `OnGamePausedEvent` | Pause called | Blur world, show pause menu |
| `OnGameResumedEvent` | Resume called | Unblur world, hide pause menu |
| `OnGameLoadedEvent` | Save game loaded | Reset UI state |
| `OnGameInstanceShutdownEvent` | Instance destroyed | Final cleanup |

## Engine Comparison

| Aspect | UE5 | Godot 4 | Unity | **UzEngine** |
|--------|-----|---------|-------|------------|
| Session container | `AGameInstance` + `AGameMode` | `SceneTree` | None (just static managers) | **GameInstance** |
| World/Scene | `UWorld` | `Node` tree | `Scene` | **World** |
| Pause | `APlayerController::SetPause()` | `get_tree().paused = true` | `Time.timeScale = 0` | **GameInstance.Pause()** |
| Scene transitions | `GetWorld()->ServerTravel()` | `get_tree().change_scene_to_file()` | `SceneManager.LoadScene()` | **LoadScene()** |
| Save/Load | `SaveGame::SaveToSlot()` | Manual node serialization | Manual prefab instantiation | **SaveGame(), LoadGame()** |

## Rules

1. **ALWAYS check `GetWorld()` for nullptr** — scene load can fail
2. **NEVER destroy World directly** — use `GameInstance::LoadScene()` for transitions
3. **ALWAYS pair Pause() with Resume()** — paused state is not automatically cleared
4. **NEVER call `BeginPlay()` manually** — GameInstance and LoadScene do this
5. **ALWAYS save on level transitions** — use autosave or explicit SaveGame()
