# Niagara Architectural Patterns — Ready-to-Implement Code

This document provides concrete C/C++ code templates that UzEngine can adapt from Niagara analysis.

---

## Pattern 1: Bitmask-Based Feature Configuration

**Source**: `NiagaraRendererRibbons.h` lines 52–103

**Problem**: Ribbon rendering has many optional features (twist, custom UV, multi-ribbon). Without bitmasks, you end up with multiple `if` statements that compile to branch mispredictions.

**Niagara Solution**: Encode all features in one `uint32` bitfield, computed once at init.

**UzEngine Implementation**:

```cpp
// File: src/engine/vfx/ribbon_config.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Feature flags — each bit represents one feature
#define UZ_RIBBON_FEATURE_TWIST              0x01
#define UZ_RIBBON_FEATURE_CUSTOM_U0_DATA     0x02
#define UZ_RIBBON_FEATURE_CUSTOM_U1_DATA     0x04
#define UZ_RIBBON_FEATURE_MULTI_RIBBON_IDS   0x08
#define UZ_RIBBON_FEATURE_FULL_RIBBON_IDS    0x10

/// Configuration for ribbon generation. Set once at init, read-only after.
///
/// Uses bitmask-based feature detection instead of virtual dispatch.
/// This allows GPU shader specialization and CPU code generation (templates).
typedef struct {
    // Feature flags: checked with bitwise AND
    uint32_t feature_flags;

    // Rendering parameters (only valid if corresponding feature is set)
    float curve_tension;           // Valid if any tessellation feature set
    int32_t max_num_ribbons;       // Valid if multi-ribbon feature set

    // Shape configuration
    uint8_t shape_mode;            // UZ_RIBBON_SHAPE_PLANE, MULTIPLANE, TUBE, CUSTOM
    int32_t tessellation_factor;   // Max subdivision factor
} uz_ribbon_generation_config_t;

// Inline accessor functions — compile to single bitwise operation
static inline bool uz_ribbon_has_twist(const uz_ribbon_generation_config_t* cfg) {
    return (cfg->feature_flags & UZ_RIBBON_FEATURE_TWIST) != 0;
}

static inline bool uz_ribbon_has_custom_u0_data(const uz_ribbon_generation_config_t* cfg) {
    return (cfg->feature_flags & UZ_RIBBON_FEATURE_CUSTOM_U0_DATA) != 0;
}

static inline bool uz_ribbon_has_custom_u1_data(const uz_ribbon_generation_config_t* cfg) {
    return (cfg->feature_flags & UZ_RIBBON_FEATURE_CUSTOM_U1_DATA) != 0;
}

static inline bool uz_ribbon_has_multi_ribbon_ids(const uz_ribbon_generation_config_t* cfg) {
    return (cfg->feature_flags & UZ_RIBBON_FEATURE_MULTI_RIBBON_IDS) != 0;
}

static inline bool uz_ribbon_has_full_ribbon_ids(const uz_ribbon_generation_config_t* cfg) {
    return (cfg->feature_flags & UZ_RIBBON_FEATURE_FULL_RIBBON_IDS) != 0;
}

/// Create a ribbon config from renderer properties.
/// This is the ONLY place feature flags are computed.
uz_ribbon_generation_config_t uz_ribbon_config_create(
    const uz_ribbon_renderer_properties_t* props
);

#endif // UZ_RIBBON_CONFIG_H


// File: src/engine/vfx/ribbon_config.c

#include "ribbon_config.h"

uz_ribbon_generation_config_t uz_ribbon_config_create(
    const uz_ribbon_renderer_properties_t* props
) {
    uz_ribbon_generation_config_t cfg = {0};

    // Set feature flags based on what's actually bound
    if (props->twist_accessor.is_valid && props->size_accessor.is_valid) {
        cfg.feature_flags |= UZ_RIBBON_FEATURE_TWIST;
    }

    if (props->uv0_settings.enable_per_particle_u_override) {
        cfg.feature_flags |= UZ_RIBBON_FEATURE_CUSTOM_U0_DATA;
    }

    if (props->uv1_settings.enable_per_particle_u_override) {
        cfg.feature_flags |= UZ_RIBBON_FEATURE_CUSTOM_U1_DATA;
    }

    if (props->ribbon_id_accessor.is_valid) {
        cfg.feature_flags |= UZ_RIBBON_FEATURE_MULTI_RIBBON_IDS;
        if (props->use_full_ribbon_ids) {
            cfg.feature_flags |= UZ_RIBBON_FEATURE_FULL_RIBBON_IDS;
        }
    }

    cfg.curve_tension = props->curve_tension;
    cfg.max_num_ribbons = props->max_num_ribbons;
    cfg.shape_mode = props->shape_mode;
    cfg.tessellation_factor = props->tessellation_factor;

    return cfg;
}
```

