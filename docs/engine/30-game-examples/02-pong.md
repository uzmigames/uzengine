# Pong — Complete 2D Arcade Game Example

> **Status**: Planned (Phase 1)
> **Source**: `games/pong/`, complete standalone project
> **Engine systems exercised**: Input, 2D rendering, basic physics, UI, audio

## Overview

Pong validates UzEngine's 2D rendering pipeline, real-time input handling, scene composition, and audio playback. The game is intentionally simple (two paddles, one ball, score counter) but demonstrates all core engine systems working together.

**Play time**: ~30 seconds per match
**Target FPS**: 60 FPS sustained
**Resolution**: 1920x1080

## Gameplay

- **Two paddles**: Player controls left paddle with W/S keys; AI controls right paddle
- **Ball physics**: Bounces off paddles, walls, and out-of-bounds edges
- **Scoring**: First player to reach 5 points wins
- **AI behavior**: Simple smooth pursuit (lerp towards ball Y)
- **Win condition**: Display winner, allow restart

## Project Structure

```
games/pong/
├── pong.uzproject              # Project configuration
├── src/
│   ├── pong.cpp               # Game logic (paddles, ball, scoring)
│   ├── pong.h                 # Component definitions
│   └── CMakeLists.txt         # Build configuration
├── content/
│   ├── scenes/
│   │   └── game.uzscene       # Main scene (paddles, ball, cameras, UI)
│   ├── meshes/
│   │   ├── paddle.gltf        # Paddle geometry
│   │   └── ball.gltf          # Ball (sphere)
│   ├── textures/
│   │   └── white.png          # Plain white texture
│   ├── shaders/
│   │   └── unlit_color.uzshader  # Simple unlit shader
│   ├── audio/
│   │   ├── bounce.wav         # Paddle/wall collision sound
│   │   └── score.wav          # Point scored sound
│   └── ui/
│       ├── hud.html           # Score display (web UI in 2D panel)
│       └── hud.css
└── README.md                  # Design doc, controls, screenshot
```

## Scene File (game.uzscene)

```json
{
  "metadata": {
    "version": "0.2",
    "title": "Pong",
    "author": "UzEngine Team",
    "description": "Classic Pong arcade game"
  },

  "nodes": [
    {
      "id": "camera",
      "name": "Main Camera",
      "components": {
        "SceneComponent": {
          "position": [0, 0, -15],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        },
        "CameraComponent": {
          "is_active": true,
          "projection": "orthographic",
          "orthographic_size": 10,
          "near": 0.1,
          "far": 1000.0
        }
      }
    },

    {
      "id": "player_paddle",
      "name": "Player Paddle",
      "components": {
        "SceneComponent": {
          "position": [-8.5, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [0.5, 2.0, 0.5]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/paddle.gltf",
          "material": "content/materials/white.uzmat"
        },
        "BoxCollider": {
          "half_extents": [0.5, 2.0, 0.5],
          "is_trigger": false
        },
        "BehaviorComponent": {
          "class_name": "PaddleBehavior",
          "properties": {
            "speed": 7.0,
            "boundary_top": 4.5,
            "boundary_bottom": -4.5
          }
        }
      }
    },

    {
      "id": "ai_paddle",
      "name": "AI Paddle",
      "components": {
        "SceneComponent": {
          "position": [8.5, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [0.5, 2.0, 0.5]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/paddle.gltf",
          "material": "content/materials/white.uzmat"
        },
        "BoxCollider": {
          "half_extents": [0.5, 2.0, 0.5]
        },
        "BehaviorComponent": {
          "class_name": "AIBehavior",
          "properties": {
            "speed": 6.0,
            "smoothing": 0.08,
            "boundary_top": 4.5,
            "boundary_bottom": -4.5
          }
        }
      }
    },

    {
      "id": "ball",
      "name": "Ball",
      "components": {
        "SceneComponent": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0],
          "scale": [0.25, 0.25, 0.25]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/ball.gltf",
          "material": "content/materials/white.uzmat"
        },
        "SphereCollider": {
          "radius": 0.25,
          "is_trigger": false
        },
        "BehaviorComponent": {
          "class_name": "BallBehavior",
          "properties": {
            "initial_speed": 6.0,
            "max_speed": 12.0,
            "out_of_bounds_x": 11.0,
            "paddle_separation": 1.0
          }
        }
      }
    },

    {
      "id": "top_wall",
      "name": "Top Wall",
      "components": {
        "SceneComponent": {
          "position": [0, 5, 0],
          "rotation": [0, 0, 0],
          "scale": [20, 0.5, 0.5]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/wall.gltf"
        },
        "BoxCollider": {
          "half_extents": [20, 0.5, 0.5]
        }
      }
    },

    {
      "id": "bottom_wall",
      "name": "Bottom Wall",
      "components": {
        "SceneComponent": {
          "position": [0, -5, 0],
          "rotation": [0, 0, 0],
          "scale": [20, 0.5, 0.5]
        },
        "StaticMeshComponent": {
          "mesh": "content/meshes/wall.gltf"
        },
        "BoxCollider": {
          "half_extents": [20, 0.5, 0.5]
        }
      }
    },

    {
      "id": "ui_canvas",
      "name": "UI Canvas",
      "components": {
        "SceneComponent": {
          "position": [0, 0, -5],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        },
        "CanvasComponent": {
          "width": 1920,
          "height": 1080,
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

## Core Game Logic (pong.cpp)

```cpp
#include "pong.h"
#include <uz/engine.h>
#include <cmath>
#include <iomanip>

