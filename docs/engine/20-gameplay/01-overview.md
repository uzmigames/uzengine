# Gameplay Systems

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/gameplay/`
> **References**: UE5 Gameplay Ability System, Behavior Trees, Save Game, Recast/Detour (clean-room)

## Purpose

High-level gameplay systems that build on the entity/component, physics, and scripting foundations. Includes: data tables, behavior trees, save system, and navigation.

## Data Tables — Data-Driven Design

Typed tables of rows loaded from `.uzdata` (JSON) files. Hot-reloadable — designers edit JSON, values update in real-time.

```cpp
/// Typed table of rows with string key lookup.
template<typename RowType>
class DataTable {
public:
    const RowType* Find(const char* key) const;  // O(1) hash
    void ForEach(std::function<void(const char* key, const RowType&)> fn) const;
    int Count() const;
};
```

### Row Definition

```cpp
struct WeaponStats {
    UZ_DATA_ROW(WeaponStats);

    UZ_PROPERTY(float, damage, 10.0f, "Damage per hit");
    UZ_PROPERTY(float, fire_rate, 1.0f, "Shots per second");
    UZ_PROPERTY(int, magazine, 10, "Ammo capacity");
    UZ_PROPERTY(float, reload_time, 2.0f, "Reload duration");
};
```

### `.uzdata` Format

```json
{
    "row_type": "WeaponStats",
    "rows": {
        "pistol":  { "damage": 15,  "fire_rate": 5.0, "magazine": 12, "reload_time": 1.5 },
        "shotgun": { "damage": 80,  "fire_rate": 1.2, "magazine": 6,  "reload_time": 2.5 },
        "rifle":   { "damage": 25,  "fire_rate": 10,  "magazine": 30, "reload_time": 2.0 }
    }
}
```

### Usage

```cpp
class WeaponSystem : public uz::Behavior {
    UZ_BEHAVIOR(WeaponSystem);
    UZ_ASSET(uz::DataTable<WeaponStats>, "data/weapons.uzdata", weapon_table);

    void Fire() {
        auto* stats = weapon_table->Find(current_weapon.c_str());
        if (!stats) return;
        SpawnProjectile(stats->damage);
        cooldown_ = 1.0f / stats->fire_rate;
    }
};
```

## Behavior Trees (AI)

Node-based AI decision trees authored in JSON (`.uzbehavior`).

### Node Types

| Category | Nodes |
|----------|-------|
| **Composite** | Sequence (AND), Selector (OR), Parallel |
| **Decorator** | Blackboard condition, Cooldown, Loop, Force success/fail |
| **Task** | MoveTo, PlayAnimation, Wait, RunEQS, Custom |
| **Service** | Periodic updates (update target, check distance) |

### Blackboard

Shared key-value store per AI entity. Tasks read/write values; decorators gate on them.

```cpp
struct Blackboard {
    void Set(StringView key, Entity value);
    void Set(StringView key, Vec3 value);
    void Set(StringView key, float value);
    void Set(StringView key, bool value);

    template<typename T>
    T Get(StringView key) const;

    bool Has(StringView key) const;
    void Clear(StringView key);
};
```

## Save System

Slot-based save system with msgpack serialization.

### Design

- **Save slots** — numbered (0–N) + autosave slot
- **Serialization** — msgpack (binary, fast, compact) for game state
- **What saves**: entities flagged with `UZ_SAVE`, their components serialized via Property System (§21)
- **What doesn't save**: runtime-spawned transient entities, physics simulation state
- **Thumbnails** — captured from the backbuffer at save time

```cpp
namespace uz::SaveSystem {
    void Save(int slot, const World& world);
    bool Load(int slot, World& world);
    bool HasSave(int slot);
    void DeleteSave(int slot);
    void Autosave(const World& world);

    struct SaveSlotInfo {
        int      slot;
        uint64_t timestamp;
        float    play_time_seconds;
        std::string level_name;
        std::string thumbnail_path;  // captured from backbuffer
    };
    std::vector<SaveSlotInfo> GetSlotInfos();
}
```

## Navigation — Recast/Detour

Navmesh-based pathfinding for AI agents.

### Components

```cpp
/// Navmesh agent — follows paths computed by the navigation system.
class NavMeshAgentComponent : public ActorComponent {
public:
    float agent_radius = 40.0f;
    float agent_height = 180.0f;
    float max_speed = 600.0f;
    float acceleration = 2048.0f;

    /// Request a path to a destination. Async — path arrives via callback.
    void MoveTo(Vec3 destination);
    void StopMovement();
    bool IsMoving() const;

    /// RVO2 avoidance (dynamic obstacle avoidance between agents).
    bool enable_avoidance = true;
    float avoidance_radius = 50.0f;

    MulticastDelegate<void(Vec3)> OnReachedDestination;
    MulticastDelegate<void()> OnPathFailed;
};
```

### Navmesh Baking

Offline baking via `uzcook` tool using Recast library. Produces `.uznavmesh` from scene geometry.

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Data tables | `UDataTable` (CSV/JSON) | Resource files | ScriptableObject | **`DataTable<T>` (.uzdata JSON)** |
| Behavior trees | BehaviorTree + Blackboard | None built-in | Third-party | **JSON .uzbehavior** |
| Save system | `USaveGame` + manual | ResourceSaver | PlayerPrefs + manual | **Slot-based, msgpack** |
| Navigation | NavMesh (Recast) | NavigationServer3D (Recast) | NavMesh (built-in) | **Recast/Detour** |
| Abilities | GAS (Gameplay Ability System) | None | None | **Planned (Phase 4)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | DataTable, basic behavior tree (Sequence/Selector), save slots, navmesh baking |
| **Phase 4** | Full BT (decorators, services, EQS), GAS-style abilities, RVO2 avoidance |