**Usage in GPU shader generation:**
```glsl
// Shader code is generated based on feature flags
// This happens ONCE at init, not per-frame

#define HAS_TWIST {{ .feature_flags & UZ_RIBBON_FEATURE_TWIST ? 1 : 0 }}
#define HAS_CUSTOM_UV {{ .feature_flags & UZ_RIBBON_FEATURE_CUSTOM_U0_DATA ? 1 : 0 }}

// Now shader code uses these as compile-time constants
#if HAS_TWIST
    // Twist-specific vertex calculations
    float twist = u_twisted_width[particle_id];
#endif

#if HAS_CUSTOM_UV
    // Custom UV calculations
    float u = u_custom_u0[particle_id];
#else
    // Standard UV calculation
    float u = normalized_age * uv_scale + uv_offset;
#endif
```

---

## Pattern 2: Offset-Based Parameter Binding

**Source**: `NiagaraParameterStore.h` lines 47–122

**Problem**: Niagara has System, Emitter, and Module scopes. Parameters flow down the hierarchy. Without this pattern, you'd need virtual dispatch per parameter per scope.

**Niagara Solution**: Pre-compute byte offsets at init, then use memcpy to propagate values.

**UzEngine Implementation**:

```cpp
// File: src/engine/vfx/param_store.h

#pragma once

#include <stdint.h>
#include <string.h>

/// A single parameter binding: copy N bytes from src offset to dst offset
typedef struct {
    uint16_t src_offset;   // Source buffer byte offset
    uint16_t dst_offset;   // Dest buffer byte offset
    uint16_t size_bytes;   // Number of bytes to copy (0–65535)
} uz_param_binding_t;

/// Collection of bindings from one store to another
/// Typical use: System params → Emitter instance params
typedef struct {
    uz_param_binding_t* bindings;
    uint32_t num_bindings;
} uz_param_store_binding_t;

/// Parameter storage buffer (opaque byte array)
typedef struct {
    uint8_t* data;
    uint32_t capacity_bytes;
    uint32_t size_bytes;
} uz_param_store_t;

/// Initialize a parameter store with a fixed size
uz_param_store_t uz_param_store_create(uint32_t capacity_bytes);

/// Release a parameter store
void uz_param_store_destroy(uz_param_store_t* store);

/// Set a parameter value in the store
/// type_size must match the parameter's declared type
void uz_param_store_set(
    uz_param_store_t* store,
    uint32_t param_offset,
    const void* value,
    uint32_t type_size
);

/// Get a parameter value from the store
void uz_param_store_get(
    const uz_param_store_t* store,
    uint32_t param_offset,
    void* out_value,
    uint32_t type_size
);

/// Copy parameters according to binding list
/// This is the HOT PATH: called every frame
static inline void uz_param_copy_inherited(
    uz_param_store_t* dst,
    const uz_param_store_t* src,
    const uz_param_store_binding_t* binding
) {
    for (uint32_t i = 0; i < binding->num_bindings; ++i) {
        const uz_param_binding_t* b = &binding->bindings[i];
        memcpy(
            dst->data + b->dst_offset,
            src->data + b->src_offset,
            b->size_bytes
        );
    }
}

/// Create a binding between two stores (done once at init)
/// Returns success; fails if offsets are invalid
bool uz_param_binding_create(
    uz_param_store_t* dst,
    const uz_param_store_t* src,
    uz_param_store_binding_t* out_binding,
    const uz_param_binding_descriptor_t* descriptors,
    uint32_t num_descriptors
);

#endif // UZ_PARAM_STORE_H


// File: src/engine/vfx/param_store.c

#include "param_store.h"
#include "../core/memory.h"
#include "../core/logging.h"

uz_param_store_t uz_param_store_create(uint32_t capacity_bytes) {
    uz_param_store_t store = {0};
    store.capacity_bytes = capacity_bytes;
    store.size_bytes = 0;

    if (capacity_bytes > 0) {
        store.data = uz_malloc(capacity_bytes, "param_store");
        memset(store.data, 0, capacity_bytes);
    }

    return store;
}

void uz_param_store_destroy(uz_param_store_t* store) {
    if (store->data) {
        uz_free(store->data);
        store->data = NULL;
    }
    store->capacity_bytes = 0;
    store->size_bytes = 0;
}

void uz_param_store_set(
    uz_param_store_t* store,
    uint32_t param_offset,
    const void* value,
    uint32_t type_size
) {
    UZ_CHECK(param_offset + type_size <= store->capacity_bytes,
             "Parameter offset out of bounds: offset={}, size={}, capacity={}",
             param_offset, type_size, store->capacity_bytes);

    memcpy(store->data + param_offset, value, type_size);

    if (param_offset + type_size > store->size_bytes) {
        store->size_bytes = param_offset + type_size;
    }
}

void uz_param_store_get(
    const uz_param_store_t* store,
    uint32_t param_offset,
    void* out_value,
    uint32_t type_size
) {
    UZ_CHECK(param_offset + type_size <= store->size_bytes,
             "Parameter not initialized: offset={}, size={}, initialized_to={}",
             param_offset, type_size, store->size_bytes);

    memcpy(out_value, store->data + param_offset, type_size);
}

bool uz_param_binding_create(
    uz_param_store_t* dst,
    const uz_param_store_t* src,
    uz_param_store_binding_t* out_binding,
    const uz_param_binding_descriptor_t* descriptors,
    uint32_t num_descriptors
) {
    // Allocate bindings array
    uz_param_binding_t* bindings = uz_malloc(
        num_descriptors * sizeof(uz_param_binding_t),
        "param_bindings"
    );

    for (uint32_t i = 0; i < num_descriptors; ++i) {
        const uz_param_binding_descriptor_t* desc = &descriptors[i];
        uz_param_binding_t* binding = &bindings[i];

        // Validate offsets
        if (desc->src_offset + desc->size_bytes > src->size_bytes) {
            UZ_LOG(VFX, Error,
                   "Source parameter out of range: src_offset={}, size={}, src_buffer_size={}",
                   desc->src_offset, desc->size_bytes, src->size_bytes);
            uz_free(bindings);
            return false;
        }

        if (desc->dst_offset + desc->size_bytes > dst->capacity_bytes) {
            UZ_LOG(VFX, Error,
                   "Destination parameter out of range: dst_offset={}, size={}, dst_capacity={}",
                   desc->dst_offset, desc->size_bytes, dst->capacity_bytes);
            uz_free(bindings);
            return false;
        }

        binding->src_offset = (uint16_t)desc->src_offset;
        binding->dst_offset = (uint16_t)desc->dst_offset;
        binding->size_bytes = (uint16_t)desc->size_bytes;
    }

    out_binding->bindings = bindings;
    out_binding->num_bindings = num_descriptors;

    return true;
}
```

