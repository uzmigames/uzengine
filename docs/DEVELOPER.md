> **DEPRECATED**: This document has been fully migrated to `docs/engine/22-developer-layer/`.
> See [docs/engine/22-developer-layer/01-overview.md](engine/22-developer-layer/01-overview.md) for the structured version.
> All 6 subsections covered: profiling, debug draw, hot-reload, automation, console, ImGui.
> This file is kept for reference only — do not update it.

---

# Developer Layer — Planning Document

> **Status**: Planning
> **Goal**: Define the `developer/` layer — tools and systems needed during development but stripped from shipping builds.

## Problem

Currently, development-only systems are scattered:
- **Profiling** lives in `core/profiling/` — but shipping games don't need Tracy integration
- **Shader hot-reload** lives in `core/shader/` — but shipping games use precompiled archives
- **Editor overlays** live in `editor/` — this is correct, but editor/ should be strictly visual tools
- **No test automation hooks** — no way to run automated tests against the engine programmatically
- **No debug draw** — no system for rendering debug lines, shapes, text in development builds

## What the Developer Layer Is

The **developer layer** contains systems that exist ONLY in development builds (`UZ_DEV_BUILD=1`). They are compiled out of shipping builds via preprocessor guards, producing a smaller, faster binary.

This mirrors:
- **UE5's `Developer/` directory** — DerivedDataCache, ShaderCompilerCommon, AutomationController, TargetPlatform
- **Godot's `TOOLS_ENABLED` guard** — strips all development tooling from export templates

```
game code / engine/
        ↓ (can optionally use)
   developer/       ← THIS LAYER (dev-only, stripped in shipping)
        ↓
   core/, rhi/, renderer/, platform/
```

## What Lives in developer/

### developer/profiling/ — CPU & GPU Profiling
- **Currently**: `core/profiling/profiler.h/.cpp` (Tracy integration)
- **Change**: Move to `developer/profiling/`. In shipping builds, `UZ_PROFILE_SCOPE` compiles to nothing.
- **Add**: GPU profiler visualization (timing queries, pipeline stats)
- **Add**: Memory profiler (allocation tracking, leak detection)
- **Add**: Frame profiler (per-system timing breakdown displayed in editor)

### developer/debug_draw/ — Debug Visualization
- **New**: Debug line rendering (rays, boxes, spheres, frustums, skeletons)
- **New**: Debug text rendering (world-space and screen-space labels)
- **New**: Gizmo rendering (translation/rotation/scale handles)
- **API**: `uz::debug::DrawLine(from, to, color, duration)`
- **Lifetime**: persistent (stays until cleared) or timed (auto-fades)
- In shipping builds, all debug draw calls compile to nothing.

### developer/hot_reload/ — Asset & Code Hot-Reload
- **Currently**: `core/shader/shader_hot_reload.h/.cpp` (shader-only)
- **Change**: Generalize to all asset types (textures, meshes, scenes, scripts)
- **FileWatcher** — monitors asset directories for changes
- **DependencyGraph** — tracks which assets depend on which (shader includes, material → texture)
- **ReloadDispatcher** — notifies affected systems when an asset changes
- Shipping builds load from precompiled archives — no watcher, no reloader.

### developer/automation/ — Test & Automation Framework
- **New**: In-engine test runner (headless mode for CI)
- **New**: Screenshot comparison tests (golden image regression)
- **New**: Performance benchmark framework (frame time targets)
- **New**: Console command system for dev builds (`/spawn`, `/teleport`, `/stat fps`)
- **API**: `uz::automation::RegisterTest("test_name", callback)`

### developer/console/ — Development Console
- **New**: In-game developer console (text input + command execution)
- **New**: CVar system (runtime-tweakable variables)
- **API**: `UZ_CVAR(float, r_shadowDistance, 100.0f, "Max shadow draw distance")`
- **Example**: type `r_shadowDistance 200` in console → updates in real-time
- Stripped from shipping builds.

### developer/imgui/ — ImGui Integration (optional)
- **New**: ImGui debug overlay for rapid prototyping
- **Windows**: Entity inspector, material editor, render stats, memory viewer
- **Only in dev builds** — not a replacement for the VSCode editor panels

## Build Configuration

