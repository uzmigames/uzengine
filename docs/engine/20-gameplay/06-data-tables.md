# Data-Driven Design — Data Tables

> **Status**: Planned (Phase 3)
> **Source**: `engine/gameplay/data_tables/`
> **References**: UE5 DataTable, Godot CSV import

## Purpose

Typed tables of rows loaded from `.uzdata` files. Enables designers to tweak numbers (weapon stats, enemy stats, loot tables) without recompiling. Supports hot-reload — edit JSON, save, values update live.

## Concept

A **DataTable** is a strongly-typed, keyed collection of rows:

```cpp
// Define a row type
struct WeaponStats {
    float damage;
    float fire_rate;
    int magazine_size;
    float reload_time;
};

// Load table from JSON
auto weapon_table = uz::DataTable<WeaponStats>::LoadFromFile("data/weapons.uzdata");

// Look up by key (string)
auto* rifle_stats = weapon_table.Find("rifle");
if (rifle_stats) {
    SpawnProjectile(rifle_stats->damage);
}
```

## Row Definition

Rows are plain C++ structs, registered with the Property System:

```cpp
namespace uz {

/// Example row for weapon configurations
struct WeaponStatsRow {
    UZ_DATA_ROW(WeaponStatsRow);  ///< Macro that registers row type

    UZ_PROPERTY(float, damage, 25.0f, "Damage per hit");
    UZ_PROPERTY(float, fire_rate, 5.0f, "Shots per second");
    UZ_PROPERTY(int, magazine_size, 30, "Ammo capacity");
    UZ_PROPERTY(float, reload_time, 2.0f, "Reload duration");
    UZ_PROPERTY(std::string, ammo_type, "5.56mm", "Ammunition type");
    UZ_PROPERTY(bool, can_zoom, true, "Has scope attachment");
    UZ_PROPERTY(float, zoom_fov, 30.0f, "Zoom field of view (degrees)");
};

/// Example row for enemy configurations
struct EnemyStatsRow {
    UZ_DATA_ROW(EnemyStatsRow);

    UZ_PROPERTY(float, health, 50.0f, "Max health points");
    UZ_PROPERTY(float, armor, 10.0f, "Armor rating");
    UZ_PROPERTY(float, speed, 300.0f, "Movement speed (cm/s)");
    UZ_PROPERTY(std::string, behavior_tree, "ai/goblin.uzbt", "Behavior tree asset");
    UZ_PROPERTY(int, level, 1, "Enemy level");
    UZ_PROPERTY(float, exp_reward, 100.0f, "Experience granted on defeat");
};

}  // namespace uz
```

## `.uzdata` File Format (JSON)

Rows are defined in a single JSON file with a "rows" object:

```json
{
    "row_type": "WeaponStatsRow",
    "description": "All weapon configurations",
    "rows": {
        "pistol": {
            "damage": 15.0,
            "fire_rate": 8.0,
            "magazine_size": 12,
            "reload_time": 1.5,
            "ammo_type": "9mm",
            "can_zoom": false,
            "zoom_fov": 0.0
        },
        "rifle": {
            "damage": 35.0,
            "fire_rate": 10.0,
            "magazine_size": 30,
            "reload_time": 2.0,
            "ammo_type": "5.56mm",
            "can_zoom": true,
            "zoom_fov": 30.0
        },
        "shotgun": {
            "damage": 80.0,
            "fire_rate": 1.2,
            "magazine_size": 8,
            "reload_time": 2.5,
            "ammo_type": "12ga",
            "can_zoom": false,
            "zoom_fov": 0.0
        }
    }
}
```

## DataTable API

```cpp
namespace uz {

/// Strongly-typed table of rows
template<typename RowType>
class DataTable {
public:
    /// Load table from .uzdata file
    /// Returns nullptr if file not found or JSON parse error
    static std::shared_ptr<DataTable<RowType>> LoadFromFile(StringView path);

    /// Find row by key (string). Returns nullptr if not found.
    const RowType* Find(StringView key) const;

    /// Try to find and safely dereference
    const RowType& GetOrDefault(StringView key, const RowType& default_val) const {
        auto* row = Find(key);
        return row ? *row : default_val;
    }

    /// Iterate all rows (key, value) pairs
    void ForEach(std::function<void(StringView key, const RowType&)> fn) const;

    /// Row count
    int Count() const;

    /// Get all row keys (for UI dropdowns, etc.)
    std::vector<std::string> GetKeys() const;

    /// Is table loaded and valid
    bool IsValid() const;

    /// Reload from disk (for hot-reload)
    bool Reload();

private:
    std::unordered_map<std::string, RowType> rows_;
    std::string source_path_;
};

}  // namespace uz
```

## Component Integration

Components can reference DataTable rows:

```cpp
struct Weapon {
    UZ_COMPONENT(Weapon);

    UZ_PROPERTY(std::string, weapon_id, "pistol", "Weapon key in data table");
    UZ_ASSET(uz::DataTable<WeaponStatsRow>, "data/weapons.uzdata", weapon_stats);

    void Fire() {
        auto* stats = weapon_stats->Find(weapon_id.c_str());
        if (!stats) return;

        SpawnProjectile(stats->damage);
        cooldown_ = 1.0f / stats->fire_rate;
    }
};
```

## Lua Integration

