# Unreal Engine Niagara VFX System — Architectural Analysis for UzEngine

## Executive Summary

Niagara demonstrates sophisticated patterns for:
1. **Parametric Ribbon Geometry** — CPU/GPU hybrid tessellation with per-particle property binding
2. **Hierarchical Parameter Inheritance** — System → Emitter → Module binding without type switching
3. **LOD and Significance-Based Culling** — Multi-criteria budget management
4. **Deferred Rendering for Dynamic Elements** — Decal proxy pattern for CPU↔RT synchronization
5. **Event Routing Without Virtual Dispatch** — Event payload structure + receiver pattern

This analysis extracts patterns UzEngine can **adapt** (not copy) for its C/C++ VFX system.

---

## 1. Ribbon/Trail Rendering System

### 1.1 Tessellation Architecture

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraRendererRibbons.h` (lines 30–103)

**Pattern: Parametric Tessellation Configuration**

```cpp
struct FNiagaraRibbonGenerationConfig
{
    // Feature flags: bitmask-based, computed once at init
    uint32 bHasFullRibbonIDs : 1;
    uint32 bHasSimpleRibbonIDs : 1;
    uint32 bHasTwist : 1;
    uint32 bHasCustomU0Data : 1;
    uint32 bHasCustomU1Data : 1;
    uint32 bWantsConstantTessellation : 1;
    uint32 bWantsAutomaticTessellation : 1;
    uint32 bNeedsPreciseMotionVectors : 1;

    // Read-only const values
    const uint32 MaterialParamValidMask;
    const float CurveTension;
    const int32 MaxNumRibbons;
};
```

**Why This Pattern:**
- **Compile-time knowledge**: Bitmasks allow template specialization without runtime branching
- **Immutable configuration**: Once created, the config doesn't change during a frame
- **Zero-cost abstraction**: Queries like `HasTwist()` compile to single bit-test operations
- **Memory efficient**: 9 bits of metadata fit in a single uint32

**UzEngine Adaptation:**
```cpp
// In uz_vfx_ribbon.h
struct uz_ribbon_config_t {
    uint32_t feature_flags;  // Bit 0: has_twist, Bit 1: has_custom_uv, etc.
    float curve_tension;
    int32_t max_ribbons;

    // Inline accessors — compile to single bitwise op
    static inline bool has_twist(const uz_ribbon_config_t* cfg) {
        return (cfg->feature_flags & UZ_RIBBON_FEATURE_TWIST) != 0;
    }
};
```

### 1.2 Ribbon Geometry Generation — CPU vs GPU

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Source/Niagara/Private/NiagaraRendererRibbons.cpp` (lines 300–325)

**Pattern: Dual-Path Buffer Allocation**

```cpp
// GPU allocation path assumes 32-bit indices
void Initialize(FRHICommandListBase& RHICmdList, const uint32 NumElements)
{
    const FRHIBufferCreateDesc CreateDesc =
        FRHIBufferCreateDesc::CreateIndex<uint32>(TEXT("NiagaraRibbonIndexBuffer"), NumElements)
        .AddUsage(EBufferUsageFlags::Static | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource)
        .SetInitialState(ERHIAccess::VertexOrIndexBuffer);

    IndexBufferRHI = RHICmdList.CreateBuffer(CreateDesc);
    UAV = RHICmdList.CreateUnorderedAccessView(...);  // For GPU writes
    SRV = RHICmdList.CreateShaderResourceView(...);   // For reads
}
```

**Why:**
- **Separate allocation paths** for CPU-generated vs GPU-computed indices
- **Dual SRV/UAV** allows GPU compute to write indices while geometry shader reads them
- **Upsize/downsize with hysteresis** (lines 461-471) prevents thrashing

**UzEngine Adaptation:**
```cpp
// In uz_gpu_buffer.h
typedef struct {
    uz_gpu_buffer_t buffer;
    uz_gpu_srv_t    srv;    // Readable by compute/shaders
    uz_gpu_uav_t    uav;    // Writable by compute (may be NULL)
    uint32_t        num_elements;
} uz_ribbon_gpu_buffer_t;

// Allocate with hysteresis
bool uz_ribbon_buffer_allocate(
    uz_rhi_device_t* device,
    uz_ribbon_gpu_buffer_t* out_buf,
    uint32_t required_elements,
    uint32_t max_elements
) {
    static const float UPSIZE_MULTIPLIER = 1.1f;
    static const float DOWNSIZE_MULTIPLIER = 1.2f;

    uint32_t current = out_buf->num_elements;
    if (current < required_elements ||
        current > (uint32_t)(required_elements * DOWNSIZE_MULTIPLIER)) {

        uint32_t new_count = (uint32_t)(required_elements * UPSIZE_MULTIPLIER);
        new_count = UZ_MIN(new_count, max_elements);
        // ... reallocate
        return true;
    }
    return false;
}
```

