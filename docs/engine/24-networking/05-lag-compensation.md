# Lag Compensation — Server-Side Rewind, Input Buffering, Client Prediction, Reconciliation

> **Depends on**: Networking system (§24), Server architecture (§24.2), Replication (§24.3)
> **Used by**: Multiplayer gameplay, hit detection, fair PvP
> **Algorithms**: Server-side rewind, client-side prediction + correction

## Purpose

Network latency (ping) causes perception mismatches:

- **Client sees**: Enemy at position X
- **Server knows**: Enemy is already at position X+dt*velocity (server is ahead in time)
- **Hitscan shot**: Client aims at X, server checks hit at X+dt*velocity → miss feels unfair

Lag compensation solves this by:

1. **Server-side rewind**: Rewind enemy positions to when client fired (accounting for ping)
2. **Client-side prediction**: Anticipate movement, render client view slightly into future
3. **Reconciliation**: Server corrects mispredicted positions with interpolation

## Server-Side Rewind

### Position History

Server maintains per-player transform history:

```cpp
namespace uz::Networking {
    /// One frame of position history.
    struct PositionFrame {
        float server_time;    // Server time when recorded
        Vec3 position;
        Quat rotation;
        Vec3 velocity;
    };

    /// Rewind buffer: 1 second of history at 60 Hz = 60 frames.
    class RewindBuffer {
    public:
        /// Record current position (called once per server tick).
        void Record(float server_time, const Transform& transform);

        /// Get position at historical time (in the past).
        Transform GetTransformAt(float historical_time) const;

        /// Clear old entries outside rewind window.
        void Cleanup(float current_time);

    private:
        std::deque<PositionFrame> history_;
        float rewind_window_ = 1.0f;  // 1 second of history
        float last_record_time_ = 0.0f;
    };
}

// Usage:
uz::Networking::RewindBuffer rewind_buffer;

// Server tick
void ServerTick(float dt) {
    current_time += dt;

    // Record position
    for (auto& player : players) {
        rewind_buffer.Record(current_time, player.transform);
    }

    // ...

    // Cleanup old entries
    rewind_buffer.Cleanup(current_time);
}
```

### Hitscan with Rewind

When a client requests a hit:

```cpp
namespace uz::Networking {
    /// Hitscan shot fired by client.
    struct HitscanRequest {
        uz::EntityID shooter;
        Vec3 ray_origin;      // Where bullet starts
        Vec3 ray_direction;   // Direction (normalized)
        float client_timestamp;  // Client's frame time
    };
}

// Server-side hit detection with rewind:
void WeaponComponent::RequestHitscan(const HitscanRequest& request) {
    // Get shooter's ping
    float ping = GetClientPing(request.shooter);

    // Server time when client fired shot
    // Client was ping/2 behind server (RTT / 2)
    float rewind_time = current_server_time - ping;

    // Check all potential targets
    for (auto& target : world->GetEntities<CharacterComponent>()) {
        if (target == request.shooter) continue;  // Can't shoot self

        // Get target's HISTORICAL position (where they were when client fired)
        Transform historical_pos = target->rewind_buffer.GetTransformAt(rewind_time);

        // Check if ray hits the historical position
        uz::RaycastHit hit;
        if (PhysicsWorld::Raycast(
            request.ray_origin,
            request.ray_direction,
            hit,
            historical_pos  // Use rewound position for hit test
        )) {
            if (hit.entity == target) {
                ApplyDamage(target, request.damage);
                break;
            }
        }
    }
}
```

### Rewind Window

Limits how far back we can rewind (prevents abuse):

```cpp
// Rewind buffer stores 1 second (60 frames at 60 Hz)
const float REWIND_WINDOW = 1.0f;

// If client has ping > 1 second (>1000 ms), not compensated
if (ping > REWIND_WINDOW) {
    // Reject request or apply penalty
}
```

## Client-Side Prediction

Client anticipates movement to reduce perceived latency.

### Prediction Loop

