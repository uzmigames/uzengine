# Godot 4 Deep Architectural Analysis: Uncovered Areas

**Document Version**: 1.0
**Date**: 2026-03-04
**Scope**: 8 subsystems not covered in `cross-engine-analysis.md`
**Status**: Active Reference

---

## Table of Contents

1. [GDScript Hot-Reload](#1-gdscript-hot-reload)
2. [GDScript VM Architecture](#2-gdscript-vm-architecture)
3. [AnimationTree / Blend Space](#3-animationtree--blend-space)
4. [Physics Backend (PhysicsServer3D)](#4-physics-backend-physicsserver3d)
5. [ResourceLoader Async + Scene Streaming](#5-resourceloader-async--scene-streaming)
6. [Debugger Protocol](#6-debugger-protocol)
7. [GDExtension Plugin System (Deep Dive)](#7-gdextension-plugin-system-deep-dive)
8. [Theme / UI Architecture](#8-theme--ui-architecture)

---

## 1. GDScript Hot-Reload

### How Godot 4 Solves It

GDScript achieves hot-reload without restarting the scene by separating **script code** (class definition, methods, constants) from **instance state** (member variables, connections). When a `.gd` file changes on disk, the engine replaces the code while preserving instance state.

### Key Classes

- `GDScript` (`modules/gdscript/gdscript.h`) -- The script resource that holds compiled bytecode, method map, property info, and constants.
- `GDScriptInstance` (`modules/gdscript/gdscript.h`) -- Per-object runtime state: member variable values, signal connections. Each `Object` that has a GDScript attached holds one `GDScriptInstance`.
- `GDScriptCache` (`modules/gdscript/gdscript_cache.h`) -- Singleton managing script loading, caching, and dependency tracking.
- `ScriptServer` (`core/object/script_language.h`) -- Language-agnostic script management; dispatches reload to language-specific implementations.

### Core Pattern: State-Preserving Reload

```cpp
// Simplified from modules/gdscript/gdscript.cpp -- GDScript::reload()

Error GDScript::reload(bool p_keep_state) {
    // Phase 1: Save instance state
    // Each GDScriptInstance serializes its member variables
    // into a temporary HashMap<StringName, Variant>

    HashMap<ObjectID, HashMap<StringName, Variant>> saved_state;

    if (p_keep_state) {
        for (PlaceHolderScriptInstance *E : placeholders) {
            // ... save placeholder state
        }
        // Real instances: walk the instances list
        for (GDScriptInstance *inst : instances) {
            HashMap<StringName, Variant> state;
            for (const auto &member : member_info) {
                state[member.key] = inst->members[member.value.index];
            }
            saved_state[inst->owner->get_instance_id()] = state;
        }
    }

    // Phase 2: Recompile the script
    // Parser -> Compiler -> new bytecode, new method map, new member_indices
    GDScriptCompiler compiler;
    Error err = compiler.compile(/* parsed tree */, this, p_keep_state);
    if (err != OK) {
        // Compilation failed -- keep old bytecode, log error
        return err;
    }

    // Phase 3: Restore instance state
    // New member_indices may have shifted (added/removed properties)
    // Only restore members that still exist in the new script version
    if (p_keep_state) {
        for (GDScriptInstance *inst : instances) {
            ObjectID oid = inst->owner->get_instance_id();
            if (saved_state.has(oid)) {
                for (auto &kv : saved_state[oid]) {
                    if (member_indices.has(kv.key)) {
                        // Member still exists in new version
                        int new_idx = member_indices[kv.key].index;
                        inst->members.write[new_idx] = kv.value;
                    }
                    // Members that no longer exist are silently dropped
                    // New members get their default values from the new script
                }
            }
        }
    }

    // Phase 4: Notify
    emit_changed();  // ResourceChanged signal
    // Editor refreshes inspector, scene tree badges

    return OK;
}
```

### Why This Is Effective

1. **No scene restart** -- Objects keep their identity (ObjectID), position, and hierarchy. Only the logic changes.
2. **Graceful degradation** -- If recompilation fails, the old bytecode stays active. The developer sees errors but the game continues running.
3. **Selective restoration** -- Members that were removed from the script are silently dropped; new members get default values. This handles schema evolution naturally.
4. **Dependency cascade** -- `GDScriptCache` tracks which scripts depend on which. A base class reload triggers reload of all derived classes, in dependency order.

### Godot 3 vs. Godot 4

In Godot 3, GDScript reload was simpler because the VM was interpreted and scripts were less cached. Godot 4 added:
- `GDScriptCache` for centralized dependency tracking (prevents reload storms)
- Placeholder instances for editor-mode scripts that haven't been loaded yet
- Typed member validation on restore (Godot 4's typed GDScript catches type mismatches during restore)

### UzEngine Implication

UzEngine's Lua hot-reload (60ms target) should follow this exact three-phase pattern:

1. **Save**: Before `lua_close()` or module re-`dofile()`, serialize each node's Lua instance table into a C++ `HashMap<node_id, HashMap<string, Variant>>` via `on_save_state()` callback.
2. **Reload**: Re-execute the `.lua` file. This creates a new module table (new functions, potentially new/removed fields).
3. **Restore**: Walk saved state, apply only keys that still exist in the new module's `_defaults` table. New keys get defaults.

**Critical lesson**: Godot does NOT recreate objects. UzEngine must NOT destroy/recreate ECS entities during Lua reload. The entity persists; only the script module table attached to it is swapped.

The `game.dll` hot-reload is a coarser version: `on_save_state` / `FreeLibrary` / `LoadLibrary` / `on_restore_state`. The Godot pattern validates that this approach works for thousands of instances simultaneously.

---

## 2. GDScript VM Architecture

### Bytecode Pipeline

```
Source (.gd)
    |
    v
GDScriptTokenizer   -- Lexer: produces token stream
    |
    v
GDScriptParser      -- Recursive descent: produces AST (GDScriptParser::Node tree)
    |                   Resolves types, validates syntax, builds class info
    v
GDScriptAnalyzer    -- Semantic analysis: type inference, constant folding,
    |                   inheritance validation, export variable analysis
    v
GDScriptCompiler    -- AST -> bytecode: produces GDScriptFunction per method
    |                   Each function = array of uint32_t instructions
    v
GDScriptFunction    -- Executable: instruction array + constant pool + stack map
    |                   Stored inside GDScript resource
    v
GDScript VM         -- Stack-based interpreter (GDScriptFunction::call())
                       Register-like optimization for local variables
```

### GDScriptFunction Internals

```cpp
// Simplified from modules/gdscript/gdscript_function.h

class GDScriptFunction {
    StringName name;
    StringName source;

    // Bytecode
    Vector<int> code;          // Instruction stream (opcodes + operands)

    // Constant pool
    Vector<Variant> constants; // Indexed by OPCODE_LOAD_CONSTANT operand

    // Stack info
    int _stack_size;           // Max stack depth needed
    int _argument_count;       // Number of parameters
    int _default_arg_count;    // Parameters with defaults
    Vector<int> default_arg_addrs; // Bytecode offsets for default value evaluation

    // Type info (Godot 4 addition)
    Vector<GDScriptDataType> argument_types;
    GDScriptDataType return_type;

    // Debug info
    Vector<int> line_numbers;     // instruction index -> source line mapping
    Vector<StringName> local_var_names;

    // Execute
    Variant call(GDScriptInstance *p_instance,
                 const Variant **p_args, int p_argcount,
                 Callable::CallError &r_error);
};
```

### VM Execution Model

```cpp
// Highly simplified from gdscript_vm.cpp -- the actual file is ~4000 lines

Variant GDScriptFunction::call(GDScriptInstance *p_instance,
                               const Variant **p_args, int p_argcount,
                               Callable::CallError &r_error) {
    // Stack frame: locals + temporaries + constants are all indexed
    // by "address" (int). Negative = constant pool, positive = stack slot.
    Variant *stack = (Variant *)alloca(_stack_size * sizeof(Variant));

    // Initialize stack slots with Nil
    for (int i = 0; i < _stack_size; i++) {
        memnew_placement(&stack[i], Variant);
    }

    // Copy arguments to stack[0..argcount-1]
    for (int i = 0; i < p_argcount; i++) {
        stack[i] = *p_args[i];
    }

    const int *ip = code.ptr();  // Instruction pointer

    // Main dispatch loop
    while (true) {
        int opcode = *ip++;

        switch (opcode) {
            case OPCODE_ASSIGN: {
                int dst = *ip++;
                int src = *ip++;
                stack[dst] = _get_variant(src, stack, constants);
                break;
            }
            case OPCODE_CALL: {
                int argc = *ip++;
                int method_name_idx = *ip++;
                int base = *ip++;
                int ret = *ip++;
                StringName method = _global_names_ptr[method_name_idx];

                // Variant::call -- polymorphic method dispatch
                Variant::CallError ce;
                stack[base].callp(method, /* args from stack */, argc,
                                  stack[ret], ce);
                break;
            }
            case OPCODE_CALL_BUILTIN: {
                // Direct call to built-in function (math, print, etc.)
                // No StringName lookup -- direct function pointer
                break;
            }
            case OPCODE_RETURN: {
                int src = *ip++;
                retvalue = _get_variant(src, stack, constants);
                goto cleanup;
            }
            case OPCODE_JUMP_IF: {
                int cond = *ip++;
                int target = *ip++;
                if (stack[cond].booleanize()) {
                    ip = code.ptr() + target;
                }
                break;
            }
            // ~80 more opcodes: arithmetic, string ops, array access,
            // dictionary access, await, yield, type checks, etc.
        }
    }

cleanup:
    for (int i = 0; i < _stack_size; i++) {
        stack[i].~Variant();
    }
    return retvalue;
}
```

### GDExtension C ABI Connection

GDExtension is the bridge between C++ modules and the GDScript VM. The interface header (`gdextension_interface.h`) exports approximately 600 function pointers organized by subsystem:

```c
// From godot-headers/gdextension_interface.h (simplified)

typedef struct {
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    const char *version_string;

    // --- Memory ---
    void *(*mem_alloc)(size_t p_bytes);
    void *(*mem_realloc)(void *p_ptr, size_t p_bytes);
    void  (*mem_free)(void *p_ptr);

    // --- String operations (70+ functions) ---
    void (*string_new_with_utf8_chars)(GDExtensionStringPtr r_dest,
                                       const char *p_contents);
    void (*string_to_utf8_chars)(GDExtensionConstStringPtr p_self,
                                  char *r_text, int64_t p_max_len);

    // --- Variant operations (100+ functions) ---
    void (*variant_new_copy)(GDExtensionVariantPtr r_dest,
                             GDExtensionConstVariantPtr p_src);
    void (*variant_call)(GDExtensionVariantPtr p_self,
                         GDExtensionConstStringNamePtr p_method,
                         const GDExtensionConstVariantPtr *p_args,
                         GDExtensionInt p_argument_count,
                         GDExtensionVariantPtr r_return,
                         GDExtensionCallError *r_error);

    // --- Object operations ---
    void *(*object_get_instance_binding)(GDExtensionObjectPtr p_o,
                                         void *p_token,
                                         const GDExtensionInstanceBindingCallbacks *p_callbacks);

    // --- ClassDB registration ---
    void (*classdb_register_extension_class2)(
        GDExtensionClassLibraryPtr p_library,
        GDExtensionConstStringNamePtr p_class_name,
        GDExtensionConstStringNamePtr p_parent_class_name,
        const GDExtensionClassCreationInfo2 *p_extension_info);

    // ... ~600 total function pointers

} GDExtensionInterface;

// Extension entry point -- called once when .gdextension library loads
typedef GDExtensionBool (*GDExtensionInitializationFunction)(
    const GDExtensionInterface *p_interface,  // Engine provides this
    GDExtensionClassLibraryPtr p_library,     // Opaque library handle
    GDExtensionInitialization *r_initialization // Extension fills this
);
```

### Why This Architecture Works

1. **Stack-based VM is simple to implement and debug** -- Each function has a flat stack array. No register allocation complexity. Stack overflow is caught by `_stack_size` limit.
2. **Variant as universal type** -- Everything is a `Variant`, which is a tagged union. Eliminates the need for complex type specialization in the VM. The cost is runtime type checking, but GDScript 4 adds optional static typing that generates type-check opcodes at compile time.
3. **Function pointer table for C ABI** -- Extensions receive a pointer to a struct of function pointers. No C++ headers, no vtables, no name mangling. The engine can add new function pointers at the end without breaking existing extensions.

### Godot 3 vs. Godot 4

- **Godot 3 (GDNative)**: Used `dlopen`/`LoadLibrary` + `dlsym` per function. Each function had to be looked up by name at runtime. Fragile, slow initialization.
- **Godot 4 (GDExtension)**: Single struct of function pointers passed at init. O(1) lookup (struct field offset). ABI versioned by struct size. Extensions compiled against Godot 4.0 work with 4.1, 4.2, etc. because new functions are appended (never inserted).

### UzEngine Implication

The GDExtension function pointer table pattern directly validates UzEngine's `plugin_api_t` and `GameContext` designs:

1. **`GameContext` is structurally identical to `GDExtensionInterface`** -- a flat struct of function pointers passed to `game.dll` at init. This is the correct pattern.
2. **Version field guards ABI compatibility** -- UzEngine's `GAME_CONTEXT_VERSION` maps to GDExtension's `version_major/minor/patch`. Game.dll should check `version >= MINIMUM_REQUIRED` and fail gracefully if not met.
3. **Never pass C++ types across the boundary** -- GDExtension uses `GDExtensionVariantPtr` (opaque pointer) instead of `Variant*`. UzEngine should ensure that `GameContext` never exposes `std::string`, `std::vector`, or `entt::registry*` directly. The current spec exposes `entt_registry*`, which is a coupling risk -- consider wrapping behind function pointers like `ecs_create_entity()`, `ecs_add_component()`.

For the LuaJIT integration, UzEngine benefits from LuaJIT being simpler than GDScript's VM: LuaJIT has its own highly optimized tracing JIT compiler. UzEngine's role is limited to binding registration (metatable setup) and pcall dispatch. The GDScript VM's `Variant::call()` polymorphic dispatch is analogous to Lua's `lua_getfield` + `lua_pcall` pattern.

---

## 3. AnimationTree / Blend Space

### How Godot 4 Solves Animation Blending

Godot 4's `AnimationTree` is a **graph-based animation processing system** where each node is a blend operation. The tree evaluates bottom-up: leaf nodes produce poses, interior nodes blend them, and the root applies the final pose to the skeleton.

### Key Classes

- `AnimationTree` (`scene/animation/animation_tree.h`) -- Node that owns the blend tree and drives evaluation each frame.
- `AnimationNode` -- Abstract base for all blend operations. Key subclasses:
  - `AnimationNodeAnimation` -- Leaf: plays a single `Animation` resource.
  - `AnimationNodeBlendSpace1D` -- 1D blend axis (e.g., speed -> walk/run).
  - `AnimationNodeBlendSpace2D` -- 2D triangulated blend (e.g., direction + speed).
  - `AnimationNodeStateMachine` -- State machine with transitions and conditions.
  - `AnimationNodeBlendTree` -- Container for arbitrary node graphs.
  - `AnimationNodeAdd2` / `AnimationNodeBlend2` -- Binary blend operations.
  - `AnimationNodeOneShot` -- Play-once overlay (e.g., attack on top of locomotion).
  - `AnimationNodeTransition` -- Cross-fade between child inputs with time.

### Core Pattern: Recursive Pose Evaluation

```cpp
// Simplified from scene/animation/animation_tree.h + animation_blend_tree.cpp

// Every AnimationNode implements _process():
// It writes blend weights and time positions into a shared AnimationTree context.

double AnimationNodeBlendSpace2D::_process(double p_time, bool p_seek,
                                            bool p_is_external_seeking) {
    // 1. Get blend parameter from AnimationTree
    Vector2 blend_pos = get_parameter("blend_position");  // e.g., (speed, direction)

    // 2. Find the triangle in the blend space that contains blend_pos
    //    Godot pre-triangulates the blend space points using Delaunay triangulation
    int triangle_idx = -1;
    Vector3 bary;  // barycentric coordinates within the triangle

    for (int i = 0; i < triangles.size(); i++) {
        // Compute barycentric coordinates of blend_pos within triangle[i]
        Vector2 a = points[triangles[i].points[0]].position;
        Vector2 b = points[triangles[i].points[1]].position;
        Vector2 c = points[triangles[i].points[2]].position;

        bary = _compute_barycentric(blend_pos, a, b, c);

        if (bary.x >= 0 && bary.y >= 0 && bary.z >= 0) {
            triangle_idx = i;
            break;
        }
    }

    if (triangle_idx < 0) {
        // Outside all triangles -- snap to nearest edge or point
        _snap_to_nearest(blend_pos, triangle_idx, bary);
    }

    // 3. Blend the three animations at the triangle corners using barycentric weights
    double mind = 0;
    for (int j = 0; j < 3; j++) {
        int point_idx = triangles[triangle_idx].points[j];
        float weight = bary[j];

        // Recursively process child animation nodes with this weight
        double child_time = blend_input(point_idx, p_time, p_seek,
                                        p_is_external_seeking,
                                        weight,    // blend amount
                                        FILTER_IGNORE);
        mind = MAX(mind, child_time);
    }

    return mind;
}
```

### AnimationNodeStateMachine: Transitions and Conditions

```cpp
// Simplified from scene/animation/animation_node_state_machine.cpp

double AnimationNodeStateMachinePlayback::_process(AnimationNodeStateMachine *sm,
                                                     double p_time, bool p_seek) {
    // 1. Check all transitions from current state
    for (int i = 0; i < sm->transitions.size(); i++) {
        TransitionInfo &tr = sm->transitions[i];
        if (tr.from != current_state) continue;

        // Evaluate transition condition
        // Conditions reference AnimationTree parameters (floats, bools)
        bool condition_met = false;

        switch (tr.transition->get_switch_mode()) {
            case AnimationNodeStateMachineTransition::SWITCH_MODE_IMMEDIATE:
                // Transition as soon as condition is true
                condition_met = _evaluate_condition(tr.transition);
                break;
            case AnimationNodeStateMachineTransition::SWITCH_MODE_AT_END:
                // Wait for current animation to finish, then check condition
                condition_met = (current_pos >= current_length) &&
                                _evaluate_condition(tr.transition);
                break;
            case AnimationNodeStateMachineTransition::SWITCH_MODE_SYNC:
                // Sync playback positions during crossfade
                condition_met = _evaluate_condition(tr.transition);
                break;
        }

        if (condition_met) {
            // Start transition: cross-fade over tr.xfade_time
            fading_from = current_state;
            current_state = tr.to;
            fading_time = tr.transition->get_xfade_time();
            fading_pos = 0;
            break;
        }
    }

    // 2. If transitioning, blend old and new states
    if (fading_from != StringName()) {
        float blend = fading_pos / fading_time;
        blend = _apply_xfade_curve(blend, tr.transition->get_xfade_curve());

        // Process both states
        double from_rem = sm->blend_input(state_index[fading_from], p_time,
                                           p_seek, false, 1.0 - blend);
        double to_rem   = sm->blend_input(state_index[current_state], p_time,
                                           p_seek, false, blend);

        fading_pos += p_time;
        if (fading_pos >= fading_time) {
            fading_from = StringName();  // Transition complete
        }
    } else {
        // Process current state only
        sm->blend_input(state_index[current_state], p_time, p_seek, false, 1.0);
    }

    return 0;
}
```

### Root Motion Extraction

```cpp
// Godot 4 extracts root motion from the "root_motion_track" (user-specified bone)

// AnimationTree::_process_root_motion() -- called after tree evaluation
// Computes delta position/rotation from the root bone track
// Stores in root_motion_position, root_motion_rotation, root_motion_scale

// Game code retrieves:
Vector3 motion = anim_tree->get_root_motion_position();
Quaternion rotation = anim_tree->get_root_motion_rotation();

// Apply to CharacterBody3D:
velocity = (transform.basis * motion) / delta;
move_and_slide();
```

### Why This Architecture Works

1. **Graph-based = composable** -- Blend spaces, state machines, one-shots, and additive layers are all `AnimationNode` subclasses. They compose freely. A state machine node can contain blend spaces; a blend tree can contain state machines.
2. **Barycentric blending in 2D** -- Delaunay triangulation of the blend space points means the blend is always smooth and the weights always sum to 1.0. No manual weight tuning needed.
3. **Parameters as indirection** -- The tree reads parameters (floats, bools) set by game code. This decouples animation logic from game logic cleanly.
4. **Advance expressions** -- Godot 4 added typed `Expression` objects for transition conditions. These can evaluate complex logic (`speed > 3.5 and is_grounded`) without requiring script callbacks.

### Godot 3 vs. Godot 4

- Godot 3 used `AnimationTreePlayer` (a flat list of blend nodes with string connections). Replaced entirely in Godot 4.
- Godot 4's `AnimationTree` uses `AnimationMixer` base class shared with `AnimationPlayer`, unifying the playback API.
- Godot 4 added `AnimationNodeBlendSpace2D` with automatic Delaunay triangulation (Godot 3 only had `BlendSpace1D`).
- Godot 4's state machine supports `SWITCH_MODE_SYNC` for synchronized crossfades (maintaining playback position ratio).

### UzEngine Implication

UzEngine's current animation spec (`.uzanim_sm` state machine) is simpler than Godot's graph-based system. Key decisions:

1. **Phase 3 is sufficient for a state machine + linear blend**. Godot's `AnimationNodeBlendSpace2D` with Delaunay triangulation is a Phase 3+ feature. UzEngine should implement it for locomotion (speed + direction), but the core state machine should come first.
2. **The `condition = speed > 0.5` syntax in `.uzanim_sm` is correct** -- it matches Godot's advance expressions. UzEngine should parse these into a simple expression evaluator, not require callback functions.
3. **Root motion extraction pattern is identical** -- UzEngine's `extract_root_motion()` already matches Godot's approach. Validate that loop-boundary accumulation handles the wrap-around correctly (accumulate `[prev_t -> duration] + [0 -> cur_t]`).
4. **Layered/masked blending via `BlendMask`** -- UzEngine's `.uzmask` format is analogous to Godot's `AnimationNode` filter system. Godot uses a bone path filter (includes/excludes); UzEngine uses per-bone float weights. The float weight approach is more flexible (partial blending for spine twist).

---

## 4. Physics Backend (PhysicsServer3D)

### How Godot 4 Abstracts Physics

Godot 4 uses a **server pattern** for physics, identical to its rendering architecture. `PhysicsServer3D` is a singleton that owns all physics objects via opaque RID handles. Game-thread nodes (RigidBody3D, CollisionShape3D) are thin wrappers that send commands to the server.

### Key Classes

- `PhysicsServer3D` (`servers/physics_server_3d.h`) -- Abstract interface (pure virtual). Defines the full physics API (~200 methods).
- `GodotPhysicsServer3D` (`servers/physics_3d/godot_physics_server_3d.h`) -- Godot's built-in implementation.
- `JoltPhysicsServer3D` (via GDExtension) -- Jolt Physics integration as a drop-in replacement.
- `PhysicsDirectBodyState3D` -- Sync access object passed during `_integrate_forces()`.
- `PhysicsBody3D` / `RigidBody3D` -- Scene tree nodes that wrap RIDs.

### Core Pattern: RID-Based Server Abstraction

```cpp
// Simplified from servers/physics_server_3d.h

class PhysicsServer3D : public Object {
    GDCLASS(PhysicsServer3D, Object);

    static PhysicsServer3D *singleton;

public:
    // --- Space (world) management ---
    virtual RID space_create() = 0;
    virtual void space_set_active(RID p_space, bool p_active) = 0;
    virtual PhysicsDirectSpaceState3D *space_get_direct_state(RID p_space) = 0;

    // --- Body management (all via RID) ---
    virtual RID body_create() = 0;
    virtual void body_set_space(RID p_body, RID p_space) = 0;
    virtual void body_set_mode(RID p_body, BodyMode p_mode) = 0;
    virtual void body_add_shape(RID p_body, RID p_shape,
                                 const Transform3D &p_transform = Transform3D(),
                                 bool p_disabled = false) = 0;
    virtual void body_set_state(RID p_body, BodyState p_state,
                                 const Variant &p_value) = 0;
    virtual Variant body_get_state(RID p_body, BodyState p_state) const = 0;

    // --- Shape management ---
    virtual RID shape_create(ShapeType p_shape) = 0;
    virtual void shape_set_data(RID p_shape, const Variant &p_data) = 0;

    // --- Raycast and queries ---
    // Done through PhysicsDirectSpaceState3D (see below)

    // --- Sync callback ---
    // Called once per physics step for each active body
    virtual void body_set_force_integration_callback(
        RID p_body, const Callable &p_callable,
        const Variant &p_userdata = Variant()) = 0;

    // --- Step ---
    virtual void step(float p_step) = 0;
    virtual void sync() = 0;  // Copy physics state back to scene nodes

    // --- Factory ---
    static PhysicsServer3D *get_singleton() { return singleton; }
};
```

### Space Isolation for Multiple Physics Scenes

```cpp
// Each "space" is an independent physics world
// This enables:
// - Editor preview (separate space from game)
// - Sub-scenes with isolated physics (e.g., UI physics, mini-games)
// - Server-side physics on a headless Godot instance

RID editor_space = PhysicsServer3D::get_singleton()->space_create();
RID game_space   = PhysicsServer3D::get_singleton()->space_create();

// Bodies in editor_space never collide with bodies in game_space
// Each space has its own broadphase, narrowphase, and solver

// Viewport has a default World3D which has a default physics space
// SubViewport can create its own World3D -> its own physics space
```

### PhysicsDirectBodyState3D: Sync Access

```cpp
// _integrate_forces is called FROM the physics thread, during the physics step.
// PhysicsDirectBodyState3D provides direct, synchronized access to body state.

void RigidBody3D::_integrate_forces(PhysicsDirectBodyState3D *p_state) {
    // This runs inside the physics step -- safe to read/write body state

    Vector3 vel = p_state->get_linear_velocity();
    float step = p_state->get_step();  // Physics delta time

    // Apply custom force
    Vector3 gravity_override = Vector3(0, -20.0, 0);  // Custom gravity
    p_state->set_linear_velocity(vel + gravity_override * step);

    // Read contacts
    int contact_count = p_state->get_contact_count();
    for (int i = 0; i < contact_count; i++) {
        Vector3 normal = p_state->get_contact_local_normal(i);
        RID collider = p_state->get_contact_collider(i);
        // ... process contact
    }

    // Transform is directly modifiable here
    Transform3D t = p_state->get_transform();
    t.origin.y = MAX(t.origin.y, 0.0f);  // Floor clamp
    p_state->set_transform(t);
}
```

### Jolt as GDExtension Drop-in

Godot 4's physics server abstraction is so clean that the community Jolt Physics module (`godot-jolt`) implements `PhysicsServer3D` as a GDExtension -- zero engine modification required:

```cpp
// godot-jolt/src/jolt_physics_server_3d.h (community project)

class JoltPhysicsServer3D : public PhysicsServer3D {
    GDCLASS(JoltPhysicsServer3D, PhysicsServer3D);

    JPH::PhysicsSystem jolt_system;
    HashMap<RID, JoltBody3D*> bodies;  // RID -> Jolt body mapping

    RID body_create() override {
        JoltBody3D *body = memnew(JoltBody3D);
        RID rid = make_rid(body);  // Godot's RID_PtrOwner allocates slot
        bodies[rid] = body;
        return rid;
    }

    void body_set_state(RID p_body, BodyState p_state,
                        const Variant &p_value) override {
        JoltBody3D *body = bodies[p_body];
        switch (p_state) {
            case BODY_STATE_TRANSFORM:
                body->set_transform(p_value);  // Converts to Jolt JPH::Mat44
                break;
            case BODY_STATE_LINEAR_VELOCITY:
                body->set_linear_velocity(p_value);
                break;
            // ...
        }
    }

    void step(float p_step) override {
        // Delegates to Jolt's PhysicsSystem::Update()
        jolt_system.Update(p_step, collision_steps,
                           &temp_allocator, &job_system);
    }
};
```

### Why This Architecture Works

1. **Backend swappable at runtime** -- Switch between Godot Physics and Jolt by changing one project setting. No recompilation. This is possible because the interface is a pure virtual class.
2. **Space isolation** -- Independent physics worlds prevent accidental cross-contamination between editor previews, game scenes, and sub-viewports.
3. **`_integrate_forces` sync callback** -- Runs inside the physics step, not on the game thread. This eliminates the race condition where game code reads body state that is mid-update.
4. **RID indirection** -- Game-thread code holds RIDs, never raw pointers. The server can move, reallocate, or destroy underlying data without invalidating game-thread references (the RID itself becomes invalid, but no dangling pointer).

### Godot 3 vs. Godot 4

- Godot 3 had `PhysicsServer` (3D) and `Physics2DServer` with similar RID-based API, but Jolt integration required forking the engine.
- Godot 4's GDExtension system made Jolt a drop-in plugin (no engine fork).
- Godot 4 added multi-threaded physics step support (bodies can be updated in parallel within a single space).

### UzEngine Implication

1. **UzEngine's physics lives in `game.dll`** -- this is different from Godot's server-in-engine approach. The trade-off: UzEngine gets faster game-specific physics (no IPC overhead), but loses backend swappability. For a single-backend engine (Jolt-only), this is acceptable.
2. **Space isolation pattern is valuable** -- UzEngine should support multiple `PhysicsWorld` instances for editor preview vs. game play. The current spec has a single `PhysicsWorld` singleton. Consider adding a second space for editor simulation.
3. **`_integrate_forces` equivalent** -- UzEngine's `process_collision_manifolds(world)` call happens after `PhysicsSystem::Update()`, which is correct for reading contacts. But custom force application needs a pre-step callback. Add a `physics_pre_step(dt)` callback to `game_api_t` that runs before `jph.Update()`, analogous to Godot's `_integrate_forces`.
4. **RID vs. EntityHandle** -- UzEngine uses `EntityHandle` (uint64_t). This is functionally equivalent to Godot's RID. The important thing is that `EntityHandle` should be a generational index (index + version), not a raw pointer cast. This prevents use-after-free when entities are destroyed and their slots reused.

---

## 5. ResourceLoader Async + Scene Streaming

### How Godot 4 Loads Resources Asynchronously

Godot 4 has a fully asynchronous resource loading system built around thread pools and a request/poll pattern. The main thread is never blocked during asset loading.

### Key Classes

- `ResourceLoader` (`core/io/resource_loader.h`) -- Singleton facade for all resource loading.
- `ResourceFormatLoader` -- Abstract base class for format-specific loaders. Subclasses: `ResourceFormatLoaderImage`, `ResourceFormatLoaderScene`, `ResourceFormatLoaderGDScript`, etc.
- `ResourceUID` (`core/io/resource_uid.h`) -- 64-bit unique ID system for stable cross-file references.
- `PackedScene` -- Resource that represents a scene as a serialized template (instantiated into live nodes).
- `ResourceInteractiveLoader` (Godot 3) -- Replaced by threaded loading in Godot 4.

### Core Pattern: Threaded Load Request/Poll

```cpp
// Simplified from core/io/resource_loader.cpp

class ResourceLoader {
public:
    // --- Async API (Godot 4 primary pattern) ---

    // Request async load -- returns immediately
    // A background thread picks up the work
    static Error load_threaded_request(
        const String &p_path,
        const String &p_type_hint = "",
        bool p_use_sub_threads = false,       // Allow sub-resource loading on threads
        ResourceFormatLoader::CacheMode p_cache_mode =
            ResourceFormatLoader::CACHE_MODE_REUSE
    );

    // Poll status -- call from main thread each frame
    static ThreadLoadStatus load_threaded_get_status(
        const String &p_path,
        float *r_progress = nullptr   // 0.0 to 1.0
    );

    // Get result once status == THREAD_LOAD_LOADED
    static Ref<Resource> load_threaded_get(const String &p_path);

    // --- Sync API (blocking, convenience) ---
    static Ref<Resource> load(const String &p_path,
                               const String &p_type_hint = "",
                               ResourceFormatLoader::CacheMode p_cache_mode =
                                   ResourceFormatLoader::CACHE_MODE_REUSE,
                               Error *r_error = nullptr);

    // --- Format registration ---
    static void add_resource_format_loader(Ref<ResourceFormatLoader> p_format_loader,
                                            bool p_at_front = false);

private:
    // Thread pool for background loading
    static WorkerThreadPool::TaskID _load_start(const String &p_path, ...);

    // Internal: tracks in-flight load requests
    struct ThreadLoadTask {
        String local_path;
        String type_hint;
        Ref<Resource> resource;  // Result, written by loader thread
        float progress = 0.0;
        ThreadLoadStatus status = THREAD_LOAD_IN_PROGRESS;
        Error error = OK;
        Mutex mutex;             // Protects status + progress + resource
        // Sub-resources can spawn their own threaded loads
        HashSet<String> sub_tasks;
    };

    static HashMap<String, ThreadLoadTask *> thread_load_tasks;
    static Mutex thread_load_mutex;
};
```

### ResourceFormatLoader: Plugin System for Custom Formats

```cpp
// Any module or GDExtension can register custom loaders

class ResourceFormatLoader : public RefCounted {
    GDCLASS(ResourceFormatLoader, RefCounted);

public:
    enum CacheMode {
        CACHE_MODE_IGNORE,     // Don't cache (reload every time)
        CACHE_MODE_REUSE,      // Return cached if exists
        CACHE_MODE_REPLACE,    // Load fresh, replace cache entry
    };

    // Override these in subclasses
    virtual Ref<Resource> load(const String &p_path,
                                const String &p_original_path = "",
                                Error *r_error = nullptr,
                                bool p_use_sub_threads = false,
                                float *r_progress = nullptr,
                                CacheMode p_cache_mode = CACHE_MODE_REUSE);

    virtual void get_recognized_extensions(List<String> *p_extensions) const;
    virtual bool handles_type(const String &p_type) const;
    virtual String get_resource_type(const String &p_path) const;

    // Dependencies (for async sub-loading)
    virtual void get_dependencies(const String &p_path,
                                   List<String> *p_dependencies,
                                   bool p_add_types = false);
};

// Registration:
// ResourceLoader::add_resource_format_loader(memnew(ResourceFormatLoaderGLTF));
```

### PackedScene: Scene as Template

```cpp
// A PackedScene is a serialized scene tree -- it stores nodes, properties, and
// connections as flat arrays. Instantiation creates live Node objects.

// Saving a scene:
Ref<PackedScene> packed = memnew(PackedScene);
packed->pack(scene_root);  // Serializes entire subtree
ResourceSaver::save(packed, "res://scenes/level1.tscn");

// Loading + instantiating:
Ref<PackedScene> scene_res = ResourceLoader::load("res://scenes/level1.tscn");
Node *instance = scene_res->instantiate();
// 'instance' is a new node tree, independent from the PackedScene
// Multiple instantiations create independent copies (prefab pattern)

get_tree()->get_current_scene()->add_child(instance);
```

### SubResource vs. ExtResource in .tscn

```
# .tscn file format (text-based)

[gd_scene load_steps=3 format=3 uid="uid://abc123"]

# ExtResource: external file reference (separate .tres/.tscn/.png)
[ext_resource type="PackedScene" uid="uid://xyz789" path="res://enemy.tscn" id="1"]
[ext_resource type="Texture2D" uid="uid://tex001" path="res://sprites/hero.png" id="2"]

# SubResource: inline resource (embedded in this file)
[sub_resource type="CircleShape2D" id="CircleShape2D_abc"]
radius = 32.0

# Node tree
[node name="Root" type="Node2D"]

[node name="Sprite" type="Sprite2D" parent="."]
texture = ExtResource("2")  # References external texture

[node name="CollisionShape" type="CollisionShape2D" parent="."]
shape = SubResource("CircleShape2D_abc")  # Inline shape

[node name="EnemySpawner" type="Node2D" parent="."]
# Instantiates an external scene as a child
[node name="Enemy1" parent="EnemySpawner" instance=ExtResource("1")]
```

### Why This Architecture Works

1. **Request/poll is non-blocking** -- The main thread never stalls. Game code requests a load, then checks status each frame. When ready, it retrieves the result. This enables loading screens with progress bars trivially.
2. **Format loaders are pluggable** -- Custom `.uzscene`, `.uzmat`, `.uzanim` format loaders can be added without modifying core engine code. Each loader knows how to read its format and produce a `Resource` subclass.
3. **UID-based stable references** -- `ResourceUID` (int64_t) survives file moves and renames. When a file is moved, only the UID mapping table updates; all references remain valid.
4. **SubResource inlining** -- Small resources (collision shapes, gradients) that belong to exactly one scene are embedded directly. This eliminates file proliferation and makes scenes self-contained.
5. **Dependency tracking** -- Loaders declare their dependencies, enabling the system to load sub-resources in parallel and track load progress accurately.

### Godot 3 vs. Godot 4

- Godot 3 used `ResourceInteractiveLoader` (a coroutine-style loader that advanced one step per `poll()` call, always on the main thread). Could not use threads.
- Godot 4 replaced this with a proper `WorkerThreadPool`-based system. Sub-resources can load on threads. Real parallelism.
- Godot 4 introduced `ResourceUID` for stable 64-bit IDs (Godot 3 used path-only references, breaking on file moves).

### UzEngine Implication

1. **Async loading is mandatory for AAA** -- UzEngine's `asset_load_raw()` in `GameContext` is synchronous (returns pointer, blocks). A threaded version is needed: `asset_load_async(path, callback)` or a request/poll pair like Godot. This should be Phase 2 (asset-hot-reload task).
2. **Format loader plugin system** -- UzEngine's asset pipeline spec already uses format-specific importers (ufbx, fastgltf). The runtime equivalent is needed: a registry of `FormatLoader` instances that can load `.uzmesh`, `.uzanim`, `.uzscene`, etc. The plugin system should allow plugins to register custom format loaders.
3. **ResourceUID maps to `stringId`** -- UzEngine's `stringId` (64-bit hash) serves a similar purpose. However, hash collisions are possible while UIDs are guaranteed unique. Consider using hash for runtime lookup but storing a collision-free UID in `.import.json` for asset references.
4. **SubResource pattern** -- UzEngine's `.uzscene` INI format could benefit from inline sub-resources for collision shapes, material overrides, etc., instead of requiring separate files for trivial resources.
5. **PackedScene = .uzscene** -- Godot's `packed->pack(scene_root)` / `scene_res->instantiate()` is exactly what UzEngine needs for prefab instantiation. The `.uzscene` loader should return a template that can be instantiated multiple times into independent entity subtrees.

---

## 6. Debugger Protocol

### How Godot 4 Implements Engine Debugging

Godot 4 has a built-in debugger that communicates between the running game (the "debuggee") and the editor (the "debugger") via a binary protocol over TCP sockets.

### Key Classes

- `EngineDebugger` (`core/debugger/engine_debugger.h`) -- Central singleton for debugger state. Manages profiler registration, breakpoints, and message dispatch.
- `RemoteDebugger` (`core/debugger/remote_debugger.h`) -- Network-facing implementation. Sends/receives messages over TCP.
- `ScriptDebugger` (`core/debugger/script_debugger.h`) -- GDScript-specific debugging: breakpoints, step-in/over/out, variable inspection.
- `EditorDebuggerNode` (`editor/debugger/editor_debugger_node.h`) -- Editor-side: receives messages, displays in Debugger panel.
- `PerformanceMonitor` (`core/debugger/engine_profiler.h`) -- Custom metrics registration.

### Debug Message Protocol

```cpp
// Messages are binary-encoded arrays sent over TCP
// Format: [message_type: String, data...: Variant[]]

// Example messages from debuggee -> editor:

// Stack trace when breakpoint hit:
["debug_enter", "Breakpoint",          // reason
    // Stack frames:
    "res://player.gd",   42,  "on_update",   // file, line, function
    "res://game.gd",     128, "_process",     // caller
    // Local variables for current frame:
    "speed", 5.0,
    "position", Vector3(10, 0, 0),
    "is_jumping", false
]

// Profiler data (sent every N frames):
["servers:frame_profile_frame",
    frame_number,
    [                    // Array of profiler entries
        "physics_step",  0.002,   // name, time_seconds
        "render_frame",  0.008,
        "script_update", 0.001,
    ]
]

// Error message:
["error",
    call_error,        // bool: is it a call error?
    error_msg,         // "Null instance"
    error_descr,       // "Attempting to call 'get_position' on null"
    "res://enemy.gd",  // file
    "on_update",       // function
    55,                // line
    // Stack trace follows
]

// Editor -> debuggee messages:
["set_breakpoint", "res://player.gd", 42, true]   // file, line, enabled
["next"]           // step over
["step"]           // step into
["continue"]       // resume execution
["inspect_object", object_id]  // request object property dump
```

### Custom Profiler Registration

```cpp
// Any module can register a custom profiler channel

class EngineDebugger {
public:
    struct Profiler {
        // Callbacks for profiler lifecycle
        typedef void (*ToggleFunc)(void *p_user, bool p_enable,
                                    const Array &p_opts);
        typedef void (*AddFunc)(void *p_user, const Array &p_data);
        typedef void (*TickFunc)(void *p_user, double p_frame_time,
                                  double p_process_time,
                                  double p_physics_time,
                                  double p_physics_frame_time);

        ToggleFunc toggle;    // Called when editor enables/disables this profiler
        AddFunc add;          // Called to add profiler data
        TickFunc tick;        // Called each frame
        void *data;
    };

    static void register_profiler(const StringName &p_name,
                                   const Profiler &p_profiler);

    // Send arbitrary profiler data to editor
    static void profiler_add_frame_data(const StringName &p_name,
                                         const Array &p_data);
};

// Usage: register a custom "audio" profiler
void AudioServer::_register_profiler() {
    EngineDebugger::Profiler prof;
    prof.toggle = [](void *ud, bool enable, const Array &opts) {
        AudioServer *self = (AudioServer *)ud;
        self->profiling = enable;
    };
    prof.tick = [](void *ud, double frame, double proc,
                    double phys, double phys_frame) {
        AudioServer *self = (AudioServer *)ud;
        if (!self->profiling) return;

        Array data;
        data.push_back("active_voices");
        data.push_back(self->get_active_voice_count());
        data.push_back("bus_peak_db");
        data.push_back(self->get_bus_peak_volume_left_db(0));

        EngineDebugger::profiler_add_frame_data("audio", data);
    };
    prof.data = this;

    EngineDebugger::register_profiler("audio", prof);
}
```

### RemoteDebugger: TCP Communication

```cpp
// Simplified from core/debugger/remote_debugger.cpp

class RemoteDebugger : public EngineDebugger {
    Ref<StreamPeerTCP> tcp_client;

    // Send a message to editor
    void _send_message(const String &p_msg, const Array &p_data) {
        // Encode: [p_msg, p_data[0], p_data[1], ...]
        // Binary format: length-prefixed Variant array

        Array msg;
        msg.push_back(p_msg);
        for (int i = 0; i < p_data.size(); i++) {
            msg.push_back(p_data[i]);
        }

        Vector<uint8_t> data;
        int len;
        Error err = encode_variant(msg, nullptr, len);  // Get size
        data.resize(4 + len);
        encode_uint32(len, data.ptrw());                // 4-byte length prefix
        encode_variant(msg, data.ptrw() + 4, len);      // Payload

        tcp_client->put_data(data.ptr(), data.size());
    }

    // Receive loop (called from debugger thread)
    void _poll() {
        while (tcp_client->get_available_bytes() >= 4) {
            uint32_t len = tcp_client->get_u32();
            Vector<uint8_t> data;
            data.resize(len);
            tcp_client->get_data(data.ptrw(), len);

            Variant msg;
            decode_variant(msg, data.ptr(), len);

            Array arr = msg;
            String type = arr[0];

            if (type == "set_breakpoint") {
                _set_breakpoint(arr[1], arr[2], arr[3]);
            } else if (type == "next") {
                script_debugger->set_depth(-1);
                script_debugger->set_lines_left(1);
            }
            // ... handle other message types
        }
    }
};
```

### Why This Architecture Works

1. **Process isolation** -- The game runs as a separate process from the editor. A game crash does not crash the editor. Debugging uses TCP, so remote debugging (game on device, editor on PC) works with zero additional code.
2. **Custom profiler channels** -- Any subsystem can register metrics. The audio system, physics system, and rendering system each have their own profiler channel. The editor displays them as separate tabs.
3. **Binary protocol is compact** -- Variant encoding is Godot's native serialization. No JSON parsing overhead. Messages are length-prefixed for clean framing.
4. **Breakpoint + stack inspection** -- The script debugger pauses the VM at breakpoints and serializes the entire call stack + local variables to the editor. This enables VS Code-like debugging within the Godot editor.

### Godot 3 vs. Godot 4

- Godot 3 used a similar TCP protocol but profiler channels were hardcoded (visual_profiler, script_profiler). Godot 4 made them extensible via `register_profiler()`.
- Godot 4 separated `EngineDebugger` (core) from `RemoteDebugger` (network), allowing headless debugging and unit test profiling without a TCP connection.
- Godot 4 added `EngineDebugger::send_message()` for arbitrary message passing between game and editor -- extensible without protocol version bumps.

### UzEngine Implication

1. **IPC debugger protocol** -- UzEngine uses WebSocket IPC between engine and VSCode extension. This is functionally equivalent to Godot's TCP debugger protocol. The key pattern to adopt is **message typing**: `[message_type, ...data]` format. UzEngine's current IPC uses `{function, args}` JSON -- this is already close.
2. **Custom profiler registration** -- UzEngine should implement `EngineDebugger::register_profiler()` equivalent. Each subsystem (renderer, physics, audio, Lua) registers its own profiler channel. The VSCode extension shows them as separate panels. This aligns with the `debug-profiling.md` spec's Tracy integration, but adds a VSCode-visible layer on top.
3. **Lua DAP server** -- UzEngine's `lua-dap-server` task should follow Godot's pattern: breakpoints sent from editor to engine, engine pauses Lua VM at breakpoint, serializes call stack + locals, sends to editor. The Debug Adapter Protocol (DAP) is standardized, so UzEngine should implement DAP directly rather than a custom protocol.
4. **Process isolation** -- UzEngine already runs the engine as a separate process from VSCode. This is correct. A crash in `UzEngine.exe` should not crash the VSCode extension. The extension should detect the crash (WebSocket close) and show a restart dialog.
5. **Performance monitor** -- Godot's `PerformanceMonitor` tracks FPS, draw calls, memory, etc. UzEngine's MCP `uz_status` already returns `fps`, `frame_count`, `uptime`. Expand this to include draw call count, GPU memory, active voice count, entity count.

---

## 7. GDExtension Plugin System (Deep Dive)

### gdextension_interface.h Structure

The GDExtension interface is organized as a flat struct of ~600 function pointers, grouped by subsystem. The struct is passed to the extension's initialization function exactly once at load time.

```c
// The struct is organized in logical groups:

// Group 1: Memory (~5 functions)
//   mem_alloc, mem_realloc, mem_free, mem_alloc_zeroed, mem_free_sized

// Group 2: Print (~5 functions)
//   print_error, print_error_with_message, print_warning, print_script_error

// Group 3: Variant (~100 functions)
//   variant_new_*, variant_destroy, variant_call, variant_get, variant_set
//   variant_evaluate (arithmetic), variant_get_type, variant_hash
//   variant_booleanize, variant_stringify, variant_duplicate

// Group 4: String / StringName (~70 functions)
//   string_new_with_*, string_to_*, string_resize
//   string_name_new_with_*, string_name_data_ptr

// Group 5: Object (~30 functions)
//   object_method_bind_call, object_method_bind_ptrcall
//   object_destroy, object_get_instance_id, object_get_class_name
//   object_cast_to, object_get_instance_binding

// Group 6: ClassDB (~20 functions)
//   classdb_construct_object, classdb_register_extension_class3
//   classdb_register_extension_class_method, classdb_register_extension_class_property
//   classdb_register_extension_class_signal

// Group 7: Callable / Signal (~15 functions)
//   callable_custom_create2, signal_emit

// Group 8: Script Instance (~10 functions)
//   script_instance_create3

// Group 9: Editor (~10 functions)
//   editor_add_plugin, editor_remove_plugin

// Group 10: Utility (~20 functions)
//   get_godot_version, get_native_struct_size
//   get_library_path, os_get_environment
```

### ABI Compatibility Guarantee

```c
// Godot guarantees ABI stability through three mechanisms:

// 1. Struct-append-only: new functions are ALWAYS appended to the end
//    Never inserted in the middle. Never reordered. Never removed.
typedef struct {
    // v4.0 functions
    void *(*mem_alloc)(size_t);
    void *(*mem_realloc)(void *, size_t);
    void  (*mem_free)(void *);
    // ...existing functions...

    // v4.1 additions (appended at end)
    void (*new_function_41a)(/* ... */);
    void (*new_function_41b)(/* ... */);

    // v4.2 additions (appended at end)
    void (*new_function_42a)(/* ... */);
} GDExtensionInterface;

// 2. Version checking: extension queries engine version at init
// and only uses functions available in that version.

// 3. Deprecated functions are KEPT (not removed), just marked deprecated.
// This means an extension compiled for 4.0 still works in 4.3.
```

### Extension Lifecycle

```c
// --- Phase 1: Discovery ---
// Engine finds .gdextension files (TOML-like):
//
// [configuration]
// entry_symbol = "my_extension_init"
// compatibility_minimum = "4.1"
//
// [libraries]
// windows.x86_64 = "bin/my_extension.windows.x86_64.dll"
// linux.x86_64   = "bin/libmy_extension.linux.x86_64.so"

// --- Phase 2: Load ---
// Engine calls LoadLibrary / dlopen, finds entry_symbol

// --- Phase 3: Initialize ---
// The entry point receives the interface struct:

GDExtensionBool my_extension_init(
    const GDExtensionInterface *p_interface,
    GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization
) {
    // Store interface pointer for later use
    godot_api = p_interface;
    library = p_library;

    // Fill initialization struct
    r_initialization->minimum_initialization_level =
        GDEXTENSION_INITIALIZATION_SCENE;  // When to init (core/servers/scene/editor)
    r_initialization->initialize = initialize_extension;
    r_initialization->deinitialize = deinitialize_extension;

    return true;  // Success
}

// --- Phase 4: Per-Level Initialization ---
// Called at each initialization level that the extension requested:

void initialize_extension(void *p_userdata,
                           GDExtensionInitializationLevel p_level) {
    if (p_level == GDEXTENSION_INITIALIZATION_SCENE) {
        // Register custom classes, methods, properties
        // Uses classdb_register_extension_class3(), etc.

        GDExtensionClassCreationInfo3 info = {};
        info.create_instance_func = MyNode_create;
        info.free_instance_func = MyNode_free;
        info.get_virtual_func = MyNode_get_virtual;
        info.class_userdata = nullptr;

        godot_api->classdb_register_extension_class3(
            library,
            &my_class_name,      // StringName
            &parent_class_name,  // "Node3D"
            &info
        );
    }
}

// --- Phase 5: Use ---
// Extension classes are now available in editor and game code.
// GDScript can instantiate them: var x = MyCustomNode.new()

// --- Phase 6: Deinitialize ---
// Called in REVERSE order of initialization levels
void deinitialize_extension(void *p_userdata,
                              GDExtensionInitializationLevel p_level) {
    if (p_level == GDEXTENSION_INITIALIZATION_SCENE) {
        // Unregister classes (in reverse order of registration)
        godot_api->classdb_unregister_extension_class(library, &my_class_name);
    }
}
```

### GDNative (Godot 3) vs. GDExtension (Godot 4): What Changed

| Aspect | GDNative (Godot 3) | GDExtension (Godot 4) |
|--------|--------------------|-----------------------|
| Function lookup | `dlsym()` per function (slow, fragile) | Single struct of pointers (fast, safe) |
| ABI stability | Unstable between minor versions | Append-only guarantee |
| Class registration | NativeScript: per-function registration | ClassDB integration: full editor support |
| Virtual functions | Manual vtable emulation | `get_virtual_func` callback pattern |
| Method binding | `godot_method_bind_get_method()` string lookup | `classdb_register_extension_class_method` |
| Editor integration | Limited (no inspector, no signals) | Full (inspector, signals, exports, docs) |
| Build system | scons + godot-cpp | cmake/scons + godot-cpp (improved) |

### Why GDExtension Is Effective

1. **O(1) function access** -- Struct field offset instead of `dlsym()` hash lookup. Initialization is a single pointer copy.
2. **ABI-safe evolution** -- Append-only struct means extensions compiled for 4.0 load in 4.3 without recompilation. The engine can add features without breaking existing plugins.
3. **Initialization levels** -- `CORE → SERVERS → SCENE → EDITOR` levels ensure that extensions are initialized after their dependencies. A physics extension initializes at SERVERS level (before scene nodes that might use it).
4. **Symmetrical shutdown** -- Deinitialize is called in reverse order, preventing use-after-free of dependencies.

### UzEngine Implication

1. **`plugin_api_t` should adopt append-only versioning** -- UzEngine's current `plugin_api_t` is version 1. When adding new functions, append them to the end. Never insert. Never reorder. This is the single most important ABI stability lesson from GDExtension.
2. **Initialization levels** -- UzEngine's plugin manifest (`.uzplugin` TOML) should add an `init_level` field: `core`, `renderer`, `scene`, `editor`. Plugins are initialized in level order, deinitialized in reverse. This prevents a terrain plugin from initializing before the renderer is ready.
3. **Virtual function dispatch** -- GDExtension uses `get_virtual_func(name)` to return a function pointer that the engine calls for virtual methods (`_process`, `_ready`, etc.). UzEngine's `game_api_t` already uses this pattern with explicit exported functions (`game_dll_on_start`, `game_dll_on_update`, etc.). For plugins, consider the `get_virtual_func` pattern to allow plugins to override specific behaviors.
4. **SEH isolation** -- UzEngine's plugin spec mentions SEH isolation. GDExtension does NOT isolate crashes -- a crashing extension crashes Godot. UzEngine's SEH approach (wrapping plugin calls in `__try/__except`) is an improvement over Godot.
5. **Discovery file** -- GDExtension uses `.gdextension` files (TOML-like, platform-keyed library paths). UzEngine's `.uzplugin` TOML manifest serves the same purpose. Consider adding platform-specific DLL paths like GDExtension:

```toml
[cpp]
dll.windows_x64 = "uz_terrain.dll"
dll.linux_x64   = "libuz_terrain.so"
```

---

## 8. Theme / UI Architecture

### How Godot 4 Renders UI

Godot 4's UI system is built on the `Control` node class, which renders via the `RenderingServer`'s CanvasItem API. Themes provide parameterized styling, and Viewports act as isolated render targets.

### Key Classes

- `Theme` (`scene/resources/theme.h`) -- Resource containing UI styling data: colors, fonts, icons, styleboxes, constants. Keyed by `(item_name, type_name)`.
- `Control` (`scene/gui/control.h`) -- Base class for all UI elements. Handles layout (anchors, margins, containers), input events, and theme resolution.
- `RenderingServer` -- Canvas items (the 2D rendering backend). Controls call `RenderingServer::canvas_item_add_*()` methods to draw.
- `Viewport` -- Render target. Can be used as a SubViewport for render-to-texture (minimaps, picture-in-picture, reflections).
- `CanvasLayer` -- Z-ordering layer. UI is typically on a CanvasLayer above the game world.
- `StyleBox` -- Abstract visual style (flat color, texture, line). Themed controls draw StyleBoxes for backgrounds.

### Theme System: Parameterized Styling

```cpp
// Simplified from scene/resources/theme.h

class Theme : public Resource {
    // Theme data is a 4-level map:
    // data_type -> item_type (e.g., "Button") -> item_name (e.g., "font_color") -> value

    // 5 data types:
    HashMap<StringName, HashMap<StringName, Color>>     color_map;
    HashMap<StringName, HashMap<StringName, int>>       constant_map;
    HashMap<StringName, HashMap<StringName, Ref<Font>>> font_map;
    HashMap<StringName, HashMap<StringName, int>>       font_size_map;
    HashMap<StringName, HashMap<StringName, Ref<Texture2D>>> icon_map;
    HashMap<StringName, HashMap<StringName, Ref<StyleBox>>>  style_map;

    // Theme inheritance: if a value is not found in this theme,
    // look in the parent theme, then the default theme
    Ref<Theme> fallback_theme;

public:
    // Getters (used by Control nodes during drawing)
    Color get_color(const StringName &p_name, const StringName &p_type) const;
    Ref<StyleBox> get_stylebox(const StringName &p_name,
                                const StringName &p_type) const;
    Ref<Font> get_font(const StringName &p_name, const StringName &p_type) const;
    int get_font_size(const StringName &p_name, const StringName &p_type) const;
    int get_constant(const StringName &p_name, const StringName &p_type) const;
    Ref<Texture2D> get_icon(const StringName &p_name,
                             const StringName &p_type) const;

    bool has_color(const StringName &p_name, const StringName &p_type) const;
    // ... same for other types
};
```

### Control Rendering: draw_*() API

```cpp
// Controls render by overriding _notification(NOTIFICATION_DRAW)
// and calling drawing methods that map to RenderingServer canvas items.

void Button::_notification(int p_what) {
    if (p_what == NOTIFICATION_DRAW) {
        // 1. Resolve theme resources
        Ref<StyleBox> style;
        Color font_color;

        switch (get_draw_mode()) {
            case DRAW_NORMAL:
                style = get_theme_stylebox("normal");
                font_color = get_theme_color("font_color");
                break;
            case DRAW_HOVER:
                style = get_theme_stylebox("hover");
                font_color = get_theme_color("font_hover_color");
                break;
            case DRAW_PRESSED:
                style = get_theme_stylebox("pressed");
                font_color = get_theme_color("font_pressed_color");
                break;
            case DRAW_DISABLED:
                style = get_theme_stylebox("disabled");
                font_color = get_theme_color("font_disabled_color");
                break;
        }

        // 2. Draw background (StyleBox handles corners, borders, etc.)
        draw_style_box(style, Rect2(Point2(), get_size()));

        // 3. Draw text (centered in the button)
        Ref<Font> font = get_theme_font("font");
        int font_size = get_theme_font_size("font_size");

        Size2 text_size = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT,
                                                  -1, font_size);
        Point2 text_ofs = (get_size() - text_size) / 2;  // Center

        draw_string(font, text_ofs, text, HORIZONTAL_ALIGNMENT_LEFT,
                    get_size().width, font_size, font_color);

        // 4. Draw icon if present
        if (icon.is_valid()) {
            Point2 icon_ofs = _compute_icon_offset();
            draw_texture(icon, icon_ofs);
        }
    }
}

// These draw_*() calls internally map to:
// RenderingServer::canvas_item_add_rect()
// RenderingServer::canvas_item_add_texture_rect()
// RenderingServer::canvas_item_add_mesh()  (for 9-patch StyleBoxTexture)
```

### Viewport as Isolated Render Target

```cpp
// SubViewport creates an isolated rendering context.
// Anything rendered inside it goes to a texture, not the screen.

// Use cases:
// 1. Minimap: camera rendering the scene from above into a SubViewport,
//    displayed as a TextureRect in the HUD
// 2. Character preview: separate 3D scene in a SubViewport for inventory screen
// 3. Render-to-texture: mirrors, security cameras, portal effects

// Setup:
SubViewport *preview = memnew(SubViewport);
preview->set_size(Vector2i(256, 256));
preview->set_transparent_background(true);
preview->set_update_mode(SubViewport::UPDATE_ALWAYS);

Camera3D *cam = memnew(Camera3D);
preview->add_child(cam);

// Display the SubViewport's texture in the UI:
TextureRect *display = memnew(TextureRect);
display->set_texture(preview->get_texture());
// The TextureRect shows the 3D preview rendered by the SubViewport
```

### CanvasLayer and Z-Ordering

```cpp
// CanvasLayers separate UI from game world rendering.
// Layer 0 = game world canvas
// Layer 1+ = UI layers (drawn on top)

// Game HUD (always on top of 3D world):
CanvasLayer *hud_layer = memnew(CanvasLayer);
hud_layer->set_layer(10);  // High layer = drawn later = on top

Control *health_bar = memnew(ProgressBar);
hud_layer->add_child(health_bar);

// Pause menu (on top of HUD):
CanvasLayer *pause_layer = memnew(CanvasLayer);
pause_layer->set_layer(20);  // Higher than HUD

// Within a single CanvasLayer, Controls use tree order (depth-first)
// for rendering and input priority.
```

### Why This Architecture Works

1. **Theme inheritance** -- Controls resolve theme values by walking up the tree: node theme -> parent theme -> project default theme. This enables per-panel theming (dark sidebar, light content area) without code changes.
2. **draw_*() abstraction over RenderingServer** -- Controls never create GPU resources directly. They call `draw_rect()`, `draw_string()`, `draw_texture()`, which translate to RenderingServer canvas items. This makes Controls rendering-backend agnostic.
3. **SubViewport isolation** -- Each SubViewport has its own World3D, own CanvasItem tree, and own physics space (optionally). A minimap SubViewport renders the game world from a different camera without affecting the main viewport.
4. **CanvasLayer separation** -- UI layers are independent of 3D world rendering. Changing the 3D camera does not affect UI layout. CanvasLayers have their own transform, so the UI can be resolution-independent while the 3D world scales.

### Godot 3 vs. Godot 4

- Godot 3 used `CanvasItem::draw_*()` identically, but the rendering backend (GLES2/GLES3) was less efficient for complex UIs.
- Godot 4 rewrote the rendering backend (Vulkan/RD), making canvas item batching significantly faster. Complex UIs with hundreds of controls render without draw call explosion.
- Godot 4 added `ThemeDB` for centralized default theme management, replacing the scattered `Control::_get_default_theme()` calls.
- Godot 4 improved SubViewport with `UPDATE_WHEN_VISIBLE` and `UPDATE_WHEN_PARENT_VISIBLE` modes, reducing unnecessary rendering of off-screen viewports.

### UzEngine Implication

1. **UzEngine uses HTML+CSS instead of a Control node tree** -- This is a fundamentally different approach from Godot. UzEngine's `uz_ui_html.dll` parses HTML/CSS and generates paint commands. This is more like a web browser engine than Godot's immediate-mode-style draw calls. The advantage is familiar web authoring; the disadvantage is a more complex rendering pipeline.
2. **CSS custom properties = Theme values** -- Godot's `get_theme_color("font_color")` maps to UzEngine's `var(--font-color)` CSS custom properties. The `ui_set_property()` C ABI call that sets these properties is the equivalent of Godot's Theme system. UzEngine should ensure that CSS custom properties cascade hierarchically (parent element vars are inherited by children), matching Godot's theme inheritance.
3. **SubViewport pattern for render-to-texture** -- UzEngine needs this for minimaps, character previews in inventory screens, etc. The renderer should support multiple render targets. Each SubViewport is a separate render pass with its own camera, producing a texture that can be referenced in HTML (`<img src="viewport://preview">`).
4. **CanvasLayer = UIPass sort_key** -- UzEngine's UIPass uses `sort_key=800` to render on top of the 3D scene. Multiple UI layers could use different sort keys (800 for HUD, 850 for menus, 900 for overlays). This is functionally equivalent to Godot's CanvasLayer numbering.
5. **Resolution independence** -- Godot's Control system uses anchors and containers for responsive layout. UzEngine's HTML+CSS approach gets this via CSS flexbox and percentage-based sizing. Ensure that the `uz_ui_html.dll` layout engine supports `vw`/`vh` units and media queries for different viewport sizes.

---

## Summary of Top Insights

### Insight 1: State-Preserving Hot-Reload Pattern
Godot's GDScript hot-reload proves that the three-phase pattern (save state -> recompile -> restore state) works reliably for thousands of instances simultaneously. UzEngine's Lua hot-reload and `game.dll` hot-reload should follow this exact pattern. The critical detail: **never destroy/recreate entities during reload**. The entity persists; only the script module attached to it is swapped.

### Insight 2: Append-Only ABI Versioning
GDExtension's function pointer table with append-only growth is the gold standard for C ABI stability. UzEngine's `GameContext` and `plugin_api_t` should adopt this discipline: new functions are always appended to the end of the struct, never inserted or reordered. An extension compiled for engine v1.0 must load in engine v2.0 without recompilation.

### Insight 3: Server Abstraction for Backend Swappability
Godot's `PhysicsServer3D` / `RenderingServer` pattern (pure virtual interface + RID-based object management + space isolation) enables the community Jolt integration as a drop-in plugin with zero engine modification. UzEngine's Jolt integration lives inside `game.dll` (tighter coupling but faster). However, UzEngine should still support multiple `PhysicsWorld` instances (game space vs. editor space) and consider abstracting physics behind a `PhysicsBackend` interface for future flexibility.
