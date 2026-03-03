## 1. Memory System — Arena Allocator

- [x] 1.1 Create `src/engine/core/arena.h` — `Arena` struct with `alloc`, `alloc_one<T>`, `alloc_array<T>`, `reset`, `child`
- [x] 1.2 Create `src/engine/core/arena.cpp` — `make_arena(capacity)` via mmap/VirtualAlloc, `free_arena`
- [x] 1.3 Global arena accessors: `permanent_arena()`, `level_arena()`, `frame_arena()`, `scratch_arena()` (thread-local)
- [x] 1.4 Arena tests: alloc, reset, overflow guard, alignment — `tests/core/test_arena.cpp`

## 2. Handle System

- [x] 2.1 Create `src/engine/core/handle.h` — `Handle<Tag>` with 24-bit index + 8-bit generation, `valid()`
- [x] 2.2 `HandlePool<T,N>` in `handle.h` — `alloc()`, `remove()`, `get()`, generation bump on remove
- [x] 2.3 Handle unit tests: alloc, free, stale handle returns null — `tests/core/test_handle.cpp`

## 3. Node Types & Data Model

- [x] 3.1 Create `src/engine/scene/scene_node.h` — `NodeId`, `NodeType` enum, `SceneNode` with `Transform3D`, `parent_id`, `children[]`
- [x] 3.2 Component data in `SceneNode`: `StaticMeshData`, `DirLightData`, `PointLightData`, `CameraData`, `AudioSourceData`
- [x] 3.3 `Transform3D::local_matrix()` — position + quaternion + scale → mat4
- [x] 3.4 `Transform3D` serialization: Euler degrees in/out, quaternion internal storage

## 4. Scene Manager

- [x] 4.1 Create `src/engine/scene/scene.h` — flat-array scene graph (max 65536 nodes), fixed capacity
- [x] 4.2 Implement `add_node()`, `remove_node()`, `get_node()` — O(1) slot-based access
- [x] 4.3 Implement `walk(fn)` — depth-first traversal from root
- [x] 4.4 Implement `serialize_tree()` → nlohmann JSON (for `scene.getTree` IPC)
- [x] 4.5 Implement `set_property(id, key, val)` / `get_property(id, key)` via string dispatch
- [x] 4.6 World matrix propagation: `scene_update_world_transforms()` called once per frame

## 5. `.uzscene` File Format

- [x] 5.1 Create `src/engine/scene/scene_loader.h/cpp` — parse `.uzscene` INI-like format
- [x] 5.2 Parser handles: `[uzscene]` header, `[node]` sections, typed property values
- [x] 5.3 Supported value types: `float`, `int`, `bool`, `string`, `float3`, Euler rotation
- [x] 5.4 `res://` path prefix resolved relative to project root
- [x] 5.5 Create `src/engine/scene/scene_saver.h/cpp` — serialize `Scene` back to `.uzscene`
- [x] 5.6 Round-trip test: load → save → load → same tree — in `tests/core/test_scene_io.cpp`
- [x] 5.7 Create `assets/scenes/default.uzscene` — sample scene (ground plane, directional light, camera)

## 6. Renderer Integration

- [x] 6.1 Scene-driven traversal: `SceneNode` iteration replaces hard-coded draw calls
- [x] 6.2 `Camera` node with `is_main=true` provides view + projection matrices
- [x] 6.3 `DirectionalLight` nodes populate per-frame light data
- [x] 6.4 Fallback to default camera when no Camera node present

## 7. IPC Handlers (`src/engine/scene/scene_handlers.h/.cpp`)

- [x] 7.1 `scene.load` — load `.uzscene` from path
- [x] 7.2 `scene.save` — serialize current scene to file
- [x] 7.3 `scene.getTree` — return full node tree as JSON
- [x] 7.4 `scene.addNode` — add node by type, return new NodeId
- [x] 7.5 `scene.removeNode` — remove node and all children
- [x] 7.6 `scene.setProperty` — modify node property
- [x] 7.7 `scene.getProperty` — return property value
- [x] 7.8 `scene.selectNode` — notify extension of selection change

## 8. Project Config

- [ ] 8.1 `project_config.h/cpp` — deferred (no toml++ dependency yet)
- [ ] 8.2 `project.uzproj` template — deferred with project config

## 9. Tests

- [x] 9.1 `tests/core/test_arena.cpp` — arena allocator tests
- [x] 9.2 `tests/core/test_handle.cpp` — handle/pool tests
- [x] 9.3 `tests/core/test_scene.cpp` — scene manager, node ops, traversal, world transforms
- [x] 9.4 `tests/core/test_scene_handlers.cpp` — IPC handler tests
- [x] 9.5 `tests/core/test_scene_io.cpp` — loader, saver, round-trip tests
- [x] 9.6 All scene tests integrated into `UzEngineTests` binary via CMakeLists.txt
