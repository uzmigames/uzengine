## 1. Memory System — Arena Allocator

- [ ] 1.1 Create `src/common/arena.h` — `Arena` struct with `alloc`, `alloc_one<T>`, `alloc_array<T>`, `reset`, `child`
- [ ] 1.2 Create `src/common/arena.cpp` — `make_arena(capacity)` via VirtualAlloc, `free_arena`
- [ ] 1.3 Add global arena accessors: `permanent_arena()`, `level_arena()`, `frame_arena()`, `scratch_arena()` (thread-local)
- [ ] 1.4 Arena tests: alloc, reset, overflow guard, alignment

## 2. Handle System

- [ ] 2.1 Create `src/common/handle.h` — `Handle<T>` with `index`, `generation`, `valid()`
- [ ] 2.2 Create `src/common/pool.h` — `Pool<T,N>` with `alloc()`, `free()`, `get()`, generation bump on free
- [ ] 2.3 Handle unit tests: alloc, free, stale handle returns null

## 3. Node Types & Data Model

- [ ] 3.1 Create `src/scene/node_types.h` — `NodeType` enum, `NodeBase`, `Transform3D`
- [ ] 3.2 Define component structs: `StaticMeshData`, `DirLightData`, `PointLightData`, `CameraData`
- [ ] 3.3 `Transform3D::local_matrix()` implementation (pos + quat + scale → mat4)
- [ ] 3.4 `Transform3D` serialization: Euler degrees in/out, quaternion internal storage

## 4. Scene Manager

- [ ] 4.1 Create `src/scene/scene_manager.h` — `SceneManager` class interface
- [ ] 4.2 Implement `add_node`, `remove_node`, `get`, `get_as<T>` (pool-backed)
- [ ] 4.3 Implement `walk(fn)` — depth-first traversal
- [ ] 4.4 Implement `serialize_tree()` → nlohmann JSON (for `scene.getTree` IPC)
- [ ] 4.5 Implement `set_property(id, key, val)` / `get_property(id, key)` via reflection map
- [ ] 4.6 World matrix propagation: `update_world_transforms()` called once per frame

## 5. `.uzscene` File Format

- [ ] 5.1 Create `src/scene/scene_loader.h/cpp` — parse `.uzscene` INI-like format
- [ ] 5.2 Parser handles: `[uzscene]` header, `[node]` sections, typed property values
- [ ] 5.3 Supported value types: `float`, `int`, `bool`, `string`, `float3`, `float3 rotation (euler)`
- [ ] 5.4 `res://` path prefix resolved relative to project root
- [ ] 5.5 Create `src/scene/scene_saver.h/cpp` — serialize `SceneManager` back to `.uzscene`
- [ ] 5.6 Round-trip test: load → save → load → same tree
- [ ] 5.7 Create `assets/scenes/default.uzscene` — sample scene (ground plane, directional light, camera)

## 6. Renderer Integration

- [ ] 6.1 Replace hard-coded pyramid draw calls with scene-driven render pass
- [ ] 6.2 `render_scene(SceneManager&)` iterates `StaticMeshData` nodes and dispatches draw calls
- [ ] 6.3 `DirectionalLight` nodes populate per-frame light CBV
- [ ] 6.4 `Camera` node marked `is_main=true` provides view + projection matrices
- [ ] 6.5 Fallback: if no Camera node, use previous hard-coded perspective camera

## 7. IPC Handlers (scene.*)

- [ ] 7.1 Register `scene.load` — load `.uzscene` from path, hot-reload scene in editor mode
- [ ] 7.2 Register `scene.save` — serialize current scene to file
- [ ] 7.3 Register `scene.getTree` — return full node tree as JSON
- [ ] 7.4 Register `scene.addNode` — add node by type, return new NodeHandle as JSON
- [ ] 7.5 Register `scene.removeNode` — remove node and all children
- [ ] 7.6 Register `scene.setProperty` — modify node property, trigger re-render
- [ ] 7.7 Register `scene.getProperty` — return property value
- [ ] 7.8 Register `scene.selectNode` — notify extension of selection change (for Inspector sync)
- [ ] 7.9 All handlers validated and tested via MCP tool calls

## 8. Project Config (`project.uzproj`)

- [ ] 8.1 Create `src/common/project_config.h/cpp` — parse TOML `project.uzproj`
- [ ] 8.2 Add `toml++` to vcpkg.json dependencies
- [ ] 8.3 Config fields: `name`, `version`, `main_scene`, `renderer.*`
- [ ] 8.4 Engine loads `project.uzproj` on startup from CWD or `--project` CLI arg
- [ ] 8.5 Create `project.uzproj` template at project root

## 9. Documentation

- [ ] 9.1 Update `docs/architecture.md` to reflect scene system
- [ ] 9.2 Confirm `docs/specs/scene-system.md` matches implementation
- [ ] 9.3 Add scene system section to `docs/ipc-mcp.md`
