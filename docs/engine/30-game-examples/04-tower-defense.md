# Tower Defense — Pathfinding, Waves, and Complex UI

> **Status**: Planned (Phase 3)
> **Source**: `games/tower_defense/`
> **Engine systems exercised**: Pathfinding (A*), spawning systems, entity pooling, UI, audio, economy, behavior trees

## Overview

Tower Defense demonstrates UzEngine's suitability for strategy games. It exercises pathfinding algorithms, entity spawning at scale (100+ enemies), dynamic UI updates, and complex game logic. The game validates the engine's capability to handle moderate complexity (not AAA-scale, but realistic game).

**Play time**: 10–20 minutes per game
**Target FPS**: 60 FPS sustained (with 100+ entities)
**Resolution**: 1920x1080

## Gameplay

1. **Map**: Top-down 2D grid-based level with a winding path
2. **Waves**: Enemies spawn at "spawn point," travel along path, reach "goal" (lose 1 HP)
3. **Towers**: Player places towers on grid squares (not on path)
4. **Combat**: Towers detect and shoot enemies in range, deal damage
5. **Economy**: Killing enemies grants gold → buy towers/upgrades
6. **Difficulty**: Each wave spawns more/stronger enemies
7. **Win/Lose**:
   - **Win**: Survive all 10 waves
   - **Lose**: 20 enemies reach goal (20 HP lost)

## Project Structure

```
games/tower_defense/
├── tower_defense.uzproject
├── src/
│   ├── enemy_ai.cpp               # Enemy pathfinding + movement
│   ├── enemy_ai.h
│   ├── tower_logic.cpp            # Tower targeting + shooting
│   ├── tower_logic.h
│   ├── pathfinding.cpp            # A* implementation
│   ├── pathfinding.h
│   ├── wave_manager.cpp           # Spawning + difficulty scaling
│   ├── wave_manager.h
│   ├── economy.cpp                # Gold, tower costs, upgrades
│   ├── economy.h
│   ├── level_manager.cpp          # Map loading, grid management
│   ├── level_manager.h
│   └── CMakeLists.txt
├── content/
│   ├── scenes/
│   │   └── level_1.uzscene
│   ├── meshes/
│   │   ├── tower_base.gltf
│   │   ├── tower_gun.gltf
│   │   ├── enemy.gltf
│   │   └── projectile.gltf
│   ├── textures/
│   │   ├── tileset.png            # Grid tiles
│   │   ├── enemy_color_0.png      # Enemy type 0 (weak)
│   │   ├── enemy_color_1.png      # Enemy type 1 (strong)
│   │   └── tower_texture.png
│   ├── behaviors/
│   │   ├── tower_ai.lua           # Tower behavior tree
│   │   ├── enemy_ai.lua           # Enemy behavior
│   │   └── projectile.lua
│   ├── audio/
│   │   ├── tower_fire.wav
│   │   ├── tower_impact.wav
│   │   ├── enemy_death.wav
│   │   ├── level_complete.wav
│   │   └── ambience.ogg
│   ├── ui/
│   │   ├── hud.html               # Wave counter, gold, HP, wave progress
│   │   ├── tower_shop.html        # Tower selection menu
│   │   ├── game_over.html         # Win/lose screen
│   │   └── styles.css
│   └── levels/
│       └── level_1_map.json       # Grid layout, path definition
└── README.md
```

## Map Format (level_1_map.json)

```json
{
    "version": 1,
    "width": 15,
    "height": 10,
    "grid": [
        [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0],
        [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0],
        [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0],
        [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0],
        [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    ],
    "notes": "0=buildable, 1=path, 2=goal"
}
```

## Core Game Logic

### Enemy AI (C++ + Lua Hybrid)

