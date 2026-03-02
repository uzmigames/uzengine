# Cloth Simulation Spec

> **Status**: Planned (Phase 4)
> **Library**: Jolt Physics (SoftBody)
> **Owner**: `game.dll` (C++ cloth engine interface)
> **Lua API**: Available Phase 4+ via `ctx.cloth` for wind zones, pinning, blend control

---

## Overview

The Cloth Simulation System provides realistic cloth physics for garments, capes, flags, and fabric objects. Built on Jolt Physics' `SoftBody` constraint system, it simulates vertex dynamics, edge constraints, wind interaction, and collision response while maintaining skinned mesh integration for seamless blending between physics and skeletal animation.

**Key principles**:
- Each cloth piece is a `SoftBody` constraint graph in Jolt Physics
- Cloth blends smoothly between animated (skeleton) and physics-driven poses
- Wind zones apply forces per vertex with spatial falloff
- Self-collision is optional; environment collision uses capsule proxies
- Cloth LOD adapts vertex count and simulation quality based on distance
- Full editor visualization with wind force vectors and constraint debugging

---

## Architecture

### Cloth Component Structure

```cpp
// Main cloth component (one per cloth entity)
struct CCloth {
    // Identity
    StringId            cloth_asset_id;     // reference to .uzcloth asset
    StringId            mesh_id;            // skinned mesh asset to simulate

    // Jolt integration
    JPH::SoftBody*      jolt_softbody;      // Jolt soft body constraint
    JPH::SoftBodySharedSettings* settings;  // Constraint configuration

    // Bone attachments (pinning cloth to skeleton)
    struct BoneAttachment {
        uint32_t        bone_index;         // skeleton bone ID
        std::vector<uint32_t> vertex_indices;  // which cloth vertices pin to this bone
        float           weight_influence;   // 0.0–1.0 (blend towards bone)
    };
    std::vector<BoneAttachment> attachments;

    // Physics properties
    float               mass_per_vertex;    // kg (affects inertia)
    float               damping;            // 0.0–1.0 (energy dissipation)
    float               stiffness;          // 0.0–1.0 (edge constraint stiffness)

    // Collision
    bool                self_collision;     // cloth vertices collide with each other
    float               self_collision_margin; // distance threshold
    bool                environment_collision; // cloth collides with static geometry
    std::vector<uint32_t> obstacle_capsule_ids; // capsule colliders for environment

    // Blending (interpolate between skeleton and physics)
    float               physics_blend;      // 0.0 = full skeleton, 1.0 = full physics
    float               blend_easing;       // smoothing for blend transitions

    // Wind interaction
    bool                wind_enabled;       // enable wind force application
    uint32_t            wind_zone_id;       // active wind zone entity (or 0)

    // State (read-only; updated each physics frame)
    std::vector<Vec3>   positions;          // current vertex positions (world-space)
    std::vector<Vec3>   velocities;         // current vertex velocities
    bool                is_simulating;      // true if active in physics step
    float               total_wind_force;   // magnitude of applied wind (for feedback)

    // Performance LOD
    uint32_t            active_vertices;    // actual simulated vertex count (may be reduced)
    float               simulation_quality; // 0.0–1.0 (lower = fewer iterations)
};

// Wind zone component (applies force to cloth)
struct CWindZone {
    Vec3                wind_direction;     // normalized wind vector
    float               wind_strength;      // magnitude (m/s²)
    float               wind_turbulence;    // 0.0–1.0 (adds variation)

    // Spatial falloff (optional; for localized wind)
    bool                use_falloff;        // true for e.g. explosion blast
    float               falloff_radius;     // distance from center
    float               falloff_inner;      // inner radius (full strength)

    // Gust dynamics
    float               gust_frequency;     // Hz (how often gusts occur)
    float               gust_strength;      // magnitude multiplier for gusts
    float               gust_duration;      // seconds per gust
};

// Simplified collider for cloth-environment interaction
struct CClothCollider {
    // Capsules are used instead of mesh colliders for performance
    Vec3                point_a;            // capsule endpoint A (local-space)
    Vec3                point_b;            // capsule endpoint B (local-space)
    float               radius;             // capsule radius
};
```

### Cloth Asset Format (`.uzcloth`)

Cloth assets define vertex groups, constraints, and pinning:

```ini
[Cloth]
name = cloth_cape
mesh_asset = res://meshes/character_cape.uzmesh
mass_per_vertex = 0.05
damping = 0.3
stiffness = 0.8

; Vertex groups (for selective pinning/constraint)
[Groups]
shoulder_pins = [0, 1, 2, 3]           ; vertices pinned to shoulder
edge_bend = [50, 51, ..., 120]         ; vertices with additional bend constraints
free_fall = [121, 122, ..., 500]       ; unconstrained vertices

[Constraints]
; Edge constraints (stretch): connect adjacent vertices
edge_compliance = 0.02                 ; low = stiff, high = elastic
edge_damping = 0.1

; Bend constraints (prevent wrinkles): 2-edge-distance neighbors
bend_compliance = 0.05
bend_damping = 0.15

[Collision]
self_collision = false
environment_collision = true
self_collision_margin = 0.02           ; 2 cm minimum distance between vertices

[Wind]
wind_enabled = true
base_wind_strength = 2.0               ; default wind force magnitude
turbulence = 0.3                       ; randomization

[Pinning]
; Pin vertex groups to skeleton bones
shoulder = "Armature.Shoulder"         ; bone name
upper_back = "Armature.Spine.001"
```

### `.uzcloth` Constraint System

Jolt's `SoftBodySharedSettings` defines constraint graphs:

```cpp
struct ClothConstraintSettings {
    // Vertex mass (affects resistance to motion)
    float               vertex_mass = 0.05f;  // kg

    // Distance constraints (preserve edge length)
    struct EdgeConstraint {
        uint32_t        vertex_a, vertex_b;
        float           rest_distance;       // natural length
        float           compliance;          // 0 = rigid, ∞ = loose
        float           damping;             // energy dissipation
    };
    std::vector<EdgeConstraint> edges;

    // Bend constraints (prevent folding)
    struct BendConstraint {
        uint32_t        vertex_a, vertex_b;  // endpoints of 2-edge path
        float           rest_angle;          // natural bending angle
        float           compliance;
    };
    std::vector<BendConstraint> bends;

    // Volume constraints (prevent compression)
    struct VolumeConstraint {
        std::vector<uint32_t> vertices;     // tetrahedron vertices
        float           rest_volume;
        float           compliance;
    };
    std::vector<VolumeConstraint> volumes;  // optional; expensive
};
```

---

## Cloth Manager

```cpp
struct ClothManager {
    JPH::PhysicsSystem* physics_system;       // Reference to main physics world
    Arena*              arena;

    // Active cloth entities
    std::unordered_map<uint32_t, CCloth*> cloths;
    std::unordered_map<uint32_t, CWindZone*> wind_zones;

    // Step cloth simulation at physics framerate (60 Hz)
    void step(float dt);

    // Create cloth from asset
    uint32_t create_cloth(const ClothAsset& asset, const Vec3& position);

    // Wind interaction
    void apply_wind_to_cloth(CCloth* cloth, const CWindZone& wind);

    // Pin/unpin vertices (for scripting)
    void pin_vertex(uint32_t cloth_id, uint32_t vertex_idx, uint32_t bone_id);
    void unpin_vertex(uint32_t cloth_id, uint32_t vertex_idx);

    // Blend control
    void set_blend(uint32_t cloth_id, float physics_blend);
    float get_blend(uint32_t cloth_id) const;
};
```

---

## Skinned Cloth Blending

The key innovation is **partial physics blend**: cloth can be 0% animated (fully physics), 50% blend (partially driven by skeleton), or 100% animated (frozen in skeletal pose).

### Blend Calculation

Each physics frame:

```cpp
void blend_cloth_to_skeleton(CCloth& cloth, const CSkeleton& skeleton) {
    // For each vertex:
    for (uint32_t v = 0; v < cloth.positions.size(); ++v) {
        // 1. Get animated position from skeleton
        Vec3 animated_pos = cloth.get_bone_attachment_position(skeleton, v);

        // 2. Get physics position (from soft body)
        Vec3 physics_pos = cloth.positions[v];

        // 3. Blend
        Vec3 blended_pos = mix(animated_pos, physics_pos, cloth.physics_blend);

        // 4. Update skinned mesh vertex
        update_skinned_mesh_vertex(v, blended_pos);
    }
}
```

**Blend values**:
- `0.0`: Cloth position = animated position (fully skeleton-driven)
- `0.5`: Cloth position = 50% animated, 50% physics
- `1.0`: Cloth position = physics position (fully simulated)

