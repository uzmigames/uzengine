# Spatial Systems Spec

> **Status**: Planned (Phase 4)
> **Owner**: `game.dll` (Spatial Index, BVH, Octree)
> **Lua API**: Full integration via `ctx.world:query_*` methods
> **Thread safety**: Game Logic thread only

---

## Overview

Naive spatial queries in the ECS (e.g., `registry.view<CTransform>()` to find enemies) are O(n) — scanning all entities every query. For games with 1,000+ entities, this becomes a bottleneck.

The Spatial Systems layer provides **efficient spatial indexing** — BVH (Bounding Volume Hierarchy) for fast range queries and raycasting. The index is automatically maintained as entities move (via a reactive observer on `CTransform` changes) and is transparent to game code.

**Key principles**:
- SpatialIndex is a generic interface; multiple implementations (BVH, Octree)
- BVH is default for dynamic scenes; Octree for large open worlds
- Queries are O(log n) instead of O(n)
- Integration with physics broad-phase for collision detection
- Full Lua API: `ctx.world:query_sphere()`, `ctx.world:raycast()`
- Editor visualization of BVH/Octree structure

---

## Problem Statement

### Before Spatial Indexing (O(n) queries)

```lua
-- Find all enemies within 10m of player
local enemies_nearby = {}

-- This iterates ALL entities in the scene
ctx.world:iter(function(node)
    local dist = vec3.distance(node.position, player.position)
    if dist < 10.0 then
        table.insert(enemies_nearby, node)
    end
end)

-- With 10k entities, this is slow
```

**Cost**: 10,000 distance calculations per query. Multiple queries per frame = unacceptable.

### After Spatial Indexing (O(log n) queries)

```lua
-- Find all enemies within 10m of player
local enemies_nearby = ctx.world:query_sphere(player.position, 10.0, {
    tags = ["is_enemy"],  -- Optional filtering
})

-- BVH automatically discards 99% of entities in early traversal
-- Only ~50 AABBs tested instead of 10k
```

**Cost**: ~50 intersection tests. Order of magnitude faster.

---

## Architecture

### SpatialIndex Interface

All spatial implementations inherit from a common interface:

```cpp
// src/game/spatial_index.h

enum class SpatialQueryType {
    Sphere,      // query_sphere(center, radius)
    AABB,        // query_aabb(min, max)
    Ray,         // raycast(origin, direction, max_distance)
    Frustum,     // query_frustum(camera frustum) — for culling
};

struct SpatialQueryResult {
    std::vector<NodeHandle> nodes;
    std::vector<float> distances;  // Distance from query origin (optional)
};

class SpatialIndex {
public:
    virtual ~SpatialIndex() = default;

    // Insert entity at position with bounds
    virtual void insert(NodeHandle node, const AABB& bounds) = 0;

    // Remove entity from index
    virtual void remove(NodeHandle node) = 0;

    // Update entity bounds when it moves (called by reactive observer)
    virtual void update(NodeHandle node, const AABB& bounds) = 0;

    // Query: sphere
    virtual SpatialQueryResult query_sphere(
        Vec3 center, float radius,
        const QueryOptions& options = {}
    ) const = 0;

    // Query: AABB
    virtual SpatialQueryResult query_aabb(
        const AABB& bounds,
        const QueryOptions& options = {}
    ) const = 0;

    // Query: ray
    virtual SpatialQueryResult raycast(
        Vec3 origin, Vec3 direction, float max_distance,
        const QueryOptions& options = {}
    ) const = 0;

    // Query: frustum (camera frustum for visibility culling)
    virtual SpatialQueryResult query_frustum(
        const Frustum& frustum,
        const QueryOptions& options = {}
    ) const = 0;

    // Rebuild index (called periodically or on demand)
    virtual void rebuild() = 0;

    // Get statistics (debugging)
    virtual size_t node_count() const = 0;
    virtual float depth() const = 0;
};

struct QueryOptions {
    std::vector<uint32_t> tag_filter;  // Only return nodes with these tags
    uint32_t max_results = UINT32_MAX; // Limit results
    bool sort_by_distance = true;      // Sort by distance from query origin
};
```