```cpp
/// Enemy entity — follows path, takes damage, dies.
class EnemyBehavior : public uz::Behavior {
    UZ_BEHAVIOR(EnemyBehavior);

private:
    uz::Path current_path_;
    size_t path_index_ = 0;
    float health_ = 100.0f;
    float max_health_ = 100.0f;
    float speed_ = 2.0f;
    float armor_ = 0.1f;  // Damage reduction (10%)
    std::string enemy_type_ = "weak";
    uint32_t bounty_ = 10;  // Gold on death

public:
    void OnBeginPlay() override {
        // Pathfind from spawn to goal
        auto level_mgr = GetWorld()->GetComponent<LevelManager>(
            GetWorld()->FindByName("level_manager")
        );
        if (level_mgr) {
            uz::Vec3 spawn_pos = GetComponent<uz::SceneComponent>()->GetPosition();
            uz::Vec3 goal_pos = level_mgr->GetGoalPosition();
            current_path_ = level_mgr->FindPath(spawn_pos, goal_pos);
        }

        UZ_LOG(Game, Info, "Enemy spawned: {} HP, bounty {}", max_health_, bounty_);
    }

    void OnUpdate(float dt) override {
        auto scene = GetComponent<uz::SceneComponent>();
        if (!scene || current_path_.empty()) return;

        uz::Vec3 pos = scene->GetPosition();
        uz::Vec3 target = current_path_[path_index_];

        // Move towards waypoint
        uz::Vec3 dir = (target - pos).Normalize();
        pos += dir * speed_ * dt;
        scene->SetPosition(pos);

        // Reached waypoint, advance path
        float dist = (target - pos).Length();
        if (dist < 0.2f) {
            path_index_++;
            if (path_index_ >= current_path_.size()) {
                // Reached goal
                UZ_LOG(Game, Info, "Enemy reached goal!");
                GetWorld()->BroadcastEvent("enemy_reached_goal");
                GetWorld()->DestroyEntity(GetEntity());
                return;
            }
        }

        // Face direction of movement
        uz::Vec3 rot = scene->GetRotation();
        rot.z = std::atan2(dir.y, dir.x);
        scene->SetRotation(rot);
    }

    /// Take damage (with armor reduction).
    void TakeDamage(float damage) {
        float reduced_damage = damage * (1.0f - armor_);
        health_ -= reduced_damage;

        UZ_LOG(Game, Debug, "Enemy took {:.1f} damage (reduced: {:.1f}), HP: {:.1f}",
            damage, reduced_damage, health_);

        if (health_ <= 0.0f) {
            OnDeath();
        }
    }

private:
    void OnDeath() {
        UZ_LOG(Game, Info, "Enemy died! Bounty: {}", bounty_);
        GetWorld()->BroadcastEvent("enemy_died", "bounty", static_cast<int>(bounty_));
        GetWorld()->GetAudio()->PlayOneShot("content/audio/enemy_death.wav");
        GetWorld()->DestroyEntity(GetEntity());
    }
};

UZ_REGISTER_BEHAVIOR(EnemyBehavior);
```

### Tower Logic (C++)

```cpp
/// Tower — detects enemies, fires projectiles, upgradeable.
class TowerBehavior : public uz::Behavior {
    UZ_BEHAVIOR(TowerBehavior);

private:
    float fire_range_ = 4.0f;
    float fire_rate_ = 0.5f;  // Shots per second
    float fire_cooldown_ = 0.0f;
    float damage_ = 10.0f;
    uint32_t upgrade_level_ = 0;
    uint32_t cost_ = 100;

public:
    void OnUpdate(float dt) override {
        fire_cooldown_ -= dt;

        auto scene = GetComponent<uz::SceneComponent>();
        uz::Vec3 tower_pos = scene->GetPosition();

        // Find enemies in range
        auto enemies = QueryEnemiesInRange(tower_pos, fire_range_);
        if (!enemies.empty()) {
            // Target closest enemy
            auto target = enemies.front();
            auto target_scene = GetWorld()->GetComponent<uz::SceneComponent>(target);

            if (fire_cooldown_ <= 0.0f) {
                FireProjectile(tower_pos, target_scene->GetPosition());
                fire_cooldown_ = 1.0f / fire_rate_;
            }
        }
    }

    /// Upgrade tower (increase damage, range, fire rate).
    void Upgrade(uint32_t levels = 1) {
        upgrade_level_ += levels;
        damage_ *= (1.0f + 0.2f * levels);          // +20% damage per upgrade
        fire_range_ *= (1.0f + 0.1f * levels);      // +10% range per upgrade
        fire_rate_ *= (1.0f + 0.15f * levels);      // +15% attack speed per upgrade

        UZ_LOG(Game, Info, "Tower upgraded to level {}. Damage: {:.1f}, Range: {:.1f}",
            upgrade_level_, damage_, fire_range_);
    }

private:
    std::vector<uz::Entity> QueryEnemiesInRange(uz::Vec3 center, float radius) {
        std::vector<uz::Entity> result;
        auto world = GetWorld();

        // Simplified: iterate all entities, check distance
        // (Real implementation would use spatial partitioning)
        world->ForEach<EnemyBehavior>([&](uz::Entity e, EnemyBehavior* enemy_behavior) {
            auto enemy_scene = world->GetComponent<uz::SceneComponent>(e);
            float dist = (enemy_scene->GetPosition() - center).Length();
            if (dist <= radius) {
                result.push_back(e);
            }
        });

        return result;
    }

    void FireProjectile(uz::Vec3 from, uz::Vec3 to) {
        auto world = GetWorld();
        uz::Entity projectile = world->Create();

        uz::Vec3 dir = (to - from).Normalize();
        float speed = 10.0f;

        world->Emplace<uz::SceneComponent>(projectile, from);
        world->Emplace<uz::StaticMeshComponent>(projectile, "content/meshes/projectile.gltf");
        world->Emplace<uz::BehaviorComponent>(projectile, "ProjectileBehavior")
            .SetProperty("target", to)
            .SetProperty("speed", speed)
            .SetProperty("damage", damage_);

        GetWorld()->GetAudio()->PlayOneShot("content/audio/tower_fire.wav");

        UZ_LOG(Game, Debug, "Tower fired projectile at {:.1f}, {:.1f}", to.x, to.y);
    }
};

UZ_REGISTER_BEHAVIOR(TowerBehavior);
```