**Usage Example: System → Emitter Inheritance**:

```cpp
// At startup: define parameter layout
typedef struct {
    float quality_scale;        // Offset 0, size 4
    float lod_distance;         // Offset 4, size 4
    float budget_fraction;      // Offset 8, size 4
} uz_system_params_t;            // Total: 12 bytes

typedef struct {
    float quality_scale;        // Offset 0, size 4
    float lod_distance;         // Offset 4, size 4
    float budget_fraction;      // Offset 8, size 4
    float emission_rate;        // Offset 12, size 4
    float max_particles;        // Offset 16, size 4
} uz_emitter_params_t;           // Total: 20 bytes

// Create stores
uz_param_store_t system_store = uz_param_store_create(sizeof(uz_system_params_t));
uz_param_store_t emitter_store = uz_param_store_create(sizeof(uz_emitter_params_t));

// Describe the inheritance: first 3 parameters come from system
uz_param_binding_descriptor_t inheritance[] = {
    { .src_offset = 0,  .dst_offset = 0,  .size_bytes = 4 },  // quality_scale
    { .src_offset = 4,  .dst_offset = 4,  .size_bytes = 4 },  // lod_distance
    { .src_offset = 8,  .dst_offset = 8,  .size_bytes = 4 },  // budget_fraction
};

uz_param_store_binding_t binding;
uz_param_binding_create(&emitter_store, &system_store, &binding,
                        inheritance, 3);

// Each frame: propagate system params to emitter
uz_param_copy_inherited(&emitter_store, &system_store, &binding);

// Now emitter instance has system params automatically
uz_system_params_t* emitter_view = (uz_system_params_t*)emitter_store.data;
UZ_LOG(VFX, Info, "Emitter inherited quality_scale={}", emitter_view->quality_scale);
```