UZ_DECLARE_LOG_CATEGORY(Pong);

// ─────────────────────────────────────────────────────────────────────────────
// Paddle Behavior — Player-controlled paddle
// ─────────────────────────────────────────────────────────────────────────────

class PaddleBehavior : public uz::Behavior {
    UZ_BEHAVIOR(PaddleBehavior);

public:
    /// Speed in units/sec
    float speed = 7.0f;

    /// Vertical boundaries (clamp paddle position)
    float boundary_top = 4.5f;
    float boundary_bottom = -4.5f;

    void OnUpdate(float dt) override {
        auto& input = GetWorld()->GetInput();
        auto scene = GetComponent<uz::SceneComponent>();
        if (!scene) return;

        // W/S keys move paddle
        float dy = 0;
        if (input.IsKeyDown(uz::Key::W)) dy += speed * dt;
        if (input.IsKeyDown(uz::Key::S)) dy -= speed * dt;

        // Update position with boundary clamp
        auto pos = scene->GetPosition();
        pos.y += dy;
        pos.y = std::clamp(pos.y, boundary_bottom, boundary_top);
        scene->SetPosition(pos);

        UZ_LOG(Pong, Debug, "Player paddle Y: {:.2f}", pos.y);
    }
};

UZ_REGISTER_BEHAVIOR(PaddleBehavior);

// ─────────────────────────────────────────────────────────────────────────────
// AI Behavior — Computer-controlled paddle with smooth pursuit
// ─────────────────────────────────────────────────────────────────────────────

class AIBehavior : public uz::Behavior {
    UZ_BEHAVIOR(AIBehavior);

private:
    float speed = 6.0f;
    float smoothing = 0.08f;  // Low-pass filter: 0 (no movement) to 1 (instant)
    float boundary_top = 4.5f;
    float boundary_bottom = -4.5f;

public:
    void OnUpdate(float dt) override {
        auto world = GetWorld();
        auto ball_entity = world->FindByName("ball");
        if (!ball_entity.IsValid()) {
            UZ_LOG(Pong, Warning, "Ball entity not found");
            return;
        }

        auto ball_scene = world->GetComponent<uz::SceneComponent>(ball_entity);
        auto paddle_scene = GetComponent<uz::SceneComponent>();
        if (!ball_scene || !paddle_scene) return;

        // Get current positions
        uz::Vec3 ball_pos = ball_scene->GetPosition();
        uz::Vec3 paddle_pos = paddle_scene->GetPosition();

        // Smooth pursuit: lerp towards ball Y
        float target_y = ball_pos.y;
        float current_y = paddle_pos.y;
        float desired_y = uz::Lerp(current_y, target_y, smoothing);
        desired_y = std::clamp(desired_y, boundary_bottom, boundary_top);

        // Update paddle position
        paddle_pos.y = desired_y;
        paddle_scene->SetPosition(paddle_pos);
    }
};

UZ_REGISTER_BEHAVIOR(AIBehavior);

// ─────────────────────────────────────────────────────────────────────────────
// Ball Behavior — Physics and collision
// ─────────────────────────────────────────────────────────────────────────────

class BallBehavior : public uz::Behavior {
    UZ_BEHAVIOR(BallBehavior);

private:
    uz::Vec3 velocity_;
    float initial_speed_ = 6.0f;
    float max_speed_ = 12.0f;
    float out_of_bounds_x_ = 11.0f;
    float paddle_separation_ = 1.0f;

