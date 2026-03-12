# Hot-Reload — Code, Shaders, and Assets

> **Status**: Planned (Phase 3–4)
> **Source**: `developer/hot_reload/`
> **References**: Unreal hot-reload (live C++ compile), Godot GDScript reload

## Purpose

Instant reload of changed assets and code without restarting the engine. Enables rapid iteration:
- **C++ DLL reload** (editor only)
- **Lua script reload**
- **Shader recompilation** with dependent material updates
- **Texture and mesh reload**
- **Data table and scene reloading**

All via FileWatcher + dependency graph + dirty flag system.

## Architecture

```
File on disk changes
  ↓
FileWatcher detects change
  ↓
Query dependency graph: "What depends on this?"
  ↓
Mark dependents as dirty
  ↓
On next use: lazy reload
  ├── C++ DLL: unload, recompile, load
  ├── Lua: parse, cache, call on_stop() → on_start()
  ├── Shader: recompile, mark dependent materials dirty
  └── Texture/Mesh: reload, mark dependent materials dirty
```

## FileWatcher API

```cpp
namespace uz::dev {

/// File change event type
enum class FileChangeEvent { Created, Modified, Deleted };

/// Callback fired when watched file changes
using FileChangeCallback = std::function<void(StringView path, FileChangeEvent)>;

/// Watch a directory/pattern for file changes
void WatchAssets(StringView vfs_pattern, FileChangeCallback callback);

/// Examples:
// Watch all Lua scripts
WatchAssets("res://*.lua", [](StringView path, FileChangeEvent evt) {
    if (evt == FileChangeEvent::Modified) {
        lua_system->ReloadScript(path);
    }
});

// Watch all shaders
WatchAssets("res://**/*.hlsl", [](StringView path, FileChangeEvent evt) {
    if (evt == FileChangeEvent::Modified) {
        shader_compiler->RecompileShader(path);
    }
});

}  // namespace uz::dev
```

## Dependency Graph

Tracks which assets depend on which, enabling cascade reloads:

```cpp
namespace uz::dev {

/// Register that A depends on B
void RegisterDependency(StringView dependent, StringView dependency);

/// Get all assets that depend on this one (transitive)
std::vector<std::string> GetDependents(StringView asset_path);

/// Example:
// Material "pbr.uzmat" depends on shader "pbr.hlsl"
RegisterDependency("content/materials/pbr.uzmat", "content/shaders/pbr.hlsl");

// When shader changes:
auto dependents = GetDependents("content/shaders/pbr.hlsl");
for (auto& mat_path : dependents) {
    MaterialSystem::MarkDirty(mat_path);  // Queued for reload on next use
}

}  // namespace uz::dev
```

## C++ Hot-Reload (Editor Only)

Recompile and reload game.dll without restarting:

```
1. FileWatcher detects .cpp or .h change
   ↓
2. Engine pauses main loop
   ↓
3. Save world state (entity positions, components, selection)
   ↓
4. Unload game.dll (and all its loaded instances)
   ↓
5. Trigger Zig build: zig build (incremental, fast)
   ↓
6. Load new game.dll
   ↓
7. Call OnBeginPlay() on all Behavior instances (re-initialize)
   ↓
8. Restore world state (camera, selection, viewport)
   ↓
9. Resume main loop
```

### Implementation

```cpp
namespace uz::dev {

class HotReloadManager {
public:
    void OnSourceFileChanged(StringView cpp_path) {
        if (!IsEditorBuild()) return;  // Only in editor

        UZ_LOG(HotReload, Info, "Recompiling: {}", cpp_path);

        // Pause the engine
        engine_->Pause();

        // Save state
        SaveEditorState();

        // Unload DLL
        dll_loader_->Unload("game.dll");

        // Recompile
        if (!CompileGameDLL()) {
            UZ_LOG(HotReload, Error, "Compilation failed");
            // Restore state, continue with old DLL
            RestoreEditorState();
            dll_loader_->Load("game.dll");
            engine_->Resume();
            return;
        }

        // Load new DLL
        if (!dll_loader_->Load("game.dll")) {
            UZ_LOG(HotReload, Error, "Failed to load new DLL");
            // Revert to old build
            CompileGameDLL(/* revert */);
            RestoreEditorState();
            engine_->Resume();
            return;
        }

        // Re-initialize behaviors
        world_->ForEachBehavior([](Behavior* behavior) {
            behavior->OnBeginPlay();
        });

        // Restore state
        RestoreEditorState();

        // Resume
        engine_->Resume();

        UZ_LOG(HotReload, Info, "Hot-reload complete");
    }

private:
    void SaveEditorState() {
        editor_state_ = {
            .camera_transform = editor_camera_->GetTransform(),
            .selected_entity = editor_selection_->GetSelectedEntity(),
            .viewport_position = viewport_->GetScrollPosition(),
        };
    }

    void RestoreEditorState() {
        editor_camera_->SetTransform(editor_state_.camera_transform);
        editor_selection_->Select(editor_state_.selected_entity);
        viewport_->SetScrollPosition(editor_state_.viewport_position);
    }

    bool CompileGameDLL() {
        // Run: zig build
        int exit_code = system("zig build");
        return exit_code == 0;
    }

    struct EditorState {
        Transform camera_transform;
        Entity selected_entity;
        Vec2 viewport_position;
    } editor_state_;
};

}  // namespace uz::dev
```