---

## Pattern 3: Culling with Reason Tracking

**Source**: `NiagaraScalabilityState.h` lines 10–78

**Problem**: When a VFX system is culled, the developer has no idea why (distance? budget? visibility?). With reason tracking, debugging is trivial.

**Niagara Solution**: Each culling criterion sets its own bit flag.

**UzEngine Implementation**:

```cpp
// File: src/engine/vfx/scalability.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Culling reason flags
#define UZ_VFX_CULL_DISTANCE   0x01  // Too far from camera
#define UZ_VFX_CULL_BUDGET     0x02  // Out of GPU/CPU budget
#define UZ_VFX_CULL_VISIBILITY 0x04  // Behind camera or off-screen
#define UZ_VFX_CULL_LOD        0x08  // Low detail setting
#define UZ_VFX_CULL_USER       0x10  // User-disabled this system

/// Per-system scalability state
typedef struct {
    // Significance: 0.0 = ignore, 1.0 = highest priority
    float significance;

    // Timestamps
    double last_visible_time;   // When was this system last rendered?

    // Culling state
    uint8_t culled;             // Current frame: is this culled?
    uint8_t prev_culled;        // Previous frame: was this culled?
    uint32_t cull_reasons;      // OR of UZ_VFX_CULL_* flags

    // Debug metrics
    int32_t distance_to_camera;
    float budget_fraction;      // 0.0–1.0: how much budget is this using?
} uz_vfx_scalability_state_t;

/// Check if culling state changed (triggers fade-in/fade-out)
static inline bool uz_vfx_culling_dirty(const uz_vfx_scalability_state_t* state) {
    return state->culled != state->prev_culled;
}

/// Apply state changes (call at end of update)
static inline void uz_vfx_apply_scalability_state(uz_vfx_scalability_state_t* state) {
    state->prev_culled = state->culled;
}

/// Update culled state based on multiple criteria
void uz_vfx_update_culling(
    uz_vfx_scalability_state_t* state,
    float max_distance,
    float distance_to_camera,
    float budget_available,
    float system_cost,
    bool is_visible
);

/// Debug: print culling reasons for a system
void uz_vfx_print_culling_reasons(uint32_t cull_reasons);

#endif // UZ_VFX_SCALABILITY_H


// File: src/engine/vfx/scalability.c

#include "scalability.h"
#include "../core/logging.h"
#include <string.h>

void uz_vfx_update_culling(
    uz_vfx_scalability_state_t* state,
    float max_distance,
    float distance_to_camera,
    float budget_available,
    float system_cost,
    bool is_visible
) {
    state->cull_reasons = 0;
    state->distance_to_camera = (int32_t)distance_to_camera;

    // Distance culling
    if (distance_to_camera > max_distance) {
        state->cull_reasons |= UZ_VFX_CULL_DISTANCE;
    }

    // Budget culling
    if (budget_available < system_cost) {
        state->cull_reasons |= UZ_VFX_CULL_BUDGET;
    }

    // Visibility culling
    if (!is_visible) {
        state->cull_reasons |= UZ_VFX_CULL_VISIBILITY;
    }

    // Update culled state: culled if ANY reason exists
    state->culled = (state->cull_reasons != 0) ? 1 : 0;
    state->budget_fraction = (budget_available > 0.001f) ?
        (system_cost / budget_available) : 1.0f;
}

void uz_vfx_print_culling_reasons(uint32_t cull_reasons) {
    if (cull_reasons == 0) {
        UZ_LOG(VFX, Info, "Not culled");
        return;
    }

    char buffer[256];
    int pos = 0;

    if (cull_reasons & UZ_VFX_CULL_DISTANCE) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "Distance ");
    }
    if (cull_reasons & UZ_VFX_CULL_BUDGET) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "Budget ");
    }
    if (cull_reasons & UZ_VFX_CULL_VISIBILITY) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "Visibility ");
    }
    if (cull_reasons & UZ_VFX_CULL_LOD) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "LOD ");
    }
    if (cull_reasons & UZ_VFX_CULL_USER) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "User ");
    }

    UZ_LOG(VFX, Info, "Culled by: {}", buffer);
}
```

