# Destructible Objects Spec

> **Status**: Planned (Phase 4)
> **Library**: Jolt Physics (rigid bodies) + Voronoi fracture (offline)
> **Owner**: `game.dll` (C++ destructible manager)
> **Lua API**: Available Phase 4+ via `ctx.destructible` for breakage, impulse queries, debris pooling

---

## Overview

The Destructible Objects System provides dynamic destruction and fragmentation of mesh objects using pre-computed Voronoi fracture patterns. When a destructible object receives sufficient impulse (collision, explosion, etc.), it breaks into fragments that fall as separate rigid bodies. Debris is pooled for performance and automatically cleaned up after a lifetime.

**Key principles**:
- Fracture pattern is computed offline (Voronoi cells), stored as `.uzfrac` asset
- Runtime: impulse-based breaking (threshold triggers activation)
- Each fracture cell becomes a separate rigid body with collision
- Debris is pooled and recycled to avoid memory spikes
- Partial destruction supported (cells break independently)
- Full editor visualization with cell previews and impulse vectors

---

## Architecture

### Destructible Component Structure

```cpp
// Main destructible component (one per destructible entity)
struct CDestructible {
    // Asset reference
    StringId            fracture_asset_id;     // .uzfrac file
    StringId            mesh_id;               // original mesh

    // Physics
    JPH::BodyID         original_body_id;      // original rigid body (disabled on break)
    std::vector<JPH::BodyID> fragment_bodies;  // active debris rigid bodies

    // Fracture state
    struct FractureCell {
        uint32_t        cell_id;               // unique within this object
        StringId        mesh_chunk_id;         // chunk of original mesh
        bool            is_active;             // true if broken off
        Vec3            center_of_mass;        // local-space COM
        JPH::BodyID     body_id;               // Jolt body ID (when active)
        float           health;                // per-cell durability (optional)
        float           mass;                  // cell mass
    };
    std::vector<FractureCell> cells;

    // Breakage thresholds
    float               break_impulse_threshold;     // N·s (min impulse to break)
    float               break_stress_threshold;      // Pa (stress-based breaking)
    bool                propagate_damage;           // cells break neighboring cells

    // Debris management
    uint32_t            max_active_fragments;       // pool size limit
    float               fragment_lifetime;          // seconds before cleanup
    bool                fragment_pooling;           // reuse rigid bodies

    // State (read-only; updated each physics frame)
    float               total_damage;              // accumulated impulse
    std::vector<uint32_t> broken_cells;            // which cells have fractured
    bool                is_destroyed;              // all cells broken
    uint32_t            active_fragment_count;     // currently simulating fragments
};

// Fracture asset (metadata + cell data)
struct FractureAsset {
    StringId            name;
    uint32_t            num_cells;

    struct Cell {
        uint32_t        id;
        Vec3            position;                   // world-space position
        std::vector<Vec3> vertices;                // mesh vertices for this cell
        std::vector<uint32_t> indices;             // triangle indices
        float            volume;                    // m³
        float            mass;                      // kg
        std::vector<uint32_t> neighbors;           // adjacent cell IDs
        Bounds           bounds;                    // AABB
        float            surface_area;             // m²
    };
    std::vector<Cell> cells;

    // Connectivity for propagation
    struct AdjacencyEdge {
        uint32_t        cell_a, cell_b;
        float           surface_area;              // shared surface
        float           strength;                  // break strength
    };
    std::vector<AdjacencyEdge> adjacency;

    // Material reference
    StringId            material_id;               // shared material for fragments
    StringId            impact_sound_id;           // SFX played on break
};

// Debris pool (reuses rigid bodies for performance)
struct DebrisPool {
    struct PooledDebris {
        JPH::BodyID     body_id;
        bool            in_use;
        float           time_remaining;           // lifetime countdown
        uint32_t        destructible_id;          // parent destructible
        uint32_t        cell_id;                  // which cell
    };
    std::vector<PooledDebris> pool;
    uint32_t            next_available_index;     // for fast allocation
};
```

### Destructible Manager

```cpp
struct DestructibleManager {
    JPH::PhysicsSystem* physics_system;           // Reference to main physics
    Arena*              arena;
    DebrisPool          debris_pool;              // shared debris pool

    // Active destructibles indexed by entity ID
    std::unordered_map<uint32_t, CDestructible*> destructibles;

    // Update fracture state each physics frame
    void step(float dt);

    // Create destructible from fracture asset
    uint32_t create_destructible(const FractureAsset& asset, const Vec3& position);

    // Apply impulse to destructible (returns broken cells)
    std::vector<uint32_t> apply_impulse(uint32_t destructible_id, const Vec3& position,
                                       const Vec3& impulse);

    // Manually break specific cell
    void break_cell(uint32_t destructible_id, uint32_t cell_id);

    // Query state
    const CDestructible* get_destructible(uint32_t entity_id) const;
    float get_damage(uint32_t destructible_id) const;
};
```

