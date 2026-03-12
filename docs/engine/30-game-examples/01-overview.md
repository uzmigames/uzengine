# Game Examples — Progressive Validation

> **Status**: Planned (Phase 1–4)
> **Source**: `games/` directory in repository
> **References**: UE5 game samples, Godot demo projects

## Purpose

Game examples are **complete, playable games** (not just technical demos) that validate UzEngine systems under realistic conditions. Each example exercises progressively more engine features, from 2D rendering to networked AI.

Examples serve three goals:
1. **Validation**: Prove each system works in a real game context.
2. **Documentation**: Show how to use engine APIs (better than reference docs alone).
3. **Dogfooding**: Catch bugs early when engine developers play the games.

Complementary to **demos** (`demos/`) — demos test single systems (shader compilation, physics simulation); examples integrate multiple systems into a cohesive game.

## Overview

| Example | Theme | Systems Exercised | Target Phase |
|---------|-------|-------------------|--------------|
| **Pong** | 2D arcade | 2D rendering, input, collision, score UI | Phase 1 |
| **Flappy Bird** | 2D arcade | Sprite animation, physics, procedural obstacles, SFX | Phase 2 |
| **Tower Defense** | Strategy | Pathfinding, spawning, projectiles, economy UI, wave system | Phase 3 |
| **FPS Prototype** | Shooter | 3D FPS controller, weapons, AI, combat, UI | Phase 3–4 |

```
Phase 0         Phase 1         Phase 2         Phase 3         Phase 4
 │               │               │               │               │
 ├───────────────┼───────────────┼───────────────┼───────────────┤
                 Pong
                 (2D basic)
                             Flappy Bird
                             (Sprites, physics)
                                         Tower Defense
                                         (Pathfinding, AI)
                                                     FPS Prototype
                                                     (Full game)
```

## Example 1: Pong (Phase 1)

### Objective

Validate **2D rendering, input handling, collision detection, and score management** in a simple arcade game.

### Gameplay

- Two paddles (player and AI) bounce a ball across a 2D court.
- Player controls paddle with W/S keys (up/down).
- AI uses simple scripted behavior (follow ball Y position with smoothing).
- Ball bounces off paddles and walls.
- Score increments when opponent misses.
- First to 5 points wins.

### File Structure

```
games/pong/
├── pong.uzproject          # Project config
├── src/
│   ├── pong.cpp           # C++ game code (Behavior classes)
│   ├── CMakeLists.txt
│   └── meson.build        # Alternative to CMake
├── content/
│   ├── scenes/
│   │   └── game.uzscene   # Main scene (paddles, ball, HUD)
│   ├── meshes/
│   │   ├── paddle.gltf
│   │   └── ball.gltf
│   ├── textures/
│   │   └── white.png
│   └── audio/
│       ├── bounce.wav
│       └── score.wav
└── README.md
```

### Key Components