### Use Cases

1. **Idle animation**: blend = 0.0 (clothanimated in place)
2. **Walking/running**: blend = 0.3–0.5 (slight physics wobble, follow skeleton mostly)
3. **Falling**: blend = 1.0 (cloth flails freely)
4. **Wind gust**: blend = 1.0 temporarily, then fade back to 0.3
5. **Ragdoll**: blend = 1.0 (cloth hangs naturally)

---

## Wind Simulation

Wind applies forces to cloth vertices with optional spatial falloff and gusting.

### Wind Force Calculation

```cpp
void apply_wind(CCloth& cloth, const CWindZone& wind, float dt) {
    if (!cloth.wind_enabled) return;

    for (uint32_t v = 0; v < cloth.positions.size(); ++v) {
        Vec3 vertex_pos = cloth.positions[v];

        // Base wind direction (scaled by strength)
        Vec3 wind_force = wind.wind_direction * wind.wind_strength;

        // Add turbulence (Perlin noise variation)
        if (wind.wind_turbulence > 0) {
            float noise = perlin_noise(vertex_pos, time * wind.gust_frequency);
            wind_force += wind.wind_direction * noise * wind.wind_turbulence;
        }

        // Spatial falloff (if enabled)
        if (wind.use_falloff) {
            float dist = distance(vertex_pos, wind.position);
            if (dist < wind.falloff_radius) {
                float falloff = 1.0f - (dist / wind.falloff_radius);
                wind_force *= falloff;
            } else {
                wind_force = Vec3(0);
            }
        }

        // Apply gusts (periodic strength multiplier)
        float gust_cycle = fmod(time * wind.gust_frequency, 1.0f);
        if (gust_cycle < wind.gust_duration) {
            wind_force *= wind.gust_strength;
        }

        // Add to vertex (via SoftBody forces)
        cloth.jolt_softbody->AddForceToVertex(v, wind_force);
    }
}
```

### Wind Physics

Wind pressure = 0.5 × ρ × v² × A, where:
- ρ = air density (1.225 kg/m³)
- v = wind velocity (m/s)
- A = surface area facing wind

For cloth vertex, we approximate A as a triangle fan around the vertex.

---

## Collision System

### Environment Collision

Cloth collides with static geometry (walls, characters, ground) using capsule proxies:

```cpp
struct ClothCollisionSetup {
    // Capsule colliders define collision volume
    std::vector<Vec3> capsule_centers;    // local-space positions
    std::vector<float> capsule_radii;
    std::vector<float> capsule_lengths;

    // Assign capsules to cloth vertices (for per-vertex collision)
    std::vector<uint32_t> vertex_to_capsule;  // which capsule each vertex belongs to
};
```

When soft body steps, Jolt handles vertex-vs-static collision automatically.

### Self-Collision

If `self_collision = true`, cloth vertices cannot overlap:

```cpp
void check_self_collision(CCloth& cloth) {
    if (!cloth.self_collision) return;

    for (uint32_t i = 0; i < cloth.positions.size(); ++i) {
        for (uint32_t j = i + 1; j < cloth.positions.size(); ++j) {
            float dist = distance(cloth.positions[i], cloth.positions[j]);

            // Check if vertices are part of same triangle (allowed to touch)
            if (is_adjacent(i, j, cloth.mesh)) {
                continue;  // Skip connected vertices
            }

            // Separate if too close
            if (dist < cloth.self_collision_margin) {
                Vec3 separation = (cloth.positions[i] - cloth.positions[j]).normalized() *
                                 (cloth.self_collision_margin - dist) * 0.5f;
                cloth.positions[i] += separation;
                cloth.positions[j] -= separation;
            }
        }
    }
}
```

**Note**: Self-collision is expensive (O(n²)). Use only when necessary (capes, long hair, etc.).

---

## Lua Cloth API

All cloth control is exposed via `ctx.cloth` namespace.

### Wind Zone Control

#### `ctx.cloth:set_wind_zone(entity, wind_config) → nil`

Apply or update wind for a cloth entity.

```lua
function M:on_update(ctx, dt)
    if self.has_wind_zone then
        ctx.cloth:set_wind_zone(self, {
            direction = Vec3(1, 0, 0):normalized(),  -- blow from left to right
            strength = 5.0,                          -- m/s²
            turbulence = 0.3,
            gust_frequency = 0.5,                    -- 0.5 Hz = 1 gust every 2 sec
            gust_strength = 2.0,
            gust_duration = 0.5
        })
    end
end
```

