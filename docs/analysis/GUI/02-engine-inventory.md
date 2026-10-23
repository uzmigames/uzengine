# 02. Engine inventory and editor readiness

Audit date: 2026-10-22. Scope: current native runtime, engine services, native editor primitives, UI and validation evidence. This chapter is a source inspection, not a new build, benchmark or exhaustive line-by-line correctness review. Code was read through the native terminal. Visual revision 5 (2026-10-23) publishes eight frames from seven sample runtime runs using a pre-existing binary. Character capture produced images but exited 1 on a shutdown allocation leak; these runs do not certify this working tree. See [capture provenance and warnings](images/README.md). Historical acceptance is labelled separately from current source presence.

## Reading the maturity labels

- **Implemented:** concrete source exists for the stated behavior; this does not establish production acceptance.
- **Integrated:** a caller or frame path connects that behavior to the application.
- **Partial:** useful implementation exists, but the wider capability or authoring workflow is incomplete.
- **Planned / not located:** the named capability appears in plans or interfaces, but its runtime implementation was not located in the inspected directories and searches.
- **Historical validation:** a dated report records executed checks; those checks were not repeated in this audit.

There is no meaningful single percentage of engine completion. A renderer, editor shell, asset pipeline, gameplay runtime and tool integration have different acceptance criteria. A checked task, existing header, registered CVar and live rendered feature are different kinds of evidence.

References use repository-relative paths and 1-based line numbers. Paths are plain code so the report remains portable across clones.

## Inventory size

Native file inventory counts `.h`, `.cpp` and `.mm` recursively within each `runtime/engine` module. They describe source distribution, not feature completeness or tested coverage.

| Module | Native files | Module | Native files |
|---|---:|---|---:|
| renderer | 315 | vfx | 289 |
| rhi | 90 | scripting | 65 |
| ecs | 50 | asset | 45 |
| ui | 31 | shader | 27 |
| math | 21 | app | 20 |
| material | 20 | animation | 17 |
| property | 9 | job | 8 |
| core | 8 | containers | 6 |
| ipc | 5 | config | 4 |
| profiling | 4 | logging | 3 |
| assertions | 2 | event | 2 |
| memory | 2 | paths | 2 |
| physics | 2 | random | 1 |

`runtime/editor` contains 32 files. `runtime/tests` contains 410 files, including fixtures and baselines; this is not a test-case count. The source already represents a substantial renderer and VFX authoring investment. Replacing the surrounding shell should preserve this investment.

## Capability matrix

