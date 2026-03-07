## Prerequisites

- [x] P1. RHI abstraction layer exists (`rhi.h`, `rhi_dx12.c`)
- [x] P2. ShaderLibrary with PSO caching (`shader_library.h/cpp`)
- [x] P3. Material/MaterialInstance system (`material.h`, `material_instance.h`)
- [x] P4. WebSocket IPC protocol (`ws_handlers.cpp`, `ipc-client.ts`)
- [x] P5. Viewport streaming infrastructure (`viewport-panel.ts`)
- [x] P6. Deferred shading pipeline with G-Buffer MRT (`renderer.cpp`)

## Phase 1 — Architecture Design

- [x] 1.1 Review and finalize design.md — validated all data structures, IPC protocol, file format against existing codebase <!-- validated: ShaderLibrary, Material/MaterialInstance, mesh_primitives, ViewportRegistry, IblBaker, camera_update_orbit, StaticMeshRenderer, rhi.h handles, nlohmann/json, G-Buffer 4-MRT layout — all match design -->
- [x] 1.2 Create `docs/specs/editor/shader-editor.md` — full specification extracted from design.md, target audience: contributors <!-- created: 12 sections, G-Buffer mapping, module boundary rules, IPC handlers, file format -->
- [x] 1.3 Update `docs/architecture/module-boundaries.md` — added MOD-9 rule, dependency graph entry, CI enforcement grep <!-- complete -->

## Phase 2 — Shader Graph Core

- [x] 2.1 Create `src/engine/renderer/shader_graph/shader_graph_types.h` — ValueType bitmask enum, helpers <!-- complete -->
- [x] 2.2 Create `src/engine/renderer/shader_graph/shader_node.h` — PinDesc, NodeInput, ShaderNode base + ShaderNodeFixed<N> template <!-- complete -->
- [x] 2.3 Create `src/engine/renderer/shader_graph/shader_graph.h/.cpp` — Connection, ShaderGraph with full API <!-- complete -->
- [x] 2.4 Implement DAG cycle detection — DFS 3-state coloring <!-- complete -->
- [x] 2.5 Implement type validation — types_compatible() check on all connections <!-- complete -->
- [x] 2.6 Implement serialize/deserialize — JSON v1 format with snapshot/restore <!-- complete -->
- [x] 2.7 Create `tests/renderer/test_shader_graph_types.cpp` — 23 tests: compatibility, promotion, component_count, hlsl_type_name, operators <!-- complete -->
- [x] 2.8 Create `tests/renderer/test_shader_graph.cpp` — 23 tests: nodes, connections, DAG validation, type validation, serialization, snapshot/restore <!-- complete -->
- [x] 2.9 Update CMakeLists.txt — auto-discovered via GLOB_RECURSE (no changes needed) <!-- complete -->
- [x] 2.10 All 1585 tests pass, 46 new shader graph tests <!-- complete -->

## Phase 3 — Node System

- [x] 3.1 Create `node_registry.h/.cpp` — singleton + UZ_REGISTER_SHADER_NODE macro <!-- complete -->
- [x] 3.2 Create `nodes/output_node.h/.cpp` — MaterialOutputNode (8 PBR inputs) <!-- complete -->
- [x] 3.3 Create `nodes/constant_nodes.h/.cpp` — Constant1/3/4 <!-- complete -->
- [x] 3.4 Create `nodes/param_nodes.h/.cpp` — ScalarParam, VectorParam, TextureParam <!-- complete -->
- [x] 3.5 Create `nodes/math_nodes.h/.cpp` — 8 math nodes + BinaryMathNode base <!-- complete -->
- [x] 3.6 Create `nodes/texture_nodes.h/.cpp` — TextureSampleNode (5 outputs) <!-- complete -->
- [x] 3.7 Create `nodes/utility_nodes.h/.cpp` — Append, ComponentMask, Time, UV <!-- complete -->
- [x] 3.8 Create `tests/renderer/test_node_registry.cpp` — 16 tests <!-- complete -->
- [x] 3.9 Create `tests/renderer/test_shader_nodes.cpp` — 46 tests: pins, compile, serialize, clone <!-- complete -->
- [x] 3.10 All 1647 tests pass (62 new Phase 3 tests) <!-- complete -->

## Phase 4 — Graph Control Layer