---

## Fracture Asset Format (`.uzfrac`)

Fracture patterns are computed offline and stored as `.uzfrac` INI files with embedded mesh data.

### `.uzfrac` Structure

```ini
[Fracture]
name = wood_crate
source_mesh = res://meshes/crate.uzmesh
voronoi_sites = 8                           ; number of fracture cells
fracture_method = voronoi                   ; voronoi | radial | planar
material = res://materials/wood.uzmat
impact_sound = res://audio/wood_break.wav

[Physics]
total_mass = 10.0                           ; kg
density = 500.0                             ; kg/m³
friction = 0.6
restitution = 0.1

[Breakage]
impulse_threshold = 50.0                    ; N·s (minimum to break)
stress_threshold = 100000.0                 ; Pa (propagation threshold)
propagate_damage = true
max_propagation_distance = 2                ; cells away from impact

; Cell definitions (one per fracture)
[Cell_0]
center = [0, 0, 0]
volume = 1.0
mass = 1.25
neighbors = [1, 2, 3]                       ; adjacent cell IDs
surface_area = 2.5
; Mesh data stored in embedded binary section (vertices, indices)

[Cell_1]
center = [0.5, 0, 0]
volume = 0.9
mass = 1.125
neighbors = [0, 2, 4]
surface_area = 2.3

; Adjacency/connectivity
[Adjacency]
edge_0_1 = 0.8, 50000                       ; shared surface area, bond strength
edge_0_2 = 0.7, 50000
edge_1_2 = 0.9, 55000
edge_2_3 = 0.6, 45000
```

### Example: Glass Vase

```ini
[Fracture]
name = glass_vase
source_mesh = res://meshes/vase_hp.uzmesh
voronoi_sites = 12
fracture_method = voronoi
material = res://materials/glass_clear.uzmat
impact_sound = res://audio/glass_shatter.wav

[Physics]
total_mass = 2.0
density = 2500.0                            ; glass is dense
friction = 0.3
restitution = 0.8                           ; glass bounces!

[Breakage]
impulse_threshold = 20.0                    ; glass breaks easily
stress_threshold = 50000.0
propagate_damage = true
max_propagation_distance = 3
```

---

## Voronoi Fracture Generation (Offline)

The offline fracture generator creates Voronoi cells from a source mesh:

```cpp
// Tool: UzBuild fracture <input.obj/gltf> <output.uzfrac> --sites=N

class FractureGenerator {
    // 1. Load source mesh
    Mesh source_mesh = load_mesh(input_path);

    // 2. Generate Voronoi sites (random or grid)
    std::vector<Vec3> sites = generate_voronoi_sites(source_mesh.bounds(), num_sites);

    // 3. Compute Voronoi cells
    std::vector<VoronoiCell> cells = compute_voronoi_partition(source_mesh, sites);

    // 4. Extract mesh chunk per cell
    std::vector<Mesh> cell_meshes;
    for (const auto& cell : cells) {
        Mesh chunk = extract_mesh_chunk(source_mesh, cell);

        // Auto-compute interior UV mapping (for interior faces)
        compute_interior_uvs(chunk);

        cell_meshes.push_back(chunk);
    }

    // 5. Save as .uzfrac with embedded mesh data
    save_fracture_asset(output_path, cells, cell_meshes, source_material);
};
```

### Interior UV Mapping

When a mesh fractures, newly exposed faces (interior surfaces) need UVs. The system auto-computes a planar projection UV space for interior faces:

```glsl
// Shader fragment: interior faces use alternate UV space
if (is_interior_face) {
    uv = interior_uv;  // planar projection coordinates
    color = sample_interior_texture(uv);  // dust, concrete, etc.
} else {
    uv = original_uv;
    color = sample_surface_texture(uv);   // original material
}
```

---

## Runtime Breaking System

### Impulse Detection & Breaking

Each physics frame, destructibles check collision impulses:

