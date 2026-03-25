# Niagara Source Code — File Reference Map

This document maps Niagara's source files to their architectural purposes and UzEngine equivalents.

---

## Ribbon Rendering System

| Niagara File | Lines | Pattern | Purpose | UzEngine Equivalent |
|---|---|---|---|---|
| `NiagaraRendererRibbons.h` | 30–103 | Feature Config | Ribbon generation configuration with bitmask features | `src/engine/vfx/ribbon_config.h` |
| `NiagaraRendererRibbons.h` | 119–144 | Multi-Ribbon Info | Per-ribbon sorting, bounds, segment metadata | `src/engine/vfx/ribbon.h` |
| `NiagaraRendererRibbons.h` | 147–182 | CPU Vertex Data | Segment data, sorted indices, tangents (CPU path) | `src/engine/vfx/ribbon_geometry.h` |
| `NiagaraRendererRibbons.h` | 207–257 | GPU Buffer | RHI-agnostic GPU buffer wrapper | `src/engine/rhi/gpu_buffer.h` |
| `NiagaraRendererRibbons.h` | 259–293 | Shape Geometry | Ribbon shape data (plane, multiplane, tube, custom) | `src/engine/vfx/ribbon_shape.h` |
| `NiagaraRendererRibbons.cpp` | 30–144 | Console Variables | Tessellation params, scalability settings | `src/engine/core/cvar.h` (use existing system) |
| `NiagaraRendererRibbons.cpp` | 200–271 | Tessellation Helpers | Bit encoding, fast acos, buffer layout structs | `src/engine/vfx/ribbon_tessellation.h` |
| `NiagaraRendererRibbons.cpp` | 273–341 | Index Buffer | Index buffer allocation and lifecycle | `src/engine/rhi/index_buffer.h` |
| `NiagaraRendererRibbons.cpp` | 376–422 | Frame Resources | Per-frame rendering state (vertex factory, buffers) | `src/engine/vfx/ribbon_frame_resources.h` |
| `NiagaraRendererRibbons.cpp` | 452–494 | Buffer Allocation | Hysteresis-based GPU buffer allocation | `src/engine/vfx/gpu_buffer.c` (Pattern 5) |

### Key Niagara Ribbon Concepts

**Tessellation Modes** (`ENiagaraRibbonTessellationMode`):
- `Automatic` — Adaptively tessellate based on angle/screen coverage
- `Custom` — Fixed tessellation factor or screen-space scaling
- `Disabled` — No tessellation, render segments as-is

**UV Distribution Modes** (`ENiagaraRibbonUVDistributionMode`):
- `ScaledUniformly` — Segment-based (all segments share 0-1 UV range)
- `ScaledUsingRibbonSegmentLength` — Length-aware (account for segment length variation)
- `TiledOverRibbonLength` — Repeating texture along ribbon
- `TiledFromStartOverRibbonLength` — Tiled with particle distance attribute

**Shape Modes** (`ENiagaraRibbonShapeMode`):
- `Plane` — Single quad per segment (screen-facing)
- `MultiPlane` — Multiple planes rotated 0–180 degrees
- `Tube` — 3D tube shape (triangular to cylindrical)
- `Custom` — User-defined cross-section vertices

---

## Parameter Binding & Inheritance

| Niagara File | Lines | Pattern | Purpose | UzEngine Equivalent |
|---|---|---|---|---|
| `NiagaraParameterStore.h` | 14–26 | Bound Parameter | Single parameter binding descriptor | `src/engine/vfx/param_store.h` (Pattern 2) |
| `NiagaraParameterStore.h` | 47–122 | Parameter Binding | Offset-based copy between stores (no virtual dispatch) | `src/engine/vfx/param_store.h` (Pattern 2) |
| `NiagaraParameterStore.h` | 125–143 | Variable with Offset | Variable name + byte offset in buffer | `src/engine/vfx/param_store.h` |
| `NiagaraTypes.h` | 23–92 | Type Definitions | Explicit POD types (float, int, vec3, quat, matrix) | `src/engine/vfx/param_types.h` |

### Key Parameter Concepts

**Parameter Scopes** (hierarchy):
- **System** — Global VFX settings (LOD, quality scale)
- **Emitter** — Per-emitter config (spawn rate, lifetime)
- **Module** — Per-behavior config (speed curve, color over life)
- **Particle** — Per-particle attributes (age, velocity, color)

**Parameter Types** (no C++ templates):
- `FNiagaraFloat`, `FNiagaraInt32`, `FNiagaraBool`
- `FNiagaraPosition`, `FVector3f`, `FVector4f`
- `FNiagaraMatrix`, `FNiagaraHalf*` (GPU-optimized)

**Binding Strategy**:
1. At init: compute byte offsets for each parameter
2. Per frame: memcpy N bytes from source to dest (no virtual calls)
3. Receivers don't know parameter types — just byte offsets

---

## Event System & Routing