### 1.3 Multi-Ribbon Sorting & Draw Order

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Public/NiagaraRendererRibbons.h` (lines 119–144)

**Pattern: View-Dependent Draw Direction**

```cpp
struct FRibbonMultiRibbonInfo
{
    FVector StartPos;
    FVector EndPos;
    int32 BaseSegmentDataIndex;
    int32 NumSegmentDataIndices;

    inline bool UseInvertOrder(const FVector& ViewDirection,
                               const FVector& ViewOriginForDistanceCulling,
                               ENiagaraRibbonDrawDirection DrawDirection) const
    {
        const double StartDist = FVector::DotProduct(ViewDirection, StartPos - ViewOriginForDistanceCulling);
        const double EndDist = FVector::DotProduct(ViewDirection, EndPos - ViewOriginForDistanceCulling);

        // Compare view distance: invert order if back-to-front AND start is closer
        return ((StartDist >= EndDist) && DrawDirection == ENiagaraRibbonDrawDirection::BackToFront)
            || ((StartDist < EndDist) && DrawDirection == ENiagaraRibbonDrawDirection::FrontToBack);
    }
};
```

**Why:**
- **Avoids per-frame sorting of large arrays**
- **Single dot product** determines if ribbon should reverse order
- **Works with multi-ribbon batches** — each ribbon can reverse independently

**UzEngine Adaptation:**
```cpp
// In uz_vfx_ribbon.h
typedef enum {
    UZ_RIBBON_DRAW_FRONT_TO_BACK,
    UZ_RIBBON_DRAW_BACK_TO_FRONT
} uz_ribbon_draw_direction_t;

// Quick test: should we reverse this ribbon's segment order?
static inline bool uz_ribbon_should_invert_order(
    const uz_vec3_t* start_pos,
    const uz_vec3_t* end_pos,
    const uz_vec3_t* view_dir,
    uz_ribbon_draw_direction_t draw_dir
) {
    float start_dist = uz_vec3_dot(view_dir, uz_vec3_sub(start_pos, view_origin));
    float end_dist = uz_vec3_dot(view_dir, uz_vec3_sub(end_pos, view_origin));

    return (draw_dir == UZ_RIBBON_DRAW_BACK_TO_FRONT && start_dist >= end_dist) ||
           (draw_dir == UZ_RIBBON_DRAW_FRONT_TO_BACK && start_dist < end_dist);
}
```

### 1.4 UV Distribution Modes Without Switch Statements

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Public/NiagaraRibbonRendererProperties.h` (lines 106–117)

**Pattern: Enum-Driven Rendering Configuration**

```cpp
UENUM()
enum class ENiagaraRibbonUVDistributionMode : uint8
{
    ScaledUniformly,                           // Segment-based: all segments same UV range
    ScaledUsingRibbonSegmentLength,            // Length-aware: account for segment lengths
    TiledOverRibbonLength,                     // Tiled: repeat texture along length
    TiledFromStartOverRibbonLength            // Tiled + particle distance attribute
};

USTRUCT()
struct FNiagaraRibbonUVSettings
{
    UPROPERTY(EditAnywhere, Category = UVs)
    ENiagaraRibbonUVDistributionMode DistributionMode;

    UPROPERTY(EditAnywhere, Category = UVs)
    ENiagaraRibbonUVEdgeMode LeadingEdgeMode;   // SmoothTransition vs Locked

    UPROPERTY(EditAnywhere, Category = UVs)
    ENiagaraRibbonUVEdgeMode TrailingEdgeMode;

    UPROPERTY(EditAnywhere, Category = UVs)
    float TilingLength;

    UPROPERTY(EditAnywhere, Category = UVs)
    FVector2D Offset, Scale;
};
```

**Why:**
- **No if/else tree**: CPU passes mode enum to GPU compute shader
- **Data-driven**: Each mode is implemented as GPU shader permutation, not CPU branching
- **Extensible**: New modes only require new shader variant, not C++ changes

