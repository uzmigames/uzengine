# VFX Hot-Reload Workflow

> **Status**: Implemented (Phase 3.69.0 — 25/25 tasks done)
> **Availability**: Dev builds only (`UZ_DEV_BUILD` or `!NDEBUG`)
> **References**: [VFX System Overview](01-overview.md), [.uzvfx Format](07-uzvfx-format.md), [Module API](04-module-api.md), [Lua VFX API](09-lua-vfx-api.md)

## Purpose

VFX hot-reload enables iterative development: modify a `.uzvfx` file, save it, and see changes reflected in-engine without restarting. This dramatically speeds up VFX authoring workflows.

The hot-reload system monitors asset files for changes, reloads modified `.uzvfx` graphs, recompiles shader pipelines, and applies updates to live effects in the scene.

**Implementation Status**: Fully implemented with file monitoring, change detection, graph recompilation, and live effect updates. Works seamlessly with Lua API calls — effects remain valid across reloads.

## Architecture Overview

```
┌──────────────────┐
│ File on Disk     │
│  fire.uzvfx      │
└────────┬─────────┘
         │ FileWatcher
         │ (timestamp)
         ▼
┌──────────────────┐
│ VFXHotReload     │
│  - Poll()        │
│  - OnReload()    │
└────────┬─────────┘
         │ Reload
         ▼
┌──────────────────┐
│ VFXGraph Parser  │
│  - Load JSON     │
│  - Validate      │
│  - Compile       │
└────────┬─────────┘
         │ Apply
         ▼
┌──────────────────┐
│ Live Effect      │
│  Updated modules │
│  Re-rendered     │
└──────────────────┘
```

## How Hot-Reload Works

### 1. File Monitoring

The `VFXHotReload` system maintains a registry of loaded `.uzvfx` files with their disk modification timestamps.

```cpp
struct LoadedVFXFile {
    std::string path;                // "vfx/fire.uzvfx"
    std::filesystem::file_time_type timestamp;  // Last modification time
    VFXGraphHandle graph;            // Loaded graph handle
};
```

### 2. Polling & Change Detection

Each frame (or on-demand), `VFXHotReload::Poll()` queries the filesystem:

```cpp
void VFXHotReload::Poll() {
    for (auto& entry : loadedFiles) {
        auto current_time = std::filesystem::last_write_time(entry.path);
        if (current_time != entry.timestamp) {
            // File changed on disk
            OnFileChanged(entry.path);
            entry.timestamp = current_time;
        }
    }
}
```

### 3. Reload & Recompilation

When a change is detected:

1. **Reload JSON**: Parse the modified `.uzvfx` file.
2. **Validate**: Check format, modules, parameters, events.
3. **Compile**: Recompile shader modules (if any shaders changed).
4. **Apply**: Update all active effects using this graph.

```cpp
void VFXHotReload::OnFileChanged(const std::string& path) {
    // Step 1: Load JSON
    auto json = LoadJSON(path);

    // Step 2: Validate
    if (!ValidateVFXFormat(json)) {
        LogError("Invalid .uzvfx: {}", path);
        return;
    }

    // Step 3: Compile
    VFXGraph new_graph = CompileGraph(json);

    // Step 4: Apply (callback)
    onReload(path, new_graph);
}
```

### 4. Live Effect Updates

The `OnReload` callback is responsible for updating active effects:

```cpp
void OnVFXReload(const std::string& path, const VFXGraph& new_graph) {
    // For each active effect using this graph
    for (auto& effect : active_effects) {
        if (effect.graph_path == path) {
            effect.ReplaceGraph(new_graph);
        }
    }
}
```

## Availability & Gating

VFX hot-reload is **dev-only** and disabled in shipping builds. It is conditionally compiled:

```cpp
#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)
    #define UZ_VFX_HOT_RELOAD 1
#else
    #define UZ_VFX_HOT_RELOAD 0
#endif
```

### Build Configuration

| Build Type | Hot-Reload | Reason |
|-----------|-----------|--------|
| Debug (NDEBUG not set) | **Enabled** | Development iteration |
| Release with dev flag (`UZ_DEV_BUILD`) | **Enabled** | Packaged dev builds |
| Shipping (`NDEBUG` set, no `UZ_DEV_BUILD`) | **Disabled** | Performance, no filesystem scanning |

### Integration

Hot-reload is automatically initialized by the VFX system if `UZ_VFX_HOT_RELOAD` is enabled. No manual code required in most cases:

```cpp
// In VFXSystem::Init() (automatic)
#if UZ_VFX_HOT_RELOAD
    vfx_hot_reload_.Initialize();
    vfx_hot_reload_.SetOnReload([this](const std::string& path, const VFXGraph& graph) {
        OnVFXReloaded(path, graph);  // Update active effects
    });
#endif

// In VFXSystem::Update() (automatic)
#if UZ_VFX_HOT_RELOAD
    vfx_hot_reload_.Poll();  // Called once per frame
#endif
```

