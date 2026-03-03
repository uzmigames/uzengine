## 1. Prerequisites

- [ ] 1.1 Confirm `phase0-async-streaming-design` spec is complete — read `docs/specs/async-streaming.md`
- [ ] 1.2 Confirm `phase1-scene-system` is complete — scene parse, entity spawn, scene graph APIs exist
- [ ] 1.3 Confirm `phase2-source-asset-db` is complete — cooked scene paths available via `AssetCooker::get_cooked_path`
- [ ] 1.4 Confirm `phase1-ecs-system-scheduler` is complete — `WorldStreamer` can register as ECS system
- [ ] 1.5 Read `docs/specs/async-streaming.md` — internalize `IoThreadPool`, `AsyncHandle`, `StreamingChunk` state machine

## 2. CStreamingVolume Component

- [ ] 2.1 Add `CStreamingVolume` to `ecs/components.h`
- [ ] 2.2 Fields: `chunk_paths[]: string[]` (res:// paths to `.uzscene` chunks), `load_radius: float`, `unload_radius: float`, `priority: int` (0=low, 1=normal, 2=high), `placeholder_mesh_path: string`
- [ ] 2.3 Assert: `unload_radius > load_radius` (hysteresis required); log error and clamp if violated
- [ ] 2.4 Update `.uzscene` parser — serialize/deserialize `CStreamingVolume`

## 3. StreamingChunk State Machine

- [ ] 3.1 Create `src/engine/streaming/streaming_chunk.h/cpp`
- [ ] 3.2 Define `LoadState` enum: `Unloaded`, `Queued`, `Loading`, `Loaded`, `UnloadQueued`, `Unloading`
- [ ] 3.3 Define `StreamingChunk` struct: `chunk_path`, `load_state`, `async_handle: AsyncHandle<ParsedScene>`, `spawned_entities: vector<entt::entity>`, `placeholder_entity: entt::entity`
- [ ] 3.4 Implement `StreamingChunk::begin_load(IoThreadPool&, priority)`:
  - State: Unloaded → Queued
  - Submit `IoThreadPool::submit({priority, fn: parse_scene_file(chunk_path)})` → store `AsyncHandle`
  - Spawn placeholder entity with `placeholder_mesh_path`
- [ ] 3.5 Implement `StreamingChunk::try_complete_load(registry)`:
  - If `async_handle.is_ready()`: State: Loading → Loaded
  - Spawn all entities from `ParsedScene` into registry
  - Destroy placeholder entity
- [ ] 3.6 Implement `StreamingChunk::begin_unload()`: State: Loaded → UnloadQueued
- [ ] 3.7 Implement `StreamingChunk::complete_unload(registry)`:
  - State: UnloadQueued → Unloading → Unloaded
  - Destroy all `spawned_entities` from registry
  - Release memory: return streaming arena pages

## 4. WorldStreamer ECS System

- [ ] 4.1 Create `src/engine/streaming/world_streamer.h/cpp` — `WorldStreamer : IEcsSystem`
- [ ] 4.2 Register with `SystemRegistry` in `PreUpdate` group
- [ ] 4.3 Implement `WorldStreamer::tick(registry, dt)`:
  - Find camera/player position (entity with `CCameraTarget` or player tag)
  - For each entity with `CStreamingVolume`:
    - Compute distance from player to volume center
    - If distance < `load_radius` and chunk `Unloaded`: call `chunk.begin_load()`
    - If distance > `unload_radius` and chunk `Loaded`: call `chunk.begin_unload()`
  - For each `Queued` chunk where `async_handle.is_ready()`: call `chunk.try_complete_load()`
  - For each `UnloadQueued` chunk: call `chunk.complete_unload()`
  - Enforce memory budget: if streaming arena > high watermark, evict lowest-priority Loaded chunk

## 5. Memory Budget

- [ ] 5.1 Create `StreamingMemoryBudget` struct: `limit_bytes: size_t` (from `.uzproj`), `current_bytes: atomic<size_t>`
- [ ] 5.2 Implement `StreamingMemoryBudget::can_load(estimated_bytes) -> bool`
- [ ] 5.3 Implement LRU eviction: track `last_player_distance_on_load` per chunk; evict farthest first
- [ ] 5.4 Push `streaming.budget_exceeded` IPC event when eviction is triggered

## 6. Async IO Integration

- [ ] 6.1 Use `IoThreadPool` from `async-streaming.md` spec — do not create a new thread pool
- [ ] 6.2 Chunk parse function runs on IO thread: open file, parse `.uzscene` text, build `ParsedScene` (entity/component list, no registry access)
- [ ] 6.3 Entity spawn runs on main thread: `try_complete_load` only called from `WorldStreamer::tick`
- [ ] 6.4 Ensure scene parsing is re-entrant: no global mutable state in parser

## 7. Placeholder Mesh

- [ ] 7.1 Load `placeholder_mesh_path` on first load of chunk; reuse same mesh entity across Loaded/Unloaded cycles
- [ ] 7.2 Placeholder visibility: visible while `state == Queued || state == Loading`; hidden when `Loaded`
- [ ] 7.3 Default placeholder: low-poly bounding box mesh at chunk AABB; `CMeshRenderer.mesh = "res://engine/meshes/streaming_placeholder.glb"`

## 8. IPC Integration

- [ ] 8.1 Push `streaming.chunk_loaded` IPC event: `{ chunk_path, entities_spawned: int, load_time_ms: float }`
- [ ] 8.2 Push `streaming.chunk_unloaded` IPC event: `{ chunk_path }`
- [ ] 8.3 Implement `scene.get_streaming_state` IPC handler — returns JSON map of all chunks and their `LoadState`

## 9. Testing

- [ ] 9.1 Unit test: `CStreamingVolume` with `unload_radius < load_radius` → error logged, radii clamped
- [ ] 9.2 Unit test: `begin_load` transitions state to `Queued`, spawns placeholder entity
- [ ] 9.3 Unit test: `try_complete_load` when `AsyncHandle` ready → state `Loaded`, entities spawned, placeholder destroyed
- [ ] 9.4 Unit test: `begin_unload` transitions `Loaded → UnloadQueued`; `complete_unload` despawns all entities
- [ ] 9.5 Unit test: LRU eviction — 3 loaded chunks, budget exceeded → farthest chunk evicted first
- [ ] 9.6 Integration test: move player within `load_radius` → chunk loads within 500ms
- [ ] 9.7 Integration test: move player outside `unload_radius` → chunk unloaded within 2 frames
- [ ] 9.8 Integration test: pause game mid-load → load completes normally after unpause (no deadlock)

## 10. Documentation

- [ ] 10.1 Update `docs/specs/scene-system.md` — "World Streaming" section with `CStreamingVolume` fields
- [ ] 10.2 Update `docs/specs/async-streaming.md` — mark `StreamingChunk` section as implemented
- [ ] 10.3 Update `docs/guides/scene-authoring.md` — "Setting Up World Streaming" section with chunk guidelines
