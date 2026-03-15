# Proposal: Application Abstraction Layer (`uz::Application`)

## Why

UzEngine demos and games currently implement ~60-70% engine boilerplate that has nothing to do
with actual game logic:

- **G-Buffer creation** — every demo manually creates render targets (albedo, normal, metallic/roughness, depth)
- **Shadow atlas creation** — every demo allocates the 4096×4096 D32F texture manually
- **Per-frame constant buffer construction** — every demo builds PerFrameCB, LightingCB, ShadowDataCB
- **Shadow cascade computation** — every demo calls ComputeCascades() manually
- **Draw list building** — every demo constructs and sorts the DrawList manually
- **Render graph scaffolding** — every demo assembles and compiles the full render graph (shadow → GBuffer → lighting → overlay → present)
- **Resource loading** — every demo loads meshes/textures and creates 1×1 fallback textures

Result: `pbr_test_demo.cpp` is **565 lines** where fewer than 100 lines are actual game logic.
Mature engines (UE5, Godot, Unity) abstract all of this — developers write only game logic.

`SceneRenderer` is already designed (`engine/renderer/scene_renderer.h`) to encapsulate all
rendering infrastructure. Missing pieces: (1) implement the `.cpp`, (2) create `uz::Application`
that wraps the full lifecycle, and (3) migrate all demos to the new format.

## What Changes

### New `uz::Application` class in `runtime/engine/app/`

What a game developer writes:

```cpp
class PBRTestApp : public uz::Application {
    uz::Entity cube_;

    void OnStart() override {
        cube_ = GetWorld().CreateEntity();
        GetWorld().Add<StaticMeshComponent>(cube_, "meshes/cube.uzmesh");
        GetWorld().Add<MaterialComponent>(cube_, "materials/stone_pbr.uzmat");
    }

    void OnUpdate(float dt) override {
        auto& t = GetWorld().Get<TransformComponent>(cube_);
        t.rotation.y += dt * 45.0f;
    }
};

UZ_APPLICATION_MAIN(PBRTestApp)
```

### Internal architecture

```
uz::Application
  ├── ApplicationConfig    — title, resolution, render path, VFS root, max FPS
  ├── PlatformWindow       — created internally
  ├── IRHIDevice           — created internally (DX12 default)
  ├── IRHISwapChain        — created internally
  ├── SceneRenderer        — owns ALL rendering (GBuffer, shadow atlas, render graph)
  ├── World (ECS)          — accessible via GetWorld()
  ├── IVFS                 — accessible via GetVFS()
  └── Frame loop           — managed internally (fence sync, resize, dt calculation)
```

### Minimal interface for games/demos

```cpp
class Application {
public:
    virtual ApplicationConfig Configure();        // called before Run()
    virtual void OnStart()          {}            // after full init
    virtual void OnUpdate(float dt) {}            // every frame
    virtual void OnStop()           {}            // before shutdown

    World&         GetWorld();
    IVFS&          GetVFS();
    Camera&        GetCamera();
    InputSystem&   GetInput();
    SceneRenderer& GetRenderer();

    void Quit();
    void LoadScene(std::string_view path);
    int  Run(int argc, char** argv);
};

#define UZ_APPLICATION_MAIN(AppClass)          \
    int main(int argc, char** argv) {          \
        AppClass app;                          \
        return app.Run(argc, argv);            \
    }
```

### Equivalent Lua API

```lua
-- Application lifecycle control
uz.app.quit()
uz.app.load_scene("scenes/demo.uzscene")
uz.app.set_render_path("deferred")   -- "deferred" | "forward"
uz.app.set_debug_view("albedo")      -- "lit" | "albedo" | "normal" | "metallic" | ...

-- Game code in Lua is equivalent to C++:
function on_start()
    local cube = uz.scene.create_entity()
    uz.scene.add_mesh(cube, "meshes/cube.uzmesh")
end

function on_update(dt)
    -- pure game logic
end
```

## Impact

- **Affected specs**:
  - `docs/specs/engine/application-layer.md` — new full spec
  - `docs/specs/lua-scripting.md` — extension with uz.app bindings
- **Affected code**:
  - `runtime/engine/app/` — new directory (application.h/.cpp, app_config.h)
  - `runtime/engine/renderer/scene_renderer.cpp` — SceneRenderer implementation (header already exists)
  - `runtime/demos/common/demo_application.h/.cpp` — kept as legacy during transition
  - All 9 demos — migrated to new format
  - `runtime/engine/scripting/api/core/lua_app_api.cpp` — new Lua bindings
  - `runtime/engine/uzengine.h` — add include for app/application.h
- **Breaking change**: NO — DemoApplication kept during transition
- **User benefit**: Demo/game goes from 500+ lines to ~50-100 lines containing only game logic