#### `ctx.cloth:set_wind_falloff(entity, radius, inner_radius) → nil`

Configure spatial falloff for wind (e.g., explosion blast).

```lua
function M:on_explosion(ctx, explosion_pos, blast_radius)
    -- Cloth near explosion gets strong wind
    ctx.cloth:set_wind_falloff(self, blast_radius, blast_radius * 0.3)
end
```

#### `ctx.cloth:clear_wind_zone(entity) → nil`

Disable wind for a cloth entity.

```lua
ctx.cloth:clear_wind_zone(self)
```

### Pinning Control

#### `ctx.cloth:pin_vertex(entity, vertex_index, bone_name) → bool`

Pin a cloth vertex to a skeletal bone. Returns `true` on success.

```lua
-- Pin shoulder vertices to shoulder bone
for i = 1, 4 do
    ctx.cloth:pin_vertex(self, i - 1, "Armature.Shoulder")
end
```

#### `ctx.cloth:unpin_vertex(entity, vertex_index) → bool`

Unpin a vertex (release from bone attachment).

```lua
-- Release all cape vertices when entering water
for i = 0, self.num_cape_vertices - 1 do
    ctx.cloth:unpin_vertex(self, i)
end
```

#### `ctx.cloth:pin_group(entity, group_name, bone_name) → nil`

Pin an entire vertex group to a bone (defined in `.uzcloth`).

```lua
ctx.cloth:pin_group(self, "shoulder_pins", "Armature.Shoulder")
ctx.cloth:pin_group(self, "upper_back", "Armature.Spine.001")
```

### Blend Control

#### `ctx.cloth:set_blend(entity, blend_factor) → nil`

Set physics blend (0.0 = full skeleton, 1.0 = full physics).

```lua
function M:on_update(ctx, dt)
    local speed = self:get_velocity():length()
    if speed < 1.0 then
        -- Standing idle: full skeleton
        ctx.cloth:set_blend(self, 0.0)
    elseif speed < 5.0 then
        -- Walking: partial physics
        ctx.cloth:set_blend(self, 0.3)
    else
        -- Running: more physics
        ctx.cloth:set_blend(self, 0.5)
    end
end
```

#### `ctx.cloth:get_blend(entity) → float`

Return current blend factor.

```lua
local blend = ctx.cloth:get_blend(self)
ctx:log("Cloth blend: " .. (blend * 100) .. "%")
```

#### `ctx.cloth:set_blend_easing(entity, easing_duration) → nil`

Smooth blend transitions over time.

```lua
-- Smoothly transition to full physics over 0.5 seconds
ctx.cloth:set_blend_easing(self, 0.5)
ctx.cloth:set_blend(self, 1.0)  -- target blend (easing will interpolate)
```

### State Queries

#### `ctx.cloth:get_vertex_count(entity) → int`

Return number of simulated vertices (may be reduced by LOD).

```lua
local vert_count = ctx.cloth:get_vertex_count(self)
ctx:log("Simulating " .. vert_count .. " cloth vertices")
```

#### `ctx.cloth:get_wind_force(entity) → float`

Return magnitude of current applied wind (for feedback/VFX).

```lua
local wind_mag = ctx.cloth:get_wind_force(self)
if wind_mag > 10.0 then
    self:play_strong_wind_sound(ctx)
end
```

#### `ctx.cloth:is_simulating(entity) → bool`

Return `true` if cloth is actively being simulated (may pause when off-screen).

```lua
if not ctx.cloth:is_simulating(self) then
    -- Cloth frozen; don't apply wind
    return
end
```

---

## `.uzscene` Syntax

### Clothed Character