```cpp
/// Pong player paddle controller.
class PaddleBehavior : public uz::Behavior {
    UZ_BEHAVIOR(PaddleBehavior);

public:
    float speed = 5.0f;  // units/sec

    void OnTick(float dt) override {
        auto& input = GetWorld()->GetInput();

        // W/S keys move paddle up/down
        float dy = 0;
        if (input.IsKeyDown(Key::W)) dy += speed * dt;
        if (input.IsKeyDown(Key::S)) dy -= speed * dt;

        auto scene = GetComponent<uz::SceneComponent>();
        auto pos = scene->GetPosition();
        pos.y += dy;
        pos.y = std::clamp(pos.y, -3.0f, 3.0f);  // Boundary
        scene->SetPosition(pos);
    }
};

UZ_REGISTER_BEHAVIOR(PaddleBehavior);

/// AI paddle follows ball with simple pursuit.
class AIBehavior : public uz::Behavior {
    UZ_BEHAVIOR(AIBehavior);

private:
    float speed = 4.0f;
    float smoothing = 0.1f;  // Low-pass filter factor

public:
    void OnTick(float dt) override {
        auto world = GetWorld();
        auto ball_entity = world->Find("ball");
        if (!ball_entity.IsValid()) return;

        auto ball_scene = world->Get<uz::SceneComponent>(ball_entity);
        auto paddle_scene = GetComponent<uz::SceneComponent>();

        // Smooth pursuit (lerp towards ball Y)
        float target_y = ball_scene->GetPosition().y;
        float current_y = paddle_scene->GetPosition().y;
        float desired_y = uz::Lerp(current_y, target_y, smoothing);
        desired_y = std::clamp(desired_y, -3.0f, 3.0f);

        auto pos = paddle_scene->GetPosition();
        pos.y = desired_y;
        paddle_scene->SetPosition(pos);
    }
};

UZ_REGISTER_BEHAVIOR(AIBehavior);

/// Ball physics and collision.
class BallBehavior : public uz::Behavior {
    UZ_BEHAVIOR(BallBehavior);

private:
    uz::Vec3 velocity;

public:
    void OnBeginPlay() override {
        velocity = uz::Vec3(5.0f, 2.0f, 0.0f);  // Initial velocity
    }

    void OnTick(float dt) override {
        auto scene = GetComponent<uz::SceneComponent>();
        auto pos = scene->GetPosition();

        // Move ball
        pos += velocity * dt;

        // Bounce off top/bottom walls
        if (pos.y > 5.0f || pos.y < -5.0f) {
            velocity.y *= -1.0f;
            pos.y = std::clamp(pos.y, -5.0f, 5.0f);
        }

        // Check paddle collisions (simplified: AABB)
        // (Full implementation would use physics volumes)

        // Out of bounds: reset, increment score
        if (pos.x > 10.0f || pos.x < -10.0f) {
            ResetBall();
        }

        scene->SetPosition(pos);
    }

private:
    void ResetBall() {
        velocity = uz::Vec3(5.0f * (uz::Random() > 0.5f ? 1 : -1), 0.0f, 0.0f);
        auto scene = GetComponent<uz::SceneComponent>();
        scene->SetPosition(uz::Vec3(0, 0, 0));
    }
};

UZ_REGISTER_BEHAVIOR(BallBehavior);
```

### Scene File

```json
{
  "metadata": {
    "version": "0.2",
    "title": "Pong",
    "author": "UzEngine Team"
  },
  "nodes": [
    {
      "id": "camera",
      "components": {
        "SceneComponent": {
          "position": [0, 0, -15],
          "rotation": [0, 0, 0]
        },
        "CameraComponent": {
          "is_active": true,
          "fov": 60.0,
          "near": 0.1,
          "far": 1000.0
        }
      }
    },
    {
      "id": "player_paddle",
      "components": {
        "SceneComponent": {
          "position": [-8, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [0.5, 2.0, 0.5]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/paddle.gltf"
        },
        "Behavior": {
          "type": "PaddleBehavior"
        }
      }
    },
    {
      "id": "ai_paddle",
      "components": {
        "SceneComponent": {
          "position": [8, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [0.5, 2.0, 0.5]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/paddle.gltf"
        },
        "Behavior": {
          "type": "AIBehavior"
        }
      }
    },
    {
      "id": "ball",
      "components": {
        "SceneComponent": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [0.25, 0.25, 0.25]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/ball.gltf"
        },
        "Behavior": {
          "type": "BallBehavior"
        }
      }
    }
  ]
}
```

### Engine Systems Exercised

- **Input** (§13): Keyboard input (W/S key polling)
- **Scene** (§11): Node hierarchy, entity lookup
- **Behavior** (§10): C++ game logic, per-frame `OnTick()`
- **Rendering** (§12): 2D mesh rendering
- **Math** (§2): Vec3, Lerp, Clamp

### Success Criteria

- [ ] Game runs without crashes
- [ ] Paddle responds to W/S input within 16ms frame
- [ ] Ball bounces predictably
- [ ] AI paddle pursues ball smoothly
- [ ] 60 FPS sustained on RTX 4070+

## Example 2: Flappy Bird (Phase 2)

### Objective

Validate **sprite animation, physics integration, procedural obstacle generation, UI, and audio** in a 2D platformer.

### Gameplay

- Player controls bird with spacebar to apply upward impulse.
- Gravity pulls bird down.
- Bird must navigate through scrolling pipes without collision.
- Each pipe passed increments score.
- Collision = game over, respawn.

### Key Systems

**Physics**:
- Bird: dynamic body with gravity.
- Pipes: static/kinematic bodies (moving in X, collision boxes for passages).
- Trigger volumes for scoring.