**Usage: Debugging why a system is culled**:

```cpp
// In HUD or debug overlay
for (uint32_t i = 0; i < num_systems; ++i) {
    uz_vfx_scalability_state_t* state = &states[i];

    if (state->culled) {
        UZ_LOG(VFX, Warning,
               "System {} culled (distance={}, budget_frac={})",
               i, state->distance_to_camera, state->budget_fraction);

        uz_vfx_print_culling_reasons(state->cull_reasons);
    }
}

// Output:
// System 0 culled (distance=500, budget_frac=0.8)
// Culled by: Distance Budget
```

---

## Pattern 4: Event Routing Without Virtual Dispatch

**Source**: `NiagaraEvents.h` lines 21–68

**Problem**: Emitters generate spawn/death/collision events. If you use virtual dispatch per event per receiver, that's O(emitters * receivers) virtual calls per frame.

**Niagara Solution**: Events are POD structs. Receivers are registered callbacks. Events are batched and routed at end-of-frame.

**UzEngine Implementation**:

```cpp
// File: src/engine/vfx/events.h

#pragma once

#include <stdint.h>
#include <string.h>

// Built-in event types
typedef enum {
    UZ_VFX_EVENT_SPAWN = 0,      // Particle spawned
    UZ_VFX_EVENT_DEATH = 1,      // Particle died
    UZ_VFX_EVENT_COLLISION = 2,  // Particle collided with geometry
    UZ_VFX_EVENT_CUSTOM_0 = 3,   // User-defined
    UZ_VFX_EVENT_CUSTOM_1 = 4,
    UZ_VFX_EVENT_COUNT = 5
} uz_vfx_event_type_t;

// --- Event Payloads (all POD structs, no pointers) ---

/// Spawn event: a particle was created
typedef struct {
    int32_t particle_id;
    int32_t particle_age_frames;
} uz_vfx_event_spawn_t;

/// Death event: a particle was removed
typedef struct {
    int32_t particle_id;
    int32_t lifetime_frames;
} uz_vfx_event_death_t;

/// Collision event: particle hit something
typedef struct {
    int32_t particle_id;
    float pos_x, pos_y, pos_z;           // World space
    float normal_x, normal_y, normal_z;  // Surface normal
    float velocity_x, velocity_y, velocity_z;
    int32_t material_index;
} uz_vfx_event_collision_t;

/// Custom event: user-defined payload
typedef struct {
    uint8_t data[128];
    uint32_t size_bytes;
} uz_vfx_event_custom_t;

// --- Event Receiver ---

/// Function signature for event receivers
typedef void (*uz_vfx_event_receiver_fn)(
    uz_vfx_event_type_t event_type,
    const void* payload,
    uint32_t payload_size,
    void* user_data
);

/// Registered event receiver
typedef struct {
    uz_vfx_event_receiver_fn callback;
    void* user_data;
    uint32_t event_mask;  // Bitmask of events to listen for
} uz_vfx_event_receiver_t;

// --- Event Batch (sent at end of frame) ---

typedef struct {
    uz_vfx_event_type_t event_type;
    uint32_t num_events;

    // Payload data (array of POD structs, type depends on event_type)
    const void* payload_data;
    uint32_t payload_stride;  // Bytes between events
} uz_vfx_event_batch_t;

/// Register an event receiver
void uz_vfx_register_event_receiver(
    uz_vfx_emitter_t* emitter,
    uz_vfx_event_receiver_fn callback,
    void* user_data,
    uint32_t event_mask
);

/// Unregister an event receiver
void uz_vfx_unregister_event_receiver(
    uz_vfx_emitter_t* emitter,
    uz_vfx_event_receiver_fn callback
);

/// Emit events in batch (called once per frame)
void uz_vfx_emit_events(
    uz_vfx_emitter_t* emitter,
    const uz_vfx_event_batch_t* batches,
    uint32_t num_batches
);

#endif // UZ_VFX_EVENTS_H


// File: src/engine/vfx/events.c

#include "events.h"
#include "../core/memory.h"
#include "../core/logging.h"
#include <stdlib.h>

#define MAX_EVENT_RECEIVERS 16

void uz_vfx_register_event_receiver(
    uz_vfx_emitter_t* emitter,
    uz_vfx_event_receiver_fn callback,
    void* user_data,
    uint32_t event_mask
) {
    UZ_CHECK(emitter->num_receivers < MAX_EVENT_RECEIVERS,
             "Too many event receivers ({}/{})", emitter->num_receivers, MAX_EVENT_RECEIVERS);

    uz_vfx_event_receiver_t* receiver = &emitter->receivers[emitter->num_receivers++];
    receiver->callback = callback;
    receiver->user_data = user_data;
    receiver->event_mask = event_mask;

    UZ_LOG(VFX, Info, "Registered event receiver for mask=0x{:x}", event_mask);
}

void uz_vfx_unregister_event_receiver(
    uz_vfx_emitter_t* emitter,
    uz_vfx_event_receiver_fn callback
) {
    for (uint32_t i = 0; i < emitter->num_receivers; ++i) {
        if (emitter->receivers[i].callback == callback) {
            // Remove by shifting down
            memmove(&emitter->receivers[i],
                    &emitter->receivers[i + 1],
                    (emitter->num_receivers - i - 1) * sizeof(uz_vfx_event_receiver_t));
            emitter->num_receivers--;
            UZ_LOG(VFX, Info, "Unregistered event receiver");
            return;
        }
    }

    UZ_LOG(VFX, Warning, "Event receiver not found");
}

void uz_vfx_emit_events(
    uz_vfx_emitter_t* emitter,
    const uz_vfx_event_batch_t* batches,
    uint32_t num_batches
) {
    // Iterate over all batches
    for (uint32_t batch_idx = 0; batch_idx < num_batches; ++batch_idx) {
        const uz_vfx_event_batch_t* batch = &batches[batch_idx];

        if (batch->num_events == 0) {
            continue;  // Skip empty batches
        }

        // Dispatch to all registered receivers that care about this event type
        uint32_t event_bit = 1 << batch->event_type;

        for (uint32_t recv_idx = 0; recv_idx < emitter->num_receivers; ++recv_idx) {
            uz_vfx_event_receiver_t* receiver = &emitter->receivers[recv_idx];

            // Check if receiver is listening for this event
            if ((receiver->event_mask & event_bit) == 0) {
                continue;  // Skip non-matching receivers
            }

            // Call the receiver for each event in the batch
            // Payload data is a stride array
            const uint8_t* payload_ptr = (const uint8_t*)batch->payload_data;

            for (uint32_t event_idx = 0; event_idx < batch->num_events; ++event_idx) {
                receiver->callback(batch->event_type, payload_ptr,
                                   batch->payload_stride, receiver->user_data);

                payload_ptr += batch->payload_stride;
            }
        }
    }
}
```

