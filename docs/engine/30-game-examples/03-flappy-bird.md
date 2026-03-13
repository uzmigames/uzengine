# Flappy Bird — 2D Physics, Animation, and Procedural Obstacles

> **Status**: Planned (Phase 2)
> **Source**: `games/flappy_bird/`
> **Engine systems exercised**: Physics simulation, sprite animation, procedural generation, UI, audio, triggers

## Overview

Flappy Bird extends Pong by introducing physics simulation, sprite animation, audio design, and procedurally generated obstacles. The game demonstrates dynamic entity creation (pipes), trigger volumes for scoring, and particle effects.

**Play time**: 5–60 seconds per game (varies with player skill)
**Target FPS**: 120 FPS sustained
**Resolution**: 1280x720 (vertical mobile-style aspect ratio)

## Gameplay

- **Player controls**: Spacebar to make bird flap (apply upward impulse)
- **Physics**: Bird falls under gravity, flaps against gravity
- **Obstacles**: Randomly-spaced pipes scroll from right to left
- **Scoring**: +1 point when bird passes between pipes
- **Game over**: Collision with pipe or ground → restart
- **Difficulty**: Pipes spawn faster/narrower as score increases

## Project Structure

```
games/flappy_bird/
├── flappy_bird.uzproject
├── src/
│   ├── bird_behavior.cpp          # Bird physics controller
│   ├── pipe_spawner.cpp           # Procedural obstacle generation
│   ├── game_manager.cpp           # Scoring, difficulty, game state
│   ├── particle_system.cpp        # Collision effects (optional Phase 2.5)
│   └── CMakeLists.txt
├── content/
│   ├── scenes/
│   │   └── game.uzscene           # Main scene
│   ├── meshes/
│   │   ├── bird.gltf              # Bird quad/sprite
│   │   ├── pipe.gltf              # Pipe geometry
│   │   └── ground.gltf            # Ground platform
│   ├── textures/
│   │   ├── bird_frame_0.png       # Animation frame 1
│   │   ├── bird_frame_1.png       # Animation frame 2
│   │   ├── bird_frame_2.png       # Animation frame 3
│   │   ├── pipe.png               # Pipe texture (green)
│   │   ├── background.png         # Scrolling background
│   │   └── ground.png
│   ├── animations/
│   │   └── bird_flap.uztexture    # Sprite sheet metadata
│   ├── audio/
│   │   ├── flap.wav               # Wing flap sound
│   │   ├── score.wav              # Point scored
│   │   ├── collision.wav          # Hit pipe/ground
│   │   └── ambience.ogg           # Background loop
│   └── ui/
│       ├── hud.html               # Score + game over screen
│       └── hud.css
└── README.md
```

## Scene File Excerpt (game.uzscene)

```json
{
  "metadata": {
    "version": "0.2",
    "title": "Flappy Bird",
    "author": "UzEngine Team"
  },

  "nodes": [
    {
      "id": "camera",
      "name": "Main Camera",
      "components": {
        "SceneComponent": {
          "position": [0, 3.6, -10],
          "rotation": [0, 0, 0]
        },
        "CameraComponent": {
          "is_active": true,
          "projection": "orthographic",
          "orthographic_size": 7.2,
          "near": 0.1,
          "far": 1000.0
        }
      }
    },

    {
      "id": "background",
      "name": "Background",
      "components": {
        "SceneComponent": {
          "position": [0, 3.6, 1],
          "rotation": [0, 0, 0],
          "scale": [10, 7.2, 1]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/background.gltf",
          "material": "content/materials/background.uzmat"
        }
      }
    },

    {
      "id": "bird",
      "name": "Bird",
      "components": {
        "SceneComponent": {
          "position": [1, 3.6, 0],
          "rotation": [0, 0, 0],
          "scale": [0.4, 0.3, 0.1]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/bird.gltf",
          "material": "content/materials/bird.uzmat"
        },
        "RigidBodyComponent": {
          "body_type": "dynamic",
          "mass": 1.0,
          "gravity_enabled": true,
          "drag": 0.2,
          "angular_drag": 0.5,
          "collision_group": 1,
          "collision_mask": 3           # Collides with pipes (group 2) and ground (group 3)
        },
        "CapsuleCollider": {
          "radius": 0.2,
          "half_height": 0.15
        },
        "AnimationComponent": {
          "animation": "content/animations/bird_flap.uztexture",
          "frame_rate": 12,
          "loop": true
        },
        "BehaviorComponent": {
          "class_name": "BirdBehavior",
          "properties": {
            "flap_force": 8.0,
            "max_velocity_y": 8.0,
            "rotation_speed": 45.0
          }
        }
      }
    },

    {
      "id": "ground",
      "name": "Ground",
      "components": {
        "SceneComponent": {
          "position": [0, -0.5, 0],
          "rotation": [0, 0, 0],
          "scale": [10, 0.5, 1]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/ground.gltf"
        },
        "RigidBodyComponent": {
          "body_type": "static",
          "collision_group": 3
        },
        "BoxCollider": {
          "half_extents": [5, 0.25, 0.5]
        }
      }
    },

    {
      "id": "ui_canvas",
      "name": "HUD Canvas",
      "components": {
        "SceneComponent": {
          "position": [0, 0, -5],
          "rotation": [0, 0, 0]
        },
        "CanvasComponent": {
          "width": 1280,
          "height": 720,
          "render_mode": "screen_space"
        },
        "BehaviorComponent": {
          "class_name": "HUDBehavior"
        }
      }
    }
  ]
}
```