| Area | What exists now | Maturity and evidence | What remains for the proposed editor |
|---|---|---|---|
| Application lifecycle | Application class, engine loop, time manager, module and subsystem registries | Implemented. `runtime/engine/app/application.h:97`; `engine_loop.cpp:114`, `:142`, `:171` | Editor session lifecycle, document recovery and explicit edit/play ownership on top of the runtime lifecycle |
| Core utilities | Logging/assertions, paths/configuration, math, containers, ownership helpers, events and profiling interfaces | Implemented foundation exposed by `runtime/engine/uzengine.h:21` and module inventory | Reuse conventions; do not replace core services merely to adopt a GUI framework |
| Job scheduling | Worker pool, priorities, work stealing and parallel-for API | Implemented. `runtime/engine/job/job_system.h:52`, `:112`, `:201` | Separate long external jobs from frame work; expose progress/cancellation and workload budgets |
| Native runtime API | Initialization, viewport creation, resize, frame render, entities, selection, input, logs, capture and encoding exports | Integrated C API. `runtime/engine/app/runtime.h:132`, `:164`, `:214`, `:285` | Session/context handles, multi-viewport policy, capability discovery and safe asynchronous editor commands |
| RHI | Device, command list, swapchain, fences, resources and GPU timing abstractions; DX12, DX11, Vulkan and Metal source | Implemented backends, not equal-platform acceptance. `runtime/engine/rhi/rhi_factory.cpp:39`, `:50`, `:62`, `:74` | Choose Windows reference backend first; validate every other backend separately. Capability-gate UI controls |
| Render graph | Registered passes, compilation, execution, resource state tracking and dependent UAV ordering | Integrated. `runtime/engine/renderer/scene/scene_render_frame.cpp:183`; `runtime/engine/renderer/render_graph/render_graph_builder.cpp:496`, `:537` | Visual graph inspection and per-view state ownership; preserve ordered-queue constraints |
| Render graph allocation | Persistent transient-resource pool with between-graph reuse | Implemented reuse, explicitly no within-graph aliasing or fence retirement. `runtime/engine/renderer/render_graph/render_graph_resources.h:8`, `:33` | Budget multiple viewports and histories before assuming allocations can be shared freely |
| Raster frame | Geometry, decals, lighting, translucency, particles, post-process and overlays are registered in frame order | Integrated. `runtime/engine/renderer/scene/scene_render_frame.cpp:183` | Native shell should host the existing renderer, not rebuild the rendering pipeline |
| Shadows and local lighting | Shadow renderer, cascades/cache/atlas, local-light atlas, clustered LightCull and distance-field passes | Integrated source. `runtime/engine/renderer/frame/frame_lighting_pass.cpp:112`; `frame_ao_passes.cpp:175`, `:323` | Match authoring controls to actual validated light/shadow capabilities and expose cost/debug views |
| GI, reflections and AO | GTAO, SSGI, global distance-field composition, radiance-cache tracing, SSR and RT reflection registration | Integrated conditional paths. `runtime/engine/renderer/frame/frame_ao_passes.cpp:67`; `frame_gi_passes.cpp:124`, `:306`, `:568`, `:891` | Per-feature hardware/capture acceptance, clear mutually active paths and diagnostic visibility; registration alone does not certify parity |
| Atmosphere, fog and clouds | ECS components, Lua APIs, renderer resources and frame integration files | Implemented substantial subsystem. `runtime/engine/ecs/environment/sky_atmosphere_component.h`; `height_fog_component.h`; `volumetric_clouds_component.h`; `runtime/engine/renderer/frame/frame_atmosphere_passes.cpp` | Current-source visual acceptance and parameter UX; do not interpret stale froxel task as absence of fog |
| Post-processing | Temporal producer, SSR composite/history copy, bloom, tonemap and diagnostic region readback | Integrated restricted chain. `runtime/engine/renderer/frame/frame_postprocess.cpp:277`, `:402`, `:463`, `:501`, `:512`, `:583` | Motion blur, DOF, CAS, FXAA, lens flare and LUT workflows must not appear enabled merely because configuration/shader files exist |
| Upscaling | Common temporal interface, TAA/TSR source and vendor SDK branches; unavailable-backend handling | Partial and build-dependent. `runtime/engine/renderer/scene/scene_upscaler.cpp:62`; `passes/postprocess/fsr2_upscaler.cpp:555`; `dlss_upscaler.cpp:697` | Report requested versus effective backend, SDK availability and hardware support. Validate real frame/camera inputs |
| Path tracing | Path-tracer passes, ray-tracing managers, BLAS/TLAS, shader table, light-BVH and denoiser source | Source exists; broad renderer-reference acceptance is not established. `runtime/engine/renderer/passes/pathtracing/path_tracer_pass.cpp`; `pipeline_path_tracer_pass.cpp`; closure report cited below | Present as a capability with documented limits. Do not advertise it as an independent ground-truth oracle |
| Material graph | Material graph persistence, expression graph and HLSL translation, compiler-extension hooks and material pipeline | Implemented. `runtime/engine/material/material_graph.h:116`, `:168`; `compiler/hlsl_material_translator.h:63`; `runtime/engine/renderer/material/material_pipeline.cpp` | Authoring session, compile diagnostics, transactional edits and validated per-node/per-domain contracts |
| Asset import | glTF/GLB mesh importer, FBX importer, texture/shader import, sidecars, registry and compiled formats | Implemented. `runtime/engine/asset/importers/mesh_importer.cpp:342`; `fbx_importer.cpp:1136`; `asset_registry.h:30` | Unified asset identity/dependencies, reimport UX, cancellation, source provenance and large-project indexing |
| Virtual file system | Loose files, pak storage, layered mounts and lazy pak/index behavior | Implemented thread-safe reads with restricted mount mutation. `runtime/engine/asset/vfs.h:27`, `:139`, `:222`, `:385` | Reuse paths/manifests from the application; preserve path-agnostic runtime and explicit ownership during reload |
| Background assets | CPU mesh read/parse and texture decode workers; render thread integrates GPU uploads | Integrated design. `runtime/engine/renderer/upload/async_mesh_loader.h:3`, `:12`; `async_texture_decoder.h:3`; `scene/scene_renderer.h:755` | Measured per-frame integration budgets, import-progress UI and cancellation/lifetime semantics |
| ECS | EnTT world, generational handles, component types, dirty flags and tick systems | Implemented. `runtime/engine/ecs/core/world.cpp:15`, `:43`, `:70`; `entity.h:38` | Durable editor identities, selection restoration and explicit document identity beyond ephemeral runtime IDs |
| Scene persistence | JSON scene load/save, component dispatch, prefab/children traversal and top-level validation before mutation | Integrated with tested fatal-input preservation. `runtime/engine/ecs/loaders/scene_loader.cpp:1119`, `:1442`; `runtime/tests/engine/ecs/test_scene_loader.cpp:109` | Hierarchy round-trip and richer transactional load/recovery. Current save flattens children |
| Reflection/property data | Class/property metadata, categories/flags and JSON serialization/deserialization | Implemented. `runtime/engine/property/property_info.h:38`; `property_serializer.cpp:228`, `:244` | Property adapters with validation, change notifications, dirty tracking and undo; partial deserialization is not a transaction |
| Lua gameplay scripting | Lua VM, restricted globals, broad engine APIs and per-entity start/update/stop lifecycle | Integrated. `runtime/engine/scripting/lua_vm.cpp:53`, `:139`, `:156`; `script_system.cpp:274`, `:295`, `:332` | LSP, debugger adapter, source navigation, breakpoints and reliable editor reload/diagnostic workflow; script execution is not IDE integration |
| Skeletal animation | Skeleton/clip formats, pose sampling, controller parameters/state transitions, crossfade, subsystem and GPU skinning | Implemented and integrated. `runtime/engine/animation/animation_controller.cpp:15`, `:85`; `skeletal_mesh_subsystem.cpp:337`, `:346`, `:570`, `:701` | Authoring/preview tools, round-trip verification and residual animation features. Do not restart animation from stale zero-checked task |
| Physics | Contact/fracture listener registry and VFX integration interfaces | Partial interface layer, not Jolt simulation. `runtime/engine/physics/physics_listener_registry.h:7`, `:122` explicitly identifies future PhysicsWorld | Physics world, bodies/colliders, stepping, queries, character controller and authoring validation |
| Audio | Asset-type and VFX audio-spectrum/provider concepts | Runtime audio engine not located in searched native modules. `runtime/engine/asset/asset_manifest.h:67`; `runtime/engine/vfx/modules/audio_spectrum_module.h:38` | Playback engine, voices, buses, spatialization, import and authoring controls; FFT consumer does not establish audio playback |
| Input/camera | Platform input, fly camera, editor input and Lua input/camera APIs | Implemented basic layers. `runtime/platform/windows/input.h`; `runtime/editor/input`; `runtime/engine/scripting/core/lua_camera_api.cpp` | Action mapping/rebinding, viewport/panel focus ownership and proposed gameplay camera modes remain distinct work |
| Native UI primitives | Own Slate-style widget, layout, renderer, focus, input, buttons, scrollbox, editable text, tooltip and drag/drop files | Implemented early framework, not UE Slate equivalence. `runtime/engine/ui/slate_widget.h`; `slate_renderer.h:89`; `slate_editable_text.h:3` | Text/IME/accessibility, precise clipping, window/docking integration and performance acceptance for general desktop use |
| VFX native authoring | Substantial VfxEditorOverlay, tabs/splitter, parameter/curve/tree/graph/browser widgets and concrete undo commands | Implemented authoring subsystem. `runtime/engine/vfx/editor/vfx_editor_overlay.h:67`, `:430`, `:452`; `undo/vfx_undo_stack.h:38` | Reuse mature overlay behavior; evaluate declarative composition migration separately. Do not equate prototype coverage with absence of native VFX editor |
| General editor primitives | Grid, gizmos, outlines, screenshot, stats, toolbar and single-entity selection/picking | Implemented reusable pieces. `runtime/editor/selection/selection_manager.h:1`, `:63`; `runtime/editor/uzeditor.h` | Multi-selection, general document/transaction model, hierarchy editing and full workspace shell |
| Diagnostics/profiling | CPU capture export, RHI GPU timings, pass dump, pixel debug and diagnostic command options | Implemented tools. `runtime/engine/profiling/profile_capture.h:5`, `:95`; `runtime/engine/app/application.h:252`, `:256`, `:260`; `rhi/rhi_device.h:493` | Unified profiler/pass inspector and timestamped baseline comparisons; use existing diagnostics before rearchitecting for speed |
| Platform/build/test | Windows/macOS/Linux platform files, Zig C++23 build and broad unit fixtures | Source and historical Windows validation. `runtime/build.zig:53`, `:58`; `runtime/platform/windows/platform_window.h:118` | Reproducible chosen-platform build and fresh acceptance. Cross-platform files do not imply equivalent editor support |