**Animation**:
- Bird wing flap on jump.
- Pipe scroll animation (parallax).
- Score popup float-and-fade on point.

**Procedural Generation**:
```cpp
/// Generate random pipe pair at screen right.
uz::Entity SpawnPipePair(uz::World* world, float x_pos) {
    float gap_y = uz::Random() * 4.0f - 2.0f;  // Clamp to playable range
    float gap_size = 2.5f;

    // Upper pipe
    auto upper = world->Create();
    world->Emplace<uz::SceneComponent>(upper, uz::Vec3(x_pos, gap_y + gap_size / 2, 0));
    world->Emplace<StaticMeshComponent>(upper, "content/meshes/pipe_upper.gltf");
    world->Emplace<uz::BoxCollider>(upper, uz::Vec3(0.5f, 2.0f, 0.1f));

    // Lower pipe
    auto lower = world->Create();
    world->Emplace<uz::SceneComponent>(lower, uz::Vec3(x_pos, gap_y - gap_size / 2, 0));
    world->Emplace<StaticMeshComponent>(lower, "content/meshes/pipe_lower.gltf");
    world->Emplace<uz::BoxCollider>(lower, uz::Vec3(0.5f, 2.0f, 0.1f));

    // Trigger volume for scoring
    auto trigger = world->Create();
    world->Emplace<uz::SceneComponent>(trigger, uz::Vec3(x_pos, 0, 0));
    world->Emplace<uz::TriggerVolume>(trigger, uz::Vec3(0.1f, gap_size, 0.1f))
        ->OnOverlapBegin([](const uz::TriggerEvent& e) {
            if (e.OtherEntity.Has<BirdTag>()) {
                IncrementScore();
                PlaySound("content/audio/score.wav");
            }
        });

    return upper;  // Parent entity
}
```

### File Structure

```
games/flappy_bird/
├── flappy_bird.uzproject
├── src/
│   ├── bird_behavior.cpp
│   ├── pipe_spawner.cpp
│   ├── game_manager.cpp
│   └── CMakeLists.txt
├── content/
│   ├── scenes/
│   │   └── game.uzscene
│   ├── meshes/
│   │   ├── bird.gltf
│   │   ├── pipe_upper.gltf
│   │   └── pipe_lower.gltf
│   ├── textures/
│   │   └── background.png
│   ├── animations/
│   │   └── bird_flap.uztexture (sprite sheet)
│   └── audio/
│       ├── flap.wav
│       ├── score.wav
│       └── game_over.wav
└── README.md
```

### Engine Systems Exercised

- **Physics** (§16): Dynamic/kinematic bodies, gravity, colliders, trigger volumes
- **Animation** (§19): Sprite sheet animation (wing flap), parallax scrolling
- **Audio** (§17): 2D sound effects (flap, score, game over)
- **UI** (§18): Score display, game over screen
- **Procedural generation**: Pipe spawning and despawning

### Success Criteria

- [ ] Bird falls and jumps smoothly with physics
- [ ] Pipes scroll at 60 FPS without stutter
- [ ] Collision detected accurately
- [ ] Score increments on trigger pass
- [ ] Audio plays without latency
- [ ] 120 FPS sustained

## Example 3: Tower Defense (Phase 3)

### Objective

Validate **pathfinding (A*), entity spawning systems, AI behavior trees, inventory/economy, wave management, and complex UI** in a strategy game.

### Gameplay

- Enemies follow a path from spawn to goal.
- Player places towers to block or damage enemies.
- Each kill grants gold; spent on tower upgrades or new towers.
- Waves escalate in difficulty.
- Goal: survive all waves with lives remaining.

### Key Systems

**Pathfinding**:
```cpp
/// A* pathfinding on a grid-based map.
class PathfindingSystem {
public:
    uz::Path FindPath(const uz::Vec3& start, const uz::Vec3& goal) {
        // A* algorithm implemented using FAST library (if available)
        // or custom implementation with binary heap for efficiency
    }
};
```

**Behavior Trees** (Lua-based):
```lua
-- Tower behavior: detect enemy, fire projectile
local tower_behavior = {
    root = {
        type = "selector",
        children = {
            {
                type = "sequence",
                children = {
                    -- Check if enemy in range
                    function(ctx) return ctx.query:enemies_in_range(ctx.entity, 5.0) end,
                    -- Fire projectile
                    function(ctx)
                        local enemy = ctx.query:closest_enemy(ctx.entity, 5.0)
                        if enemy then
                            ctx.world:fire_projectile(ctx.entity, enemy, 15.0)
                            return true
                        end
                        return false
                    end,
                }
            },
            -- Idle if no enemies
            function(ctx) return true end,
        }
    }
}
```