**Usage Example: Spawn-on-Death**:

```cpp
// Define a receiver callback
void spawn_on_death_receiver(
    uz_vfx_event_type_t event_type,
    const void* payload,
    uint32_t payload_size,
    void* user_data
) {
    UZ_CHECK(event_type == UZ_VFX_EVENT_DEATH, "Only death events expected");
    UZ_CHECK(payload_size >= sizeof(uz_vfx_event_death_t), "Invalid payload");

    uz_vfx_emitter_t* child_emitter = (uz_vfx_emitter_t*)user_data;
    const uz_vfx_event_death_t* death = (const uz_vfx_event_death_t*)payload;

    // Spawn a particle in child emitter at death location
    uz_vfx_emit_particle(child_emitter, death->lifetime_frames);
}

// Register the receiver
uz_vfx_register_event_receiver(
    parent_emitter,
    spawn_on_death_receiver,
    child_emitter,
    (1 << UZ_VFX_EVENT_DEATH)  // Listen only for death events
);

// At end of frame: batch and emit events
uz_vfx_event_death_t deaths[1024];  // Array of death events
uint32_t num_deaths = 0;

// ... collect death events into deaths[] array ...

uz_vfx_event_batch_t batch = {
    .event_type = UZ_VFX_EVENT_DEATH,
    .num_events = num_deaths,
    .payload_data = deaths,
    .payload_stride = sizeof(uz_vfx_event_death_t)
};

uz_vfx_emit_events(parent_emitter, &batch, 1);
```