**UzEngine Adaptation:**
```cpp
// In uz_ribbon_uv.h
typedef enum {
    UZ_RIBBON_UV_MODE_UNIFORM,
    UZ_RIBBON_UV_MODE_LENGTH_SCALED,
    UZ_RIBBON_UV_MODE_TILED,
    UZ_RIBBON_UV_MODE_TILED_FROM_DISTANCE
} uz_ribbon_uv_mode_t;

typedef struct {
    uz_ribbon_uv_mode_t distribution_mode;
    uz_uv_edge_mode_t leading_edge_mode;   // Smooth vs Locked
    uz_uv_edge_mode_t trailing_edge_mode;
    float tiling_length;
    uz_vec2_t offset, scale;
} uz_ribbon_uv_settings_t;

// CPU just passes the mode to GPU — no branching
// Shader switch on mode to compute UV per vertex
```

---

## 2. Hierarchical Parameter Binding System

### 2.1 Parameter Store Architecture

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Public/NiagaraParameterStore.h` (lines 47–122)

**Pattern: Offset-Based Parameter Copying**

```cpp
struct FNiagaraParameterStoreBinding
{
    struct FParameterBinding
    {
        FParameterBinding(int32 InSrcOffset, int32 InDestOffset, int32 InSize)
            : SrcOffset((uint16)InSrcOffset), DestOffset((uint16)InDestOffset), Size((uint16)InSize)
        {}

        uint16 SrcOffset;   // Where data lives in source store
        uint16 DestOffset;  // Where data lives in dest store
        uint16 Size;        // How many bytes to copy
    };

    TArray<FParameterBinding> ParameterBindings;      // Raw data bindings
    TArray<FInterfaceBinding> InterfaceBindings;      // Data interface refs
    TArray<FUObjectBinding> UObjectBindings;          // UObject refs

    void CopyParameters(FNiagaraParameterStore* Dest,
                       const FNiagaraParameterStore* Src) const
    {
        for (const auto& Binding : ParameterBindings) {
            FMemory::Memcpy(
                (uint8*)Dest + Binding.DestOffset,
                (const uint8*)Src + Binding.SrcOffset,
                Binding.Size
            );
        }
    }
};
```

**Why This Pattern:**
- **Zero virtual calls**: Binding is computed once, then straight memcpy
- **Batching**: Copy multiple unrelated parameters in one loop
- **Inheritance chain**: System params → Emitter params → Instance params
- **Type-agnostic**: Works with any struct layout, not tied to C++ RTTI

**The Problem It Solves:**
- Niagara has **System** (global), **Emitter**, and **Module** scopes
- Without this pattern, you'd need virtual method dispatch per parameter per scope
- With offset binding: one memcpy operation for **all** parameters from one scope

**UzEngine Adaptation:**
```cpp
// In uz_vfx_parameters.h

typedef struct {
    uint16_t src_offset;   // Source buffer byte offset
    uint16_t dst_offset;   // Dest buffer byte offset
    uint16_t size;         // Number of bytes to copy (1-4KB range fits uint16)
} uz_param_binding_t;

typedef struct {
    uz_param_binding_t* param_bindings;
    uint32_t num_bindings;

    // Optional: interface/object ref bindings
    // uz_param_interface_binding_t* interface_bindings;
    // uint32_t num_interface_bindings;
} uz_param_store_binding_t;

// Copy inherited parameters: System → Emitter
void uz_param_copy_inherited(
    uint8_t* dst_buffer,
    const uint8_t* src_buffer,
    const uz_param_store_binding_t* binding
) {
    for (uint32_t i = 0; i < binding->num_bindings; ++i) {
        const uz_param_binding_t* b = &binding->param_bindings[i];
        memcpy(dst_buffer + b->dst_offset,
               src_buffer + b->src_offset,
               b->size);
    }
}
```

**Why UzEngine Should Use This:**
- Lua-scripted emitters will need to inherit system-level CVars (quality, LOD budget)
- This pattern copies those values **once per frame** without virtual dispatch
- Scales to hundreds of parameters without performance cost

### 2.2 Variable Types Without RTTI

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Public/NiagaraTypes.h` (lines 23–92)

**Pattern: Explicit Type Wrappers**