**Spawning**:
```cpp
/// Wave spawner — releases enemies on a schedule.
class WaveSpawner : public uz::Behavior {
    UZ_BEHAVIOR(WaveSpawner);

private:
    struct WaveConfig {
        uint32_t wave_number;
        uint32_t enemy_count;
        float spawn_interval;
        std::string enemy_type;
    };

    std::vector<WaveConfig> waves;
    uint32_t current_wave = 0;
    float spawn_timer = 0;
    uint32_t enemies_spawned = 0;

public:
    void OnTick(float dt) override {
        if (current_wave >= waves.size()) {
            // All waves complete
            return;
        }

        const auto& wave = waves[current_wave];
        spawn_timer += dt;

        if (spawn_timer >= wave.spawn_interval && enemies_spawned < wave.enemy_count) {
            SpawnEnemy(wave.enemy_type);
            enemies_spawned++;
            spawn_timer = 0;

            if (enemies_spawned >= wave.enemy_count) {
                current_wave++;
                enemies_spawned = 0;
            }
        }
    }

private:
    void SpawnEnemy(const std::string& type) {
        auto world = GetWorld();
        auto enemy = world->Create();
        // Set up enemy entity with components
    }
};
```

### File Structure

```
games/tower_defense/
├── tower_defense.uzproject
├── src/
│   ├── enemy_ai.cpp
│   ├── tower_logic.cpp
│   ├── pathfinding.cpp
│   ├── wave_manager.cpp
│   ├── economy.cpp
│   └── CMakeLists.txt
├── content/
│   ├── scenes/
│   │   └── level_1.uzscene
│   ├── meshes/
│   │   ├── tower_base.gltf
│   │   ├── tower_gun.gltf
│   │   └── enemy.gltf
│   ├── behaviors/
│   │   ├── tower_ai.lua
│   │   └── enemy_ai.lua
│   ├── audio/
│   │   └── tower_fire.wav
│   └── levels/
│       └── map_data.json
└── README.md
```

### Engine Systems Exercised

- **Pathfinding** (§20): A* navigation
- **Physics** (§16): Collision, trigger zones
- **Behavior trees**: C++ (tower logic) + Lua (enemy behavior)
- **Spawning**: Dynamic entity creation, pooling
- **UI** (§18): Wave counter, gold display, tower selection, stats
- **Audio** (§17): 3D spatial audio (tower fire)
- **Economy**: Resource tracking, tower upgrades

### Success Criteria

- [ ] Pathfinding finds optimal routes
- [ ] Towers fire at enemies within range
- [ ] Wave escalation increases difficulty
- [ ] Gold economy flows correctly (earn, spend, balance)
- [ ] UI updates in real-time
- [ ] 60 FPS sustained with 100+ entities

## Example 4: FPS Prototype (Phase 3–4)

### Objective

Validate **3D first-person controller, weapon systems, AI combat, networking infrastructure, and advanced UI** in a complete game.

### Gameplay

- Player controls a soldier in 3D environment.
- WASD to move, mouse to look, click to fire.
- Enemies (spawned via wave system) pursue and shoot back.
- Health/ammo HUD.
- Objective: survive 10 waves.

### Key Systems