```cpp
void DestructibleManager::step(float dt) {
    for (auto& [entity_id, destructible] : destructibles) {
        // Query contact events for original body
        for (const CollisionEvent& event : collision_events) {
            if (event.node_a.id == entity_id || event.node_b.id == entity_id) {
                // Calculate impact force at contact point
                Vec3 position = event.contact_point;
                float impulse_magnitude = event.collision_impulse;
                Vec3 impulse = event.contact_normal * impulse_magnitude;

                // Check if exceeds threshold
                if (impulse_magnitude > destructible->break_impulse_threshold) {
                    break_destructible_at(destructible, position, impulse);
                }
            }
        }
    }
}
```

### Breaking Algorithm

```cpp
void break_destructible_at(CDestructible* destructible, const Vec3& impact_pos,
                          const Vec3& impact_impulse) {
    float impulse_mag = impact_impulse.length();

    // Find nearest cell to impact point
    uint32_t primary_cell = find_nearest_cell(destructible, impact_pos);

    // Break primary cell
    break_cell_internal(destructible, primary_cell, impulse_mag);

    // Propagate damage to neighbors (if enabled)
    if (destructible->propagate_damage) {
        std::queue<uint32_t> to_process;
        std::set<uint32_t> processed;

        to_process.push(primary_cell);

        while (!to_process.empty()) {
            uint32_t cell_id = to_process.front();
            to_process.pop();

            if (processed.count(cell_id)) continue;
            processed.insert(cell_id);

            const FractureCell& cell = destructible->cells[cell_id];

            // Check each neighbor
            for (uint32_t neighbor_id : cell.neighbors) {
                if (processed.count(neighbor_id)) continue;

                // Propagate if stress exceeds threshold
                float propagation_stress = impulse_mag / cell.surface_area;
                if (propagation_stress > destructible->break_stress_threshold) {
                    break_cell_internal(destructible, neighbor_id, impulse_mag * 0.8f);
                    to_process.push(neighbor_id);
                }
            }
        }
    }

    // Dispatch event
    dispatch_event(DestructibleBrokenEvent{
        entity_id: destructible->entity_id,
        broken_cells: destructible->broken_cells,
        impact_position: impact_pos,
        impact_impulse: impact_impulse
    });
}
```

### Debris Activation

When a cell breaks, it becomes a separate rigid body:

```cpp
void break_cell_internal(CDestructible* destructible, uint32_t cell_id, float impulse_mag) {
    CDestructible::FractureCell& cell = destructible->cells[cell_id];

    if (cell.is_active) return;  // already broken
    cell.is_active = true;
    destructible->broken_cells.push_back(cell_id);

    // Allocate debris from pool
    uint32_t debris_index = debris_pool.allocate();
    PooledDebris& debris = debris_pool.pool[debris_index];

    // Create rigid body for this cell
    Mesh cell_mesh = load_cell_mesh(destructible->fracture_asset_id, cell_id);
    JPH::Shape* shape = create_shape_from_mesh(cell_mesh);
    JPH::Body* body = physics_system->AddBody(shape, cell.mass);

    // Position at cell center
    body->SetPosition(cell.center_of_mass);

    // Apply impulse (propagates outward)
    Vec3 force_direction = (cell.center_of_mass - destructible->position).normalized();
    body->AddImpulse(force_direction * impulse_mag);

    // Set lifetime and pooling info
    debris.body_id = body->GetID();
    debris.in_use = true;
    debris.time_remaining = destructible->fragment_lifetime;
    debris.destructible_id = destructible->entity_id;
    debris.cell_id = cell_id;

    cell.body_id = debris.body_id;
    destructible->active_fragment_count++;

    // Disable original body if all cells are broken
    if (destructible->broken_cells.size() == destructible->cells.size()) {
        physics_system->GetBody(destructible->original_body_id)->SetEnabled(false);
        destructible->is_destroyed = true;
    }
}
```

### Debris Cleanup

Debris is recycled by the pool:

```cpp
void DebrisPool::step(float dt) {
    for (auto& debris : pool) {
        if (!debris.in_use) continue;

        debris.time_remaining -= dt;

        if (debris.time_remaining <= 0) {
            // Return to pool
            physics_system->RemoveBody(debris.body_id);
            debris.in_use = false;
            debris.destructible_id = 0;
            debris.cell_id = 0;
        }
    }
}
```

---

## Lua Destructible API

All destructible control is exposed via `ctx.destructible` namespace.

### Breaking Control

#### `ctx.destructible:break_at(entity, position, impulse) → nil`

Manually trigger breakage at a specific location with an impulse vector.

```lua
-- Explosion breaks nearby destructible
function M:on_explosion(ctx, explosion_pos, blast_radius)
    local objects = ctx.world:overlap_sphere(explosion_pos, blast_radius)

    for _, obj in ipairs(objects) do
        if obj:has_tag("destructible") then
            local direction = (obj.position - explosion_pos):normalized()
            local force = 100.0  -- N·s
            ctx.destructible:break_at(obj, explosion_pos, direction * force)
        end
    end
end
```