```cpp
// NO use of C++ templates or RTTI. Each type is explicit.

USTRUCT(meta = (DisplayName = "float"))
struct FNiagaraFloat { float Value = 0; };

USTRUCT(meta = (DisplayName = "int32"))
struct FNiagaraInt32 { int32 Value = 0; };

USTRUCT(meta=(DisplayName="bool"))
struct FNiagaraBool {
    enum BoolValues {
        True = INDEX_NONE,  // VM special value: -1 (all bits set)
        False = 0           // All bits clear
    };
    int32 Value = False;
};

USTRUCT(meta = (DisplayName = "Position"))
struct FNiagaraPosition : public FVector3f {};

// Matrices, half-floats, etc. — all explicit
USTRUCT(meta = (DisplayName = "Matrix"))
struct FNiagaraMatrix {
    FVector4f Row0, Row1, Row2, Row3;
};
```

**Why:**
- **VM compatibility**: The Niagara VM isn't C++, so types can't use C++ inheritance
- **Byte layout control**: Each type's layout is explicitly defined for GPU/VM
- **Special semantics**: `FNiagaraBool::True` is `INDEX_NONE` (not just `1`) for bitwise ops in VM
- **No surprises**: Padding/alignment is explicit and testable

**UzEngine Adaptation:**
```cpp
// In uz_vfx_types.h

// Explicit POD types for Lua VFX script parameter passing
typedef struct { float x, y, z, w; } uz_vfx_quat_t;
typedef struct { float x, y, z; } uz_vfx_vec3_t;
typedef struct { float m[16]; } uz_vfx_mat4_t;

// Special boolean for particle systems (bitwise ops)
typedef struct {
    int32_t value;  // 0 = false, non-zero = true
} uz_vfx_bool_t;

// Type metadata for parameter store
typedef enum {
    UZ_VFX_TYPE_FLOAT,
    UZ_VFX_TYPE_INT32,
    UZ_VFX_TYPE_BOOL,
    UZ_VFX_TYPE_VEC3,
    UZ_VFX_TYPE_QUAT,
    UZ_VFX_TYPE_MAT4,
} uz_vfx_type_id_t;

typedef struct {
    uz_vfx_type_id_t type_id;
    uint16_t size_bytes;
    uint16_t alignment;
} uz_vfx_type_metadata_t;
```

---

## 3. Event System Without Type Switching

### 3.1 Event Receiver Pattern

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Classes/NiagaraEvents.h` (lines 45–68)

**Pattern: Virtual Base for Type-Safe Event Handling**

```cpp
UCLASS(abstract)
class UNiagaraEventReceiverEmitterAction : public UObject
{
    GENERATED_BODY()
public:
    virtual void PerformAction(FNiagaraEmitterInstance& OwningSim,
                               const FNiagaraEventReceiverProperties& OwningEventReceiver) {}
};

UCLASS(EditInlineNew)
class UNiagaraEventReceiverEmitterAction_SpawnParticles : public UNiagaraEventReceiverEmitterAction
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category = "Spawn")
    uint32 NumParticles;

    virtual void PerformAction(FNiagaraEmitterInstance& OwningSim,
                               const FNiagaraEventReceiverProperties& OwningEventReceiver) override;
};
```

**Event Payload:**
```cpp
USTRUCT()
struct FNiagaraCollisionEventPayload
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY()
    FVector CollisionPos;
    UPROPERTY()
    FVector CollisionNormal;
    UPROPERTY()
    FVector CollisionVelocity;
    UPROPERTY()
    int32 ParticleIndex;
    UPROPERTY()
    int32 PhysicalMaterialIndex;
};
```

**Why:**
- **No magic numbers**: Event types are known at compile-time via UClass
- **Payload is POD struct**: Easy to serialize, pass on-GPU, etc.
- **Receiver actions are pluggable**: New event handlers are new UClass subclasses, not C++ edits
- **Built-in **: Uses Unreal's reflection system for serialization

**UzEngine Adaptation:**
```cpp
// In uz_vfx_events.h

// Event type enumeration
typedef enum {
    UZ_VFX_EVENT_SPAWN,
    UZ_VFX_EVENT_DEATH,
    UZ_VFX_EVENT_COLLISION,
    UZ_VFX_EVENT_CUSTOM_0,  // User-defined
    UZ_VFX_EVENT_CUSTOM_1,
    UZ_VFX_EVENT_COUNT
} uz_vfx_event_type_t;

// Collision event payload (POD)
typedef struct {
    uz_vec3_t pos;
    uz_vec3_t normal;
    uz_vec3_t velocity;
    int32_t particle_id;
    int32_t material_index;
} uz_vfx_collision_event_t;

