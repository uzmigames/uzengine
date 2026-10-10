## 1. VFX Graph Asset

- [x] 1.1 Create `runtime/engine/vfx/asset/vfx_graph.h` — VFXGraph class: name, version, emitter config (max_particles, lifespan_default), modules vector (ordered), parameters map (name → ParameterDef), events vector (EventDef). Static Load(path) factory. GetModules(), GetParameters(), GetEvents() accessors.
- [x] 1.2 Create `runtime/engine/vfx/asset/vfx_graph.cpp` — JSON parsing using nlohmann::json: validate "format"=="uzvfx", parse version, emitter section, modules array (instantiate via ModuleRegistry), parameters section (type/default/min/max), events section (name/emission_count/velocity_scale).
- [x] 1.3 Create `runtime/engine/vfx/asset/vfx_graph_validator.h/.cpp` — Validate loaded VFX graphs: at least one module, modules reference valid types, parameters have valid types, events have positive emission counts, no duplicate parameter/event names. Logs UZ_LOG(VFX, Error) on validation failure.

## 2. Parameter System

- [x] 2.1 Create `runtime/engine/vfx/parameters/vfx_parameter_store.h/.cpp` — VFXParameterStore: stores runtime parameter values per emitter instance. SetFloat(name, value), SetVec3(name, value), SetVec4(name, value). GetFloat(name, default), GetVec3(name, default), GetVec4(name, default). Backed by std::unordered_map<std::string, Vec4> (all types stored as Vec4). UploadToGPU(cbAllocator) packs parameters into constant buffer for compute shader access.
- [x] 2.2 Integrate parameter store with ParticleEmitter — ParticleEmitter holds VFXParameterStore. SetParameter(name, value) forwards to store. Parameters uploaded each frame before compute dispatch.

## 3. Event System

- [x] 3.1 Create `runtime/engine/vfx/events/vfx_event.h/.cpp` — VFXEvent struct: name, emission_count, velocity_scale (default 1.0). VFXEventQueue: queue of pending events per emitter. EmitEvent(name) pushes event to queue. Drain() returns pending events and clears.
- [x] 3.2 Integrate events with spawn — ParticleSimulationPass checks event queue before spawn dispatch. If burst event pending, spawn emission_count particles in single frame (override normal rate). Apply velocity_scale to spawn velocity.

## 4. Composite Effects

- [x] 4.1 Create `runtime/engine/vfx/asset/vfx_composite.h/.cpp` — VFXComposite: loads .uzvfx_composite JSON. Contains array of emitter entries (name, graph path, offset Vec3, delay float). Spawn() creates multiple ParticleEmitter components on entity, each with its own VFXGraph, offset, and delayed start.
- [x] 4.2 Implement delayed start — Emitter with delay > 0 waits specified seconds before starting emission. VFXSystem tracks delay timer per emitter.

## 5. Hot-Reload

- [x] 5.1 Create `runtime/engine/vfx/asset/vfx_hot_reload.h/.cpp` — VFXHotReload: registers FileWatcher on content/vfx/**/*.uzvfx. On file change: (a) reload VFXGraph from disk, (b) recompile compute shader via ModuleCompositor, (c) iterate all active emitters using that graph, (d) swap their PSO and module list. Guarded by #if UZ_DEV_BUILD.

## 6. Lua API

- [x] 6.1 Create `runtime/engine/vfx/scripting/vfx_lua_bindings.h/.cpp` — Register Lua bindings for ParticleSystem: ctx.vfx:spawn(graph_path, position) → entity, :set_parameter(name, float_or_vec3), :emit_event(name), :start(), :stop(), :pause(bool), :get_alive_count() → int. Register with LuaVM at VFX system init.

## 7. Testing

- [x] 7.1 Unit tests for VFXGraph loader — load valid .uzvfx JSON, verify modules instantiated in order, parameters parsed with correct types/defaults
- [x] 7.2 Unit tests for VFXGraph validator — invalid module type, duplicate parameter names, missing required fields
- [x] 7.3 Unit tests for VFXParameterStore — set/get float/vec3/vec4, default values, upload size calculation
- [x] 7.4 Unit tests for VFXEventQueue — emit event, drain returns events, double drain returns empty
- [x] 7.5 Unit tests for VFXComposite — load composite JSON, verify emitter entries with offsets/delays
- [x] 7.6 Unit tests for VFXHotReload — file change detection, graph reload callback invocation

## 8. Demo

- [x] 8.1 Create explosion composite demo — fire emitter (500 particles, upward, orange→transparent) + smoke emitter (200 particles, slow rise, gray, delay=0.2s) + debris emitter (50 mesh particles, outward burst, delay=0s). Triggered by spacebar press via EmitEvent("explode").
- [x] 8.2 Add Lua scripting demo — Lua script that modifies emitter parameters dynamically (intensity oscillation, color cycling)

## 9. Documentation

- [x] 9.1 Document .uzvfx JSON format specification with complete field reference
- [x] 9.2 Document .uzvfx_composite format specification
- [x] 9.3 Document Lua VFX API with examples
- [x] 9.4 Document hot-reload workflow for VFX artists

## 10. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 10.1 Update or create documentation covering the implementation
- [ ] 10.2 Write tests covering the new behavior
- [ ] 10.3 Run tests and confirm they pass
