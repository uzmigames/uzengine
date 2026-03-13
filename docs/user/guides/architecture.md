# Architecture

UzEngine is a three-layer system: a VSCode extension (TypeScript), a C++ runtime
loaded as a native N-API addon, and an MCP server for AI integration.

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                  VSCode Extension (TypeScript)                  │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌──────────────────┐  │
│  │ Viewport │ │ Inspector │ │ Hierarchy│ │  Asset Browser   │  │
│  │ (webview)│ │ (webview) │ │ (webview)│ │  (webview)       │  │
│  └──────────┘ └───────────┘ └──────────┘ └──────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                 Runtime (C++ / N-API .node addon)               │
│  ┌─────┐ ┌──────────┐ ┌─────────┐ ┌───────┐ ┌──────────────┐  │
│  │ RHI │ │ Renderer │ │ Physics │ │ Audio │ │    Scene/ECS  │  │
│  │     │ │          │ │  (Jolt) │ │       │ │    (EnTT)     │  │
│  └──┬──┘ └──────────┘ └─────────┘ └───────┘ └──────────────┘  │
│     │ DX12 | Vulkan | Metal                                    │
├─────┼───────────────────────────────────────────────────────────┤
│     │           MCP Server (Node.js)                            │
│     │  AI tools, content generation, scene manipulation         │
└─────┼───────────────────────────────────────────────────────────┘
      ▼
   ┌──────┐
   │  GPU │
   └──────┘
```

## Process Model

The runtime runs **in-process** with the VSCode extension (via N-API). No separate
process, no IPC overhead. The 3D viewport renders to its own native HWND with a DX12
swapchain — no frame streaming for the main viewport.

2D editor panels (inspector, hierarchy, asset browser) use webview panels.
When these need a live preview, H.264 frame streaming delivers 120 FPS via
Media Foundation + WebCodecs.

## Directory Structure

```
runtime/
├── src/                     # Runtime C API (runtime.h/.cpp)
├── engine/
│   ├── logging/             # log.h — spdlog-backed structured logging
│   ├── assertions/          # engine_assert.h — UZ_CHECK, UZ_ENSURE, UZ_VERIFY
│   ├── profiling/           # profiler.h — Tracy integration, stat counters
│   ├── memory/              # ownership.h — Own<T>/Ref<T>, allocators.h
│   ├── config/              # config.h — .uzproject JSON + CLI overrides
│   ├── math/                # engine_math.h — DirectXMath SIMD wrappers
│   ├── containers/          # Ring buffer, free list, handle map, frame array
│   ├── ecs/                 # ECS system (EnTT integration)
│   │   ├── world.h               # World container
│   │   ├── entity.h              # Entity type and utilities
│   │   ├── world_subsystem.h     # ECS subsystem lifecycle
│   │   ├── component.h           # Component base class + lifecycle
│   │   ├── scene_component.h     # Transform hierarchy + parent/child
│   │   ├── primitive_component.h # Renderable with RenderProxy bridge
│   │   ├── static_mesh_component.h # Mesh + material rendering
│   │   ├── camera_component.h    # Camera with projection matrices
│   │   └── light_component.h     # Directional/Point/Spot light sources
│   ├── event/               # Event dispatch system
│   │   ├── delegate.h            # MulticastDelegate<Args...> for callbacks
│   │   └── event_queue.h         # Deferred per-frame event dispatch
│   ├── random/              # random.h — PCG generator
│   ├── rhi/                 # Abstract interfaces (rhi.h, rhi_types.h)
│   │   └── dx12/            # DX12 backend implementation
│   ├── shader/              # Shader pipeline (VFS, compiler, cache, hot-reload)
│   │   ├── shader_vfs.h           # Virtual filesystem for shader sources
│   │   ├── shader_source_manager.h # Include parsing, content hashing
│   │   ├── shader_dependency_graph.h # Forward/reverse include tracking
│   │   ├── shader_cache.h         # Disk-based bytecode cache
│   │   ├── shader_compiler.h      # DXC integration (HLSL→DXIL/SPIRV)
│   │   ├── shader_pipeline.h      # High-level compile orchestrator
│   │   ├── shader_permutation.h   # Feature flag variants
│   │   ├── shader_reflection.h    # SPIRV-Reflect binding extraction
│   │   ├── shader_hot_reload.h    # FileWatcher + dirty flags
│   │   └── shader_archive.h       # Shipping mode precompiled archive
│   ├── asset/               # Asset pipeline (VFS, importers, cache, registry)
│   │   ├── vfs.h                  # General-purpose virtual filesystem
│   │   ├── importers/             # Importer implementations
│   │   ├── formats/               # Binary format definitions
│   │   ├── asset_cache.h          # Asset content cache
│   │   ├── asset_manifest.h       # Build manifest tracking
│   │   ├── import_sidecar.h       # .import.json sidecar files
│   │   └── importers/asset_registry.h # Global importer registry
│   ├── renderer/            # Renderer and render graph
│   │   ├── render_world.h         # RenderWorld proxy container
│   │   ├── material.h             # PBRMaterial (Cook-Torrance params)
│   │   ├── draw_list.h            # DrawList with 64-bit sort keys
│   │   ├── constant_buffers.h     # Per-frame constant buffers
│   │   ├── camera.h               # Camera matrix computation
│   │   ├── mesh.h                 # Mesh topology and rendering
│   │   ├── upload_context.h       # GPU upload ring buffer
│   │   ├── vertex.h               # Vertex format definitions
│   │   └── render_graph/          # Render graph system
│   │       ├── render_graph_builder.h   # Graph builder API
│   │       ├── render_graph_pass.h      # Pass definition
│   │       ├── render_graph_resources.h # Resource binding
│   │       └── render_graph_types.h     # Type definitions
│   └── paths/               # Path resolution (paths.h)
├── platform/
│   └── windows/             # Win32 window, input system
├── tools/                   # Standalone executables (uzengine, uzcook)
├── demos/                   # Standalone demo projects (shared build_common.zig)
│   ├── common/              # Shared build system (build_common.zig)
│   │   └── build.zig        # Re-exports build_common.zig
│   ├── clear_screen/        # DX12 animated clear screen demo
│   ├── triangle/            # DX12 triangle demo (SM6.6 bindless)
│   └── ...                  # Additional demos
├── tests/                   # GoogleTest suites (mirrors source structure)
├── thirdparty/              # All third-party dependencies (git-tracked)
│   └── entt/                # EnTT ECS header-only library
├── build.zig                # Zig build system
└── CHANGELOG.md             # Version history
```

## Dependency Flow

Dependencies flow downward. No circular references.

```
tools/ ──→ engine/rhi/ ──→ engine/shader/ ──→ engine/ (logging, assertions, math, config)
             │
             ├──→ engine/rhi/dx12/  (DX12 backend)
             └──→ engine/rhi/vulkan/ (planned)