    uint32_t player_score_ = 0;
    uint32_t ai_score_ = 0;

public:
    void OnBeginPlay() override {
        // Random initial direction
        bool towards_ai = uz::Random() > 0.5f;
        float angle = (uz::Random() - 0.5f) * 60.0f * uz::kDegreesToRadians;  // ±30°

        velocity_.x = initial_speed_ * (towards_ai ? 1.0f : -1.0f) * std::cos(angle);
        velocity_.y = initial_speed_ * std::sin(angle);
        velocity_.z = 0.0f;

        UZ_LOG(Pong, Info, "Pong game started. Initial velocity: ({:.2f}, {:.2f})",
            velocity_.x, velocity_.y);
    }

    void OnUpdate(float dt) override {
        auto scene = GetComponent<uz::SceneComponent>();
        if (!scene) return;

        uz::Vec3 pos = scene->GetPosition();

        // Move ball
        pos += velocity_ * dt;

        // Bounce off top/bottom walls
        if (pos.y > 4.75f) {
            velocity_.y *= -1.0f;
            pos.y = 4.75f;
            PlaySound("content/audio/bounce.wav");
        }
        if (pos.y < -4.75f) {
            velocity_.y *= -1.0f;
            pos.y = -4.75f;
            PlaySound("content/audio/bounce.wav");
        }

        // Check paddle collisions (simplified AABB)
        auto world = GetWorld();
        CheckPaddleCollision(world, "player_paddle", uz::Vec3(-8.5f, 0, 0), pos);
        CheckPaddleCollision(world, "ai_paddle", uz::Vec3(8.5f, 0, 0), pos);

        // Out of bounds: reset and update score
        if (pos.x > out_of_bounds_x_) {
            UZ_LOG(Pong, Info, "Player scores!");
            player_score_++;
            PlaySound("content/audio/score.wav");
            ResetBall();
            return;
        }
        if (pos.x < -out_of_bounds_x_) {
            UZ_LOG(Pong, Info, "AI scores!");
            ai_score_++;
            PlaySound("content/audio/score.wav");
            ResetBall();
            return;
        }

        scene->SetPosition(pos);

        // Check for game over
        if (player_score_ >= 5) {
            UZ_LOG(Pong, Info, "Player wins! Final score: {} - {}", player_score_, ai_score_);
            GetWorld()->BroadcastEvent("game_over", "player");
        } else if (ai_score_ >= 5) {
            UZ_LOG(Pong, Info, "AI wins! Final score: {} - {}", ai_score_, player_score_);
            GetWorld()->BroadcastEvent("game_over", "ai");
        }
    }

private:
    void CheckPaddleCollision(uz::World* world, const char* paddle_name, uz::Vec3 paddle_pos, uz::Vec3& ball_pos) {
        auto paddle_entity = world->FindByName(paddle_name);
        if (!paddle_entity.IsValid()) return;

        auto paddle_scene = world->GetComponent<uz::SceneComponent>(paddle_entity);
        if (!paddle_scene) return;

        uz::Vec3 p = paddle_scene->GetPosition();

        // Simple AABB collision (paddle is 0.5 × 2.0)
        if (std::abs(ball_pos.x - p.x) < paddle_separation_ + 0.25f &&
            std::abs(ball_pos.y - p.y) < 1.0f + 0.25f) {

            // Bounce: reverse X velocity, add spin from paddle Y speed
            velocity_.x *= -1.05f;  // Slight speed increase
            velocity_.x = std::clamp(velocity_.x, -max_speed_, max_speed_);

            // Add spin based on paddle position relative to ball
            float paddle_center_y = p.y;
            float hit_offset = (ball_pos.y - paddle_center_y) / 1.0f;  // Normalize to [-1, 1]
            velocity_.y += hit_offset * 2.0f;  // Add up to 2.0 units/sec spin

            PlaySound("content/audio/bounce.wav");

            // Push ball out of paddle to prevent multiple collisions
            ball_pos.x += velocity_.x * 0.01f;  // Small nudge
        }
    }

    void ResetBall() {
        auto scene = GetComponent<uz::SceneComponent>();
        if (!scene) return;

        // Random initial direction again
        bool towards_ai = uz::Random() > 0.5f;
        float angle = (uz::Random() - 0.5f) * 60.0f * uz::kDegreesToRadians;

        velocity_.x = initial_speed_ * (towards_ai ? 1.0f : -1.0f) * std::cos(angle);
        velocity_.y = initial_speed_ * std::sin(angle);

        scene->SetPosition(uz::Vec3(0, 0, 0));
    }

    void PlaySound(const char* path) {
        auto audio = GetWorld()->GetAudio();
        if (audio) {
            audio->PlayOneShot(path);
        }
    }
};

UZ_REGISTER_BEHAVIOR(BallBehavior);