---

## Pattern 5: Hysteresis-Based Buffer Allocation

**Source**: `NiagaraRendererRibbons.cpp` lines 452–494

**Problem**: Ribbon particle count varies (burst spawns, deaths). If you reallocate on every size change, you'll thrash the allocator.

**Niagara Solution**: Allocate with hysteresis: upsize at 1.1x, downsize only at 1.2x current.

**UzEngine Implementation**:

```cpp
// File: src/engine/vfx/gpu_buffer.h

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    // RHI buffer (opaque)
    void* rhi_buffer;

    // Views
    void* srv;  // Shader resource view (readable)
    void* uav;  // Unordered access view (writable)

    // Tracking
    uint32_t element_size_bytes;
    uint32_t num_allocated_elements;
    uint32_t num_used_elements;

    // Debug
    const char* debug_name;
} uz_gpu_buffer_t;

/// Allocate or resize a GPU buffer with hysteresis
///
/// Upsizes at 1.1x required size (reduces reallocations during growth)
/// Downsizes only at 1.2x current size (avoids thrashing during shrinking)
/// Returns true if reallocation occurred, false if size already sufficient
bool uz_gpu_buffer_allocate(
    void* rhi_device,
    uz_gpu_buffer_t* buffer,
    uint32_t required_elements,
    uint32_t max_elements,
    uint32_t element_size_bytes
);

/// Release the buffer
void uz_gpu_buffer_release(uz_gpu_buffer_t* buffer);

#endif // UZ_GPU_BUFFER_H


// File: src/engine/vfx/gpu_buffer.c

#include "gpu_buffer.h"
#include "../core/logging.h"
#include <math.h>

#define UPSIZE_MULTIPLIER 1.1f
#define DOWNSIZE_MULTIPLIER 1.2f

bool uz_gpu_buffer_allocate(
    void* rhi_device,
    uz_gpu_buffer_t* buffer,
    uint32_t required_elements,
    uint32_t max_elements,
    uint32_t element_size_bytes
) {
    UZ_CHECK(required_elements <= max_elements,
             "Required elements {} exceeds maximum {}",
             required_elements, max_elements);

    uint32_t current_allocated = buffer->num_allocated_elements;

    // Check if reallocation is needed
    bool needs_realloc = false;

    if (current_allocated < required_elements) {
        // Grow: upsize with multiplier to reduce future reallocations
        needs_realloc = true;
    } else if (current_allocated > (uint32_t)(required_elements * DOWNSIZE_MULTIPLIER)) {
        // Shrink: only downsize if we're significantly over-allocated
        needs_realloc = true;
    } else {
        // Size is OK: no reallocation needed
        buffer->num_used_elements = required_elements;
        return false;
    }

    if (!needs_realloc) {
        buffer->num_used_elements = required_elements;
        return false;
    }

    // Calculate new size
    uint32_t new_size = required_elements;

    if (current_allocated < required_elements) {
        // Growing: upsize by multiplier
        new_size = (uint32_t)ceilf(required_elements * UPSIZE_MULTIPLIER);
    } else {
        // Shrinking: downsize more aggressively
        new_size = (uint32_t)ceilf(required_elements * 1.05f);  // Less aggressive
    }

    // Clamp to max
    new_size = (new_size > max_elements) ? max_elements : new_size;

    uint32_t new_size_bytes = new_size * element_size_bytes;

    UZ_LOG(VFX, Info,
           "GPU buffer '{}' realloc: {} -> {} elements ({} -> {} bytes)",
           buffer->debug_name, current_allocated, new_size,
           current_allocated * element_size_bytes, new_size_bytes);

    // Release old buffer
    if (buffer->rhi_buffer) {
        uz_gpu_buffer_release(buffer);
    }

    // Allocate new buffer via RHI device
    // (This is pseudo-code; real implementation uses uz_rhi_device_t)
    struct {
        void* buffer;
        void* srv;
        void* uav;
    } new_resources = uz_rhi_allocate_gpu_buffer(
        rhi_device,
        new_size_bytes,
        UZ_RHI_BUFFER_FLAG_SHADER_RESOURCE | UZ_RHI_BUFFER_FLAG_UNORDERED_ACCESS
    );

    buffer->rhi_buffer = new_resources.buffer;
    buffer->srv = new_resources.srv;
    buffer->uav = new_resources.uav;
    buffer->num_allocated_elements = new_size;
    buffer->num_used_elements = required_elements;

    return true;  // Reallocation occurred
}

void uz_gpu_buffer_release(uz_gpu_buffer_t* buffer) {
    if (buffer->rhi_buffer) {
        uz_rhi_free_gpu_buffer(buffer->rhi_buffer);
        buffer->rhi_buffer = NULL;
        buffer->srv = NULL;
        buffer->uav = NULL;
    }
    buffer->num_allocated_elements = 0;
    buffer->num_used_elements = 0;
}
```

