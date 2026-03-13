# Navigation — Pathfinding and AI Movement

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/gameplay/navigation/`
> **References**: Recast/Detour library (open-source), RVO2 for avoidance (clean-room)

## Purpose

AI navigation mesh (navmesh) system for pathfinding, steering, and obstacle avoidance. Integrated with behavior trees to enable agents (NPCs) to move intelligently around the game world.

## Architecture

```
NavMeshBaking (offline, uzcook tool)
  ├── Scene geometry → Recast
  └── Produces: navmesh.uznavmesh (serialized Detour NavMesh)
        ↓
NavMeshWorld (runtime)
  ├── Load navmesh.uznavmesh
  ├── Query navmesh (path requests, raycasts)
  └── NavMeshAgent components
      ├── Steering (move along path)
      ├── RVO2 (dynamic avoidance between agents)
      └── Callbacks (path found, path failed, reached destination)
```

## Navmesh Definition

```cpp
namespace uz::navigation {

/// Runtime navmesh representation (loaded from .uznavmesh)
class NavMesh {
public:
    /// Query a path from start to end
    /// Returns path as array of Vec3 waypoints (or empty if no path found)
    std::vector<Vec3> FindPath(Vec3 start, Vec3 end) const;

    /// Raycast on navmesh — test if direct line-of-sight exists
    bool Raycast(Vec3 from, Vec3 to) const;

    /// Get nearest point on navmesh to given position
    Vec3 GetNearestPoint(Vec3 pos) const;

    /// Test if point is on navmesh
    bool IsPointInNavMesh(Vec3 pos) const;

    /// Get all triangles (for debug visualization)
    const std::vector<NavMeshTriangle>& GetTriangles() const;
};

}  // namespace uz::navigation
```

## NavMeshAgent Component

```cpp
namespace uz::navigation {

/// AI agent that navigates along paths on navmesh
class NavMeshAgentComponent : public ActorComponent {
public:
    UZ_COMPONENT(NavMeshAgentComponent);

    // ─────────────────────────────────────────────────────────────
    // Agent Settings (copied to Detour at initialization)
    // ─────────────────────────────────────────────────────────────

    float agent_radius = 40.0f;     ///< Agent collision radius (cm)
    float agent_height = 180.0f;    ///< Agent height (cm)
    float max_speed = 600.0f;       ///< Max movement speed (cm/s)
    float acceleration = 2048.0f;   ///< Acceleration (cm/s²)

    // ─────────────────────────────────────────────────────────────
    // Avoidance Settings (RVO2)
    // ─────────────────────────────────────────────────────────────

    bool enable_avoidance = true;   ///< Enable local obstacle avoidance
    float avoidance_radius = 50.0f; ///< RVO2 agent radius for avoidance
    float time_horizon = 0.5f;      ///< Predict avoidance N seconds ahead

    // ─────────────────────────────────────────────────────────────
    // Steering API
    // ─────────────────────────────────────────────────────────────

    /// Request a path to destination (async — fires OnPathFound callback)
    void MoveTo(Vec3 destination);

    /// Stop current movement (clear path, freeze agent)
    void StopMovement();

    /// Check if currently moving along a path
    bool IsMoving() const;

    /// Get current target (if moving)
    Vec3 GetCurrentDestination() const;

    /// Get remaining distance to destination
    float GetRemainingDistance() const;

    /// Manually set agent velocity (for jumping, knockback, etc.)
    void SetVelocity(Vec3 velocity);

    // ─────────────────────────────────────────────────────────────
    // Callbacks
    // ─────────────────────────────────────────────────────────────

    /// Fired when path is found and agent begins moving
    MulticastDelegate<void(const std::vector<Vec3>& path)> OnPathFound;

    /// Fired when path cannot be found
    MulticastDelegate<void()> OnPathFailed;

    /// Fired when agent reaches within arrival_distance of destination
    MulticastDelegate<void()> OnReachedDestination;

    /// Fired when agent is stuck (cannot move despite trying)
    MulticastDelegate<void()> OnStuck;

private:
    std::vector<Vec3> current_path_;
    int current_path_index_ = 0;
    float arrival_distance_ = 50.0f;  ///< Distance to destination before "reached"
};

}  // namespace uz::navigation
```

## Navmesh Baking (offline, `uzcook`)

Recast library processes static geometry to build a navmesh:

```bash
$ uzcook --bake-navmesh level_01.uzscene --output level_01.uznavmesh
```

Baking parameters (in `.uzproj`):

```toml
[navigation]
cell_size = 20.0           # Voxel grid cell size (cm)
cell_height = 20.0         # Voxel grid height (cm)
agent_height = 180.0       # Agent height (cm)
agent_radius = 40.0        # Agent radius (cm)
agent_max_climb = 27.0     # Max step height agent can climb (cm)
edge_max_error = 1.3       # Navmesh polygon edge max deviation (cm)
region_min_size = 8        # Min region size
region_merge_size = 20     # Merge small regions
filter_type = "walkable"   # Geometry filter: walkable, all, no_jump
```

## Navmesh Queries

```cpp
// Find path
auto path = navmesh->FindPath(from, to);
if (!path.empty()) {
    agent->MoveTo(path.back());  // Move to final waypoint
}