#### `ctx.destructible:break_cell(entity, cell_index) → bool`

Break a specific fracture cell (0-indexed). Returns `true` if successful.

```lua
-- Smash first (weakest) cell
local cell_broken = ctx.destructible:break_cell(self, 0)
if cell_broken then
    self:play_crack_sound(ctx)
end
```

#### `ctx.destructible:is_fully_destroyed(entity) → bool`

Return `true` if all cells are broken.

```lua
if ctx.destructible:is_fully_destroyed(self) then
    ctx.scene:remove_node(self)
end
```

### State Queries

#### `ctx.destructible:get_damage(entity) → float`

Return accumulated damage (impulse sum). Useful for health-bar display.

```lua
local damage = ctx.destructible:get_damage(self)
local damage_pct = damage / self.max_damage * 100
ctx:log(string.format("Damage: %.0f%%", damage_pct))
```

#### `ctx.destructible:get_broken_cells(entity) → int[]`

Return list of broken cell indices.

```lua
local broken = ctx.destructible:get_broken_cells(self)
ctx:log("Broken cells: " .. #broken .. " / " .. self.total_cells)
```

#### `ctx.destructible:get_active_fragments(entity) → int`

Return count of active debris pieces currently simulating.

```lua
if ctx.destructible:get_active_fragments(self) > 20 then
    self:request_lod_reduction(ctx)  -- too many pieces
end
```

---

## `.uzscene` Syntax

### Breakable Crate

```ini
[node id=80 type=Node3D name="Crate" parent=1]
position = [5, 1, 0]
rotation = [0, 0, 0]
mesh = res://meshes/crate.uzmesh
destructible = res://fractures/wood_crate.uzfrac
destructible_impulse_threshold = 50.0
destructible_propagate_damage = true
destructible_fragment_lifetime = 5.0

; Optional: script to track breakage
[node id=81 type=Node3D name="Script" parent=80]
script = res://scripts/breakable_crate.lua
```

### Glass Vase with Custom Handling

```ini
[node id=90 type=Node3D name="Vase" parent=1]
position = [0, 0.5, 0]
mesh = res://meshes/vase_hp.uzmesh
destructible = res://fractures/glass_vase.uzfrac
destructible_impulse_threshold = 20.0      ; glass breaks easily
destructible_propagate_damage = true
destructible_fragment_lifetime = 3.0        ; disappear faster

[node id=91 type=Node3D name="VaseController" parent=90]
script = res://scripts/glass_vase_controller.lua
```

---

## Destructible Events

### `DestructibleBrokenEvent` — ECS Event

```cpp
struct DestructibleBrokenEvent {
    NodeHandle  destructible_entity;
    std::vector<uint32_t> broken_cells;     // which cells fractured
    Vec3        impact_position;             // world-space break location
    Vec3        impact_impulse;              // force vector
    float       impact_magnitude;            // impulse magnitude
};
```

### Lua Callbacks

#### `M:on_destructible_broken(ctx, event_data)`

Called when destructible object breaks.

```lua
function M:on_destructible_broken(ctx, event)
    ctx:log("Object broke at " .. event.impact_position:str())

    -- Spawn debris effect
    ctx.scene:spawn_node("dust_cloud", event.impact_position)

    -- Play sound
    self:play_break_sound(ctx, event.impact_magnitude)
end
```

---

## Physics Debug Draw

In editor mode, fracture visualization shows:

```cpp
if (UZ_EDITOR) {
    for (auto& [entity_id, destructible] : destructible_manager.destructibles) {
        for (size_t i = 0; i < destructible->cells.size(); ++i) {
            const CDestructible::FractureCell& cell = destructible->cells[i];

            // Draw cell bounding box
            Vec3 color = cell.is_active ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            draw_box(cell.bounds, color);

            // Label cell ID
            draw_text(cell.center_of_mass, "Cell " + std::to_string(i));

            // Show connectivity
            for (uint32_t neighbor_id : cell.neighbors) {
                const Vec3& neighbor_com = destructible->cells[neighbor_id].center_of_mass;
                draw_line(cell.center_of_mass, neighbor_com, Vec3(0.5f, 0.5f, 1.0f));
            }
        }

        // Draw last impact point
        if (destructible->total_damage > 0) {
            draw_sphere(destructible->last_impact_pos, 0.2f, Vec3(1, 1, 0));
        }
    }
}
```