```
┌─────────────────────────────────────┐
│ Client Frame                        │
├─────────────────────────────────────┤
│ 1. Read input (WASD, mouse)        │
│    Apply locally (no network delay) │
│                                      │
│ 2. Poll network (receive server     │
│    updates from 2-3 frames ago)     │
│                                      │
│ 3. Compare: predicted vs actual     │
│    If mismatch > threshold:         │
│       Correct position with lerp    │
│                                      │
│ 4. Continue predicting from         │
│    corrected position               │
│                                      │
│ 5. Render predicted state           │
└─────────────────────────────────────┘
```

### Implementation

```cpp
class PlayerCharacter {
    Vec3 predicted_position_;
    Vec3 server_position_;
    Vec3 velocity_;

    float prediction_error_threshold_ = 0.5f;  // 50cm

public:
    void Tick(float dt) {
        // 1. Apply input immediately (no network latency)
        Vec3 input_dir = GetInputDirection();
        predicted_position_ += input_dir * speed * dt;

        // 2. Network update arrives (from server, 100-200ms old)
        if (HasNetworkUpdate()) {
            auto server_update = ReceiveNetworkUpdate();
            server_position_ = server_update.position;
            velocity_ = server_update.velocity;

            // 3. Check prediction error
            float error = (predicted_position_ - server_position_).Length();
            if (error > prediction_error_threshold_) {
                // Mismatch: server knows better, correct gradually
                predicted_position_ = Lerp(predicted_position_, server_position_, 0.1f);
            }
        }

        // 4. Continue predicting velocity for next frame
        velocity_ += input_dir * accel * dt;

        // 5. Render at predicted position (no perceived latency)
        renderer->DrawAt(predicted_position_);
    }
};
```

## Reconciliation

When prediction deviates significantly, smoothly correct:

```cpp
/// Reconciliation buffer: store past predictions for validation.
class PredictionBuffer {
    struct PredictionFrame {
        uint32_t frame_number;
        Vec3 predicted_position;
        Vec3 input_direction;
    };

    std::deque<PredictionFrame> predictions_;

public:
    void Record(uint32_t frame, const Vec3& pos, const Vec3& input) {
        predictions_.push_back({frame, pos, input});
        if (predictions_.size() > 60) predictions_.pop_front();  // 1 sec at 60 Hz
    }

    void Reconcile(uint32_t server_frame, const Vec3& server_position) {
        // Find the prediction at server_frame
        auto it = std::find_if(predictions_.begin(), predictions_.end(),
            [server_frame](const auto& pf) { return pf.frame_number == server_frame; });

        if (it == predictions_.end()) {
            // Server frame too old or not recorded
            current_position_ = server_position;
            return;
        }

        // Server corrects us
        float error = (it->predicted_position - server_position).Length();
        if (error > 0.1f) {
            // Significant mismatch: snap or lerp
            current_position_ = Lerp(it->predicted_position, server_position, 0.1f);
        }

        // Remove old predictions
        predictions_.erase(predictions_.begin(), it + 1);
    }
};
```

## Input Buffering

Server queues client input and processes oldest-first (prevents jitter):

```cpp
// Client-side: send input ~every 3 frames (uncapped rendering)
const uint32_t INPUT_SEND_RATE = 3;

void PlayerInput::Update() {
    if (frame_count % INPUT_SEND_RATE == 0) {
        SendInput(GetCurrentInputState());
    }
}

// Server-side: process queued input
std::map<uz::PlayerID, std::deque<InputFrame>> input_queues;

void ServerTick() {
    for (auto& [player_id, queue] : input_queues) {
        // Process oldest inputs first
        while (!queue.empty() && queue.front().frame <= current_server_frame) {
            auto input = queue.front();
            queue.pop_front();

            auto player = GetPlayer(player_id);
            player->character->ApplyInput(input.state);
        }
    }
}
```

## Interpolation

Clients interpolate between server snapshots for smooth motion:

```cpp
// Server sends position snapshots every 2-3 frames
// Client interpolates between them

class InterpolationBuffer {
    struct Snapshot {
        uint32_t frame;
        Vec3 position;
        float timestamp;
    };

    std::deque<Snapshot> snapshots_;

public:
    Vec3 GetInterpolatedPosition(float current_time) {
        if (snapshots_.size() < 2) {
            return snapshots_.empty() ? Vec3::Zero : snapshots_.back().position;
        }

        // Find surrounding snapshots
        auto next = std::find_if(snapshots_.begin(), snapshots_.end(),
            [current_time](const auto& s) { return s.timestamp >= current_time; });

        if (next == snapshots_.end()) {
            return snapshots_.back().position;  // Ahead of time
        }

        auto prev = (next == snapshots_.begin()) ? next : prev - 1;

        // Interpolate between prev and next
        float alpha = (current_time - prev->timestamp) / (next->timestamp - prev->timestamp);
        return Lerp(prev->position, next->position, alpha);
    }
};
```

## Packet Loss Recovery

If a network update is lost, prediction continues:

```
Frame 100: Server sends position A → Lost (packet dropped)
Frame 101: Client predicts (no correction)
Frame 102: Server sends position B → Client receives, corrects
           Mismatch from frames 100–101 absorbed by interpolation
Frame 103+: Normal operation
```

## Lag Compensation Best Practices

1. **Always validate server-side**: Don't trust client input for critical logic
2. **Rewind only for hit detection**: Don't use for movement validation (encourages teleporting)
3. **Limit rewind window**: 1 second max (500–1000 ms ping friendly, beyond is sketchy)
4. **Smoothly correct**: Use lerping, not snapping (jarring for player)
5. **Log mismatches**: Track prediction errors for balancing feedback
6. **Tolerance threshold**: Small errors (<10cm) are normal; don't correct every frame

## Performance Impact

Rewind buffer memory:

```
Per player: 60 frames × 3 transforms (pos + rot + vel) × 28 bytes = ~5 KB
64 players: 320 KB (negligible)
```

CPU cost: Raycast with rewind adds ~1ms per hitscan (negligible for most games).

## Example: Fair FPS Combat

Complete example (hitscan + client prediction):

```cpp
class HitscanWeapon {
    void RequestFire(Vec3 ray_origin, Vec3 ray_direction) {
        // Client-side: instant, no network delay
        renderer->PlayMuzzleFlash();
        PlayFireSound();

        // Send to server
        SendHitscanRequest(ray_origin, ray_direction);
    }

    void OnServerHitscanResult(bool hit, uz::EntityID target) {
        if (hit) {
            PlayHitEffect(target->GetPosition());
            target->TakeDamage(25);
        }
    }
};

// Server-side hitscan with rewind:
void WeaponComponent::RequestHitscan(const HitscanRequest& req) {
    float ping = GetClientPing(req.shooter_id);
    float rewind_time = current_server_time - ping;

    uz::RaycastHit hit;
    bool is_hit = false;
    uz::EntityID hit_entity;

    for (auto target : GetEnemies()) {
        // Rewind to when client fired
        auto historical_transform = target->rewind_buffer.GetTransformAt(rewind_time);

        if (PhysicsWorld::Raycast(req.ray_origin, req.ray_direction, hit, historical_transform)) {
            is_hit = true;
            hit_entity = target;
            break;
        }
    }

    // Notify client
    SendHitscanResult(is_hit, hit_entity);

    // Apply damage if hit
    if (is_hit) {
        auto target = world->Get(hit_entity);
        target->Get<HealthComponent>()->TakeDamage(25);
    }
}
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Quake III | **UzEngine** |
|---------|----------|---------|----------|--------------|
| Server rewind | Yes | No | Yes | **Yes** |
| Client prediction | Yes | Limited | Yes | **Yes** |
| Reconciliation | Yes (smooth) | None | Snap | **Smooth lerp** |
| Input buffering | Yes | No | Yes | **Yes** |
| Rewind window | 1 sec | N/A | 0.5 sec | **Configurable** |
| Max fair ping | ~200 ms | ~100 ms | ~200 ms | **~500 ms** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 6** | Rewind buffer, hitscan with rewind, client prediction |
| **Phase 7** | **Input buffering**, reconciliation, interpolation, loss recovery |
| **Phase 8** | Bandwidth optimization, lag simulation/debugging tools, per-game tuning |