```cpp
// developer/developer_config.h

#if defined(UZ_SHIPPING)
    #define UZ_DEV_BUILD 0
#else
    #define UZ_DEV_BUILD 1
#endif

// Usage in engine code:
#if UZ_DEV_BUILD
    #include "developer/profiling/profiler.h"
    #define UZ_PROFILE_SCOPE(name) uz::dev::ProfileScope _scope(name)
#else
    #define UZ_PROFILE_SCOPE(name) ((void)0)
#endif

#if UZ_DEV_BUILD
    #include "developer/debug_draw/debug_draw.h"
    #define UZ_DEBUG_LINE(from, to, color) uz::dev::DrawLine(from, to, color)
#else
    #define UZ_DEBUG_LINE(from, to, color) ((void)0)
#endif
```

## Dependency Rules

```
developer/      → core/, rhi/, renderer/, engine/, platform/  (can use anything)
engine/         → (does NOT depend on developer/)
core/           → (does NOT depend on developer/)
rhi/            → (does NOT depend on developer/)
```

**Strict invariant:** No non-developer module may `#include` a developer/ header without a `#if UZ_DEV_BUILD` guard. The developer layer is a leaf — nothing depends on it at the module level.

## What Moves from Current Structure

| Current Location | New Location | Reason |
|---|---|---|
| `core/profiling/profiler.h/.cpp` | `developer/profiling/` | Profiling is dev-only |
| `core/shader/shader_hot_reload.h/.cpp` | `developer/hot_reload/` | Hot-reload is dev-only |
| `core/shader/shader_dependency_graph.h/.cpp` | `developer/hot_reload/` | Dependency tracking is for hot-reload |
| `core/shader/shader_source_manager.h/.cpp` | `developer/hot_reload/` | Source watching is dev-only |
| `core/shader/shader_vfs.h/.cpp` | Stays in renderer/shader/ | VFS is needed in shipping (for loading archives) |

## What Does NOT Move

| Module | Stays Where | Reason |
|---|---|---|
| `editor/grid/` | `editor/` | Editor visual feature, not a dev tool |
| `editor/stats/` | `editor/` | Editor overlay, not a dev tool |
| `editor/font/` | `editor/` | Editor text rendering |
| `core/logging/` | `core/` | Logging ships in all builds (level-gated, not stripped) |
| `core/assertions/` | `core/` | Assertions ship in all builds (debug + release) |

## editor/ vs developer/ — The Boundary

| | editor/ | developer/ |
|---|---|---|
| **Purpose** | Visual tools for content creation | Infrastructure for development workflow |
| **Examples** | Grid overlay, gizmos, property inspector, stats HUD | Profiling, hot-reload, debug draw, console, CVars |
| **Ships in** | Editor builds only (`UZ_EDITOR_BUILD`) | Dev builds only (`UZ_DEV_BUILD`) |
| **Depends on** | engine/, developer/ | core/, rhi/, renderer/, engine/ |
| **UI** | Rendered in viewport or VSCode panels | Console, ImGui, or headless |

Note: `UZ_EDITOR_BUILD` implies `UZ_DEV_BUILD`. Editor builds always have dev tools. But dev builds can exist without the editor (e.g., a standalone dev game executable with profiling and console but no editor panels).

## Build Matrix

| Build Type | `UZ_DEV_BUILD` | `UZ_EDITOR_BUILD` | `UZ_SHIPPING` | Includes |
|---|---|---|---|---|
| **Editor** | 1 | 1 | 0 | core + rhi + renderer + engine + developer + editor |
| **Development** | 1 | 0 | 0 | core + rhi + renderer + engine + developer |
| **Shipping** | 0 | 0 | 1 | core + rhi + renderer + engine |

## Migration Strategy

1. **Phase 1**: Create `developer/` directory, move `core/profiling/` → `developer/profiling/`
2. **Phase 2**: Move shader hot-reload + dependency graph → `developer/hot_reload/`
3. **Phase 3**: Implement `developer/debug_draw/` (debug lines, shapes)
4. **Phase 4**: Implement `developer/console/` (CVar system + command input)
5. **Phase 5**: Implement `developer/automation/` (test runner, screenshot tests)
6. **Phase 6**: Update build system — add `UZ_DEV_BUILD` / `UZ_SHIPPING` defines
7. **Phase 7**: Verify shipping build compiles and runs without developer/ code

## Open Questions

1. **CVar persistence?** — Should CVars save to a config file, or are they session-only?
2. **ImGui or custom?** — ImGui is battle-tested but adds a dependency. Custom debug UI is leaner but more work.
3. **Remote profiling?** — Should the profiler support remote connections (e.g., profile a game running on another machine)?
4. **Debug draw in editor?** — Should `editor/` use `developer/debug_draw/` for gizmos, or have its own rendering?