## Core Game Logic

### Bird Behavior

```cpp
/// Bird controller — applies gravity, handles flap input, manages rotation.
class BirdBehavior : public uz::Behavior {
    UZ_BEHAVIOR(BirdBehavior);

private:
    float flap_force_ = 8.0f;
    float max_velocity_y_ = 8.0f;
    float rotation_speed_ = 45.0f;  // degrees/sec
    uint32_t frame_count_ = 0;

public:
    void OnUpdate(float dt) override {
        auto physics = GetWorld()->GetPhysics();
        auto body = GetComponent<uz::RigidBodyComponent>();
        auto scene = GetComponent<uz::SceneComponent>();
        auto animator = GetComponent<uz::AnimationComponent>();

        if (!body || !scene) return;

        // Handle flap input (spacebar)
        if (GetWorld()->GetInput().IsKeyPressed(uz::Key::Space)) {
            uz::Vec3 impulse(0, flap_force_, 0);
            body->ApplyImpulse(impulse);

            // Play flap sound
            GetWorld()->GetAudio()->PlayOneShot("content/audio/flap.wav");

            // Trigger wing animation (play one frame)
            if (animator) {
                animator->SetFrame(1);  // Mid-flap frame
            }

            UZ_LOG(Game, Info, "Bird flapped! Velocity Y: {:.2f}", body->GetLinearVelocity().y);
        }

        // Clamp falling velocity (terminal velocity)
        uz::Vec3 vel = body->GetLinearVelocity();
        if (vel.y < -max_velocity_y_) {
            vel.y = -max_velocity_y_;
            body->SetLinearVelocity(vel);
        }

        // Rotate bird based on velocity (tilts down when falling, up when rising)
        float target_rotation = std::clamp(vel.y * 10.0f, -45.0f, 45.0f);
        uz::Vec3 rot = scene->GetRotation();
        rot.z = uz::Lerp(rot.z, target_rotation * uz::kDegreesToRadians, rotation_speed_ * dt);
        scene->SetRotation(rot);

        frame_count_++;
    }
};

UZ_REGISTER_BEHAVIOR(BirdBehavior);
```

### Pipe Spawner

```cpp
/// Procedurally spawns pipe pairs at random heights.
class PipeSpawner : public uz::Behavior {
    UZ_BEHAVIOR(PipeSpawner);

private:
    float pipe_scroll_speed_ = 4.0f;
    float spawn_interval_ = 3.0f;  // Seconds between pipe pairs
    float spawn_timer_ = 0.0f;
    float spawn_x_ = 7.0f;         // Right edge of viewport

    float gap_size_ = 2.0f;        // Space between upper and lower pipe
    float min_gap_y_ = 1.0f;       // Minimum Y position of gap center
    float max_gap_y_ = 5.0f;       // Maximum Y position of gap center

    std::vector<uz::Entity> active_pipes_;

public:
    void OnUpdate(float dt) override {
        auto world = GetWorld();

        // Spawn new pipe pair
        spawn_timer_ += dt;
        if (spawn_timer_ >= spawn_interval_) {
            SpawnPipePair(world);
            spawn_timer_ = 0.0f;

            // Increase difficulty over time
            spawn_interval_ *= 0.98f;  // Spawn 2% faster each time
            spawn_interval_ = std::max(spawn_interval_, 1.5f);  // Minimum interval
        }

        // Scroll and despawn pipes
        for (auto it = active_pipes_.begin(); it != active_pipes_.end(); ) {
            auto entity = *it;
            if (!entity.IsValid()) {
                it = active_pipes_.erase(it);
                continue;
            }

            auto scene = world->GetComponent<uz::SceneComponent>(entity);
            uz::Vec3 pos = scene->GetPosition();
            pos.x -= pipe_scroll_speed_ * dt;
            scene->SetPosition(pos);

            // Despawn if off-screen left
            if (pos.x < -7.0f) {
                world->DestroyEntity(entity);
                it = active_pipes_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    void SpawnPipePair(uz::World* world) {
        // Random gap center Y
        float gap_y = uz::Random() * (max_gap_y_ - min_gap_y_) + min_gap_y_;

        // Upper pipe
        uz::Entity upper_pipe = CreatePipe(world,
            uz::Vec3(spawn_x_, gap_y + gap_size_ / 2.0f, 0),
            "upper"
        );
        active_pipes_.push_back(upper_pipe);

        // Lower pipe
        uz::Entity lower_pipe = CreatePipe(world,
            uz::Vec3(spawn_x_, gap_y - gap_size_ / 2.0f, 0),
            "lower"
        );
        active_pipes_.push_back(lower_pipe);

        // Scoring trigger (invisible volume)
        uz::Entity trigger = world->Create();
        world->Emplace<uz::SceneComponent>(trigger, uz::Vec3(spawn_x_, gap_y, 0));
        world->Emplace<uz::TriggerVolume>(trigger, uz::Vec3(0.1f, gap_size_, 1.0f))
            ->OnOverlapBegin([world](const uz::TriggerEvent& e) {
                // Check if overlapper is the bird
                auto bird_entity = world->FindByName("bird");
                if (e.OtherEntity == bird_entity) {
                    UZ_LOG(Game, Info, "Bird passed pipes! Score +1");
                    world->BroadcastEvent("score_point");
                    world->GetAudio()->PlayOneShot("content/audio/score.wav");
                }
            });

        UZ_LOG(Game, Debug, "Spawned pipe pair at Y={:.2f}", gap_y);
    }

    uz::Entity CreatePipe(uz::World* world, uz::Vec3 position, const char* name) {
        uz::Entity pipe = world->Create();

        // Scene component
        world->Emplace<uz::SceneComponent>(pipe, position);

        // Mesh
        world->Emplace<uz::StaticMeshComponent>(pipe,
            "content/meshes/pipe.gltf",
            "content/materials/pipe.uzmat"
        );

        // Collision
        world->Emplace<uz::RigidBodyComponent>(pipe).body_type = uz::RigidBodyType::Static;
        world->Emplace<uz::BoxCollider>(pipe, uz::Vec3(0.5f, 3.0f, 0.1f));

        return pipe;
    }
};

UZ_REGISTER_BEHAVIOR(PipeSpawner);
```

