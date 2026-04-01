<!-- doc-meta: category=analysis, area=falcor, topic=architecture -->

# Falcor Architecture & Infrastructure Analysis

**Source**: NVIDIA GameWorks Falcor v8.0 (August 2024)
**Repository**: https://github.com/NVIDIAGameWorks/Falcor
**Primary Language**: C++ (90.4%), HLSL/Slang shaders
**Analyzed**: 2026-03-30

---

## 1. Overview

Falcor is NVIDIA's open-source real-time rendering research framework, targeting DX12 and Vulkan.
Its design goal is researcher productivity: a single cohesive framework that abstracts GPU APIs,
provides a high-level render graph, and exposes everything to Python scripting for rapid prototyping.

It is **not** a game engine — it has no ECS, no scripting for gameplay, no physics. It is a
rendering testbed and reference implementation for SIGGRAPH-grade techniques.

---

## 2. Core Architecture

### 2.1 Module Structure

```
Source/Falcor/
├── Core/
│   ├── API/          # Device, Buffer, Texture, Sampler, Fence, Swapchain, RT acceleration
│   ├── Pass/         # Base pass types (ComputePass, FullScreenPass, RasterPass)
│   ├── Platform/     # OS abstractions (file I/O, window, monitor)
│   ├── Program/      # Shader compilation, reflection, variant management
│   └── State/        # PSO wrappers (GraphicsState, ComputeState)
├── Scene/
│   ├── Animation/    # AnimationController, skinning, vertex caches
│   ├── Camera/       # Camera, CameraController (FPS/Orbit/6DOF), CameraData.slang
│   ├── Lights/       # Analytic lights, LightCollection (emissive meshes), EnvMap
│   ├── Material/     # Material base class, BasicMaterial (PBR), MaterialSystem
│   └── Curves/       # Hair/strand geometry
└── RenderGraph/
    ├── RenderGraph         # Graph topology, pass registry, edge management
    ├── RenderGraphCompiler # Dependency analysis, execution ordering, resource allocation
    ├── RenderGraphExe      # Compiled execution container
    ├── RenderPass          # Base class for all render passes
    ├── RenderPassReflection # I/O declaration for graph compiler
    └── ResourceCache       # Transient resource reuse
```

### 2.2 API Abstraction Layer