**Usage: Ribbon vertex buffer management**:

```cpp
// In ribbon renderer update
uz_gpu_buffer_t ribbon_index_buffer = {
    .debug_name = "RibbonIndexBuffer",
    .element_size_bytes = sizeof(uint32_t),  // 32-bit indices
};

// Each frame
uint32_t num_segments = ribbon->num_particles - 1;
uint32_t num_indices = num_segments * 6;  // 2 triangles per segment

bool reallocated = uz_gpu_buffer_allocate(
    rhi_device,
    &ribbon_index_buffer,
    num_indices,
    MAX_RIBBON_INDICES,
    sizeof(uint32_t)
);

if (reallocated) {
    UZ_LOG(VFX, Info, "Ribbon buffer reallocated");
    // May trigger PSO cache invalidation, shader recompilation, etc.
}

// Use the buffer for rendering
uz_rhi_bind_shader_resource(
    compute_cmdlist,
    SLOT_RIBBON_INDEX_BUFFER,
    ribbon_index_buffer.srv
);
```

---

## Summary

These patterns are **directly adapted from Niagara** and ready to integrate into UzEngine:

1. **Bitmask Configuration**: Feature flags at init, zero branches at runtime
2. **Parameter Binding**: Memcpy inheritance chains, no virtual dispatch
3. **Culling Reasons**: Bitfield tracking for debugging
4. **Event Routing**: POD payloads, batched callbacks
5. **Buffer Hysteresis**: Avoid thrashing allocator during growth/shrink

Each pattern solves a **specific performance or maintainability problem** that UzEngine will face at scale (thousands of VFX systems, millions of particles).