---

## BVH (Bounding Volume Hierarchy) Implementation

### Overview

A BVH is a binary tree where each node contains an AABB and a list of child nodes or triangles. Interior nodes partition space; leaf nodes contain actual entities.

BVHs are efficient for:
- Dynamic scenes (entities moving frequently)
- Raycasting (tight ray/box intersection tests)
- Memory-conscious setups (no grid allocation)

### Top-Down Construction

```cpp
// src/game/bvh.h

struct BVHNode {
    AABB bounds;
    bool is_leaf;
    union {
        struct {
            uint32_t left_child;   // Index in nodes_ array
            uint32_t right_child;
        } internal;
        struct {
            std::vector<NodeHandle> entities;
        } leaf;
    };
};

class BVH : public SpatialIndex {
public:
    explicit BVH(const BVHConfig& config = {});

    // Insert entity
    void insert(NodeHandle node, const AABB& bounds) override;

    // Remove entity
    void remove(NodeHandle node) override;

    // Update entity bounds
    void update(NodeHandle node, const AABB& bounds) override;

    // Query methods
    SpatialQueryResult query_sphere(Vec3 center, float radius,
                                     const QueryOptions& opts = {}) const override;

    SpatialQueryResult query_aabb(const AABB& bounds,
                                   const QueryOptions& opts = {}) const override;

    SpatialQueryResult raycast(Vec3 origin, Vec3 direction, float max_distance,
                               const QueryOptions& opts = {}) const override;

    SpatialQueryResult query_frustum(const Frustum& frustum,
                                      const QueryOptions& opts = {}) const override;

    void rebuild() override;
    size_t node_count() const override { return nodes_.size(); }
    float depth() const override { return depth_; }

private:
    std::vector<BVHNode> nodes_;
    float depth_;

    // Top-down recursive builder using SAH (Surface Area Heuristic)
    uint32_t build_recursive(const std::vector<NodeHandle>& entities,
                             const std::vector<AABB>& bounds,
                             uint32_t depth);

    // SAH: choose split axis and position that minimizes surface area
    struct SplitResult {
        int axis;      // 0=X, 1=Y, 2=Z
        int split_pos; // Index in sorted array
    };
    SplitResult find_best_split(const std::vector<AABB>& bounds) const;

    // Sphere query recursion
    void query_sphere_recursive(uint32_t node_idx, Vec3 center, float radius,
                               const QueryOptions& opts,
                               SpatialQueryResult& out) const;

    // Ray query recursion (returns closest hit first)
    void raycast_recursive(uint32_t node_idx, Vec3 origin, Vec3 direction,
                          float max_distance, const QueryOptions& opts,
                          SpatialQueryResult& out) const;
};

struct BVHConfig {
    size_t max_leaf_entities = 8;        // Leaf node max capacity
    bool incremental_rebuild = false;    // Rebuild on each insert (slower) or batch rebuild
};
```

### SAH (Surface Area Heuristic) Split

The BVH builder uses SAH to find optimal split planes:

```cpp
// Pseudocode: Find split axis that minimizes SAH cost

float best_cost = INFINITY;
int best_axis = 0;
int best_split = 0;

for (int axis = 0; axis < 3; ++axis) {
    // Sort entities by axis
    std::sort(bounds.begin(), bounds.end(),
        [axis](const AABB& a, const AABB& b) {
            return a.center()[axis] < b.center()[axis];
        });

    // Try all split positions
    for (int split = 1; split < bounds.size(); ++split) {
        AABB left_bounds = compute_union(bounds[0..split-1]);
        AABB right_bounds = compute_union(bounds[split..n]);

        float cost = left_bounds.surface_area() * split +
                     right_bounds.surface_area() * (bounds.size() - split);

        if (cost < best_cost) {
            best_cost = cost;
            best_axis = axis;
            best_split = split;
        }
    }
}
```