```lua
-- Load table
local weapon_table = uz.DataTable.LoadFromFile("data/weapons.uzdata")

-- Find row
local rifle_stats = weapon_table:find("rifle")
if rifle_stats then
    print("Rifle damage: " .. rifle_stats.damage)
end

-- Iterate all rows
weapon_table:for_each(function(key, row)
    print(key .. ": " .. row.damage .. " damage")
end)

-- Get all keys for UI dropdown
local keys = weapon_table:get_keys()
for _, key in ipairs(keys) do
    ui_dropdown:add_item(key)
end
```

## Hot-Reload

`.uzdata` files are watched by FileWatcher. When edited, the table reloads:

```cpp
// In uz::dev::FileWatcher (developer layer)
uz::dev::WatchAssets("res://data/**/*.uzdata", [](StringView path, FileChangeEvent evt) {
    if (evt == FileChangeEvent::Modified) {
        // Reload all data tables for this path
        for (auto* table : active_data_tables_) {
            if (table->GetSourcePath() == path) {
                table->Reload();
                UZ_LOG(Data, Info, "Reloaded data table: {}", path);
            }
        }
    }
});
```

## Usage Patterns

### 1. Weapon Lookup

```cpp
class WeaponSystem : public Behavior {
    uz::AssetRef<uz::DataTable<WeaponStatsRow>> weapon_table_;

    void OnWeaponEquipped(StringView weapon_id) {
        auto* stats = weapon_table_->Find(weapon_id);
        if (!stats) {
            UZ_LOG(Weapon, Error, "Unknown weapon: {}", weapon_id);
            return;
        }

        current_weapon_damage_ = stats->damage;
        current_reload_time_ = stats->reload_time;
        current_magazine_ = stats->magazine_size;
    }
};
```

### 2. Enemy Spawning

```cpp
class EnemyManager : public Behavior {
    uz::AssetRef<uz::DataTable<EnemyStatsRow>> enemy_table_;

    void SpawnEnemy(StringView enemy_type, Vec3 position) {
        auto* stats = enemy_table_->Find(enemy_type);
        if (!stats) return;

        auto entity = world_->Create();
        auto health = world_->Attach<HealthComponent>(entity, stats->health);
        auto movement = world_->Attach<MovementComponent>(entity, stats->speed);
        world_->SetPosition(entity, position);

        // Load and start behavior tree
        auto behavior = world_->Attach<BehaviorTreeComponent>(entity);
        behavior->StartBehavior(stats->behavior_tree);
    }
};
```

### 3. Loot Table

```cpp
struct LootTableRow {
    UZ_DATA_ROW(LootTableRow);

    UZ_PROPERTY(std::string, item_id, "", "Item to drop");
    UZ_PROPERTY(float, drop_chance, 0.5f, "Probability (0.0–1.0)");
    UZ_PROPERTY(int, min_count, 1, "Min items");
    UZ_PROPERTY(int, max_count, 3, "Max items");
};

// Data file: content/data/loot_tables.uzdata
// {
//     "row_type": "LootTableRow",
//     "rows": {
//         "goblin": {"item_id": "copper_coin", "drop_chance": 0.8, "min_count": 1, "max_count": 3},
//         "boss": {"item_id": "rare_sword", "drop_chance": 1.0, "min_count": 1, "max_count": 1}
//     }
// }

void EnemyDefeated(StringView enemy_type) {
    auto* loot = loot_table_->Find(enemy_type);
    if (!loot || uz::Random() > loot->drop_chance) return;

    int count = uz::Random(loot->min_count, loot->max_count);
    for (int i = 0; i < count; ++i) {
        SpawnItem(loot->item_id, entity_->position);
    }
}
```

## Validation

DataTableManager validates schema at load time:

```cpp
// Check: all properties in JSON exist in RowType
// Check: types match (float vs int, etc.)
// Check: required properties present
// Warn: extra properties in JSON (typos)

bool LoadFromFile(StringView path) {
    auto json = LoadJSON(path);
    auto row_type_name = json["row_type"].as<std::string>();

    // Find RowType in registry
    auto row_type = ClassRegistry::FindType(row_type_name);
    if (!row_type) {
        UZ_LOG(Data, Error, "Unknown row type: {}", row_type_name);
        return false;
    }

    // For each row in JSON:
    for (auto& [key, row_json] : json["rows"].as_object()) {
        // Validate properties match schema
        for (auto& prop : row_type->GetProperties()) {
            if (!row_json.contains(prop.name)) {
                UZ_LOG(Data, Warning, "Missing property: {}", prop.name);
                // Use default value
            }
        }
    }

    return true;
}
```

## Comparison

| Feature | UE5 DataTable | Excel + CSV import | Godot CSV | **UzEngine** |
|---------|--------------|-------------------|-----------|--------------|
| Format | Native binary | CSV | CSV | JSON |
| Type safety | Strong (C++) | Weak (manual parsing) | Weak (Variant) | Strong (template) |
| Hot-reload | Yes (editor) | No | No | Yes (FileWatcher) |
| Validation | Yes (schema) | Manual | Manual | Automatic |
| Lookup | Map (by key) | Array | Array | Hash map (O(1)) |
| Designer-friendly | Editor UI | Spreadsheet | Editor | Text editor |
| Version control | Binary (merge conflicts) | Text (mergeable) | Text (mergeable) | Text (mergeable) |

## Implementation Phase

- **Phase 3**: Core DataTable<T> template, JSON loading, property validation, hot-reload via FileWatcher
- **Phase 4**: DataTableManager (central registry), schema versioning/migration, Lua bindings
- **Phase 5**: Visual editor (JSON UI in VSCode), CSV import/export, remote data (cloud tables)