// Custom event payload (user-defined bytes)
typedef struct {
    uint8_t data[128];  // Arbitrary user data
    uint32_t size;
} uz_vfx_custom_event_t;

// Event routing callback
typedef void (*uz_vfx_event_receiver_fn)(
    uz_vfx_event_type_t event_type,
    const void* payload,
    uint32_t payload_size,
    void* user_data
);

typedef struct {
    uz_vfx_event_receiver_fn on_event;
    void* user_data;
    uint32_t event_mask;  // Bitmask of which events to listen for
} uz_vfx_event_receiver_t;

// Emit an event to all registered receivers
void uz_vfx_emit_event(
    uz_vfx_emitter_t* emitter,
    uz_vfx_event_type_t event_type,
    const void* payload,
    uint32_t payload_size
) {
    for (uint32_t i = 0; i < emitter->num_receivers; ++i) {
        uz_vfx_event_receiver_t* rcv = &emitter->receivers[i];
        if ((rcv->event_mask & (1 << event_type)) != 0) {
            rcv->on_event(event_type, payload, payload_size, rcv->user_data);
        }
    }
}
```

**Why This Beats Virtual Dispatch:**
- Events are **batched** at end-of-frame, not per-particle
- Payload is passed by reference, not unpacked into virtual method params
- Lua callbacks can be registered without C++ recompilation
- Collision events can skip receivers if emitter isn't listening

---

## 4. Scalability & Significance System

### 4.1 Multi-Criteria Culling

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Public/NiagaraScalabilityState.h` (lines 10–78)

**Pattern: Bitfield-Based Culling Reason Tracking**

```cpp
USTRUCT()
struct FNiagaraScalabilityState
{
    UPROPERTY(VisibleAnywhere, Category="Scalability")
    float Significance;            // 0.0–1.0: importance relative to other systems

    UPROPERTY(VisibleAnywhere, Category = "Scalability")
    float LastVisibleTime;         // When was it last rendered?

    uint8 bNewlyRegistered : 1;    // Just added to world?
    uint8 bNewlyRegisteredDirty : 1;
    uint8 bCulled : 1;             // Overall culled state
    uint8 bPreviousCulled : 1;     // Previous frame's state (for transitions)

    // Per-reason culling (for debugging)
    uint8 bCulledByDistance : 1;       // Too far away
    uint8 bCulledByInstanceCount : 1;  // Too many particles globally
    uint8 bCulledByVisibility : 1;     // Behind camera
    uint8 bCulledByGlobalBudget : 1;   // Out of GPU/CPU budget

    bool IsDirty() const { return bCulled != bPreviousCulled; }
    void Apply() { bPreviousCulled = bCulled; }
};
```

**Why:**
- **Diagnostic tracking**: Each culling reason is separate, so developers can debug which system culled a VFX
- **Transition detection**: `IsDirty()` tells renderer if culling state changed, triggering fade-in/fade-out
- **Bitpacked**: All flags fit in ~4 bytes + metadata
- **Scalability queries**: "Show me all systems culled by distance", "by budget", etc.

**UzEngine Adaptation:**
```cpp
// In uz_vfx_scalability.h

typedef enum {
    UZ_VFX_CULL_DISTANCE = 1 << 0,
    UZ_VFX_CULL_BUDGET = 1 << 1,
    UZ_VFX_CULL_VISIBILITY = 1 << 2,
    UZ_VFX_CULL_LOD = 1 << 3,
    UZ_VFX_CULL_USER = 1 << 4,
} uz_vfx_cull_reason_t;

typedef struct {
    float significance;           // Importance: 0.0–1.0
    double last_visible_time;    // For fade-out timing

    uint32_t culled : 1;
    uint32_t prev_culled : 1;
    uint32_t cull_reasons;       // OR of uz_vfx_cull_reason_t flags

    // Metadata
    int32_t distance_to_camera;
    float budget_fraction;       // 0.0–1.0: how much of budget is this using?
} uz_vfx_scalability_state_t;

// Check if culling state changed (for fade transitions)
static inline bool uz_vfx_culling_dirty(const uz_vfx_scalability_state_t* state) {
    return state->culled != state->prev_culled;
}

// Update culled state based on multiple criteria
void uz_vfx_update_culling(
    uz_vfx_scalability_state_t* state,
    float max_distance,
    float distance_to_camera,
    float global_budget_available
) {
    state->distance_to_camera = (int32_t)distance_to_camera;

    // Multi-criteria OR: culled if ANY criterion says so
    state->cull_reasons = 0;
    if (distance_to_camera > max_distance) {
        state->cull_reasons |= UZ_VFX_CULL_DISTANCE;
    }
    if (global_budget_available <= 0.0f) {
        state->cull_reasons |= UZ_VFX_CULL_BUDGET;
    }

    state->culled = (state->cull_reasons != 0);
}
```