This produces more balanced trees than axis-aligned medians alone.

---

## Octree Implementation

### Overview

An Octree recursively subdivides 3D space into 8 octants. Better for:
- Large open worlds (outdoor scenes)
- Uniform entity density
- Fixed node capacity (no entity limit)

Worse for:
- Small scenes (overhead > benefit)
- Non-uniform density (many empty octants)

### C++ API

```cpp
// src/game/octree.h

class Octree : public SpatialIndex {
public:
    explicit Octree(const OctreeConfig& config);

    void insert(NodeHandle node, const AABB& bounds) override;
    void remove(NodeHandle node) override;
    void update(NodeHandle node, const AABB& bounds) override;

    SpatialQueryResult query_sphere(Vec3 center, float radius,
                                     const QueryOptions& opts = {}) const override;
    SpatialQueryResult query_aabb(const AABB& bounds,
                                   const QueryOptions& opts = {}) const override;
    SpatialQueryResult raycast(Vec3 origin, Vec3 direction, float max_distance,
                               const QueryOptions& opts = {}) const override;
    SpatialQueryResult query_frustum(const Frustum& frustum,
                                      const QueryOptions& opts = {}) const override;

    void rebuild() override;
    size_t node_count() const override;
    float depth() const override;

private:
    struct OctreeNode {
        AABB bounds;
        std::array<std::unique_ptr<OctreeNode>, 8> children;
        std::vector<NodeHandle> entities;
        bool is_leaf = true;
    };

    std::unique_ptr<OctreeNode> root_;

    // Recursively insert into appropriate octant
    void insert_recursive(OctreeNode* node, NodeHandle entity, const AABB& bounds);

    // Subdivide node into 8 octants
    void subdivide(OctreeNode* node);
};

struct OctreeConfig {
    Vec3 world_min;           // World AABB min
    Vec3 world_max;           // World AABB max
    uint32_t max_depth = 10;  // Max recursion depth
    size_t max_entities_per_node = 16;  // Subdivision threshold
};
```

---

## Integration with ECS (Reactive Observer Pattern)

The SpatialIndex automatically updates when entities move via EnTT signals:

```cpp
// src/game/spatial_system.h

class SpatialSystem : public System {
public:
    SpatialSystem(GameContext& ctx, SystemScheduler& sched);

    // Called from SystemScheduler::tick()
    void tick(float dt) override;

    // Public query API (called by game code)
    SpatialQueryResult query_sphere(Vec3 center, float radius,
                                     const QueryOptions& opts = {}) const;
    SpatialQueryResult query_aabb(const AABB& bounds,
                                   const QueryOptions& opts = {}) const;
    SpatialQueryResult raycast(Vec3 origin, Vec3 direction, float max_distance,
                               const QueryOptions& opts = {}) const;

private:
    GameContext& ctx_;
    std::unique_ptr<SpatialIndex> index_;  // BVH or Octree

    // Called when CTransform is modified
    void on_transform_changed(entt::entity e, const CTransform& transform);
};

// Setup:
auto spatial_system = std::make_unique<SpatialSystem>(ctx, scheduler);

// In GameContext initialization:
ctx.registry.on_update<CTransform>().connect<&SpatialSystem::on_transform_changed>(spatial_system.get());
```

**Optimization**: Changes are batched. The spatial index is rebuilt once per frame (after all updates), not on every single entity move.

---

## Query API

### C++ API (Low-level)

```cpp
// In a system or game code
SpatialQueryResult enemies = ctx.spatial_system->query_sphere(
    player.position,
    10.0f,  // 10m radius
    QueryOptions{
        .tag_filter = { TAG_ENEMY },
        .max_results = 50,
        .sort_by_distance = true,
    }
);

for (const auto& node : enemies.nodes) {
    // Do something with nearby enemy
}
```