## Findings relevant to the GUI decision

### F-002. The current embedded API is one viewport with strict ownership

`runtime/engine/app/runtime.cpp:308` rejects viewport creation outside Windows. At `:312`, a second viewport is rejected until the existing viewport is destroyed. `runtime/engine/app/runtime.h:6` documents initialization/destruction restrictions and requires frame rendering on the thread that created the viewport. Returned strings have temporary internal-buffer lifetime (`runtime.h:14`). These are useful current embedding contracts, not a multi-document/multi-window editor API.

The existing `OwnerCommandQueue` is a **single-slot synchronous rendezvous**, not a general asynchronous work queue (`runtime/engine/app/owner_command_queue.h:18`). `Submit` returns Busy when occupied (`:35`) and waits for pickup/completion (`:44`, `:57`). Completion timeout does not undo a command already executing. Commands must not throw or recursively submit. A host UI must not call it synchronously for every property-drag event and then assume native UI automatically stays responsive.

Required next contract: explicit EditorSession/Viewport identifiers, immutable UI snapshots, bounded mutation requests with coalescing, completion events and documented world/render/GPU ownership. Begin with one main native viewport; add preview windows only after independent history/resources and lifetimes are proven. This is a recommendation, not implemented functionality.

### F-003. Scene I/O is real, but scene hierarchy does not round-trip