// ─────────────────────────────────────────────────────────────────────────────
// HUD Behavior — Display score and game state
// ─────────────────────────────────────────────────────────────────────────────

class HUDBehavior : public uz::Behavior {
    UZ_BEHAVIOR(HUDBehavior);

private:
    uint32_t player_score_ = 0;
    uint32_t ai_score_ = 0;
    bool game_over_ = false;
    std::string winner_;

public:
    void OnBeginPlay() override {
        GetWorld()->ListenToEvent("game_over", [this](const uz::EventData& data) {
            game_over_ = true;
            winner_ = data.GetString("winner");
            UZ_LOG(Pong, Info, "Game over event received: {}", winner_);
        });
    }

    void OnUpdate(float dt) override {
        // Update score from ball behavior (simplified: query world state)
        // Real implementation would use message passing or shared data
    }

    void RenderUI() {
        // Render score text in top center
        // Render "Press SPACE to restart" if game_over_
    }
};

UZ_REGISTER_BEHAVIOR(HUDBehavior);
```

## Project Configuration (pong.uzproject)

```toml
[project]
name = "Pong"
version = "1.0.0"
author = "UzEngine Team"
description = "Classic Pong arcade game for UzEngine"

[game]
title = "Pong"
startup_scene = "content/scenes/game.uzscene"
target_fps = 60
enable_vsync = true

[graphics]
rhi = "dx12"
shader_model = "sm6.6"
render_width = 1920
render_height = 1080
anti_aliasing = "msaa_4x"
upscaling = "none"

[physics]
gravity = [0, 0, 0]           # No gravity in 2D pong
max_bodies = 100
simulation_substeps = 1

[audio]
master_volume = 1.0
audio_device = "default"
```

## Engine Systems Exercised

### 1. Input System (§13)
- **W/S key polling** for paddle movement
- Real-time input with no frame delay

### 2. Scene Management (§11)
- Entity hierarchy (camera, paddles, ball, walls)
- Entity lookup by name (`world->FindByName("ball")`)
- Component composition (SceneComponent, MeshComponent, BehaviorComponent)

### 3. Behavior System (§10)
- Three behavior classes: PaddleBehavior, AIBehavior, BallBehavior
- Lifecycle hooks: OnBeginPlay(), OnUpdate(dt)
- Component access via GetComponent<>()

### 4. Rendering (§12)
- 2D orthographic rendering
- Static mesh rendering (paddles, ball, walls)
- Materials and textures
- Camera setup (orthographic projection)

### 5. Audio (§17)
- 2D sound effects (bounce.wav, score.wav)
- PlayOneShot() for one-off sounds
- Sound triggered by collision events

### 6. Physics (§16) (Optional)
- Basic collision detection (paddle-ball)
- Boundary constraints
- Simplified: AABB instead of full physics engine

### 7. Event System
- BroadcastEvent for game-over signaling
- ListenToEvent for HUD updates

## Success Criteria

- [ ] Game launches without crashes
- [ ] Paddle responds to W/S input within 1 frame (16 ms @ 60 FPS)
- [ ] Ball moves smoothly and bounces predictably
- [ ] AI paddle pursues ball without jerky motion
- [ ] Collisions detected accurately (within 1 frame)
- [ ] Audio plays with <100 ms latency
- [ ] Score increments correctly
- [ ] Game-over screen appears after 5 points
- [ ] 60 FPS sustained on RTX 4070+
- [ ] Hot-reload works: edit PaddleBehavior.cpp, save, see changes in-game

## Quick Start

```bash
# Navigate to pong project
cd games/pong

# Build engine + pong
zig build -Drelease --prefix zig-out

# Run pong
./zig-out/bin/pong

# Or via VSCode extension:
# Ctrl+Shift+P → "UzEngine: Open Example" → "Pong"
```

## Controls

| Key | Action |
|-----|--------|
| **W** | Move paddle up |
| **S** | Move paddle down |
| **SPACE** | (Future) Pause/Resume |
| **R** | (Future) Restart game |
| **ESC** | Exit to main menu |

## Telemetry & Debugging

```cpp
// In-game console (Ctrl+`) shows:
// - Frame time (ms)
// - FPS counter
// - Ball velocity magnitude
// - Collision count per frame
// - Score
```

## Extend Pong

Future enhancements:
- **Difficulty levels** — Speed up ball for hard mode
- **Power-ups** — Slow ball, expand paddle, multi-ball
- **Particle effects** — Collision sparks, trail
- **Background music** — Looping soundtrack
- **Leaderboard** — Steam integration (§27-02)
- **Networking** — Local 2-player (human vs human)