### Lua API (High-level, Preferred)

```lua
-- Query: find all enemies within 10m
local enemies = ctx.world:query_sphere(player.position, 10.0, {
    tags = { "enemy" },
    max_results = 50,
})

for _, enemy in ipairs(enemies) do
    print("Enemy at", enemy.position, "distance", enemy.distance)
end

-- Query: raycast from camera through mouse
local ray_origin = camera.position
local ray_direction = camera:get_ray_direction()
local hits = ctx.world:raycast(ray_origin, ray_direction, 1000.0, {
    tags = { "selectable" },
    max_results = 1,  -- Only first hit
})

if #hits > 0 then
    print("Selected:", hits[1].id)
end

-- Query: AABB (useful for overlap checks)
local bounds = AABB.from_center(position, Vec3(5, 5, 5))
local colliding = ctx.world:query_aabb(bounds, {
    tags = { "collider" },
})

-- Query: frustum (visibility culling)
local visible = ctx.world:query_frustum(camera.frustum, {
    tags = { "renderable" },
})
```

---

## Physics Broad-Phase Integration

The spatial index can be leveraged by the physics engine's broad-phase collision detection:

```cpp
// In PhysicsSystem::step()

// Instead of Jolt's default broad-phase, use our BVH
SpatialQueryResult potential_collisions = spatial_index_->query_sphere(
    entity.position,
    entity.collision_radius * 2.0f
);

// Narrow-phase: only test these candidates against this entity
for (const auto& candidate : potential_collisions.nodes) {
    if (detailed_collision_test(entity, candidate)) {
        // Add to Jolt's collision manifold
    }
}
```

This can provide 10-100x speedup for games with many colliders.

---

## Debug Visualization

### Editor Visualization

The editor displays the spatial index structure as wireframe AABBs:

```python
# VSCode Command: /uz call spatial.toggle_debug_draw

# Renders BVH/Octree nodes as colored wireframes:
# - Green: leaf nodes
# - Blue: depth 1 interior nodes
# - Red: depth 2+ interior nodes
```

### Statistics API

```cpp
// Query index statistics (for profiler, editor UI)
struct SpatialStats {
    size_t total_nodes;
    size_t total_entities;
    float tree_depth;
    float avg_entities_per_leaf;
    float rebuild_time_ms;
};

SpatialStats stats = spatial_system->get_stats();
```

---

## Performance Characteristics

### BVH

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Insert | O(log n) | Incremental rebuild |
| Remove | O(log n) | + tree rebalance |
| Update | O(log n) | If entity stays in same subtree |
| Sphere query | O(log n) | SAH produces tight trees |
| Raycast | O(log n) | Early termination helps |
| Rebuild | O(n log n) | Full rebuild is expensive; batch when possible |

### Octree

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Insert | O(log n) | Fixed depth, consistent |
| Remove | O(log n) | Cleanup empty octants |
| Update | O(log n) | Might move up/down octree |
| Sphere query | O(log n) | Worst case: all octants overlap |
| Raycast | O(log n) | Grid-like traversal |
| Rebuild | O(n) | Faster than BVH for static data |

**Recommendation**: Use BVH for dynamic scenes; Octree for open worlds with many static objects.

---

## Hot-Reload Behavior

When game.dll is reloaded:
1. SpatialSystem is recreated
2. Index is rebuilt from current entity transforms
3. No entity data is lost

---

## Test Coverage

### Unit Tests (C++)