- [x] 4.1 Create `graph_ops.h/.cpp` — OpResult, GraphOps, atomic operations <!-- complete -->
- [x] 4.2 Implement compound operations — insert_between, replace_node <!-- complete -->
- [x] 4.3 Implement expose_parameter — ScalarParam for Scalar pins, TextureParam for texture pins <!-- complete -->
- [x] 4.4 Implement query operations — query_graph, query_node, find_nodes, list_node_types, query_connection <!-- complete -->
- [x] 4.5 Implement GraphEvent system — 5 event types, set_event_listener callback <!-- complete -->
- [x] 4.6 Implement validation in mutations — type checking, auto-disconnect, descriptive errors <!-- complete -->
- [x] 4.7 Create `tests/renderer/test_graph_ops.cpp` — 15 tests: atomic ops + events <!-- complete -->
- [x] 4.8 Create `tests/renderer/test_graph_ops_compound.cpp` — 12 tests: insert_between, replace_node, expose_parameter <!-- complete -->
- [x] 4.9 Create `tests/renderer/test_graph_ops_query.cpp` — 9 tests: all query operations <!-- complete -->
- [x] 4.10 Event tests integrated into test_graph_ops.cpp (every mutation asserts events) <!-- complete -->
- [x] 4.11 All 1687 tests pass, 165 shader graph tests <!-- complete -->

## Phase 5 — Prompt Command Interface

- [ ] 5.1 Create `src/engine/renderer/shader_graph/commands/graph_command.h` — `CommandType` enum (ADD_NODE, DELETE_NODE, CONNECT, DISCONNECT, SET_PARAM, INSERT_BETWEEN, REPLACE_NODE, EXPOSE_PARAM, COMPILE, QUERY, BATCH), `GraphCommand` struct, `CommandResult` struct
- [ ] 5.2 Create `src/engine/renderer/shader_graph/commands/command_executor.h/.cpp` — `CommandExecutor` class with `execute()`, `execute_batch()`, `parse()` methods
- [ ] 5.3 Implement single command execution — `execute()` dispatches `CommandType` to corresponding `GraphOps` method, returns `CommandResult` with events
- [ ] 5.4 Implement batch execution with rollback — `execute_batch()` executes commands sequentially, on failure rolls back all previous commands in the batch (snapshot + restore graph state)
- [ ] 5.5 Implement batch variable references — `"$0"`, `"$1"` in params resolve to `nodeId` from previous batch command results; `resolve_refs()` method
- [ ] 5.6 Implement COMPILE command — triggers `HlslTranslator::compile()`, returns generated HLSL code + errors in `CommandResult`
- [ ] 5.7 Implement QUERY command — delegates to `GraphOps::query_*()` methods based on `params.query` field (graph, node, nodeTypes, findNodes, connection)
- [ ] 5.8 Create IPC handlers — `shader_cmd.execute` (single command), `shader_cmd.batch` (batch), `shader_cmd.query` (convenience query wrapper) in `src/engine/editor/ipc_shader_cmd_handlers.h/.cpp`
- [ ] 5.9 Wire IPC handlers in `src/main.cpp` — register `shader_cmd.*` handlers in `ipcRegistry`
- [ ] 5.10 Create `tests/renderer/test_command_executor.cpp` — single command tests: each CommandType dispatches correctly, invalid command type returns error, parse from JSON
- [ ] 5.11 Create `tests/renderer/test_command_batch.cpp` — batch tests: all-succeed batch, fail-and-rollback batch, variable reference resolution ($0 → nodeId), nested batch error
- [ ] 5.12 Create `tests/editor/test_ipc_shader_cmd_handlers.cpp` — IPC handler tests: execute, batch, query; error cases; JSON serialization round-trip
- [ ] 5.13 All tests pass, ≥95% coverage on command_executor, ipc_shader_cmd_handlers

## Phase 6 — Shader Compilation Pipeline