---

## Common Patterns

### Crate Puzzle (Break Open)

```lua
local M = {}

function M:on_start(ctx)
    self.item_inside = "key"
    self.fully_broken = false
end

function M:on_destructible_broken(ctx, event)
    local damage_pct = 100 * event.impact_magnitude / self.max_impulse

    if damage_pct > 50 and not self.fully_broken then
        self.fully_broken = true

        -- Drop item
        local item = ctx.scene:spawn_node("item_" .. self.item_inside,
                                         self.position + Vec3(0, 1, 0))
        item:apply_impulse(event.impact_impulse * 0.5)

        ctx:log("Crate broken! Got: " .. self.item_inside)
    end
end

return M
```

### Glass Breaking on Impact

```lua
local M = {}

function M:on_start(ctx)
    self.impact_threshold = 20.0  -- N·s
    self.has_broken = false
end

function M:on_collision_enter(ctx, other, contact)
    if not self.has_broken and contact.impulse > self.impact_threshold then
        self.has_broken = true

        -- Break with impulse from collision
        ctx.destructible:break_at(self, contact.point, contact.relative_velocity * 5.0)

        self:play_shatter_sound(ctx, contact.impulse)
    end
end

return M
```

---

## Performance Considerations

### Fragment Count

- **Debris pool size**: 50–100 pieces max (all destructibles combined)
- **Per-object**: 4–20 fragments typical (8–12 Voronoi cells optimal)
- **Lifetime**: 3–5 seconds before cleanup

High fragment count stalls physics solver.

### Optimization Tips

1. **Limit Voronoi sites**: Use 8–12 cells, not 20+
2. **Disable propagation**: For small objects, break only impact cell
3. **Shorter lifetime**: 2–3 seconds → faster cleanup
4. **LOD fragments**: Beyond 50m, hide fragments entirely
5. **No self-collision**: Debris doesn't need to collide with itself

### LOD System

Beyond `destructible_lod_distance` (default: 100m), destructible objects don't simulate fractures—they disappear entirely when broken.

---

## Phase Implementation Details

### Phase 4 — Destructibles (this phase)

**Deliverables**:
1. `CDestructible` ECS component
2. `DestructibleManager` singleton in `game.dll`
3. `.uzfrac` INI asset format with embedded mesh data
4. Voronoi fracture generator (UzBuild tool)
5. Jolt rigid body pool for debris
6. Impulse-based breaking with propagation
7. Lua destructible API: break_at, break_cell, is_fully_destroyed, get_damage
8. Destructible events and Lua callbacks
9. Physics debug visualization
10. IPC commands for destructible inspection

**Out of scope (Phase 5+)**:
- Progressive deformation (before breaking)
- Debris sticking/adhering to surfaces
- Breakable constraints (ropes, chains)
- Procedural mesh generation on break
- Network replication of debris

---

## Related Documentation

- [specs/physics-system.md](physics-system.md) — Base physics system, rigid bodies
- [specs/asset-pipeline.md](asset-pipeline.md) — Asset cooking, offline processing
- [guides/physics-advanced-guide.md](../guides/physics-advanced-guide.md) — Setup and debugging
- [engine/design-decisions.md](../engine/design-decisions.md) — Why Voronoi + pre-computed

---

## C++ Source Structure

```
src/engine/physics/
├── destructible_system.h/cpp    — DestructibleManager, CDestructible
├── fracture_asset.h/cpp         — .uzfrac loading, cell mesh extraction
├── debris_pool.h/cpp            — DebrisPool, lifecycle management
├── breaking_logic.h/cpp         — Impulse detection, propagation algorithm
├── fracture_generator.h/cpp     — Voronoi partition, cell extraction
└── destructible_debug_draw.h/cpp — Debug visualization

src/engine/ecs/components.h      — CDestructible component definition

src/engine/scripting/            — Lua bindings
└── lua_destructible_binding.h/cpp — ctx.destructible namespace

src/tools/
├── uz_fracture.cpp              — CLI tool: UzBuild fracture <input> <output>

game/
└── destructible_callbacks.cpp   — on_destructible_* Lua method dispatch
```

---

## Integration with Other Systems

### Physics System

Destructibles integrate with Jolt's collision event system. When a breakage impulse exceeds threshold, cells convert to separate rigid bodies.

### Audio System

Impact SFX is defined per fracture asset. Breaking triggers sound events with impulse magnitude for volume/pitch modulation.

### VFX System

Break events can spawn particle systems (dust clouds, sparks) at impact points.