## Workflow: Edit → Save → See Changes

### Step 1: Open VFX Editor

Create or open a `.uzvfx` file in your editor:

```bash
# In Visual Studio Code or text editor
vfx/fire_campfire.uzvfx
```

### Step 2: Modify the Effect

Change parameters, add modules, or adjust spawn rate:

```json
{
  "format": "uzvfx",
  "version": 1,
  "name": "fire_campfire",
  "emitter": {
    "max_particles": 5000,
    "lifespan_default": 3.0
  },
  "modules": [
    {
      "type": "spawn",
      "config": {
        "rate": 300,      // <- Changed from 200
        "spawn_velocity": [0, 3, 0]  // <- Changed from [0, 2, 0]
      }
    }
    // ... rest of modules
  ]
}
```

### Step 3: Save the File

Use Ctrl+S (or equivalent). The file is written to disk.

### Step 4: Watch It Update

If the engine is running with `VFXHotReload` active:

1. **Within next poll cycle** (typically < 100 ms):
   - FileWatcher detects the file change.
   - `VFXHotReload::Poll()` reads the new timestamp.
   - JSON is parsed and validated.
   - Shaders are recompiled (if affected).
2. **Active effects update immediately**:
   - Any particle effect using this `.uzvfx` is replaced with the new graph.
   - Existing particles continue, but new spawns use updated config.
3. **Visual feedback**: Effect visibly changes in-engine (higher spawn rate, faster initial velocity).

### Step 5: Iterate

Tweak values, save again, see changes instantly. No restart required.

## VFXHotReload API

### Initialization

```cpp
namespace uz {
    class VFXHotReload {
    public:
        /// Initialize hot-reload system. Must be called once at engine startup.
        /// Scans asset directories and registers all loaded .uzvfx files.
        void Initialize();

        /// Shutdown hot-reload system. Call at engine shutdown.
        void Shutdown();

        /// Poll for file changes. Call once per frame (or less frequently).
        /// Returns count of files reloaded.
        int Poll();

        /// Register a callback for VFX reload events.
        /// Callback: void(const std::string& path, const VFXGraph& new_graph)
        void SetOnReload(std::function<void(const std::string&, const VFXGraph&)> callback);
    };
}
```

### Example: Engine Integration

```cpp
// In Engine::Init()
#if UZ_VFX_HOT_RELOAD
    vfx_hot_reload_.Initialize();
    vfx_hot_reload_.SetOnReload([this](const std::string& path, const VFXGraph& graph) {
        vfx_system_->OnVFXReloaded(path, graph);
    });
#endif

// In Engine::Update()
#if UZ_VFX_HOT_RELOAD
    vfx_hot_reload_.Poll();
#endif

// In Engine::Shutdown()
#if UZ_VFX_HOT_RELOAD
    vfx_hot_reload_.Shutdown();
#endif
```

## Limitations

### Files Not Detected If Not Already Loaded

Hot-reload monitors **currently loaded** `.uzvfx` files. If you create a new `.uzvfx` file on disk, it is **not** automatically picked up until explicitly spawned in the engine.

**Workaround:**
1. Spawn the new effect in-engine once: `ctx.vfx:spawn("vfx/my_new_effect.uzvfx", pos)`
2. Now the file is registered with hot-reload.
3. Further edits are hot-reloaded.

### Shader Recompilation Overhead

If `.uzvfx` changes cause shader pipeline changes (e.g., different module combination), shaders must be recompiled:

- Recompilation time: typically 50–200 ms per effect (depends on shader complexity).
- During recompilation, the effect is briefly unavailable (renders as invisible).
- No frame stuttering (recompilation is off-thread where possible).

### No Undo of Disk Changes

Hot-reload applies changes from the disk file. If you:
1. Modify `.uzvfx` and save.
2. See hot-reloaded change in-engine.
3. Want to revert...

You must **manually revert the file** (Ctrl+Z in editor, Git checkout, etc.). Hot-reload does not maintain undo history.

### Parameter Bindings Not Hot-Reloaded

If you add a new parameter to `.uzvfx` but Lua scripts reference old parameter names, those references are **not automatically updated**:

```lua
-- Old code
ctx.vfx:set_parameter(fire, "intensity", 1.0)  -- Works

-- You rename parameter to "flame_intensity" in fire.uzvfx
ctx.vfx:set_parameter(fire, "intensity", 1.0)  -- Still references old name, logs warning
```

Update script references manually.

## Troubleshooting

### Hotspot: "File was changed on disk, but effect didn't update"

**Cause**: File might not be registered with hot-reload yet.