- [ ] 6.1 Create `src/engine/renderer/shader_graph/compiler/shader_compiler.h` — `IShaderCompiler` interface with all virtual methods per design.md §3.1
- [ ] 6.2 Create `src/engine/renderer/shader_graph/compiler/hlsl_translator.h/.cpp` — `CodeChunk` struct, `ExpressionKey`, `HlslTranslator` class with `compile(ShaderGraph&) → CompileResult`
- [ ] 6.3 Implement code chunk system — `add_chunk()`, `create_symbol()`, `get_code()`, `get_type()`, inline vs. named variable decisions (constants and simple swizzles inline, complex expressions get local variables)
- [ ] 6.4 Implement expression caching — `expr_cache_` map, `call_expression()` with cache lookup + cycle detection via `compilation_stack_`
- [ ] 6.5 Implement type promotion — `promote()` method: Float1→Float3 via `float3(x,x,x)`, compatible types pass through, incompatible → error with node context
- [ ] 6.6 Implement all `IShaderCompiler` methods in `HlslTranslator` — constants, arithmetic, math functions, texture sampling, built-in inputs (UV, time, world position, world normal, camera position), component mask, append
- [ ] 6.7 Implement `assemble_pixel_shader()` — emit includes, cbuffer MaterialParams from collected params, Texture2D declarations from collected textures, local variable definitions (non-inline chunks in dependency order), PS output struct assignment from MaterialOutput pins mapping to G-Buffer layout
- [ ] 6.8 Implement `assemble_vertex_shader()` — standard PBR vertex shader with MVP transform, tangent frame output, UV passthrough, optional vertex displacement from Displacement pin
- [ ] 6.9 Create `tests/renderer/test_hlsl_translator.cpp` — compilation tests: single constant → output, Add(const, const), Multiply chain, TextureSample with UV, type promotion (Float1+Float3), expression caching (diamond graph compiled once → single local var), cycle detection error, empty graph
- [ ] 6.10 Create `tests/renderer/test_shader_compilation_e2e.cpp` — end-to-end: build full PBR graph programmatically (TextureSample → Multiply → MaterialOutput with Normal, Roughness params) → compile → verify HLSL contains expected cbuffer fields, texture bindings, local variables, output assignments
- [ ] 6.11 All tests pass, ≥95% coverage on shader_compiler, hlsl_translator

## Phase 7 — UzScene Preview Environment

- [ ] 7.1 Create `src/engine/renderer/preview/uz_scene_preview.h/.cpp` — `UzScenePreview` class with `PreviewMesh` enum (Sphere/Cube/Plane), `initialize()`, `shutdown()`, `is_active()`
- [ ] 7.2 Implement `create_preview_scene()` — programmatically build in-memory Scene: PreviewMesh (StaticMesh at origin) + PreviewCamera (orbit, 60° FOV, 2.5m distance) + Sun (DirectionalLight, warm white, 2048 shadow) + Sky (SkyAtmosphere, neutral studio colors)
- [ ] 7.3 Implement procedural mesh generation for preview — call existing `generate_sphere()`, `generate_cube()`, `generate_plane()` from `mesh_primitives.h`, upload via `create_gpu_mesh()`, cache all three at init
- [ ] 7.4 Implement `set_preview_mesh(PreviewMesh)` — swap active mesh geometry on the preview node without recreating the scene; update vertex/index buffers on the StaticMesh component
- [ ] 7.5 Implement preview camera — `set_camera_orbit(yaw, pitch, distance)` for manual positioning, `set_camera_auto_rotate(bool)` for turntable mode using existing `camera_update_orbit()`
- [ ] 7.6 Implement IBL baking for preview — use existing `IblBaker::bake()` to generate irradiance + prefiltered cubemaps from the sky atmosphere data; bake once at scene creation
- [ ] 7.7 Implement viewport allocation — use existing `ViewportRegistry` with `ViewportType::Material`, allocate at `initialize()`, release at `shutdown()`; default 720p @ 60fps, JPEG quality 85
- [ ] 7.8 Implement `apply_compiled_shader()` — receive VS/PS HLSL strings, compile via `ShaderLibrary`, create PSO, assign to preview `MaterialInstance`; store fallback PSO for error recovery
- [ ] 7.9 Implement material parameter tweaking — `set_scalar_param()`, `set_vector_param()`, `set_texture_param()` update the preview `MaterialInstance` overrides without recompilation
- [ ] 7.10 Create IPC handlers — `shader_graph.preview` (start), `shader_graph.stopPreview` (stop), `shader_graph.setPreviewMesh`, `shader_graph.setPreviewCamera`, `shader_graph.setPreviewParam` in `src/engine/editor/ipc_preview_handlers.h/.cpp`
- [ ] 7.11 Wire IPC handlers in `src/main.cpp` — register all `shader_graph.preview*` handlers in `ipcRegistry`
- [ ] 7.12 Create `tests/renderer/test_uz_scene_preview.cpp` — preview tests: initialize/shutdown lifecycle, create_preview_scene (verify 4 nodes created), set_preview_mesh (sphere→cube→plane), apply_compiled_shader (valid shader, error fallback), parameter tweaking, camera orbit
- [ ] 7.13 Create `tests/editor/test_ipc_preview_handlers.cpp` — IPC handler tests: preview start/stop, setPreviewMesh, setPreviewCamera, setPreviewParam, error cases (invalid viewportId, missing path)
- [ ] 7.14 All tests pass, ≥95% coverage on uz_scene_preview, ipc_preview_handlers