### Game Manager

```cpp
/// Manages score, difficulty, and game-over conditions.
class GameManager : public uz::Behavior {
    UZ_BEHAVIOR(GameManager);

private:
    uint32_t score_ = 0;
    bool game_over_ = false;

public:
    void OnBeginPlay() override {
        GetWorld()->ListenToEvent("score_point", [this](const uz::EventData&) {
            score_++;
            UZ_LOG(Game, Info, "Score: {}", score_);
            GetWorld()->BroadcastEvent("score_updated", "score", static_cast<int>(score_));
        });

        GetWorld()->ListenToEvent("bird_collision", [this](const uz::EventData&) {
            if (!game_over_) {
                game_over_ = true;
                UZ_LOG(Game, Warning, "Game Over! Final score: {}", score_);
                GetWorld()->BroadcastEvent("game_over");
                GetWorld()->GetAudio()->PlayOneShot("content/audio/collision.wav");
            }
        });
    }

    uint32_t GetScore() const { return score_; }
    bool IsGameOver() const { return game_over_; }
};

UZ_REGISTER_BEHAVIOR(GameManager);
```

## Engine Systems Exercised

### 1. Physics Simulation (§16)
- Dynamic rigid body (bird with gravity)
- Static colliders (pipes, ground)
- Impulse forces (flap)
- Collision detection and response
- Velocity clamping

### 2. Sprite Animation (§19)
- Sprite sheet animation (bird wing flap)
- Frame-based animation playback
- Animation triggered by game events

### 3. Trigger Volumes
- Invisible collision-free zones for scoring
- OnOverlapBegin callback pattern
- Event broadcasting on trigger

### 4. Procedural Generation
- Randomized pipe placement
- Spawn scheduling and despawning
- Difficulty scaling (spawn rate increase)

### 5. Audio (§17)
- Sound effects triggered by events (flap, score, collision)
- Background ambience loop
- Positional audio (optional)

### 6. Input (§13)
- Key polling (spacebar for flap)
- Responsive input with physics integration

### 7. UI & HUD
- Score display (text overlay)
- Game-over screen with final score
- Restart prompt

## Success Criteria

- [ ] Game runs at 120 FPS sustained
- [ ] Bird falls smoothly under gravity
- [ ] Flap response immediate (<1 frame)
- [ ] Pipes scroll at constant speed
- [ ] Collisions detected accurately
- [ ] Scoring triggers only once per pipe pair
- [ ] Difficulty increases (spawn interval decreases)
- [ ] Audio plays in sync with gameplay
- [ ] Game-over screen appears on collision
- [ ] 10+ pipe pairs spawned before OOM (no memory leaks)

## Extend Flappy Bird

- **Particle effects** — Collision sparks, feather trails
- **Power-ups** — Shield (2x collision tolerance), slow-mo
- **Obstacles** — Moving clouds (no collision)
- **Backgrounds** — Parallax scrolling (multiple layers)
- **Leaderboard** — Store best score locally, integrate Steam (§27-02)
- **Sound design** — Dynamic pitch shifts based on score
- **Mobile input** — Touch/tap instead of spacebar