### Wave Manager

```cpp
/// Manages enemy spawning waves and difficulty progression.
class WaveManager : public uz::Behavior {
    UZ_BEHAVIOR(WaveManager);

private:
    struct WaveConfig {
        uint32_t wave_number;
        uint32_t enemy_count;
        float spawn_interval;
        std::string enemy_type;
        float enemy_health_scale;
    };

    std::vector<WaveConfig> waves_;
    uint32_t current_wave_ = 0;
    float spawn_timer_ = 0.0f;
    uint32_t enemies_spawned_ = 0;
    bool wave_in_progress_ = false;
    float wave_start_delay_ = 2.0f;  // Time before first wave

public:
    void OnBeginPlay() override {
        GenerateWaves();
        UZ_LOG(Game, Info, "Wave manager initialized with {} waves", waves_.size());
    }

    void OnUpdate(float dt) override {
        if (current_wave_ >= waves_.size()) {
            if (!wave_in_progress_) {
                UZ_LOG(Game, Warning, "All waves completed!");
                GetWorld()->BroadcastEvent("level_complete");
            }
            return;
        }

        if (wave_start_delay_ > 0.0f) {
            wave_start_delay_ -= dt;
            return;
        }

        const auto& wave = waves_[current_wave_];
        spawn_timer_ += dt;

        if (spawn_timer_ >= wave.spawn_interval && enemies_spawned_ < wave.enemy_count) {
            SpawnEnemy(wave);
            enemies_spawned_++;
            spawn_timer_ = 0.0f;

            if (enemies_spawned_ >= wave.enemy_count) {
                AdvanceWave();
            }
        }
    }

    uint32_t GetCurrentWave() const { return current_wave_ + 1; }
    uint32_t GetTotalWaves() const { return waves_.size(); }

private:
    void GenerateWaves() {
        // Progressively harder waves
        for (uint32_t i = 0; i < 10; ++i) {
            waves_.push_back({
                .wave_number = i + 1,
                .enemy_count = 5 + i * 2,                    // 5, 7, 9, ... enemies
                .spawn_interval = 1.0f - i * 0.05f,          // Faster spawns
                .enemy_type = i < 5 ? "weak" : "strong",
                .enemy_health_scale = 1.0f + i * 0.15f       // Stronger enemies
            });
        }
    }

    void SpawnEnemy(const WaveConfig& wave) {
        auto level_mgr = GetWorld()->GetComponent<LevelManager>(
            GetWorld()->FindByName("level_manager")
        );
        uz::Vec3 spawn_pos = level_mgr->GetSpawnPosition();

        uz::Entity enemy = GetWorld()->Create();
        GetWorld()->Emplace<uz::SceneComponent>(enemy, spawn_pos);
        GetWorld()->Emplace<uz::BehaviorComponent>(enemy, "EnemyBehavior")
            .SetProperty("enemy_type", wave.enemy_type)
            .SetProperty("health_scale", wave.enemy_health_scale);

        UZ_LOG(Game, Debug, "Spawned enemy {} of wave {}",
            enemies_spawned_ + 1, wave.wave_number);
    }

    void AdvanceWave() {
        current_wave_++;
        enemies_spawned_ = 0;
        wave_start_delay_ = 3.0f;  // 3 second break between waves
        wave_in_progress_ = false;

        GetWorld()->BroadcastEvent("wave_complete",
            "wave_number", static_cast<int>(current_wave_));

        UZ_LOG(Game, Info, "Wave {} complete! Next wave in 3 seconds", current_wave_);
    }
};

UZ_REGISTER_BEHAVIOR(WaveManager);
```

### Economy System