engine/shader/ ──→ engine/paths/ (resource resolution)
             ──→ engine/ (VFS, source loading, DXC integration)

engine/asset/  ──→ engine/shader/ (shader importer)
             ──→ engine/paths/ (resource resolution)
             ──→ engine/ (logging, VFS)

engine/renderer/ ──→ engine/rhi/ (GPU upload, vertex buffers)
             ──→ engine/math/ (Vec3, Quat, Mat4, Color)
             ──→ engine/containers/ (handle_map, free_list)

engine/ecs/  ──→ engine/renderer/ (PrimitiveComponent → RenderProxy)
             ──→ engine/math/ (Vec3, Quat, Mat4 transforms)
             ──→ engine/containers/ (handle_map for entities)

engine/event/ ──→ engine/ (logging, assertions)

engine/job/ ──→ engine/ (no complex deps)

platform/windows/ ──→ engine/paths/ (path resolution)
                  ──→ engine/ (logging, input)

tests/ ──→ everything (read-only, via link against static lib)
```

## Component Model

Entities are built as composable hierarchies of components:

```
Entity
├── ActorComponent          — No transform, pure logic (InputComponent, MovementComponent)
└── SceneComponent          — Has local transform, parent/child hierarchy
    ├── CameraComponent     — View/projection matrices
    ├── AudioSourceComponent — 3D positional sound playback
    ├── CollisionObject     — Physics presence (separate from rendering)
    │   ├── RigidBody       — Simulated physics
    │   ├── CharacterBody   — Kinematic ground detection
    │   └── TriggerVolume   — Overlap events only
    │
    └── PrimitiveComponent  — Visible in scene, render proxy in RenderWorld
        ├── StaticMeshComponent — Mesh + material rendering
        ├── SkeletalMeshComponent — Skinned mesh + bone transforms
        ├── LightComponent   — Directional/Point/Spot light
        ├── DecalComponent   — Projected material
        └── ParticleSystemComponent — GPU/CPU particle emitter
```

**Key design**: Physics and rendering are **independent**. A `CollisionObject` does NOT inherit from
`PrimitiveComponent`. This allows invisible physics objects and renderable-only objects.

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| N-API in-process, not IPC | POC 0.2 proved IPC adds unacceptable latency for frame-rate-sensitive ops |
| Own HWND for 3D viewport | WebGL/webview cannot match native DX12 quality; frame streaming adds latency |
| Zig + CMake build systems | Zig (primary) + CMake (MSVC/Clang). Three verified compilers: Zig, MSVC, Clang |
| SM6.6 bindless primary | One global descriptor heap, no descriptor table management, simpler shaders |
| Agility SDK bundled | Unlocks latest DX12 features regardless of OS-installed runtime |
| JSON for all project files | Human-readable, AI-parseable, diff-friendly |
| Content-addressed shader cache | xxHash64 of source + includes eliminates stale cache bugs during iteration |
| Exe-relative path resolution | All resources (DLLs, shaders, configs) relative to executable, never CWD |
| Virtual shader filesystem | Mount points decouple shader code from filesystem, enables mods/overrides |
| Lazy shader compilation | Mark dirty on file change, compile on next use (batches rapid saves) |
| Component-based entities | UE5-style class hierarchy with render proxy bridge, not flat ECS storage |
| PBR with Cook-Torrance | Physically based rendering: GGX NDF, Smith geometry, Schlick Fresnel |

## Related Pages

- [RHI Overview](rhi/overview.md) — Hardware abstraction layer design
- [Shader Pipeline](rendering/shader-pipeline.md) — Compilation, caching, hot-reload
- [Build System](build-system/zig-build.md) — Zig build details
- [Building from Source](../getting-started/building.md) — Quick start
- [API: Core Reference](../reference/core/index.md) — Logging, paths, config