Fatal top-level input is checked before live world clearing: malformed JSON, non-object roots and missing/non-array entities return first (`runtime/engine/ecs/loaders/scene_loader.cpp:1131`, `:1148`). Regression tests explicitly preserve world/CVars/materials on malformed input (`runtime/tests/engine/ecs/test_scene_loader.cpp:109`). The earlier blanket claim that all load failures destroy the scene is stale.

However, child recursion calls `ProcessEntityJson(world, child, sourceName)` without a parent argument (`scene_loader.cpp:1106`), serialization writes `children = []` (`:1435`), and SaveScene emits every valid entity at root level (`:1452`). Existing parsing of nested JSON is not persistent parent/child editor behavior. Top-level validation also does not imply rollback of every later per-component warning or mutation.

A human-facing outliner must reuse existing runtime parent relationships and cycle/self-parent rejection, then preserve those relationships through scene loading and saving. The shared hierarchy boundary already rejects cycles (`runtime/engine/ecs/scene/scene_component.cpp:98`; stabilization checklist item 2.4). Remaining work includes hierarchy-preserving serialization, durable prefab/reference identity and end-to-end reparent/save/reopen tests. It should not present a tree that is discarded when saving.

### F-004. Existing Slate-style UI is useful, but not a finished desktop toolkit

The implementation is substantial enough to evaluate and reuse, particularly for VFX tools. Nevertheless, `runtime/engine/ui/slate_editable_text.h:3` explicitly describes a single-line control and excludes selection, clipboard, IME and multiline behavior. This is unsuitable as the Python/C++/Lua code editor. Monaco or another proven editor should own that responsibility.

`runtime/engine/ui/slate_renderer.cpp:83` rejects elements wholly outside a scissor rectangle but permits partial overlaps; the header describes precise GPU scissoring as later work (`slate_renderer.h:18`). `SlateRenderer` and TextRenderer batches are single-threaded (`slate_renderer.h:21`). These limitations matter for nested panels, scrolling and browser/native interoperation. They are not proof of an observed clipping defect: the subsequent screenshot runs did not exercise a targeted nested-widget clipping acceptance case.

Do not confuse three surfaces: **engine-native UI primitives**, the **substantial native VFX overlay**, and the **proposed full editor shell/game UI stack**. Completing one does not automatically complete the others. A custom shell must fund keyboard/IME/accessibility, docking, multi-monitor/DPI, clipboard, drag/drop, modal/focus rules and automation, not only draw attractive widgets.

