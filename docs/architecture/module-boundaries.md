# Module Boundaries — Architecture Directives

> **Status**: MANDATORY — applies to all new code and migration of existing code.
> **Enforced by**: Code review, include-guard analysis, AGENTS.md rule §MODULE-BOUNDARY.

---

## 1. The Problem This Solves

An engine without strict module boundaries becomes an unmaintainable monolith. The current
UzEngine codebase has a **dual-renderer problem**: two parallel rendering subsystems
exist simultaneously:

```
src/renderer/        ← LEGACY — direct DX12, no abstraction (~3000 LOC violation)
src/engine/renderer/ ← CORRECT — uses rhi.h abstraction

src/engine/scene/    ← Scene data, BUT scene_node.h includes engine/core/ (OK)
src/renderer/        ← WRONG — directly includes d3d12.h, scene_node.h
```

This creates a dependency tangle: the old renderer knows about both the scene AND DX12 directly.
This document defines the strict module boundaries that prevent this from happening again.

---

## 2. The Official Module Tree

```
e:\UzEngine\src\
│
├── engine\                   ← ENGINE LAYER (no platform, no GPU backend, no game)
│   ├── core\                 ← Foundation: math, arena, handles, result, assert
│   │   ├── math.h            ← Vec3/Vec4/Mat4/Quat (own types, NOT DirectXMath)
│   │   ├── arena.h           ← Frame/level/permanent allocators
│   │   ├── handle.h          ← Handle<T> generational indices
│   │   ├── result.h          ← Result<T,E> error handling
│   │   └── string_id.h       ← Interned string IDs
│   │
│   ├── scene\                ← Scene graph: SceneNode, ComponentStore, SceneLoader
│   │   ├── scene_node.h      ← NodeType, SceneNode struct (data only)
│   │   ├── component_data.h  ← Per-component data structs (no GPU types)
│   │   ├── component_store.h ← Type-mapped component storage
│   │   ├── scene.h           ← Scene class (flat node array + component stores)
│   │   └── scene_manager.h   ← Loads/unloads/queries scenes
│   │
│   ├── renderer\             ← RENDER ABSTRACTION LAYER (uses rhi.h, not DX12)
│   │   ├── rhi\              ← Backend C API
│   │   │   ├── rhi.h         ← THE WALL — only file that cross-boundary code includes
│   │   │   ├── rhi_dx12.c    ← DX12 backend (d3d12.h ONLY here)
│   │   │   ├── rhi_vulkan.c  ← Vulkan backend (vulkan.h ONLY here)
│   │   │   ├── rhi_metal.c   ← Metal backend (Metal.h ONLY here)
│   │   │   └── rhi_common.c  ← PSO cache, hash helpers
│   │   ├── render_graph.h    ← Frame DAG (uses rhi.h, NOT d3d12.h)
│   │   ├── shader_manager.h  ← Shader loading/hot-reload (uses rhi.h)
│   │   ├── viewport_registry.h ← Multi-viewport management
│   │   └── passes\           ← Render passes (GBuffer, Lighting, SSAO, etc.)
│   │
│   ├── editor\               ← Editor systems (asset registry, undo/redo)
│   │   ├── asset_registry.h  ← Asset discovery (no GPU types)
│   │   └── undo_redo.h       ← EditorCommand pattern
│   │
│   └── debug\                ← Debug/profiling systems
│       ├── console.h         ← Debug console
│       └── cvar.h            ← Console variables
│
├── platform\                 ← OS abstractions (window, file IO, threads)
│   └── platform.h            ← HWND/window handle (minimal OS surface)
│
├── network\                  ← WebSocket server for VSCode IPC
│   └── ws_server.h
│
├── common\                   ← Shared utilities (IPC handlers, ring buffers)
│   ├── ipc_registry.h
│   └── types.h               ← FrameData, primitive types
│
└── main.cpp                  ← Entry point, wires everything together
```

> **DEPRECATED** (to be deleted after migration):
> `src/renderer/` — the entire old directory is legacy and will be removed.

---

## 3. Dependency Rules

### The Dependency Graph (arrows = "may depend on")

```
main.cpp
    │
    ├──► engine/renderer/  ──► engine/renderer/rhi/ (rhi.h)
    │                          └──► [NO FURTHER DEPS from above]
    │
    ├──► engine/scene/     ──► engine/core/
    │
    ├──► engine/editor/    ──► engine/scene/
    │                      ──► engine/core/
    │
    ├──► engine/debug/     ──► engine/core/
    │
    ├──► common/           ──► engine/scene/
    │                      ──► engine/renderer/
    │
    └──► network/          ──► [external: uWebSockets]
```

### RULE MOD-1: engine/core has ZERO external dependencies

`engine/core/` MUST NOT include headers from:
- `engine/scene/`
- `engine/renderer/`
- Any external library except standard C library (`<stdint.h>`, `<stdbool.h>`, `<string.h>`)
- Platform headers (`<windows.h>`, `<unistd.h>`)

```cpp
// ❌ WRONG — core including scene
// engine/core/some_system.h
#include "engine/scene/scene_node.h"  // FORBIDDEN

// ✅ CORRECT — core is self-contained
// engine/core/arena.h
#include <stdint.h>
#include <stddef.h>
// Engine's own types only
```

### RULE MOD-2: engine/scene has NO renderer dependencies

`engine/scene/` MUST NOT include headers from:
- `engine/renderer/`
- `engine/renderer/rhi/rhi.h`
- Any DX12/Vulkan/Metal headers
- Any GPU handle types

Scene data is backend-agnostic. The renderer reads from the scene; the scene does NOT
know about the renderer.

