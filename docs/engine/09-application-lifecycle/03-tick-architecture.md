# Application Lifecycle — Tick Architecture

> **Status**: Planned (Milestone 2.0)
> **Source**: `engine/core/lifecycle/time_manager.h`, `tick_manager.h`, `main_loop.h`
> **Depends on**: Event system, World, Physics, Renderer
> **References**: Godot `MainTimerSync`, UE5 `FEngineLoop::Tick`

## Purpose

The tick architecture manages the frame loop timing and subsystem update order. It decouples physics (fixed timestep) from rendering (variable framerate) and provides ordered tick groups so that systems update in the correct sequence.

## Main Loop Overview

```
Frame Boundary
│
├─ Poll OS Events (window, input, messages)
│
├─ Time Update (dt = now - last_frame)
│
├──────────────────────────────────────────────────
│ Physics Loop — Fixed timestep (1/60s, may iterate N times)
├──────────────────────────────────────────────────
│  while (accumulated_time >= fixed_dt) {
│      Fire: OnPrePhysics
│      Tick Group: PrePhysics
│          └─ Character controller update
│          └─ Input processing
│      Tick Group: Physics
│          └─ Jolt Physics simulation step
│      Tick Group: PostPhysics
│          └─ Callbacks, constraints solving
│      Fire: OnPostPhysics
│      accumulated_time -= fixed_dt
│  }
│
├──────────────────────────────────────────────────
│ Game Update — Variable frequency (once per frame)
├──────────────────────────────────────────────────
│  Fire: OnPreUpdate
│  Tick Group: Update
│      └─ Behavior::OnTick()
│      └─ Lua script updates
│      └─ Animation state machine updates
│  Tick Group: PostUpdate
│      └─ Deferred event processing
│      └─ Scheduled callbacks
│  Fire: OnPostUpdate
│
├──────────────────────────────────────────────────
│ Render — Variable frequency (once per frame)
├──────────────────────────────────────────────────
│  Fire: OnPreRender
│  RenderWorld sync
│      └─ Camera transform from active camera
│      └─ Visibility culling
│      └─ Skeletal mesh LOD selection
│  Build draw calls
│      └─ Static mesh rendering
│      └─ Skeletal mesh rendering
│      └─ Decal rendering
│      └─ Particle effects
│  Fire: OnPostRender
│  Present to screen
│
├─ Deferred Entity Destruction
│  └─ Destroy entities marked for deletion
│
└─ Frame Complete
```

## Time Manager

The `TimeManager` converts wall-clock time into engine-relative time and manages fixed timestep accumulation.

```cpp
class TimeManager {
public:
    /// Call once per frame with current wall-clock time (seconds, high-precision)
    void Update(double current_time);

    /// Elapsed time in the current frame (seconds)
    float DeltaTime() const;

    /// Total time since engine start (seconds)
    double TotalTime() const;

    /// Paused state — time stops advancing
    void Pause();
    void Resume();
    bool IsPaused() const;

    /// Fixed physics timestep (typically 1/60 = 0.01666s)
    float FixedDeltaTime() const { return kFixedDt; }

    /// How much time has accumulated beyond exact physics steps
    /// Used for interpolation between physics frames
    float PhysicsSubstep() const;
};
```

### Fixed Timestep Accumulation

Physics requires deterministic, fixed timesteps. Each frame, variable delta-time is accumulated, and physics steps are taken for each accumulated fixed-timestep interval:

```cpp
// Pseudocode: MainLoop timing
double wall_time = GetCurrentTime();
time_manager.Update(wall_time);

float dt = time_manager.DeltaTime();
accumulated_time += dt;

int physics_iterations = 0;
while (accumulated_time >= FIXED_DT) {
    TickPhysics(FIXED_DT);
    accumulated_time -= FIXED_DT;
    physics_iterations++;
}

// Typically 1 iteration at 60 FPS, 2 at 30 FPS
UZ_LOG(Physics, Debug, "Physics iterations this frame: {}", physics_iterations);
```

### Why Not Variable Timestep Physics?

Variable timestep (`dt = actual_elapsed_time`) is simple but breaks determinism:

- **Same inputs, different outputs**: Objects fall through walls at 30 FPS but not 60 FPS
- **Non-reproducible**: Replays diverge mid-game
- **Networking breaks**: Deterministic simulation is required for multiplayer sync
- **Fixed timestep + interpolation** achieves smooth visuals at any framerate while maintaining physics stability

## Tick Groups

Game code doesn't run in a single `OnTick()` callback. Instead, systems are organized into **tick groups** — explicit ordering categories. Each tick group fires at a specific point in the frame:

| Tick Group | Frequency | Purpose |
|-----------|-----------|---------|
| `PrePhysics` | Fixed (each physics step) | Character controller input, constraint prep |
| `Physics` | Fixed (each physics step) | Physics simulation step (Jolt) |
| `PostPhysics` | Fixed (each physics step) | Physics callbacks, constraint solving, ragdoll updates |
| `Update` | Variable (once per frame) | Behavior ticks, animation updates, game logic |
| `PostUpdate` | Variable (once per frame) | Event processing, deferred callbacks, cleanup |