### F-005. Reusable editing mechanisms exist, but universal transactions are not established

Reflection metadata provides field names, types, descriptions, categories and flags (`runtime/engine/property/property_info.h:38`). JSON deserialization reports partial success and retains successful field writes; the contract explicitly allows partial results (`runtime/engine/property/property_serializer.h:68`). This cannot by itself provide atomic undoable property changes.

VFX has real command objects and undo/redo history (`runtime/engine/vfx/editor/undo/vfx_undo_stack.h:38`, `vfx_undo_commands.h:58`). General scene selection currently holds one entity (`runtime/editor/selection/selection_manager.h:1`). Native searches for transactions/undo found VFX-specific implementations, not a universal scene/material/asset document transaction service in the inspected runtime/editor scope.

Build the shared edit contract around before/after state, validation, command grouping, undo/redo, dirty/save points, version conflicts and target identity. Human gestures, Lua tools, Claude Code/Codex operations and asset import acceptance should use that same contract. A VFX-specific stack can inform this work without pretending all editors already share it.

### F-006. Renderer options must distinguish configuration from live capability

Current source labels MotionBlur, DOF, CAS and FXAA settings inert (`runtime/engine/renderer/frame/frame_postprocess.cpp:32`, `:48`). The registered chain at `:277`, `:402`, `:463`, `:501`, `:512` matches temporal processing, SSR history/composite, Bloom and Tonemap. `docs/analysis/post-process/postprocess-chain-status.md:5` documents the additional inactive effects and corrects earlier misleading pass-count claims.

SDK-less FSR2 and DLSS explicitly return unavailable (`runtime/engine/renderer/passes/postprocess/fsr2_upscaler.cpp:568`; `dlss_upscaler.cpp:700`). Availability fallback exists in `runtime/engine/renderer/scene/scene_upscaler.cpp:62`; it must not be presented as the requested vendor algorithm running. The FSR2 SDK branch still sets fixed frame time/near-plane/FOV (`fsr2_upscaler.cpp:519`, `:530`, `:532`), which warrants real camera/timing integration and hardware validation before broad acceptance.

The GUI should expose actual capability state and effective selection. New controls for inert effects create false functionality. No new shader or visual-parity claim is made by this inventory.

### F-007. Rendering and VFX maturity exceeds several gameplay foundations

There are concrete Lua scripting and skeletal animation implementations, including GPU skinning dispatch (`runtime/engine/animation/skeletal_mesh_subsystem.cpp:570`, `:701`). Treating animation as wholly absent would waste existing work.

By contrast, `runtime/engine/physics/physics_listener_registry.h:7` explicitly describes a future PhysicsWorld. Audio-spectrum VFX consumes a provider interface (`runtime/engine/vfx/modules/audio_spectrum_module.h:38`), not an implemented engine mixer. Searches of native engine/editor/platform sources did not locate a Python embedding runtime, Jolt world implementation, XAudio2 engine, Recast/Detour navigation runtime or general gameplay save-slot service. Their pending Rulebook scopes corroborate expansion work, but absence here is scoped to the inspected code, not a claim about every external repository.

Python editing and Python tool execution should be specified separately from Python gameplay embedding. C++ language tooling/build/debug is separate from safe live coding of loaded game code. Lua execution is separate from LSP/DAP support. The proposed editor can deliver useful renderer/asset/material/VFX authoring before all gameplay systems exist, provided it describes those boundaries honestly.

### F-008. Historical validation is strong evidence, but not an all-green current gate

`docs/analysis/project-stabilization/16-audit-closure-2026-10-17.md:54` records a successful reviewed-source ReleaseFast build, GPU validation with specified barrier IDs absent, reference-image identity, native tests and extension/MCP gates. Its native result is **7,827 tests / 781 suites: 7,807 passed, 17 skipped and 3 pre-existing VFX failures**, with exit 1. It explicitly does not claim every native test passes.

The same report keeps Falcor matrix item 4.1 open. Static calibrated proxy results do not establish matched camera-cut, deformation, temporal-toggle, resize and live-editor acceptance. Old priorities also warn that checklist status is not fresh test evidence (`.rulebook/PRIORITIES.md:10`) and that scene-loader, animation, material-graph and froxel tasks need reconciliation.