```cpp
// ❌ WRONG — scene depending on renderer
// engine/scene/scene_node.h
#include "engine/renderer/rhi/rhi.h"  // FORBIDDEN

struct SceneNode {
    RhiBuffer gpu_mesh_buffer;  // FORBIDDEN — GPU handle in scene data
};

// ✅ CORRECT — scene data has no GPU types
struct StaticMeshData {
    char mesh_path[256];    // Asset path — resolved by renderer when creating proxy
    uz::Aabb local_bounds;  // Logical bounds
};
```

### RULE MOD-3: engine/renderer depends on engine/scene and engine/core, but NOT vice versa

The renderer reads scene data to build render proxies. This is a one-way dependency.

```
✅ ALLOWED:   engine/renderer/passes/gbuffer_pass.cpp  →  engine/scene/component_data.h
❌ FORBIDDEN: engine/scene/scene_manager.cpp           →  engine/renderer/render_graph.h
```

### RULE MOD-4: The src/renderer/ directory is LEGACY — no new files

**MUST NOT** create new files in `src/renderer/`. All new rendering code goes in
`src/engine/renderer/`. The `src/renderer/` directory exists only until migration completes.

### RULE MOD-5: rhi.h is the ONLY file that crosses the RHI boundary

The include chain MUST be:
```
game_pass.cpp
    → render_graph.h (uses RhiCmdList, RhiTexture)
        → rhi.h (defines RhiCmdList, RhiTexture)
            [STOP — rhi_dx12.c is compiled separately, linked as backend]
```

Nothing above `rhi.h` may include `rhi_dx12.h` or `d3d12.h`.

---

## 4. Header Include Rules

### RULE MOD-6: Headers include only what they directly use

Avoid transitive include pollution. Each header MUST include only what it needs.

```cpp
// ❌ WRONG — pulls in everything transitively
// engine/scene/scene_manager.h
#include "renderer.h"      // pulls in d3d12.h, DirectXMath.h, everything
#include "engine/core/core.h"  // pulls in all of core

// ✅ CORRECT — minimal, explicit includes
// engine/scene/scene_manager.h
#include "engine/scene/scene_node.h"
#include "engine/scene/scene.h"
#include <cstdint>
```

### RULE MOD-7: Forward declarations over includes in headers

Prefer forward declarations in `.h` files and full includes in `.cpp` files.

```cpp
// ✅ CORRECT — forward declare in header
// engine/renderer/passes/gbuffer_pass.h
namespace uz { struct StaticMeshData; }  // Forward declare — no include needed

class GBufferPass {
    void setup_mesh(const StaticMeshData& data);  // Forward declared type OK in declaration
};

// engine/renderer/passes/gbuffer_pass.cpp
#include "engine/scene/component_data.h"  // Full include here is fine
```

---

## 5. The src/renderer/ Migration Plan

| Status | File | Migration Target |
|--------|------|-----------------|
| 🔴 ACTIVE | `src/renderer/renderer.h` | → `src/engine/renderer/renderer.h` (RHI-based) |
| 🔴 ACTIVE | `src/renderer/renderer.cpp` | → `src/engine/renderer/renderer.cpp` |
| 🔴 ACTIVE | `src/renderer/components/editor_component.h` | → `src/engine/renderer/passes/editor_pass.h` |
| 🔴 ACTIVE | `src/renderer/components/camera_component.h` | → scene/component_data.h + CameraProxy |
| 🔴 ACTIVE | `src/renderer/components/light_component.h` | → scene/component_data.h + LightProxy |
| 🔴 ACTIVE | `src/renderer/gizmos/*.h` | → `src/engine/renderer/gizmos/*.h` (RHI-based) |
| 🔴 ACTIVE | `src/renderer/fps_overlay.h` | → `src/engine/renderer/fps_overlay.h` |
| 🔴 ACTIVE | `src/renderer/scene_params.h` | → `src/engine/scene/scene_params.h` |

### RULE MOD-8: After migration, delete src/renderer/ entirely

Once all files are migrated, `src/renderer/` is deleted. No symlinks, no "compat shims",
no re-exports. Clean deletion.

---

## 6. Adding a New Module

When adding a new subsystem (e.g., `engine/audio/`, `engine/physics/`):

1. Create a directory under `src/engine/<module_name>/`
2. Define what the module OWNS (its types, its files)
3. Define what it CAN depend on (use the dependency graph above as guide)
4. Define what it MUST NOT depend on (typically: renderer, other high-level modules)
5. Document in this file

```
engine/audio/ — May depend on:
  ✅ engine/core/ (math, handles)
  ✅ engine/scene/ (component_data.h for CAudioSource)
  ❌ engine/renderer/ (audio has no GPU dependency)
  ❌ network/ (audio is not a network service)
```

---

## 7. Compile-Time Enforcement (Future CI Gate)

The following grep check MUST pass in CI before any merge to main:

```bash
# No DX12/Vulkan/Metal includes outside RHI backend files
grep -rn "#include.*d3d12\|#include.*vulkan\|#include.*Metal" \
     src/ \
     --include="*.h" --include="*.cpp" --include="*.c" \
     | grep -v "rhi_dx12\|rhi_vulkan\|rhi_metal"
# Must produce zero output

# No engine/scene/ including engine/renderer/
grep -rn "#include.*engine/renderer\|#include.*render_graph\|#include.*rhi.h" \
     src/engine/scene/ \
     --include="*.h" --include="*.cpp"
# Must produce zero output

# No engine/core/ including anything above it
grep -rn "#include.*engine/scene\|#include.*engine/renderer\|#include.*engine/editor" \
     src/engine/core/ \
     --include="*.h" --include="*.cpp"
# Must produce zero output
```