**Character Controller**:
```cpp
/// FPS character controller with ground detection, jumping, sprinting.
class FPSCharacter : public uz::Behavior {
    UZ_BEHAVIOR(FPSCharacter);

private:
    float move_speed = 5.0f;
    float sprint_speed = 8.0f;
    float jump_force = 5.0f;
    bool is_grounded = false;

public:
    void OnTick(float dt) override {
        auto input = GetWorld()->GetInput();
        auto physics = GetWorld()->GetPhysics();

        // WASD input
        uz::Vec3 move_input(0, 0, 0);
        if (input.IsKeyDown(Key::W)) move_input.z += 1.0f;
        if (input.IsKeyDown(Key::S)) move_input.z -= 1.0f;
        if (input.IsKeyDown(Key::A)) move_input.x -= 1.0f;
        if (input.IsKeyDown(Key::D)) move_input.x += 1.0f;

        if (move_input.LengthSq() > 0.01f) {
            move_input = move_input.Normalize();
        }

        float current_speed = input.IsKeyDown(Key::Shift) ? sprint_speed : move_speed;
        auto body = GetComponent<uz::RigidBody>();
        body->SetLinearVelocity(uz::Vec3(move_input.x, 0, move_input.z) * current_speed);

        // Jump
        if (input.IsKeyPressed(Key::Space) && is_grounded) {
            body->ApplyImpulse(uz::Vec3(0, jump_force, 0));
            is_grounded = false;
        }

        // Ground check (racast down)
        auto scene = GetComponent<uz::SceneComponent>();
        uz::RaycastResult hit = physics->Raycast(scene->GetPosition(), uz::Vec3(0, -1, 0), 1.5f);
        is_grounded = hit.IsHit;
    }
};

UZ_REGISTER_BEHAVIOR(FPSCharacter);
```

**Weapon System**:
```cpp
/// Weapon that fires projectiles on input.
class Weapon : public uz::Behavior {
    UZ_BEHAVIOR(Weapon);

private:
    uint32_t ammo = 120;
    float fire_rate = 0.1f;  // seconds
    float fire_cooldown = 0.0f;
    float bullet_speed = 50.0f;

public:
    void OnTick(float dt) override {
        auto input = GetWorld()->GetInput();

        fire_cooldown -= dt;
        if (input.IsMouseButtonDown(MouseButton::Left) && fire_cooldown <= 0 && ammo > 0) {
            FireBullet();
            ammo--;
            fire_cooldown = fire_rate;
        }
    }

private:
    void FireBullet() {
        auto world = GetWorld();
        auto scene = GetComponent<uz::SceneComponent>();

        // Raycast from camera forward
        auto camera = world->Find("main_camera");
        auto cam_scene = world->Get<uz::SceneComponent>(camera);
        uz::Vec3 forward = cam_scene->GetForwardVector();

        uz::RaycastResult hit = world->GetPhysics()->Raycast(
            cam_scene->GetPosition(), forward, 1000.0f);

        if (hit.IsHit) {
            // Apply damage to hit entity
            if (hit.HitEntity.Has<HealthComponent>()) {
                auto health = world->Get<HealthComponent>(hit.HitEntity);
                health->TakeDamage(25.0f);
            }
            // Play impact effect
            PlayVFX("content/vfx/bullet_impact.uzfx", hit.Position);
        }
    }
};

UZ_REGISTER_BEHAVIOR(Weapon);
```

**Enemy AI**:
```lua
-- Enemy behavior: patrol, detect player, chase, attack
local function enemy_update(ctx, dt)
    local enemy = ctx.entity
    local world = ctx.world

    -- Detect player in range
    local player = world:find("player")
    if not player then return end

    local enemy_pos = world:get(enemy, "SceneComponent").position
    local player_pos = world:get(player, "SceneComponent").position
    local dist = (player_pos - enemy_pos):length()

    if dist < 20.0 then
        -- Chase player
        local direction = (player_pos - enemy_pos):normalize()
        local body = world:get(enemy, "RigidBody")
        body:set_velocity(direction * 3.0)

        -- Fire at player
        if dist < 10.0 and math.random() < 0.02 then
            world:fire_projectile(enemy, player, 25.0)
        end
    else
        -- Patrol behavior
        local body = world:get(enemy, "RigidBody")
        body:set_velocity(uz.Vec3(2.0, 0, 0))
    end
end

return { on_update = enemy_update }
```

### File Structure

```
games/fps_prototype/
├── fps_prototype.uzproject
├── src/
│   ├── character_controller.cpp
│   ├── weapon_system.cpp
│   ├── damage_system.cpp
│   ├── hud.cpp
│   └── CMakeLists.txt
├── content/
│   ├── scenes/
│   │   ├── level_1.uzscene
│   │   └── level_2.uzscene
│   ├── meshes/
│   │   ├── character.gltf
│   │   ├── rifle.gltf
│   │   ├── enemy.gltf
│   │   └── level.gltf
│   ├── shaders/
│   │   ├── standard_pbr.uzshader
│   │   └── character_skin.uzshader
│   ├── behaviors/
│   │   ├── enemy_ai.lua
│   │   └── boss_ai.lua
│   ├── vfx/
│   │   ├── muzzle_flash.uzfx
│   │   └── bullet_impact.uzfx
│   ├── audio/
│   │   ├── rifle_fire.wav
│   │   ├── reload.wav
│   │   ├── hit.wav
│   │   └── ambience.ogg
│   └── ui/
│       ├── hud.html
│       └── hud.css
└── README.md
```