Falcor abstracts DX12 and Vulkan through a **GFX** backend (NVIDIA's Slang GFX library), not a
hand-written RHI. The `Device` class wraps `gfx::IDevice` and exposes:

- Typed resource factories: `createBuffer()`, `createTexture1D/2D/3D/Cube()`, `createSampler()`
- PSO factories: `createComputeStateObject()`, `createGraphicsStateObject()`, `createRtStateObject()`
- Capability queries: `isFeatureSupported()`, `isShaderModelSupported()`
- Debug features: Aftermath (NVIDIA GPU crash dump), DX12 debug layer

**Key design**: All resources are `ref<T>` (intrusive reference counted via `Object` base).
No raw pointers own GPU resources; all lifetimes are managed by shared references.

### 2.3 Memory Management

**Transient Heaps** — `kInFlightFrameCount = 3` upload heaps rotate per frame via fence-gated
reuse. `getCurrentTransientResourceHeap()` returns the active heap for upload/readback.

**GpuMemoryHeap** — page-based allocator:
- Active page serves current allocations; exhausted pages move to "used" map
- Available pages (drained by GPU fence) move to reuse queue
- `executeDeferredReleases()` processes a fence-ordered priority queue, preventing GPU UAF

**Deferred Release Queue** — resources enqueued for deletion instead of immediately freed.
A fence value tracks when GPU finishes with each resource before memory is reclaimed.

**Comparison with UzEngine `CBRingAllocator`**: UzEngine uses a triple-buffered ring allocator
for constant buffer uploads — a narrower approach (CB-only) but functionally equivalent for that
use case. Falcor's page-based heap is more general, handling large transient geometry.

### 2.4 Descriptor Management (D3D12)

Falcor maintains separate CPU and GPU descriptor pools via:
- `D3D12DescriptorPool` — heap management with set/layout allocation
- `D3D12DescriptorSet` — binding table for a single draw
- `D3D12RootSignature` — constructed from `ProgramReflection` automatically

All descriptor operations require `requireD3D12()` guard, enforcing API boundary.

**Key difference from UzEngine**: UzEngine uses explicit root signature slots defined in
`root_slots.h` with a fixed 14-parameter layout. Falcor auto-generates root signatures from
shader reflection data at program creation time.

---

## 3. Render Graph System

### 3.1 Architecture

Falcor's render graph is **persistent and scriptable**. A `RenderGraph` object survives across
frames. Passes are registered once at startup, connected by named edges, and the graph is
recompiled only when topology or scene changes occur (`mRecompile` flag).

```
RenderGraph (persistent)
├── Registered passes: map<name, RenderPass*>
├── Edge list: "passA.resource" → "passB.resource"
└── RenderGraphCompiler → RenderGraphExe (compiled snapshot)
    └── ResourceCache (transient GPU resource reuse)
```

Execution order: `RenderGraph::compile()` → `RenderGraphExe` → `execute()` per frame.

### 3.2 Pass Interface

Every pass inherits `RenderPass : Object`:

```cpp
class RenderPass : public Object {
    virtual RenderPassReflection reflect(const CompileData&);  // declare I/O
    virtual void compile(RenderContext*, const CompileData&);  // validate on graph compile
    virtual void execute(RenderContext*, RenderData&);         // per-frame GPU work
    virtual void setProperties(const Properties&);
    virtual Properties getProperties() const;
    virtual void setScene(RenderContext*, const ref<Scene>&);
    virtual void renderUI(Gui*, const Gui::Group&);
    virtual bool onMouseEvent(const MouseEvent&);
    virtual bool onKeyEvent(const KeyboardEvent&);
    virtual void onHotReload(HotReloadFlags);
};
```

**Resource declaration** is via `RenderPassReflection` returned from `reflect()`. Each field entry
specifies name, type (texture/buffer), dimensions, format, and access (input/output/internal).
The graph compiler reads this to determine edges and allocate transient resources.

**Resource access** in `execute()` is via `RenderData::getTexture(name)` / `getResource(name)` —
string-keyed lookup into the compiled resource set.

### 3.3 Graph Compilation

`RenderGraphCompiler` performs:
1. `resolveExecutionOrder()` — topological sort from dependency edges
2. `compilePasses()` — calls `reflect()` on each pass with resolved dimensions
3. `insertAutoPasses()` — auto-inserts format-conversion passes for incompatible edge types
4. `allocateResources()` — allocates physical GPU resources via `ResourceCache`, with aliasing
   for non-overlapping lifetimes

The original graph topology is preserved; `restoreCompilationChanges()` can undo auto-inserted
passes, making recompilation clean.

### 3.4 Comparison with UzEngine Render Graph

| Feature | Falcor | UzEngine |
|---------|--------|----------|
| **Lifetime** | Persistent (frame-to-frame) | Per-frame (new `RgBuilder` each frame) |
| **Pass declaration** | `reflect()` virtual method | `AddPass()` lambda with `RgPassBuilder` |
| **Resource addressing** | String names (`"passName.resource"`) | `RgResourceId` integer handles |
| **Graph compilation** | On topology change (`mRecompile`) | Every frame (`Compile()` in `RenderFrame`) |
| **Resource aliasing** | `ResourceCache` (overlap analysis) | `RgResourcePool` (descriptor-keyed reuse) |
| **Auto barriers** | Auto-inserted by compiler | Scheduled in `ScheduleBarriers()` |
| **Culling** | On compile | `CullPasses()` per frame |
| **Scripting** | Python (Mogwai) | None (Lua is game scripting, not render graph) |
| **Pass hot-reload** | `onHotReload()` virtual | Not implemented |
| **Auto format conversion** | `insertAutoPasses()` | Not implemented |
| **Parallel recording** | Not in base graph | `SetParallelRecordingEnabled()` API exists |

**UzEngine advantage**: Per-frame rebuild allows dynamic pass toggling (e.g., `--pass-toggle`,
`--shadow-debug`) with zero special-casing — the entire graph is rebuilt each frame based on
current settings flags. Falcor must track `mRecompile` triggers manually.

**Falcor advantage**: Persistent passes with `setScene()` callbacks allow passes to cache
scene-dependent GPU resources (BVH handles, material CBs) across frames without re-querying
the scene. UzEngine's per-frame approach rebuilds all pass state each frame.

**Gap**: Falcor's `insertAutoPasses()` for automatic format conversion is a quality-of-life
feature UzEngine lacks. Currently UzEngine requires manual format compatibility in pass design.

---

## 4. Scene System

### 4.1 Architecture

Falcor's `Scene` is a monolithic rendering representation — NOT an ECS. It is purpose-built for
the renderer: geometry, materials, lights, cameras, and acceleration structures are all centralized
and GPU-ready.

```
Scene
├── Geometry
│   ├── MeshDesc[] — mesh metadata
│   ├── MeshGroup[] — BLAS groupings for ray tracing
│   ├── GeometryInstanceData[] — per-instance transforms + material refs
│   └── Curve/CustomPrimitive support
├── Materials
│   └── MaterialSystem — typed material registry with UpdateFlags
├── Animation
│   └── AnimationController — keyframe + vertex cache animations
├── Cameras
│   └── Camera[] + CameraController (FPS/Orbit/6DOF)
├── Lights
│   ├── AnalyticLight[] (Directional, Point, Spot, Distant, Rect, Disc, Sphere)
│   ├── LightCollection (emissive mesh lights, GPU-computed)
│   └── EnvMap (HDRI environment)
└── Acceleration Structures
    ├── BLAS[] — per MeshGroup, Rebuild or Refit update modes
    └── TLAS cache — keyed by ray type count, Rebuild mode
```

`Scene::update()` is called once per frame, returns `UpdateFlags` indicating geometry, material,
camera, light, or transform changes. Passes query these flags via `onSceneUpdates()` to
selectively invalidate their cached GPU state.

### 4.2 Shader Integration

The scene provides a **bindless shader interface** via a `ParameterBlock` (`mpSceneBlock`):

- `scene.bindShaderData(var)` — binds the entire scene to a shader variable
- `scene.getSceneDefines()` — returns `#define` list for shader variant selection
- `scene.getTypeConformances()` — Slang dynamic dispatch conformances for material types
- `scene.getShaderModules()` — Slang modules to include for scene types

This makes scene data accessible in shaders as structured data (geometry buffers, material arrays,
light arrays) — no manual root signature management. The Slang reflection system auto-generates
root signature / descriptor layout from shader source.

### 4.3 Comparison with UzEngine Scene

| Feature | Falcor | UzEngine |
|---------|--------|----------|
| **Scene representation** | Monolithic `Scene` class | ECS `World` (EnTT) + `SceneRenderer` |
| **Geometry** | `MeshDesc` + `GeometryInstanceData` | `DrawList` built from ECS components |
| **Materials** | `MaterialSystem` with type hierarchy | `MaterialInterface` + per-mesh material refs |
| **Lights** | Typed analytic lights + emissive mesh lights | ECS components → `LightFrameData` |
| **Animation** | `AnimationController` (keyframe + vertex cache) | Not yet implemented in renderer |
| **Cameras** | Multiple named cameras with viewpoints | Single `Camera` + `FlyCameraController` |
| **BVH** | BLAS/TLAS auto-management | Not implemented (no ray tracing) |
| **Update tracking** | `UpdateFlags` from `update()` | Full rebuild each frame via ECS gather |
| **Shader binding** | Bindless `ParameterBlock` via Slang | Explicit CBs (PerFrameCB, LightingCB, ShadowDataCB) |
| **Scripting** | Python scene loading (`.pyscene`) | Lua scene scripting (ECS-level) |

**Falcor advantage**: UpdateFlags allow passes to skip work when unrelated scene data changes
(e.g., a material update doesn't force BLAS rebuild). UzEngine rebuilds all CB data every frame.

**UzEngine advantage**: ECS decouples game logic from rendering. Falcor's Scene is render-only
with no concept of entity lifecycle, components, or game-simulation separation. UzEngine's
`World` → `DrawList` pipeline cleanly separates simulation from rendering.

**Gap**: UzEngine has no equivalent of `LightCollection` (emissive mesh lights). Falcor computes
light contribution from geometry marked emissive as analytic lights — a significant GI quality
feature for research rendering.

---

## 5. Shader System (Program / Slang)

### 5.1 Slang vs. HLSL

Falcor uses **Slang** (NVIDIA's shader language), not HLSL. Slang compiles to HLSL/SPIR-V/DXIL
and adds:
- **Interfaces and generics**: runtime polymorphism for materials (no giant switch statement)
- **Modules**: reusable shader libraries imported like C++ headers
- **Automatic reflection**: root signature auto-generated from shader declarations
- **Type conformances**: dynamic dispatch for material/light types

UzEngine uses **HLSL** with manual `#include` chaining and a fixed root signature layout.

### 5.2 Program Variant System

`Program` manages variants keyed by `(DefineList, TypeConformanceList)`:

- `addDefine("ENABLE_SHADOWS", "1")` creates a new variant key
- Actual compilation is deferred until the variant is needed
- `ProgramManager` handles DXC compilation with Slang as frontend
- `ProgramReflection` provides fully typed parameter layouts for auto-binding

UzEngine's `ShaderManager` compiles shaders with explicit entry points (`"main"`) and stores
handles in `PipelineLibrary`. Variants are created by calling `CreateShader()` again with
different source paths. There is no define-based variant system — new pipeline states must be
explicitly created.

**Gap**: UzEngine lacks a shader variant/permutation system. Adding `#define`-gated variants
requires creating separate `ShaderManager` entries and separate PSOs manually.

---

## 6. Plugin System

### 6.1 Design

Falcor uses **dynamic library plugins** for render passes:

```cpp
// Plugin macro adds static create() factory + plugin metadata
FALCOR_PLUGIN_CLASS(MyPass, "MyPass", "My render pass description");

// Registration function exported from DLL
extern "C" void registerPlugin(Falcor::PluginRegistry& registry) {
    registry.registerClass<RenderPass, MyPass>();
}
```

`PluginManager` is a singleton that loads `.dll`/`.so` files on demand, maps type names to
factory functions, and provides thread-safe instantiation via `createClass<Base, T>()`.

### 6.2 Comparison with UzEngine

UzEngine has no dynamic plugin system. All render passes are compiled into the runtime static
library. Falcor's plugin approach allows:
- Shipping passes as distributable binaries
- Loading research passes without recompiling the framework
- Hot-swapping pass implementations

**Gap**: For a game engine, compile-time linking is appropriate. For a research framework,
Falcor's plugin system is critical. If UzEngine ever exposes a rendering API to game DLLs, a
plugin/extension pattern will be needed.

---

## 7. Python Scripting (Mogwai)

Falcor's `Testbed` application exposes a Python API for render graph construction:

```python
# .pyscene — Python scene file
m.loadScene("scene.fbx")
graph = RenderGraph("PathTracer")
graph.addPass(createPass("PathTracer"), "PathTracer")
graph.addPass(createPass("ToneMapper"), "ToneMapper")
graph.addEdge("PathTracer.color", "ToneMapper.src")
graph.markOutput("ToneMapper.dst")
m.addGraph(graph)
```

This makes render graph construction **data-driven** — researchers can define rendering pipelines
in Python without recompiling C++. Pass parameters are also settable via Python:

```python
graph.updatePass("PathTracer", {"samplesPerPixel": 4, "maxBounces": 8})
```

UzEngine has no equivalent for rendering pipeline scripting. Lua scripting is game-logic only
(ECS component access, camera control, etc.). Building the render graph in Lua/Python would
require exposing `RgBuilder` to the scripting layer.

---

## 8. Application Framework (SampleApp)

Falcor's `SampleApp` provides a minimal but complete application skeleton:

```
SampleApp
├── onLoad(RenderContext*)          — one-time initialization
├── run()                           — main loop entry, returns exit code
├── onFrameRender(RenderContext*, Fbo*) — per-frame rendering
├── onGuiRender(Gui*)               — per-frame UI
├── onResize(uint32_t, uint32_t)    — framebuffer resize
└── Input callbacks (keyboard, mouse, gamepad, file drop)
```

Configuration via `SampleAppConfig`:
- Color/depth format selection
- Headless mode support
- Shader cache configuration

**Comparison with UzEngine**: UzEngine's `Application` class (`application.cpp`) is analogous but
tightly coupled to the engine loop via `EngineLoop`. Falcor's `SampleApp` is intentionally thin —
it does not own a scene or render graph; those are constructed by the subclass. UzEngine's
`SceneRenderer::RenderFrame()` bundles ECS sync + CB setup + graph build + execute in one call,
which is more convenient for game use but less composable for research workflows.

---

## 9. Lessons for UzEngine

### 9.1 High-Priority Gaps

| Gap | Falcor Feature | UzEngine Gap | Priority |
|-----|---------------|-------------|----------|
| Shader variants | `DefineList`-keyed `ProgramVersion` | Manual separate PSOs only | HIGH |
| Material type dispatch | Slang interfaces + conformances | Fixed PBR pipeline, no runtime type | HIGH |
| UpdateFlags propagation | `Scene::UpdateFlags` prevents redundant work | All CBs rebuilt every frame | MEDIUM |
| Multiple cameras | `getCameras()` vector + viewpoints | Single `Camera` per renderer | MEDIUM |
| Emissive mesh lights | `LightCollection` from geometry | Not implemented | LOW |
| Graph hot-reload | `onHotReload()` per pass | Not implemented | LOW |
| Auto format conversion | `insertAutoPasses()` | Manual compatibility required | LOW |
| Python/scripted graph | Mogwai `.pyscene` | Lua is game-only | LOW |

### 9.2 Architecture Patterns to Adopt

**UpdateFlags pattern**: Replace full per-frame CB rebuild with change-tracking flags.
When only a material changes, skip shadow CB recomputation. Estimated savings: 5-15% CPU time
for complex scenes.

**Variant system**: The `DefineList` → `ProgramVersion` pattern maps cleanly to UzEngine.
Add a `ShaderVariantKey { std::vector<std::pair<string,string>> defines }` type to
`ShaderManager`. Each unique key gets its own compiled `IDxcBlob` + PSO entry.

**Pass-level scene binding**: Falcor's `setScene()` callback lets passes cache scene-dependent
resources (e.g., a shadow pass caches the TLAS handle). UzEngine currently queries the shadow
atlas and light list directly in `RenderFrame()`. Exposing a `OnSceneChanged()` callback on
pass-like objects would allow this optimization.

### 9.3 Patterns UzEngine Has Right

**Per-frame graph rebuild**: UzEngine's `RgBuilder` rebuild per frame (vs. Falcor's persistent
graph with `mRecompile`) makes dynamic feature toggling trivial — `--pass-toggle`, debug modes,
screen percentage changes, etc. require zero special-casing. This is the correct design for a
game engine where per-frame settings changes are normal.

**ECS separation**: Falcor's monolithic `Scene` conflates rendering data with scene management.
UzEngine's `World` (ECS) → `DrawList` → GPU pipeline cleanly separates simulation from rendering
and scales better for game use cases.

**Explicit root signature**: UzEngine's `root_slots.h` fixed layout with explicit binding points
is more predictable and debuggable than Slang's auto-generated root signatures. For DX12,
explicit control over root signature layout is important for performance.

**Screenshot infrastructure**: Falcor has no equivalent of `--screenshot-stdout`, `--diag`, or
the buffer visualization grid. UzEngine's diagnostic CLI is significantly more developed.

---

## 10. Summary Matrix

| Area | Falcor Approach | UzEngine Approach | Winner |
|------|----------------|-------------------|--------|
| GPU API abstraction | GFX (Slang backend) | Hand-written RHI | Tie |
| Shader language | Slang (modules, generics) | HLSL | Falcor |
| Shader variants | Define-keyed permutations | Manual PSO creation | Falcor |
| Render graph model | Persistent DAG, named resources | Per-frame builder, typed IDs | UzEngine (for games) |
| Scene model | Monolithic render-only Scene | ECS World + DrawList | UzEngine |
| Material system | Typed hierarchy + Slang dispatch | PBR + fixed pipeline | Falcor |
| Memory management | Page-based GpuMemoryHeap | CBRingAllocator (CB-only) | Falcor |
| Plugin system | Dynamic DLL passes | Static compile-time | Falcor (for research) |
| Python scripting | Full Mogwai pipeline scripting | Lua game scripting only | Falcor |
| Diagnostics | None | Full CLI diagnostic suite | UzEngine |
| Game/ECS integration | None | Full ECS (EnTT) | UzEngine |

---

*Analysis based on Falcor v8.0 source files in `Source/Falcor/Core/`, `Source/Falcor/Scene/`,
`Source/Falcor/RenderGraph/`, cross-referenced with UzEngine `runtime/engine/renderer/`.*