### 4.2 Significance Manager — Budget Allocation

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Plugins/FX/Niagara/Public/NiagaraScalabilityManager.h` (lines 15–111)

**Pattern: Tiered Significance Evaluation**

```cpp
struct FComponentIterationContext
{
    TArray<int32> SignificanceIndices;          // Sorted by importance
    TBitArray<> ComponentRequiresUpdate;        // Which systems need culling check?

    int32 MaxUpdateCount = 0;                   // Max to evaluate per frame
    float WorstGlobalBudgetUse = 0.0f;          // Highest budget fraction seen

    bool bNewOnly = false;                      // Only evaluate newly-registered systems?
    bool bProcessAllComponents = false;         // Force full pass?
    bool bHasDirtyState = false;                // Did any system change culling?
    bool bRequiresGlobalSignificancePass = false; // Need full significance recalc?
};

struct FNiagaraScalabilitySystemData
{
    uint16 InstanceCount = 0;         // How many active instances of this system?
    uint16 CullProxyCount = 0;        // How many proxy instances?

    uint16 bNeedsSignificanceForActiveOrDirty : 1;  // Active systems need reeval?
    uint16 bNeedsSignificanceForCulled : 1;         // Even culled ones?
};
```

**Why:**
- **Hierarchical update**: Only recalculate significance if something changed
- **Budget-aware**: Tracks worst-case budget usage across all systems
- **Lazy evaluation**: Newly-registered systems can skip full pass on frame 0
- **Per-system metadata**: Cache whether each system type cares about significance

**The Loop (conceptually):**
1. Check newly-registered systems (cheapest)
2. If no dirty state, skip rest
3. Otherwise: re-sort by significance
4. Evaluate distance/visibility culling
5. Apply global budget constraints
6. Transition visibility (fade-in/fade-out)

**UzEngine Adaptation:**
```cpp
// In uz_vfx_significance_manager.h

typedef struct {
    uint32_t system_id;
    float significance;
    int32_t distance_to_camera;
} uz_vfx_ranked_system_t;

typedef struct {
    uz_vfx_ranked_system_t* systems;
    uint32_t num_systems;

    // Budget tracking
    float total_budget_available;
    float budget_consumed;

    // Update strategy
    bool dirty;                    // Did any culling state change?
    bool needs_full_significance;  // Recompute all significances?
    uint32_t max_updates_per_frame;
} uz_vfx_significance_manager_t;

// Evaluate which systems to cull based on significance
void uz_vfx_evaluate_significance(
    uz_vfx_significance_manager_t* mgr,
    const uz_camera_t* camera,
    float time_delta
) {
    // Only do full pass if dirty or forced
    if (!mgr->dirty && !mgr->needs_full_significance) {
        return;  // Early exit: all systems' culling is stable
    }

    // Re-sort systems by significance
    uz_vfx_qsort(mgr->systems, mgr->num_systems,
                 uz_vfx_compare_significance);

    // Allocate budget: start from most significant
    mgr->budget_consumed = 0.0f;
    for (uint32_t i = 0; i < mgr->num_systems; ++i) {
        uz_vfx_ranked_system_t* sys = &mgr->systems[i];

        float cost = uz_vfx_estimate_cost(sys->system_id);
        if (mgr->budget_consumed + cost > mgr->total_budget_available) {
            // Out of budget: cull this and all lower-significance systems
            uz_vfx_cull_system(sys->system_id, UZ_VFX_CULL_BUDGET);
        } else {
            mgr->budget_consumed += cost;
            uz_vfx_uncull_system(sys->system_id);
        }
    }

    mgr->dirty = false;
}
```

**Key Insight:**
- **Significance isn't just distance**: It's a blended score (distance + player-facing + size)
- **Budget is global**: All systems compete for GPU/CPU cycles
- **Feedback loop**: Systems that exceeded budget on previous frame are culled first

---

## 5. Deferred Rendering for Dynamic VFX (Decals as Reference)

### 5.1 Scene Proxy Pattern for Thread-Safe Updates

**File**: `U:/UnrealGITHUB/UnrealEngine/Engine/Source/Runtime/Engine/Public/SceneProxies/DeferredDecalProxy.h` (lines 14–76)

**Pattern: CPU→RT Synchronization via Immutable Proxy**

```cpp
class FDeferredDecalProxy
{
public:
    // Constructor runs on game thread, captures component state
    FDeferredDecalProxy(const UDecalComponent* InComponent);