## Lua Script Hot-Reload

Scripts are reloaded on modification:

```cpp
namespace uz::dev {

class LuaHotReload {
public:
    void OnScriptChanged(StringView script_path) {
        UZ_LOG(Lua, Info, "Reloading script: {}", script_path);

        // Find all entities with this script
        world_->ForEachComponent<ScriptComponent>([&](ScriptComponent* script) {
            if (script->GetScriptPath() != script_path) return;

            // Call on_stop() on old script
            script->CallLifecycleFunction("on_stop");

            // Reload script from disk
            script->ReloadScript();

            // Call on_start() on new script
            script->CallLifecycleFunction("on_start");

            // Preserve state via ctx.saves
            // (component properties and saved values survive reload)
        });
    }
};

}  // namespace uz::dev
```

Example script with state preservation:

```lua
-- content/scripts/player_controller.lua

local self = {}

function self.on_start()
    print("Player controller started")
    -- Restore state from previous run
    self.health = ctx.saves:get("player_health") or 100
end

function self.on_update(dt)
    -- ... game logic ...
end

function self.on_stop()
    -- Save state for next reload
    ctx.saves:set("player_health", self.health)
end

return self
```

## Shader Hot-Reload

Shaders are recompiled and dependents are marked dirty:

```cpp
namespace uz::dev {

void OnShaderChanged(StringView shader_path) {
    UZ_LOG(Shader, Info, "Recompiling shader: {}", shader_path);

    // Recompile shader
    ShaderPipeline::RecompileShader(shader_path);

    // Mark all dependent materials as dirty
    auto dependents = GetDependents(shader_path);
    for (auto& mat_path : dependents) {
        MaterialSystem::MarkDirty(mat_path);
    }

    UZ_LOG(Shader, Info, "Marked {} materials for reload", dependents.size());
}

}  // namespace uz::dev
```

## Texture/Mesh Hot-Reload

Assets are reloaded when changed:

```cpp
namespace uz::dev {

void OnTextureChanged(StringView texture_path) {
    // Reload texture
    TextureSystem::ReloadTexture(texture_path);

    // Mark dependents (materials, scenes) as dirty
    auto dependents = GetDependents(texture_path);
    for (auto& mat_path : dependents) {
        MaterialSystem::MarkDirty(mat_path);
    }
}

void OnMeshChanged(StringView mesh_path) {
    // Reload mesh
    MeshSystem::ReloadMesh(mesh_path);

    // Mark entities using this mesh for update
    world_->ForEachComponent<MeshComponent>([&](MeshComponent* mesh_comp) {
        if (mesh_comp->GetMeshPath() == mesh_path) {
            mesh_comp->ReloadMesh();
        }
    });
}

}  // namespace uz::dev
```

## Data Table Hot-Reload

DataTables reload automatically when JSON changes:

```cpp
uz::dev::WatchAssets("res://**/*.uzdata", [](StringView path, FileChangeEvent evt) {
    if (evt == FileChangeEvent::Modified) {
        // Find all DataTable instances for this path
        for (auto* table : active_data_tables_) {
            if (table->GetSourcePath() == path) {
                table->Reload();
                UZ_LOG(Data, Info, "Reloaded data table: {}", path);
            }
        }
    }
});
```

## Scene Hot-Reload

Scenes can be reloaded (editor only):

```cpp
void ReloadScene(StringView scene_path) {
    // Save current state
    SaveWorldState();

    // Clear world
    world_->Clear();

    // Reload from disk
    SceneLoader::Load(scene_path, *world_);

    // Restore editor selections, camera
    RestoreEditorState();

    UZ_LOG(Scene, Info, "Reloaded scene: {}", scene_path);
}
```

## Configuration

`.uzproj` hot-reload settings:

```toml
[editor]
enable_hot_reload = true
cpp_hot_reload = true           # Reload game.dll on .cpp changes
lua_hot_reload = true           # Reload scripts on .lua changes
shader_hot_reload = true        # Recompile on .hlsl changes
asset_hot_reload = true         # Reload textures, meshes, etc.
```

## Comparison

| Feature | Unreal | Godot | Unity | **UzEngine** |
|---------|--------|-------|-------|--------------|
| C++ hot-reload | Yes (compile) | N/A | No (Editor reload) | Yes (DLL) |
| Script hot-reload | Blueprint | Yes (GDScript) | No | Yes (Lua) |
| Shader hot-reload | Yes (recompile) | Yes | Yes (rebuild) | Yes (recompile) |
| Asset hot-reload | Yes (reimport) | Yes | Yes (reimport) | Yes (reload) |
| Dependency tracking | Yes (implicit) | Yes | Limited | Yes (explicit graph) |
| State preservation | Via save/restore | Via script state | Session state | Via ctx.saves |

## Debugging Hot-Reload Issues

If hot-reload fails:

1. Check compilation errors: `zig build 2>&1 | grep error`
2. Check asset path resolution: `uz::ResolveAssetPath(path)`
3. Check dependency graph: `uz::dev::GetDependents(path)`
4. Enable debug logging: `UZ_LOG(HotReload, Debug, ...)`

## Implementation Phase

- **Phase 3**: Lua script reload, shader recompile, FileWatcher monitoring, basic dependency graph
- **Phase 4**: C++ DLL hot-reload (editor), entity state preservation, scene reload
- **Phase 5**: Incremental compilation, distributed build (remote compile), replay system integration