```cpp
// test/spatial_systems_test.cpp

TEST(BVH, InsertsAndQueries) {
    BVH bvh;
    NodeHandle n1 = create_test_entity(Vec3(0, 0, 0));
    NodeHandle n2 = create_test_entity(Vec3(5, 0, 0));
    NodeHandle n3 = create_test_entity(Vec3(10, 0, 0));

    bvh.insert(n1, AABB::from_center(Vec3(0, 0, 0), 1.0f));
    bvh.insert(n2, AABB::from_center(Vec3(5, 0, 0), 1.0f));
    bvh.insert(n3, AABB::from_center(Vec3(10, 0, 0), 1.0f));

    auto result = bvh.query_sphere(Vec3(0, 0, 0), 6.0f);
    EXPECT_THAT(result.nodes, UnorderedElementsAre(n1, n2));
}

TEST(BVH, Raycast) {
    BVH bvh;
    NodeHandle target = create_test_entity(Vec3(10, 0, 0));

    bvh.insert(target, AABB::from_center(Vec3(10, 0, 0), 2.0f));

    auto result = bvh.raycast(Vec3(0, 0, 0), Vec3(1, 0, 0), 20.0f);
    EXPECT_THAT(result.nodes, ElementsAre(target));
}

TEST(Octree, SubdividesOnCapacity) {
    Octree octree(OctreeConfig{
        .world_min = Vec3(-10, -10, -10),
        .world_max = Vec3(10, 10, 10),
        .max_entities_per_node = 4,
    });

    // Insert 8 entities in same octant
    for (int i = 0; i < 8; ++i) {
        NodeHandle n = create_test_entity(Vec3(i * 0.1f, 0, 0));
        octree.insert(n, AABB::from_center(Vec3(i * 0.1f, 0, 0), 0.05f));
    }

    // Root should have subdivided
    EXPECT_GT(octree.depth(), 1.0f);
}

TEST(SpatialSystem, UpdatesOnTransformChange) {
    GameContext ctx;
    SpatialSystem spatial(ctx, scheduler);

    NodeHandle entity = ctx.create_node();
    ctx.registry.emplace<CTransform>(entity);
    spatial.index_->insert(entity, AABB::from_center(Vec3(0, 0, 0), 1.0f));

    // Move entity
    ctx.registry.get<CTransform>(entity).position = Vec3(10, 0, 0);

    // Spatial index should update on next tick
    spatial.tick(0.016f);

    auto result = spatial.query_sphere(Vec3(10, 0, 0), 2.0f);
    EXPECT_THAT(result.nodes, ElementsAre(entity));
}
```

### Integration Tests (Lua)

```lua
-- test/spatial_systems_test.lua

describe("Spatial Systems", function()
    it("should find entities in sphere", function()
        local e1 = create_node("e1", {position = Vec3(0, 0, 0)})
        local e2 = create_node("e2", {position = Vec3(5, 0, 0)})
        local e3 = create_node("e3", {position = Vec3(10, 0, 0)})

        local result = ctx.world:query_sphere(Vec3(0, 0, 0), 6.0, {})
        assert_equals(#result, 2)
    end)

    it("should raycast and return closest hit", function()
        local target = create_node("target", {position = Vec3(10, 0, 0)})

        local hits = ctx.world:raycast(Vec3(0, 0, 0), Vec3(1, 0, 0), 20.0)
        assert_equals(#hits, 1)
        assert_equals(hits[1].id, target.id)
    end)

    it("should filter by tags", function()
        local enemy = create_node("enemy", {position = Vec3(5, 0, 0)}, {tags = {"enemy"}})
        local ally = create_node("ally", {position = Vec3(5, 0, 0)}, {tags = {"ally"}})

        local result = ctx.world:query_sphere(Vec3(0, 0, 0), 10.0, {tags = {"enemy"}})
        assert_equals(#result, 1)
        assert_equals(result[1].id, enemy.id)
    end)
end)
```

---

## Related Docs

- [Physics System](physics-system.md) — Broad-phase integration, collision detection
- [ECS & Scene System](ecs-advanced.md) — Entity transform management
- [Gameplay Systems](gameplay-systems.md) — Uses spatial queries for ability targeting
- [Asset Pipeline](asset-pipeline.md) — Loading spatial index definitions