```ini
[node id=10 type=Node3D name="Character" parent=1]
position = [0, 1, 0]
mesh = res://meshes/character.uzmesh         ; skeletal mesh
skeleton = Armature                          ; skeleton name

; Cloth: cape attached to shoulders
[node id=11 type=Node3D name="Cape" parent=10]
cloth_asset = res://cloth/cape.uzcloth
cloth_mesh = res://meshes/cape.uzmesh
cloth_mass_per_vertex = 0.05
cloth_damping = 0.3
cloth_stiffness = 0.8
cloth_self_collision = false
cloth_wind_enabled = true

; Cape script for physics blending
[node id=12 type=Node3D name="CapeScript" parent=11]
script = res://scripts/cape_controller.lua

; Cloth: long hair attached to head
[node id=13 type=Node3D name="Hair" parent=10]
cloth_asset = res://cloth/long_hair.uzcloth
cloth_mesh = res://meshes/hair.uzmesh
cloth_mass_per_vertex = 0.02
cloth_damping = 0.4
cloth_stiffness = 0.9
cloth_self_collision = true                 ; hair collides with itself
cloth_self_collision_margin = 0.01
cloth_wind_enabled = true
```

### Flag (Simple Cloth)

```ini
[node id=50 type=Node3D name="FlagPole" parent=1]
position = [10, 2, 0]

[node id=51 type=Node3D name="Flag" parent=50]
position = [0.5, 0, 0]
cloth_asset = res://cloth/flag.uzcloth
cloth_mesh = res://meshes/flag_fabric.uzmesh
cloth_mass_per_vertex = 0.01
cloth_damping = 0.2
cloth_stiffness = 0.7
cloth_wind_enabled = true
```

---

## Cloth Events

### `ClothEvent` — ECS Event

```cpp
struct ClothEvent {
    NodeHandle cloth_entity;
    enum class Type {
        CollisionEnter,       // cloth hit something
        CollisionExit,        // cloth separated
        PinAttached,          // vertex pinned to bone
        PinDetached,          // vertex released
        BlendChanged,         // physics/skeleton blend changed
        WindActive,           // wind applied
        SelfCollisionDetect,  // vertices overlapped
    };
    Type       type;
    Vec3       position;               // contact point or event location
    float      magnitude;              // impact force, wind strength, etc.
};
```

### Lua Callbacks

#### `M:on_cloth_collision(ctx, contact_info)`

Called when cloth vertex hits environment.

```lua
function M:on_cloth_collision(ctx, contact_info)
    if contact_info.magnitude > 5.0 then
        self:play_impact_sound(ctx, contact_info.position)
    end
end
```

---

## Physics Debug Draw

In editor mode, cloth visualization shows:

```cpp
if (UZ_EDITOR) {
    for (auto& [entity_id, cloth] : cloth_manager.cloths) {
        // Draw cloth vertices
        for (const Vec3& pos : cloth.positions) {
            draw_sphere(pos, 0.02f, {1.0f, 1.0f, 0.0f});
        }

        // Draw edges (constraints)
        for (const Edge& edge : cloth.edges) {
            Vec3 p0 = cloth.positions[edge.v0];
            Vec3 p1 = cloth.positions[edge.v1];
            draw_line(p0, p1, {0.5f, 0.5f, 1.0f});
        }

        // Draw pinned vertices (attachment points)
        for (const Attachment& att : cloth.attachments) {
            for (uint32_t v_idx : att.vertex_indices) {
                Vec3 pos = cloth.positions[v_idx];
                draw_sphere(pos, 0.03f, {0.0f, 1.0f, 0.0f});
            }
        }

        // Draw wind force vectors
        if (cloth.wind_enabled && cloth.total_wind_force > 0) {
            for (uint32_t i = 0; i < cloth.positions.size(); i += 4) {
                Vec3 pos = cloth.positions[i];
                Vec3 wind_dir = cloth.wind_direction * (cloth.total_wind_force * 0.1f);
                draw_arrow(pos, pos + wind_dir, {1.0f, 0.5f, 0.0f});
            }
        }
    }
}
```

---

## Common Patterns

### Character Cape

```lua
local M = {}

function M:on_start(ctx)
    self.physics_blend = 0.0
    self.idle_blend = 0.0
    self.blend_speed = 2.0  -- blend per second
end

function M:on_update(ctx, dt)
    local parent_speed = self:get_parent():get_velocity():length()

    local target_blend
    if parent_speed < 0.5 then
        target_blend = 0.0  -- stationary: full skeleton
    elseif parent_speed < 3.0 then
        target_blend = 0.2  -- slow walk
    elseif parent_speed < 6.0 then
        target_blend = 0.4  -- normal walk
    else
        target_blend = 0.6  -- running
    end

    -- Smooth blend transition
    self.physics_blend = lerp(self.physics_blend, target_blend, self.blend_speed * dt)
    ctx.cloth:set_blend(self, self.physics_blend)
end

function M:on_cloth_collision(ctx, contact_info)
    if contact_info.magnitude > 3.0 then
        self:play_cape_flap_sound(ctx)
    end
end

return M
```