| Niagara File | Lines | Pattern | Purpose | UzEngine Equivalent |
|---|---|---|---|---|
| `NiagaraEvents.h` | 17–20 | Event Names | Predefined event name constants | `src/engine/vfx/events.h` |
| `NiagaraEvents.h` | 25–40 | Collision Payload | POD struct for collision event data | `src/engine/vfx/events.h` (Pattern 4) |
| `NiagaraEvents.h` | 48–68 | Event Receiver | Virtual base class for event actions | `src/engine/vfx/events.h` (callbacks instead) |
| `NiagaraEvents.cpp` | N/A | Event Emission | Dispatch events to registered receivers | `src/engine/vfx/events.c` |

### Key Event Concepts

**Built-in Events**:
- `Spawn` — Particle created (internal name: `"Spawn"`)
- `Death` — Particle removed (internal name: `"Death"`)
- `Collision` — Particle hit geometry (internal name: `"NiagaraSystem_Collision"`)

**Event Payload**:
- POD structs only (serializable, GPU-passable)
- No pointers, no virtual methods
- Size is known at compile-time

**Receiver Pattern**:
- Virtual dispatch on UClass hierarchy (Niagara uses reflection)
- UzEngine adapts with C callbacks (no UObject overhead)
- Registered at emitter init, not dynamically

---

## Scalability & Significance

| Niagara File | Lines | Pattern | Purpose | UzEngine Equivalent |
|---|---|---|---|---|
| `NiagaraScalabilityState.h` | 10–78 | Scalability State | Per-system culling state with reason tracking | `src/engine/vfx/scalability.h` (Pattern 3) |
| `NiagaraScalabilityManager.h` | 15–111 | Significance Manager | Budget allocation, culling decisions | `src/engine/vfx/significance_manager.h` |
| `NiagaraScalabilityManager.cpp` | N/A | Update Loop | Evaluate significance, apply culling, transitions | `src/engine/vfx/significance_manager.c` |

### Key Scalability Concepts

**Culling Criteria**:
- **Distance** — Too far from camera (per-frame distance check)
- **Budget** — Exceeds global GPU/CPU budget (significance-ranked allocation)
- **Visibility** — Behind camera or off-screen (frustum cull)
- **Significance** — Lower importance under budget pressure

**Significance Factors**:
- Distance to camera (closer = higher significance)
- System importance level (artist-authored)
- Screen coverage (larger = higher significance)
- Previously visible (favor systems that were recently on-screen)

**Significance Manager Loop**:
1. **Frame 0**: Register new systems
2. **Frame N**: Check dirty (any culling state changes?)
3. If dirty: re-sort by significance
4. Allocate budget: highest significance first
5. Transition visibility: fade-in/fade-out based on culling change

---

## GPU Compute & Vertex Factories

| Niagara File | Lines | Pattern | Purpose | UzEngine Equivalent |
|---|---|---|---|---|
| `NiagaraRibbonVertexFactory.h` | N/A | Vertex Factory | RHI vertex input layout definition | `src/engine/rhi/vertex_factory.h` |
| `NiagaraRibbonCompute.h` | N/A | Compute Shader | GPU ribbon geometry generation | `src/engine/vfx/ribbon_compute.hlsl` |
| `NiagaraRibbonShared.h` | N/A | Shared Definitions | Constant buffer layouts, macro definitions | `src/engine/vfx/ribbon_shared.hlsl` |

### Key Vertex Factory Concepts

**Input Attributes**:
- Particle position, velocity, age
- Ribbon width, twist, facing vector
- Custom UV overrides, material params

**Output Attributes**:
- Vertex position, normal, tangent
- UV coordinates (UV0, UV1)
- Vertex color, material index

---

## Type System & Compilation

| Niagara File | Lines | Pattern | Purpose | UzEngine Equivalent |
|---|---|---|---|---|
| `NiagaraTypes.h` | 1–250 | Type Definitions | POD types, no C++ templates | `src/engine/vfx/param_types.h` |
| `NiagaraCommon.h` | N/A | Common Defines | Enums, constants, macros | `src/engine/vfx/common.h` |
| `NiagaraDefines.h` | N/A | Defines & Flags | Feature flags, debug macros | `src/engine/vfx/defines.h` |

---

## File Size & Responsibility

Niagara's modular structure provides a template for UzEngine:

```
Niagara Organization:
├── NiagaraRendererRibbons.h      (~400 lines) — Ribbon renderer interface
├── NiagaraRibbonRendererProperties.h (~500 lines) — Ribbon config (editor-facing)
├── NiagaraRendererRibbons.cpp    (~2000 lines) — Ribbon implementation
├── NiagaraRibbonVertexFactory.h  (~200 lines) — GPU input layout
├── NiagaraRibbonCompute.h        (~100 lines) — GPU compute interface
├── NiagaraRibbonShared.h         (~100 lines) — Shader constants
├── NiagaraParameterStore.h       (~250 lines) — Parameter binding
├── NiagaraScalabilityManager.h   (~100 lines) — Budget management
└── NiagaraScalabilityState.h     (~80 lines)  — Per-system culling state

UzEngine Equivalent:
├── src/engine/vfx/ribbon_config.h          (~150 lines)
├── src/engine/vfx/ribbon.h                 (~200 lines)
├── src/engine/vfx/ribbon_geometry.c        (~300 lines)
├── src/engine/vfx/ribbon_render.c          (~400 lines)
├── src/engine/vfx/param_store.h            (~150 lines)
├── src/engine/vfx/param_store.c            (~200 lines)
├── src/engine/vfx/scalability.h            (~100 lines)
├── src/engine/vfx/events.h                 (~150 lines)
├── src/engine/vfx/events.c                 (~200 lines)
└── src/engine/rhi/gpu_buffer.h             (~100 lines)
```