## Phase 8 — Live Shader Update System

- [ ] 8.1 Create `src/engine/renderer/preview/shader_update_controller.h/.cpp` — `ShaderUpdateController` class with constructor taking `GraphOps&`, `HlslTranslator&`, `UzScenePreview&`; `start()`, `stop()`, `compile_and_apply()` methods
- [ ] 8.2 Implement event-driven compilation — `on_graph_event()` listener registered on `GraphOps`; triggers compilation on NodeAdded, NodeRemoved, ConnectionAdded, ConnectionRemoved, ParamChanged events
- [ ] 8.3 Implement debounce mechanism — coalesce rapid mutations within 100ms window; set `compile_pending_` flag on event, execute compile cycle after debounce timeout
- [ ] 8.4 Implement compile cycle — `do_compile_cycle()`: validate graph → compile HLSL → build PSO → apply to preview; on any stage failure, push diagnostics and keep fallback shader/PSO
- [ ] 8.5 Implement fallback shader retention — store `last_good_vs_`/`last_good_ps_` after every successful compilation; on failure, preview continues rendering with last working shader (never blank frame)
- [ ] 8.6 Implement diagnostics push — `set_diagnostics_callback()` for IPC integration; push `shader_graph.diagnostics` event with errors/warnings including node IDs; push `shader_graph.compiled` event on success with timing + param manifest
- [ ] 8.7 Create `src/engine/renderer/shader_graph/shader_graph_manager.h/.cpp` — `ShaderGraphManager` class: load `.uzshader` from disk, cache compiled graphs, save to disk, hot-reload on file change
- [ ] 8.8 Integrate with MaterialManager — `Material.shader_name = "graph:path"` triggers graph loading + compilation via ShaderGraphManager instead of built-in shader lookup
- [ ] 8.9 Wire ShaderUpdateController into engine — in `src/main.cpp`: create controller when preview starts, connect to GraphOps events, connect diagnostics callback to IPC push
- [ ] 8.10 Create `tests/renderer/test_shader_update_controller.cpp` — update controller tests: event triggers compilation, debounce coalesces rapid events, fallback shader retained on error, diagnostics callback fires with correct data, compile_and_apply manual trigger
- [ ] 8.11 Create `tests/renderer/test_shader_graph_manager.cpp` — manager tests: load/save/cache, hot-reload, graph:path material integration, invalid file handling
- [ ] 8.12 Update `docs/API.md` — document all `shader_graph.*` and `shader_cmd.*` IPC handlers including preview and diagnostics events
- [ ] 8.13 All tests pass, ≥95% coverage on shader_update_controller, shader_graph_manager

## Documentation

- [ ] D1. Update `docs/specs/editor/shader-editor.md` — reflect final implemented architecture (Graph Control Layer + Prompt Command Interface, no visual UI)
- [ ] D2. Create `docs/guides/shader-editor-usage.md` — usage guide: creating shader graphs via commands, available node types, compilation, preview
- [ ] D3. Update `docs/guides/vscode-extension.md` — add shader graph command interface section
- [ ] D4. Update `docs/architecture/module-boundaries.md` — add shader_graph module rules (if not done in Phase 1)

## Deferred (Architecture Supports, Not Implemented)

- [ ] X1. Visual node editor UI (Canvas/WebGL in VSCode) — task: `phase3-shader-visual-editor`
- [ ] X2. LLM-powered natural language → command translation — task: `phase3-shader-llm-parser`
- [ ] X3. Material instances from shader graphs — task: `phase3-shader-graph-instances`
- [ ] X4. Subgraphs / shader functions — task: `phase3-shader-subgraphs`
- [ ] X5. Custom HLSL code nodes — task: `phase3-custom-hlsl-nodes`
- [ ] X6. Undo/redo system for graph operations — task: `phase3-shader-editor-undo-redo`
- [ ] X7. Shader permutation system (static switches) — task: `phase4-shader-permutations`
- [ ] X8. Preshader / uniform expression folding — task: `phase4-shader-preshader`
- [ ] X9. Optimization passes (dead code elimination, constant folding) — task: `phase3-shader-optimization`