### Engine Systems Exercised

- **3D Rendering** (§12): Mesh rendering, shaders, lighting
- **Physics** (§16): Character controller, raycast, damage
- **Scripting** (§15): C++ and Lua behavior systems
- **Input** (§13): WASD, mouse look, click-to-fire
- **Audio** (§17): 3D spatial audio, music
- **UI** (§18): HUD (health, ammo, wave count)
- **VFX** (§25): Muzzle flash, impact effects, blood spray
- **Networking** (§24): Player state sync, enemy spawning (Phase 4)
- **AI** (§20): Enemy behavior trees, pathfinding

### Success Criteria

- [ ] Character moves and jumps smoothly
- [ ] Weapon fires and damages enemies
- [ ] AI enemies pursue and shoot back
- [ ] Waves spawn and escalate
- [ ] HUD updates in real-time
- [ ] 60 FPS sustained with 50+ entities
- [ ] Network sync (Phase 4): players see each other's actions

## Template: Starting a New Game

All game projects follow this structure:

```
games/<project_name>/
├── <project_name>.uzproject              # TOML config
├── src/
│   ├── *.cpp                             # Game code
│   └── CMakeLists.txt or meson.build
├── content/
│   ├── scenes/                           # .uzscene files
│   ├── meshes/                           # .gltf/.uzmesh
│   ├── textures/                         # .png/.hdr
│   ├── shaders/                          # .uzshader
│   ├── animations/                       # .uztexture (sprite sheets)
│   ├── audio/                            # .wav/.ogg
│   ├── behaviors/                        # .lua scripts
│   └── vfx/                              # .uzfx (particle systems)
└── README.md                             # Game design doc, controls, credits
```

### Project Configuration

```toml
# games/pong/pong.uzproject

[project]
name = "Pong"
version = "1.0.0"
author = "UzEngine Team"

[game]
title = "Pong"
startup_scene = "content/scenes/game.uzscene"
target_fps = 60

[graphics]
rhi = "dx12"
shader_model = "sm6.6"
resolution = [1920, 1080]
vsync = true

[physics]
gravity = [0, -9.81, 0]
max_bodies = 1024

[audio]
master_volume = 1.0
```

## Demos vs. Games

| Aspect | Demos | Games |
|--------|-------|-------|
| Purpose | Test single system | Integrated gameplay |
| Scope | < 500 lines code | 2000+ lines code |
| Location | `demos/` | `games/` |
| Build | Standalone `build.zig` | Shared engine build + local compile |
| Example | `demos/cube_render.cpp` | `games/pong/` |
| Test | Automated (CI/CD) | Manual (play-through) |

## Implementation Timeline

| Phase | Examples Added |
|-------|-----------------|
| **Phase 1** | Pong (validates basic rendering, input, collision) |
| **Phase 2** | Flappy Bird (validates animation, physics, audio, procedural generation) |
| **Phase 3** | Tower Defense (validates pathfinding, behavior trees, complex UI, economy) |
| **Phase 3.5** | FPS Prototype (validates 3D rendering, weapons, AI combat) |
| **Phase 4** | FPS with networking (validates Netcode for Entities, multiplayer) |

## Quick Start: Running Pong

```bash
# Build engine and Pong
zig build -Drelease --prefix zig-out

# Run Pong
./zig-out/bin/pong.exe
```

Or in VSCode extension:
```
Ctrl+Shift+P → "UzEngine: Open Example" → "Pong"
```

## Contributing New Examples

If you implement a new game:

1. **Follow the structure** — place in `games/<name>/`, use `.uzproject` config.
2. **Document gameplay** — explain controls and objectives in `README.md`.
3. **List systems exercised** — create a table showing which engine features it validates.
4. **Pass all success criteria** — no placeholder gameplay, run at target FPS.
5. **Add to table above** — update this document with the new example.
6. **Update roadmap** — adjust phase timeline if needed.