// Check direct line of sight
bool can_see = navmesh->Raycast(enemy_pos, player_pos);
if (can_see) {
    agent->MoveTo(player_pos);  // Chase directly
} else {
    agent->MoveTo(GetFlankingPosition(player_pos));
}

// Snap position to navmesh
Vec3 grounded = navmesh->GetNearestPoint(entity->position);
entity->SetPosition(grounded);
```

## RVO2 Local Avoidance

Agents avoid collisions with each other using RVO2 (Reciprocal Velocity Obstacles):

```cpp
// In NavMeshAgentComponent::OnTick():
// 1. Compute desired velocity (toward next waypoint)
Vec3 desired_vel = (next_waypoint - position).Normalize() * max_speed_;

// 2. Query nearby agents
auto nearby = world->FindEntitiesInRadius(position, avoidance_radius * 2);

// 3. Compute RVO2 avoidance velocity
Vec3 avoidance_vel = rvo_solver_->ComputeNewVelocity(
    position, desired_vel, agent_radius, nearby_agents
);

// 4. Apply avoidance velocity
position += avoidance_vel * dt;
```

## Lua Integration

```lua
-- Get NavMeshAgent component
local agent = npc:nav_mesh_agent()

-- Request path to player
agent:move_to(player:position())

-- Check if moving
if agent:is_moving() then
    print("NPC is moving")
end

-- Stop movement
agent:stop_movement()

-- React to path found
agent:on_path_found():connect(function(path)
    print("Path found with " .. #path .. " waypoints")
end)

-- React to destination reached
agent:on_reached_destination():connect(function()
    print("Reached destination!")
    npc:play_animation("idle")
end)
```

## Common Patterns

### 1. Chase Player

```cpp
class EnemyAI : public Behavior {
    void OnUpdate(float dt) override {
        auto agent = entity_->Get<NavMeshAgentComponent>();
        if (CanSeePlayer()) {
            agent->MoveTo(player_->position);
        }
    }
};
```

### 2. Patrol Waypoints

```cpp
class PatrolAI : public Behavior {
    std::vector<Vec3> waypoints_ = {...};
    int current_waypoint_ = 0;

    void OnBeginPlay() override {
        auto agent = entity_->Get<NavMeshAgentComponent>();
        agent->OnReachedDestination.AddDynamic(this, &PatrolAI::OnWaypointReached);
        agent->MoveTo(waypoints_[0]);
    }

    void OnWaypointReached() {
        current_waypoint_ = (current_waypoint_ + 1) % waypoints_.size();
        entity_->Get<NavMeshAgentComponent>()->MoveTo(waypoints_[current_waypoint_]);
    }
};
```

### 3. Flanking Movement

```cpp
Vec3 GetFlankingPosition(Vec3 target) {
    // Compute position to the side of target
    Vec3 offset = Quat::AngleAxis(90.0f * uz::Random(), Vec3::Up()) *
                  (Vec3::Forward() * 500.0f);
    return target + offset;
}
```

## Debug Visualization

Navmesh can be visualized in developer builds:

```cpp
// In developer/debug_draw.h
namespace uz::dev {
    /// Draw navmesh triangles (wireframe)
    void DrawNavMesh(const uz::navigation::NavMesh& navmesh, uint32_t color = 0xFF00FF00u);

    /// Draw agent path
    void DrawAgentPath(const std::vector<Vec3>& path, uint32_t color = 0xFFFFFF00u);
}

// Usage
uz::dev::DrawNavMesh(nav_world->GetNavMesh());
uz::dev::DrawAgentPath(agent->GetCurrentPath());
```

## `.uznavmesh` Format

Serialized Detour NavMesh:

```
Header:
  - Magic: "UZNA" (4 bytes)
  - Version: 1 (uint32)
  - TileCount: uint32

TileData (for each tile):
  - TileX, TileY: int32
  - DataSize: uint32
  - NavMeshData: [DataSize] bytes (raw Detour dtNavMeshData)
```

## Comparison

| Feature | UE5 NavMesh | Godot NavigationServer3D | Unity NavMesh | **UzEngine** |
|---------|------------|-------------------------|---------------|--------------|
| Baking | Unreal Editor | GodotEngine editor | Unity Editor | uzcook tool |
| Runtime | Detour (encrypted) | Recast/Detour | Internal (encrypted) | Recast/Detour |
| Query | FindPath, Raycast | Path2D/Path3D queries | NavMeshAgent | Detour API |
| Avoidance | Spatial avoidance | Local avoidance (built-in) | NavMeshObstacle | RVO2 |
| Agents | Crowd (managed) | NavigationAgent3D | NavMeshAgent | NavMeshAgentComponent |
| Streaming | Level streaming aware | Per-level | Per-level | Per-level |

## Implementation Phase

- **Phase 3**: Navmesh baking (Recast), Detour loading, path queries, NavMeshAgent basic steering
- **Phase 4**: RVO2 avoidance, stuck detection, dynamic obstacles, debug visualization
- **Phase 5**: Crowd management, hierarchical pathfinding, off-mesh links (ladders, jumps)