    // Updates are batched and applied on render thread
    void SetTransformIncludingDecalSize(
        const FTransform& InComponentToWorldIncludingDecalSize,
        const FBoxSphereBounds& InBounds
    );

    void InitializeFadingParameters(
        float AbsSpawnTime,
        float FadeDuration,
        float FadeStartDelay,
        float FadeInDuration,
        float FadeInStartDelay
    );

    // Queries (read-only, safe on render thread)
    bool IsShown(const FSceneView* View) const;
    inline const FBoxSphereBounds& GetBounds() const { return Bounds; }

private:
    // Cached game thread data
    const USceneComponent* Component;
    UMaterialInterface* DecalMaterial;
    FTransform ComponentTrans;
    FBoxSphereBounds Bounds;

    // Culling/fading
    int32 SortOrder;
    float InvFadeDuration;
    float FadeStartDelayNormalized;
    float FadeScreenSize;
    FLinearColor DecalColor;
};
```

**Why:**
- **No virtual calls on RT**: All methods are simple getters or math
- **Decoupled lifetime**: Proxy can outlive component; they sync via command buffer
- **Batched updates**: Multiple decals' transforms are updated together
- **Safe to parallelize**: Different decals' proxies don't share mutable state

**UzEngine Adaptation (for dynamic VFX like trails/ribbons):**

```cpp
// In uz_vfx_render_proxy.h

typedef struct {
    uz_vec3_t position;
    uz_quat_t rotation;
    uz_vec3_t scale;
} uz_transform_t;

// Scene proxy for a single VFX instance (trails, ribbons, etc.)
typedef struct {
    // Game-thread-captured state
    uint32_t vfx_system_id;
    uz_transform_t world_transform;
    uz_bounding_sphere_t bounds;

    // Rendering parameters
    uz_material_handle_t material;
    uz_vec4_t tint_color;
    float opacity;

    // Lifecycle/visibility
    uint32_t sort_order;
    float spawn_time;
    float fade_duration;
    float fade_start_delay;
} uz_vfx_render_proxy_t;

// Updates are queued on GT, applied on RT
typedef struct {
    uint32_t proxy_id;
    uz_transform_t new_transform;
    uz_bounding_sphere_t new_bounds;
    float new_opacity;
    uint32_t operation;  // ENQUEUE, UPDATE, REMOVE
} uz_vfx_proxy_update_t;