Each file adheres to **single responsibility**:
- Config files: pure data structures, enums, constants
- Header files: interfaces, inline accessors, type definitions
- Implementation files: allocation, lifecycle, update loops

---

## Mapping: Specific Niagara Implementations → UzEngine

### Ribbon Tessellation

**Niagara**:
- `FNiagaraRendererRibbons::CalculateTessellationFactor()` (line ~343)
- `FNiagaraRendererRibbons::CalculateUVScaleAndOffsets()` (line ~329)
- `ENiagaraRibbonTessellationMode`, `FNiagaraRibbonTessellationConfig`

**UzEngine Equivalent**:
```cpp
// src/engine/vfx/ribbon_tessellation.h
uint32_t uz_ribbon_calculate_tessellation_factor(
    const uz_ribbon_tessellation_config_t* cfg,
    float angle,
    float screen_coverage
);

void uz_ribbon_calculate_uv_scale_and_offsets(
    const uz_ribbon_uv_settings_t* uv_settings,
    float total_length,
    float* out_u_scale,
    float* out_u_offset,
    float* out_u_distribution_scaler
);
```

### Parameter Inheritance

**Niagara**:
- `FNiagaraParameterStoreBinding::CopyParameters()` (line ~111)
- `FNiagaraParameterStore::Bind()` (internal)

**UzEngine Equivalent**:
```cpp
// src/engine/vfx/param_store.c
void uz_param_copy_inherited(
    uz_param_store_t* dst,
    const uz_param_store_t* src,
    const uz_param_store_binding_t* binding
);
```

### Culling & Significance

**Niagara**:
- `FNiagaraScalabilityManager::Update()` (line ~72)
- `FNiagaraScalabilityManager::EvaluateCullState()` (line ~102)
- `FNiagaraScalabilityState::IsDirty()` (line ~44)

**UzEngine Equivalent**:
```cpp
// src/engine/vfx/significance_manager.c
void uz_vfx_evaluate_significance(
    uz_vfx_significance_manager_t* mgr,
    const uz_camera_t* camera,
    float time_delta
);

bool uz_vfx_culling_dirty(const uz_vfx_scalability_state_t* state);
```

### Event Routing

**Niagara**:
- `UNiagaraEventReceiverEmitterAction::PerformAction()` (virtual)
- `FNiagaraCollisionEventPayload` (POD struct)

**UzEngine Equivalent**:
```cpp
// src/engine/vfx/events.h
typedef void (*uz_vfx_event_receiver_fn)(
    uz_vfx_event_type_t event_type,
    const void* payload,
    uint32_t payload_size,
    void* user_data
);

void uz_vfx_emit_events(
    uz_vfx_emitter_t* emitter,
    const uz_vfx_event_batch_t* batches,
    uint32_t num_batches
);
```

---

## Adaptation Strategy

1. **Read Niagara code** → Understand the problem domain
2. **Identify the pattern** → What architectural problem does this solve?
3. **Adapt, don't copy** → Rewrite in UzEngine's C/C++ style, without UObject overhead
4. **Test early** → Implement one subsystem completely before moving to the next
5. **Profile** → Measure against UzEngine's expected VFX scale (thousands of systems)

---

## References

### Niagara Source Locations

- **Ribbon Rendering**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraRendererRibbons.h`
- **Parameter System**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraParameterStore.h`
- **Event System**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEvents.h`
- **Scalability**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraScalabilityManager.h`
- **Decal System** (reference): `U:/UnrealGITHUB/UnrealEngine/Engine/Source/Runtime/Engine/Public/SceneProxies/DeferredDecalProxy.h`

### UzEngine Implementation Guides

- **NIAGARA_VFX_ANALYSIS.md** — Detailed pattern analysis with trade-offs
- **NIAGARA_IMPLEMENTATION_PATTERNS.md** — Ready-to-use code templates
- **This file (NIAGARA_SOURCE_MAP.md)** — Quick reference mapping

---

## Summary

Niagara's codebase is organized around these principles:

1. **Decoupling by concern**: Rendering ≠ Simulation ≠ Scalability
2. **POD data structures**: Serializable, GPU-passable, no virtual overhead
3. **Immutable configuration**: Features computed once, referenced zero-cost
4. **Batching for efficiency**: Events, parameter copies, significance evaluation
5. **Multi-criterion evaluation**: Culling reasons, significance factors

UzEngine should adopt these principles to achieve both **performance** (no virtual dispatch in hot loops) and **debuggability** (reason tracking, bitfield diagnostics).