### Windy Flag

```lua
local M = {}

function M:on_start(ctx)
    self.wind_time = 0
    self.gust_interval = 2.0
end

function M:on_update(ctx, dt)
    self.wind_time = self.wind_time + dt

    -- Continuous wind with occasional gusts
    local base_strength = 3.0
    local gust_strength = 8.0

    local is_gusting = math.floor(self.wind_time / self.gust_interval) % 2 == 1

    ctx.cloth:set_wind_zone(self, {
        direction = Vec3(1, 0, 0):normalized(),
        strength = is_gusting and gust_strength or base_strength,
        turbulence = 0.2,
        gust_frequency = 0.5,
        gust_strength = 1.5,
        gust_duration = 0.3
    })
end

return M
```

---

## Performance Considerations

### Vertex Count

- **Hair**: 2,000–5,000 vertices
- **Cape/skirt**: 1,000–3,000 vertices
- **Flag**: 500–1,000 vertices

Constraint solving is O(vertices × edges), so optimize edge count.

### Optimization Tips

1. **Reduce mesh density**: Use LOD meshes (simplified topology)
2. **Limit pinning**: Pin only shoulder/upper vertices, free lower cloth
3. **Skip self-collision**: Only use if visible clingyness is obvious
4. **Batch constraints**: Group bend/stretch constraints by region
5. **Use LOD**: Beyond `cloth_lod_distance` (default: 50m), reduce vertex count 50–75%

### LOD System

Cloth beyond `cloth_lod_distance` uses a simplified vertex set and fewer constraint iterations. Visible vertices are interpolated from LOD positions.

---

## Phase Implementation Details

### Phase 4 — Cloth Simulation (this phase)

**Deliverables**:
1. `CCloth` and `CWindZone` ECS components
2. `ClothManager` singleton in `game.dll`
3. `.uzcloth` INI asset format with constraint definitions
4. Jolt `SoftBody` integration
5. Physics-skeleton blending with easing
6. Wind simulation with turbulence and gusting
7. Self-collision and environment collision
8. Lua cloth API: set_wind_zone, pin_vertex, set_blend, get_wind_force
9. Cloth events and Lua callbacks
10. Physics debug visualization
11. Editor IPC commands for cloth inspection

**Out of scope (Phase 5+)**:
- Complex tear mechanics (dynamic mesh modification)
- Advanced wrinkle simulation (requires surface deformation)
- Fluid-aware cloth (splash, buoyancy)
- Vegetation cloth (grass, leaves)

---

## Related Documentation

- [specs/physics-system.md](physics-system.md) — Base physics system
- [specs/skeletal-mesh.md](skeletal-mesh.md) — GPU skinning, skeleton, bone structure
- [specs/animation-system.md](animation-system.md) — Skeletal animation blending
- [specs/particle-system.md](particle-system.md) — Interaction with particle effects (wind-blown particles)
- [guides/physics-advanced-guide.md](../guides/physics-advanced-guide.md) — Cloth setup and debugging

---

## C++ Source Structure

```
src/engine/physics/
├── cloth_system.h/cpp          — ClothManager, CCloth, CWindZone
├── cloth_constraints.h/cpp     — Jolt SoftBody constraint setup
├── cloth_blending.h/cpp        — Physics-skeleton blending logic
├── cloth_wind.h/cpp            — Wind force calculation, turbulence
├── cloth_collision.h/cpp       — Self-collision, environment collision
├── cloth_config.h/cpp          — .uzcloth parser, asset loading
└── cloth_debug_draw.h/cpp      — Debug visualization

src/engine/ecs/components.h     — CCloth, CWindZone component definitions

src/engine/scripting/           — Lua bindings
└── lua_cloth_binding.h/cpp     — ctx.cloth namespace

game/
└── cloth_callbacks.cpp         — on_cloth_* Lua method dispatch
```

---

## Integration with Other Systems

### Skeletal Animation

Cloth blends between bone positions and physics simulation. Bones drive pinned vertices; free vertices fall under gravity and wind.

### Audio System

Cloth collision impacts trigger impact SFX; wind turbulence can trigger ambient cloth rustling.

### VFX System

Wind zones affect particles (flags, leaves, dust) via shared `.uzwind` assets.