### Registering Systems in Tick Groups

Each system (including game behaviors) registers a callback with its tick group:

```cpp
class MyBehavior : public Behavior {
public:
    MyBehavior(Entity owner) : Behavior(owner) {
        // Register tick callbacks
        TickManager::Register(TickGroup::Update, [this](float dt) {
            this->OnTick(dt);
        });
    }

    void OnTick(float dt) {
        // Called once per frame in Update group
        // Access world, physics, input, etc.
    }
};
```

Or via direct API:

```cpp
// Register a system to tick in PostPhysics group
TickManager::Register(TickGroup::PostPhysics, [world](float dt) {
    world->UpdateSkeletalMeshes(dt);
});
```

## Frame Loop with Tick Groups

```cpp
void MainLoop(World* world) {
    while (is_running) {
        // 1. Poll input
        InputSystem::Poll();

        // 2. Update time
        double wall_time = PlatformTime::GetSeconds();
        time_manager.Update(wall_time);

        // 3. Fixed-timestep physics loop
        float accumulated_time = time_manager.GetAccumulatedTime();
        while (accumulated_time >= FIXED_DT) {
            TickManager::Fire(TickGroup::PrePhysics, FIXED_DT);
            TickManager::Fire(TickGroup::Physics, FIXED_DT);
            TickManager::Fire(TickGroup::PostPhysics, FIXED_DT);
            accumulated_time -= FIXED_DT;
        }

        // 4. Variable-rate game update
        float dt = time_manager.DeltaTime();
        TickManager::Fire(TickGroup::Update, dt);
        TickManager::Fire(TickGroup::PostUpdate, dt);

        // 5. Render
        RenderWorld::Sync(world);
        Renderer::Render(world);
        SwapChain::Present();

        // 6. Cleanup
        world->DestroyDeferredEntities();
    }
}
```

## Pause and Timescale

Games often need to pause (gameplay stopped, UI continues) or slow-motion effects. The time manager supports both:

```cpp
// Complete pause — all ticks stopped
if (user_pressed_pause) {
    time_manager.Pause();
    // OnTick() callbacks don't fire
    // Physics doesn't step
    // But rendering still works (UI animations continue)
}

// Timescale (slow-motion)
if (player_hit_by_slowtime_spell) {
    time_manager.SetTimescale(0.5f);  // Half speed
}
```

### Implementation

```cpp
class TimeManager {
private:
    float timescale = 1.0f;
    bool paused = false;
    double last_time = 0.0;

public:
    void Update(double current_time) {
        if (paused) {
            delta_time = 0.0f;  // No progression
            return;
        }

        double raw_dt = current_time - last_time;
        delta_time = raw_dt * timescale;  // Scale affects all ticks
        last_time = current_time;
    }
};
```

## Physics Interpolation

Since physics runs at fixed timestep but rendering happens at variable framerate, visual jitter occurs if we render the exact physics state. **Interpolation** smooths movement:

```cpp
// Render frame during physics update
float substep = time_manager.PhysicsSubstep() / FIXED_DT;  // 0.0 to 1.0

// Interpolate bone transforms
Transform prev_pose = skeleton.GetPhysicsPose(frame_n - 1);
Transform curr_pose = skeleton.GetPhysicsPose(frame_n);
Transform interpolated = Transform::Lerp(prev_pose, curr_pose, substep);

renderer.SkeletalMesh(entity, interpolated);
```

## Engine Comparison

| Aspect | UE5 | Godot 4 | Unity | **UzEngine** |
|--------|-----|---------|-------|--------------|
| Physics timestep | Fixed (fixed_delta_time) | Fixed (physics_tps) | Fixed (0.02s) | **Fixed (configurable, 1/60s)** |
| Main loop | `FEngineLoop::Tick` | `MainTimerSync::Process` | Built into engine | **TickManager::Fire** per group |
| Tick groups | 6+ (PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork, etc.) | `_physics_process`, `_process` | Fixed/LateUpdate | **4 groups** (PrePhysics, Physics, PostPhysics, Update, PostUpdate) |
| Frame limiting | `bEnableFrameRateLimit` | target_fps | `Application.targetFrameRate` | **SwapChain VSYNC + frame limiter** |
| Pause support | Persistent timers + `bPauseSubobjects` | `process_mode` per node | `Time.timeScale` | **TimeManager.Pause()** |

## Rules

1. **ALWAYS use fixed timestep for physics** — variable timestep breaks determinism
2. **NEVER call physics queries outside Physics/PostPhysics groups** — race conditions
3. **ALWAYS register ticks in the correct group** — UpdatePhysics in PrePhysics, not Update
4. **NEVER modify entities during iteration** — defer destruction to PostUpdate
5. **ALWAYS respect pause and timescale** — use `time_manager.DeltaTime()`, not `GetCurrentTime() - last_time`