This audit did not run the full native suite or build. Before committing to a replacement shell, capture fresh standalone/current-host/candidate measurements under the same revision, release build, scene, renderer backend, render/output resolution and feature settings. Do not claim a measured speedup from the architecture proposal alone.

## Performance and lifetime constraints for a native host

1. **Keep rendering independent of interactive panel stalls.** The standalone application waits for the reused frame allocator's fence at `runtime/engine/app/application.cpp:285`, renders at `:319` and presents at `:326`. If a GUI thread drives that whole sequence, it inherits those waits. Decide ownership explicitly and communicate through bounded snapshots/commands.
2. **Keep GPU transfers intentional.** CPU asset workers do not own the RHI (`runtime/engine/renderer/upload/async_mesh_loader.h:14`). Preserve that separation; importing/generated assets should not mutate GPU state from a tool callback.
3. **Treat resize as a synchronization event.** `runtime/engine/rhi/rhi_swapchain.h:71` requires GPU idle before resize. Coalesce interactive resizing and measure stalls. Do not repeatedly recreate a swapchain on every transient panel-layout sample without measurement.
4. **Respect graph resource ownership.** The pool is not thread-safe, has no within-graph aliasing and requires ordered reuse/caller-owned GPU completion for destruction (`runtime/engine/renderer/render_graph/render_graph_resources.h:33`). Multiple independent preview viewports need their own resource/history ownership strategy.
5. **Keep diagnostic stalls out of normal frame accounting.** Some explicit captures call WaitIdle, for example application screenshot/preload paths (`runtime/engine/app/application.cpp:338`). Record diagnostic modes in benchmarks rather than comparing a capture-heavy run with a clean viewport run.
6. **Separate UI rendering cadence from viewport cadence.** Static panels can invalidate on change; active viewport, animated previews and code editing have different schedules. Existing Slate code is not evidence that the full proposed application already has this policy.
7. **Keep external tools out of render-critical work.** Builds, Python tasks, Claude/Codex sessions and Meshy/EZPainter jobs need bounded queues, cancellation, logs, output provenance and result acceptance on the owner thread. Remote latency should never be a frame dependency.

## Recommended remaining work, by user-visible result

| Desired result | Existing base to reuse | Missing acceptance or implementation contract |
|---|---|---|
| Responsive scene workspace | Application, RHI, SceneRenderer, grid/gizmo/picking, native platform window | Shell/docking, viewport ownership, snapshot updates, layout persistence, DPI/focus testing and measured p95/p99 response |
| Reliable Outliner and Inspector | ECS, metadata, scene I/O and selection | Persistent hierarchy, multi-selection, transactional edits, stable editor IDs and hierarchy-preserving round trips |
| Safe asset authoring | Importers, sidecars, manifests, VFS and background loading | Search/index/dependency model, reimport/cancel/retry UX, source/output ownership and provenance |
| Material workspace | Graph persistence/HLSL translation/material pipeline | Full graph document workflow, diagnostics, validated controls, preview lifetime and shared undo contract |
| VFX workspace | Existing overlay/composition/widgets/undo | Preserve proven workflows, reconcile declarative migration tests, integrate into shared shell without losing behavior |
| Animation workspace | Clips/skeletons/controller/skinning | Asset preview, timeline/state-machine editing and feature-specific validation; no assumption of full animation-suite parity |
| Python/C++/Lua code workspace | Lua runtime and existing engine APIs/build system | Monaco or equivalent, language servers, debugger adapters, process/task management, project config and file-change reconciliation |
| AI-assisted editing | Existing runtime entry points plus proposed command contract | Structured actions, contextual snapshots, diff/review, provenance, cancellation and undoable application of changes |
| Diagnostics workspace | Pass inspector infrastructure, CPU/GPU profiling, dump/pixel/validation tools | Unified timeline, frame/capture identity, capability display and reproducible baseline comparison |
| Production gameplay features | ECS/tick/scripting/math/jobs/asset base | Physics/audio/navigation/save/input/game ABI scopes tracked in Rulebook; implement as separate deliverables after current priorities |

The practical migration unit is therefore an **editor host and shared editing services around the existing renderer**, with a measured native viewport path and a proven embedded code editor. The inventory does not justify replacing the renderer, discarding native VFX authoring, or claiming that the current Slate layer already solves a polished desktop editor.