// Batched update for render thread
void uz_vfx_apply_proxy_updates(
    uz_vfx_render_proxy_t* proxies,
    const uz_vfx_proxy_update_t* updates,
    uint32_t num_updates
) {
    for (uint32_t i = 0; i < num_updates; ++i) {
        const uz_vfx_proxy_update_t* upd = &updates[i];
        uz_vfx_render_proxy_t* proxy = &proxies[upd->proxy_id];

        proxy->world_transform = upd->new_transform;
        proxy->bounds = upd->new_bounds;
        proxy->opacity = upd->new_opacity;
    }
}
```

---

## 6. Architectural Recommendations for UzEngine

### 6.1 Ribbon/Trail VFX System

**Adopt:**
1. **Bitmask-based feature configuration** (instead of if-else trees)
   - Compile-time feature selection with zero runtime cost
   - Template specialization for GPU shaders

2. **Dual CPU/GPU geometry paths**
   - Small ribbons: CPU-computed (instant feedback in editor)
   - Large ribbons: GPU-computed (scales to thousands)
   - Use hysteresis to prevent thrashing

3. **View-dependent draw sorting**
   - Single dot product per ribbon (not sorting entire array)
   - Front-to-back and back-to-front modes

4. **Per-particle UV overrides**
   - Bind particle attributes to UV modes (no shader permutations)
   - Supports tiling, stretching, custom coordinates

### 6.2 Parameter System

**Adopt:**
1. **Offset-based parameter binding**
   - Memcpy entire inheritance chains (System → Emitter → Module)
   - No virtual dispatch, no type checking

2. **Explicit POD types**
   - Not C++ templates: define types explicitly (Vec3, Mat4, Bool, etc.)
   - Works with Lua, GPU, serialization

3. **Per-emitter parameter store**
   - Emitter defines its "interface" upfront
   - System binds values once at init, then memcpy each frame

### 6.3 Scalability

**Adopt:**
1. **Bitfield culling reasons**
   - Track why each system is culled (for debugging)
   - Transitions (fade-in/fade-out) triggered by culling state change

2. **Significance-based budget allocation**
   - Most significant systems get budget first
   - Lazy evaluation: recompute only if dirty

3. **Per-system metadata caching**
   - Cache "this system needs significance check" to skip cheap systems

### 6.4 Event System

**Adopt:**
1. **POD event payloads**
   - Collision, spawn-on-death, custom events are just data structs
   - No virtual dispatch at emission time

2. **Receiver callback registration**
   - Lua scripts register event handlers without C++ changes
   - Batched at end-of-frame, not per-particle

3. **Event type enumeration**
   - Built-in: spawn, death, collision
   - User-defined: custom event 0-N with arbitrary payloads

---

## 7. What NOT to Copy (Anti-Patterns)

### 7.1 Avoid: UObject Overhead

**Niagara does:**
```cpp
UCLASS()
class UNiagaraRibbonRendererProperties : public UNiagaraRendererProperties
{
    UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
    TObjectPtr<UMaterialInterface> Material;
    // ...
};
```

**Why UzEngine shouldn't:**
- UzEngine targets high-frequency updates (60/120 FPS)
- Niagara's properties are editor-only, serialized once per asset
- UObject reflection adds runtime cost

**Instead:**
- Use plain C structs for runtime config
- Serialize to TOML/JSON for asset storage
- Load once at init, never GC'd

### 7.2 Avoid: Dynamic Allocation in Hot Paths

**Niagara sometimes does:**
```cpp
TArray<uint32> SegmentData;      // Reallocates on ribbon expand
TArray<FVector4f> TangentAndDistances;  // Per ribbon
```

**Why it's fine for Niagara:**
- Ribbons are artist-authored, static count
- Allocation happens once per emitter at init

**Why UzEngine should avoid:**
- Particle count is dynamic (spawn/death events)
- Use **arena allocators** or pre-allocated pools
- Reallocations stall the simulation thread

---

## 8. Implementation Checklist for UzEngine

- [ ] **Ribbon system**
  - [ ] Bitmask-based feature flags (twist, custom UV, multi-ribbon)
  - [ ] CPU geometry generation with hysteresis buffer resize
  - [ ] GPU compute path for large ribbons
  - [ ] View-dependent draw order (dot product test)

- [ ] **Parameter system**
  - [ ] Offset-based binding cache (computed once at init)
  - [ ] Memcpy-based parameter inheritance
  - [ ] Lua parameter exposure (via POD struct marshaling)

- [ ] **Scalability**
  - [ ] Significance tracker per VFX instance
  - [ ] Bitfield culling reasons
  - [ ] Budget-aware significance sorting
  - [ ] Fade-in/fade-out on culling state change

- [ ] **Event system**
  - [ ] Collision event payload (POD struct)
  - [ ] Spawn-on-death event receiver
  - [ ] Custom event callbacks (Lua)
  - [ ] End-of-frame event batching

- [ ] **Deferred rendering proxy**
  - [ ] Proxy object for CPU↔RT sync
  - [ ] Batched update commands
  - [ ] Sort order and fade timing

---

## Summary

Niagara's strength is **decoupling concern domains** without runtime cost:
- **Rendering** (ribbon shapes, tessellation) doesn't know about **simulation** (parameter updates)
- **Scalability** (culling) doesn't know about **rendering** (materials, shaders)
- **Events** (spawn, collision) don't require type switching at emission time

UzEngine should adopt this philosophy:
1. Use **configuration objects** (enums, bitflags) instead of virtual dispatch
2. Use **offset-based binding** for parameter inheritance
3. Use **POD structs** for event payloads and parameter types
4. Use **arena allocation** to avoid dynamic reallocations in hot loops
5. Use **bitfields** for culling diagnostics and state tracking

This keeps the VFX system **fast, debuggable, and extensible** without C++ template complexity or reflection overhead.