**Solution**: Ensure the effect is spawned at least once:
```lua
-- First spawn
fire = ctx.vfx:spawn("vfx/fire.uzvfx", position)

-- Now edits to fire.uzvfx will hot-reload
```

### Hotspot: "Shader recompilation failed"

**Cause**: Invalid shader syntax introduced in `.uzvfx` module changes.

**Solution**: Check shader compiler logs. Revert the change and fix syntax:
```json
// Bad: Module type doesn't exist
{ "type": "invalid_module", "config": {} }

// Good: Use valid module type
{ "type": "turbulence", "config": { "strength": 2.0 } }
```

### Hotspot: "Parameter not found warning"

**Cause**: Lua script references parameter name not in `.uzvfx`.

**Solution**: Ensure parameter names match:
```lua
-- In fire.uzvfx: parameter name is "intensity"
ctx.vfx:set_parameter(fire, "intensity", 1.0)  -- Correct

-- Don't do this:
ctx.vfx:set_parameter(fire, "burn_intensity", 1.0)  -- Wrong name, logs warning
```

## Best Practices

### 1. Organize VFX by Function

Create focused, single-purpose `.uzvfx` files:

```
vfx/
├── fire_campfire.uzvfx
├── fire_torch.uzvfx
├── smoke_small.uzvfx
├── smoke_large.uzvfx
├── debris_particles.uzvfx
└── explosion_large.uzvfx_composite
```

This allows fast iteration on individual effects.

### 2. Use External Editor + Auto-Save

Keep `.uzvfx` in a text editor with auto-save enabled:

```bash
# VS Code with auto-save
File → Preferences → Settings
Search "auto-save" → Select "afterDelay"
```

Changes are applied within 100 ms of editing (no manual save needed).

### 3. Test with Live Effects

Always keep at least one active effect running while iterating:

```lua
-- In debug scene or dev mode
local test_effect = ctx.vfx:spawn("vfx/fire.uzvfx", {0, 0, 0})
-- Keep this running during development for instant feedback
```

### 4. Monitor Frame Rate

Hot-reload polling is cheap (< 1 ms), but shader recompilation can cause frame hitches. Keep a frame rate overlay visible:

```lua
-- Enable FPS counter in debug build
game.EnableDebugOverlay()
```

Watch for frame stutters during hot-reload.

### 5. Version Control

Commit tested `.uzvfx` files to version control. Hot-reload changes are not saved to version control automatically:

```bash
git add vfx/fire_campfire.uzvfx
git commit -m "Tweak fire spawn rate and color"
```

## Implementation Architecture

### File Monitoring

The hot-reload system uses `std::filesystem::last_write_time()` for change detection:

```cpp
class VFXHotReload {
    struct LoadedFile {
        std::string path;
        std::filesystem::file_time_type lastWrite;
        VFXGraphHandle graphHandle;
    };

    std::vector<LoadedFile> loadedFiles_;
    std::function<void(const std::string&, const VFXGraph&)> onReload_;
};
```

On each poll, timestamps are checked and changed files are reloaded. The system is efficient (O(n) where n = number of loaded VFX files, typically < 50).

### Recompilation Pipeline

When a `.uzvfx` file changes:

1. **Parse JSON** — Load and validate file structure
2. **Compile Graph** — Build shader modules, validate module chain
3. **Register PSOs** — Add/update compute and graphics pipelines in `PipelineLibrary`
4. **Apply Update** — Call `OnReload` callback with new graph
5. **Update Effects** — Replace graph on all active effects

The recompilation is **not** frame-locked — it runs immediately when change detected, then effects use new graph next frame.

### Shader Compilation

If shader source changes (rare for `.uzvfx` — shaders are typically static), recompilation happens via:

- Module type → shader stage mapping (SpawnModule → compute, etc.)
- DXC compilation with full debug info (`-Zi -Od`)
- Pipeline library update with new PSO handle

## Future Extensions (Phase 3.0+)

Potential enhancements (not yet implemented):

- **Automatic new file detection**: New `.uzvfx` files in asset directories automatically registered (currently requires one spawn to register).
- **Parameter persisting**: Changed parameters persist across hot-reload (not reset to defaults).
- **Diff visualization**: Show what changed between old and new version before reloading.
- **Undo/Redo**: Record hot-reload history and allow reverting.
- **Multi-platform watch**: fsnotify-style file watching instead of polling (platform-specific, optional optimization).

## See Also

- [VFX System Overview](01-overview.md) — Architecture and systems
- [.uzvfx Format](07-uzvfx-format.md) — Asset file format
- [Module API](04-module-api.md) — Module programming (affects shader compilation)
- [Lua Scripting Overview](../../28-lua/01-scripting-overview.md) — Script integration with hot-reload
- [Lua VFX API](09-lua-vfx-api.md) — Effect control from Lua scripts