```cpp
/// Player gold, tower purchasing, upgrades.
class EconomyManager : public uz::Behavior {
    UZ_BEHAVIOR(EconomyManager);

private:
    uint32_t gold_ = 500;  // Starting gold
    uint32_t lives_ = 20;  // HP

    struct TowerCost {
        std::string type;
        uint32_t base_cost;
        std::vector<uint32_t> upgrade_costs;
    };

    std::map<std::string, TowerCost> tower_catalog_ = {
        { "basic", { "basic", 100, { 50, 75, 100 } } },
        { "sniper", { "sniper", 200, { 100, 150, 200 } } },
        { "tesla", { "tesla", 300, { 150, 200, 300 } } }
    };

public:
    void OnBeginPlay() override {
        GetWorld()->ListenToEvent("enemy_died", [this](const uz::EventData& data) {
            uint32_t bounty = data.GetInt("bounty");
            AddGold(bounty);
        });

        GetWorld()->ListenToEvent("enemy_reached_goal", [this](const uz::EventData&) {
            lives_--;
            UZ_LOG(Game, Warning, "Enemy reached goal! Lives: {}", lives_);
            GetWorld()->BroadcastEvent("lives_changed", "lives", static_cast<int>(lives_));

            if (lives_ <= 0) {
                GetWorld()->BroadcastEvent("game_over", "result", "lose");
            }
        });
    }

    uint32_t GetGold() const { return gold_; }
    uint32_t GetLives() const { return lives_; }

    bool TryBuyTower(const std::string& tower_type) {
        auto it = tower_catalog_.find(tower_type);
        if (it == tower_catalog_.end()) return false;

        uint32_t cost = it->second.base_cost;
        if (gold_ >= cost) {
            gold_ -= cost;
            GetWorld()->BroadcastEvent("gold_changed", "gold", static_cast<int>(gold_));
            UZ_LOG(Game, Info, "Purchased tower: {}. Gold: {}", tower_type, gold_);
            return true;
        }

        return false;
    }

    uint32_t GetTowerCost(const std::string& tower_type) const {
        auto it = tower_catalog_.find(tower_type);
        return it != tower_catalog_.end() ? it->second.base_cost : 0;
    }

private:
    void AddGold(uint32_t amount) {
        gold_ += amount;
        GetWorld()->BroadcastEvent("gold_changed", "gold", static_cast<int>(gold_));
        UZ_LOG(Game, Debug, "Gold +{}. Total: {}", amount, gold_);
    }
};

UZ_REGISTER_BEHAVIOR(EconomyManager);
```

## Engine Systems Exercised

### 1. Pathfinding (§20)
- A* algorithm on grid-based map
- Per-enemy path computation
- Dynamic obstacle avoidance (optional)

### 2. Spawning & Pooling
- Wave spawner creating enemies at timed intervals
- Entity despawning (enemies reaching goal)
- Projectile pooling (optional optimization)

### 3. Physics & Collision (§16) (Optional)
- Projectile-enemy collision detection
- Simple AABB for tower range queries

### 4. Behavior Trees (via Lua)
```lua
-- tower_ai.lua
local function tower_think(ctx, dt)
    local enemies = ctx.world:query_enemies_in_range(ctx.entity, 4.0)
    if #enemies > 0 then
        local target = enemies[1]  -- Closest
        ctx.world:fire_projectile(ctx.entity, target)
    end
end
```

### 5. UI & HUD
- Wave counter (current/total)
- Gold display (dynamic updates)
- Lives/HP counter
- Tower shop menu
- Game-over screen (win/lose)

### 6. Audio (§17)
- Tower fire sound (3D positional)
- Enemy death sound
- Impact sounds
- Background ambience

### 7. Event System
- Enemy death → economy update
- Enemy reaches goal → lives decrease
- Wave complete → difficulty increase
- Gold changed → UI refresh

### 8. Economy & Progression
- Gold resource management
- Tower purchasing system
- Upgrade costs and effects
- Difficulty scaling per wave

## Success Criteria

- [ ] 100+ enemies on map without performance degradation
- [ ] Frame rate maintains 60 FPS with 100 entities
- [ ] Pathfinding finds optimal routes in <10 ms
- [ ] Tower targeting works accurately
- [ ] Projectiles hit enemies reliably
- [ ] Scoring system tracks correctly
- [ ] Difficulty progression feels balanced
- [ ] UI updates in real-time without lag
- [ ] 10 waves complete successfully
- [ ] Game-over screen shows correctly on win/loss

## Extend Tower Defense

- **Procedural map generation** — Random map each play
- **Multiple tower types** — Different mechanics (splash, slow, Tesla)
- **Enemy varieties** — Fast, tanky, flying (ignore terrain)
- **Special events** — Boss waves, double-speed waves
- **Achievements** — "Beat wave 5 without upgrades," "1M damage in one wave"
- **Leaderboards** — Best score, fastest completion (Steam §27-02)
- **Modding** — Custom maps via .uzlevel, Lua tower AI

