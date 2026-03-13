# Networking — Authoritative Server, Client-Side Prediction, Replication

> **Status**: Planned (Phase 4)
> **Source**: `engine/networking/`
> **Transport**: ENet (UDP with reliability layer, congestion-aware)
> **References**: UE5 Replication Graph + Property Replication, Godot MultiplayerSynchronizer, Source Engine (lag compensation, interest management)

## Purpose

Multiplayer infrastructure for dedicated server and client-server games. Authoritative server maintains game state; clients predict movement and await server confirmation. Supports up to ~64 players per server instance (designed for PvP and PvE). Property replication automatically synchronizes entity state with interest management (players only receive data for nearby entities).

## Architecture

```
┌──────────────────────────────┐
│ Dedicated Server             │
│ (Authoritative, no rendering)│
│  ├── World (all entities)    │
│  ├── Physics simulation      │
│  ├── Player input processing │
│  ├── Interest management     │
│  └── Property replication    │
└─────────┬──────────────┬──────────────┐
          │              │              │
   ┌──────▼──┐    ┌──────▼──┐    ┌──────▼──┐
   │ Client 1 │    │ Client 2 │    │ Client N │
   │ Predicts │    │ Predicts │    │ Predicts │
   │ Movement │    │ Movement │    │ Movement │
   │ Renders  │    │ Renders  │    │ Renders  │
   │ Waits for│    │ Waits for│    │ Waits for│
   │ confirm  │    │ confirm  │    │ confirm  │
   └─────┬────┘    └─────┬────┘    └─────┬────┘
         │              │              │
         └──────────────┴──────────────┘
              Input → Server
              State → Clients
```

## Dedicated Server

Headless, authoritative server with no rendering overhead. Runs as a separate process.

### Server Configuration

```toml
# server.uzproj
[server]
headless = true
max_players = 64
tick_rate = 60           # server ticks per second (fixed)
player_timeout = 30      # seconds before disconnect
save_state = true        # periodically save game state

[networking]
port = 7777
max_bandwidth = 10_000_000  # bits per second (10 Mbps)
max_packet_size = 1200       # bytes (avoid fragmentation)
```

### Server API

```cpp
namespace uz::Networking {
    /// Run dedicated server loop (blocks until shutdown).
    /// Handles accept, input processing, physics, state validation, replication.
    void RunDedicatedServer(const ServerConfig& config);

    /// Per-frame server update (if embedding server in main loop).
    void ServerTick(float dt);

    /// Callback: player connected and loaded into world.
    using OnPlayerConnectFn = std::function<void(PlayerConnection*)>;
    void OnPlayerConnect(OnPlayerConnectFn fn);

    /// Callback: player disconnected or timed out.
    using OnPlayerDisconnectFn = std::function<void(PlayerID player_id, DisconnectReason reason)>;
    void OnPlayerDisconnect(OnPlayerDisconnectFn fn);
}

// Usage:
uz::Networking::OnPlayerConnect([](uz::PlayerConnection* conn) {
    UZ_LOG(Networking, Info, "Player {} connected from {}", conn->player_id, conn->address);
    // Spawn player character, send initial world state
});
```

## Client-Side Prediction and Server Reconciliation

Clients predict their own movement immediately (low latency feel); server processes input authoritative, sends corrections.

### Prediction Loop

**Client-side**:
```
Frame N:
  ├── Poll input (WASD, mouse aim)
  ├── Apply input locally → predict movement
  ├── Render predicted state
  │
Frame N+1:
  ├── Network receive: Server state for Frame N-2 (delayed)
  ├── Compare: predicted vs. authoritative
  ├── If mismatch > threshold: snap or interpolate to server state
  ├── Continue predicting from corrected position
```

### Prediction Buffer

```cpp
namespace uz::Networking {
    /// One frame of prediction state (position, rotation, velocity).
    struct PredictionFrame {
        uint32_t frame_number;
        Vec3 position;
        Quat rotation;
        Vec3 velocity;
    };

    /// Track predicted states for reconciliation.
    class PredictionBuffer {
    public:
        /// Store a prediction for later validation.
        void Record(uint32_t frame, const Transform& transform);

        /// Server confirms a frame was correct (no correction needed).
        void Validate(uint32_t frame);

        /// Server sends correction for a frame. Rewind to that frame,
        /// replay inputs since then with corrected starting state.
        void Reconcile(uint32_t frame, const Transform& server_transform);

        /// Get last recorded prediction for rendering.
        const PredictionFrame& GetLatest() const;
    };
}

// Usage: player movement component
class PlayerMovement : public Component {
    uz::Networking::PredictionBuffer prediction_buffer;

    void OnTick(float dt) override {
        // Predict movement
        Vec3 input_dir = GetInputDirection();
        position_ += input_dir * speed_ * dt;

        // Record for later validation
        prediction_buffer.Record(frame_count_, {position_, rotation_});
    }

    void OnNetworkUpdate(const PlayerState& server_state) {
        // Server corrects us
        if (server_state.frame > last_validated_frame_) {
            prediction_buffer.Reconcile(server_state.frame, server_state.transform);
            last_validated_frame_ = server_state.frame;
        }
    }
};
```

## Property Replication System

Automatic network synchronization of component properties. Only changed properties are sent.

### Replicated Properties

```cpp
class CharacterComponent : public uz::Component {
    UZ_COMPONENT(CharacterComponent);

public:
    /// Mark this property as replicated from server to clients.
    /// Only changes are sent (delta compression).
    UZ_REPLICATED_PROPERTY(Vec3, position, ReplicationType::ServerToClient);

    /// Rotation replicated with compression (packed Quat, 16 bits per component).
    UZ_REPLICATED_PROPERTY_COMPRESSED(
        Quat, rotation,
        ReplicationType::ServerToClient,
        CompressionType::PackedQuat16
    );

    /// Health replicated, only on relevancy (see Interest Management below).
    UZ_REPLICATED_PROPERTY(
        int, health,
        ReplicationType::ServerToClient,
        RelevancyCondition::RoleRelevant  // only replicate to relevant clients
    );

    /// Fired on all clients when server sets this property.
    UZ_REPLICATED_CALL(Teleport, Vec3 new_position, ReplicationType::ClientToServer);
};
```

### Serialization

Properties are automatically serialized to/from network packets using a binary format.

```cpp
namespace uz::Networking {
    /// Per-frame: collect changed properties from all entities.
    /// Filter by relevancy (interest management).
    /// Combine into delta-encoded packets.
    struct ReplicationUpdate {
        EntityID entity;
        uint32_t property_mask;     // bitmask of changed properties
        std::vector<uint8_t> data;  // serialized property values
    };

    /// Incoming: deserialize properties, apply to entities.
    void ApplyReplicationUpdates(const std::vector<ReplicationUpdate>& updates);
}
```

## RPC System — Remote Procedure Calls

Server or client invokes a function on the remote machine.

### RPC Declaration

```cpp
class WeaponComponent : public uz::Component {
    UZ_COMPONENT(WeaponComponent);

public:
    /// Fire weapon. Called on client, executed on server.
    /// Server validates (ammo, cooldown), fires on all clients.
    UZ_RPC(Fire,
        Quat aim_rotation,
        RPCTarget::Server,
        RPCReliability::Reliable
    );

    /// Hit notification. Server calls this on client that hit.
    /// One-way, reliable (message guaranteed to arrive).
    UZ_RPC(OnHit,
        EntityID hit_entity,
        Vec3 impact_position,
        RPCTarget::Client,
        RPCReliability::Reliable
    );

    /// Sound effect. Multicast: called on all clients (authority replicates to others).
    UZ_RPC(PlayFireSound,
        RPCTarget::Multicast,
        RPCReliability::Unreliable  // sound miss is acceptable
    );

private:
    void Fire_Impl(Quat aim_rotation) {
        // Server-side logic: validation, damage, recoil
        int ammo = GetAmmo();
        if (ammo <= 0) return;

        DecrementAmmo();
        ApplyRecoil(aim_rotation);

        // Notify all clients
        OnHit(target_entity, hit_position);
        PlayFireSound();
    }
};
```

### RPC Usage

```cpp
// Client code: player presses fire button
void PlayerInput::Fire() {
    auto weapon = entity->Get<WeaponComponent>();
    weapon->Fire(aim_rotation);  // Sends to server
}

// Server code: receives Fire RPC
void WeaponComponent::Fire_Impl(Quat aim_rotation) {
    // Validate, apply game logic
    // RPCs to clients (OnHit, PlayFireSound) are sent automatically
}
```

## Network Relevancy and Interest Management

Not all entities need to be replicated to all clients. Interest management sends entity state only to nearby players (relevancy-based).

### Relevancy Distance

```cpp
class CharacterComponent : public uz::Component {
    UZ_COMPONENT(CharacterComponent);

    /// Clients within this radius receive updates for this entity.
    UZ_RELEVANCY_DISTANCE(5000.0f);  // 5000 units (meters in typical games)
};
```

### Interest Management Algorithm

```
Per client, per frame:
  For each entity in world:
    ├── Distance from client's player to entity
    ├── Is distance < entity's relevancy distance?
    │
    ├── If YES (in scope):
    │   ├── Send property updates (delta-encoded)
    │   └── Replicate new properties if first frame in scope
    │
    └── If NO (out of scope):
        └── (Don't send data, optionally notify client to "forget" entity)
```

```cpp
namespace uz::Networking {
    /// Built-in: spherical distance-based relevancy.
    /// Override for custom relevancy (e.g., line-of-sight, AI interest).
    class RelevancyChecker {
    public:
        virtual bool IsRelevant(
            Entity entity,
            Vec3 entity_position,
            PlayerConnection* client,
            Vec3 client_position
        ) const;
    };

    /// Set custom relevancy checker.
    void SetRelevancyChecker(std::unique_ptr<RelevancyChecker> checker);
}

// Usage: custom relevancy (only replicate if in line-of-sight)
class LineOfSightRelevancy : public uz::Networking::RelevancyChecker {
    bool IsRelevant(Entity ent, Vec3 ent_pos, uz::PlayerConnection* client, Vec3 client_pos) const override {
        // Check distance (broad)
        if ((ent_pos - client_pos).LengthSquared() > 5000 * 5000) return false;

        // Check line-of-sight (narrow)
        uz::RaycastHit hit;
        if (physics_world->Raycast(client_pos, (ent_pos - client_pos), hit)) {
            return hit.entity == ent;  // LOS unobstructed
        }
        return false;
    }
};
```

## Lag Compensation — Server-Side Rewind

Server maintains a history of player positions for each tick. On hit detection, server rewinds to the client's tick time (accounting for ping), then checks if the attack hit.

### Rewind Buffer

```cpp
namespace uz::Networking {
    /// Per-player position history (position, rotation, time).
    class RewindBuffer {
    public:
        /// Store current transform (called once per server tick).
        void Record(const Transform& transform);

        /// Get transform at a historical time (past ticks).
        /// Time = server_time - client_ping - network_jitter.
        Transform GetTransformAt(float historical_time) const;

        /// Clear old entries outside rewind window.
        void Cleanup(float current_time);

    private:
        std::vector<std::pair<float, Transform>> history_;
        float rewind_window_ = 1.0f;  // 1 second of history
    };
}

// Usage: server-side hitscan weapon
void WeaponComponent::Fire_ServerSide(const RaycastRequest& request) {
    // Get client's ping (RTT / 2)
    float client_ping = GetClientPing();

    // Find historical position of target
    float rewind_time = server_time_ - client_ping;

    auto& target_player = GetPlayer(request.target_id);
    Transform historical_pos = target_player.rewind_buffer.GetTransformAt(rewind_time);

    // Check if hitscan ray hits the rewound position
    uz::RaycastHit hit;
    if (physics_world_->Raycast(
        request.ray_origin,
        request.ray_direction,
        hit,
        historical_pos.position  // use rewound position
    )) {
        // Direct hit! Notify all clients.
        ApplyDamage(hit.entity, request.damage);
    }
}
```

## Network Tick Rate and Frame Synchronization

Server updates at fixed tick rate (e.g., 60 Hz). Clients render at variable frame rate, interpolating between server ticks.

### Fixed Tick Server, Variable Frame Client

```cpp
// Server: fixed 60 Hz
const float server_tick_rate = 1.0f / 60.0f;  // 16.7 ms

while (running) {
    frame_time = Clock::Now();
    ServerTick(server_tick_rate);  // Always 16.7 ms

    // Sleep if frame finished early
    sleep_time = server_tick_rate - (Clock::Now() - frame_time);
    if (sleep_time > 0) Sleep(sleep_time);
}

// Client: variable, capped at 144 FPS
while (running) {
    frame_time = Clock::Now();
    ClientTick(delta_time);      // Variable delta (6–16 ms)

    // Receive server updates (may arrive during frame)
    ReceiveNetworkPackets();

    Render();
    vsync_frame_time = 1.0f / 144.0f;  // 6.9 ms
}
```

## Transport Layer — ENet

ENet provides UDP with optional reliability per packet.

### ENet Configuration

```cpp
namespace uz::Networking {
    struct TransportConfig {
        uint16_t port;
        int max_peers;           // max connections
        int send_bandwidth;      // bits/sec (0 = unlimited)
        int recv_bandwidth;      // bits/sec (0 = unlimited)
    };

    void InitTransport(const TransportConfig& config);
}
```

### Packet Types

```
Reliable (important data):
  ├── Property replication (position, health)
  ├── RPC calls (damage, death, spawn)
  └── Acknowledgments (server confirms client input)

Unreliable (frequently updated, loss acceptable):
  ├── Input samples (if server sampling client input)
  ├── Sound effects
  ├── Particle effects
  └── Non-critical state (other player animations)
```

## Connection Lifecycle

### Connect Phase

```
1. Client sends ConnectionRequest (player name, version, token)
   ↓
2. Server validates (game version match, slots available, auth token valid)
   ↓
3. Server sends ConnectionAccepted (player ID, world state snapshot)
   ↓
4. Client receives: loads world, spawns remote players
   ↓
5. Client acknowledges ready (ACK)
   ↓
6. Game starts: property replication begins
```

### Disconnect Phase

```
1. Client sends DisconnectRequest
   ↓
2. Server sends DisconnectConfirm
   ↓
3. Server destroys player entity, notifies other players
   ↓
4. Connection closed
```

### Timeout

If no packet received for `player_timeout` seconds, server forcibly disconnects.

## Lag and Jitter Mitigation

### Input Buffering

Server queues player inputs, processes at fixed tick rate.

```cpp
// Client: send input ~every 3 frames (uncap rendering)
if (frame_count % 3 == 0) {
    SendInput(input_state);
}

// Server: process input oldest-first
while (!input_queue.empty() && input_queue.front().frame <= current_frame) {
    ProcessInput(input_queue.front());
    input_queue.pop();
}
```

### Interpolation

Client interpolates between server updates (smooth motion despite discrete packets).

```cpp
// Server sends position at tick T: pos_T
// Server sends position at tick T+1: pos_T1
// Client renders time t where T < t < T+1
// Interpolated position = lerp(pos_T, pos_T1, (t - T) / (T+1 - T))
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Netcode 1 | **UzEngine** |
|---------|----------|---------|----------|--------------|
| Architecture | Dedicated server | P2P / listen server | Dedicated | **Dedicated server** |
| Prediction | Client-side with correction | None built-in | Full prediction | **Client-side prediction + reconciliation** |
| Replication | `UPROPERTY(Replicated)` properties + `RPC` | None built-in | Manual sync | **`UZ_REPLICATED_PROPERTY` + `UZ_RPC`** |
| Interest management | Replication Graph (distance/volume) | None | Manual | **Distance-based + custom checks** |
| Lag compensation | Server-side rewind | None | None | **Rewind buffer (hit detection)** |
| Transport | Custom (TCP/UDP abstraction) | ENet, WebSocket | Custom | **ENet (UDP + reliability)** |
| Max players | 100+ (scales with replication) | Depends on sync | ~32 | **~64 (per-server instance)** |
| Tick rate | ~30 Hz typical | Varies | Custom | **Configurable (60 Hz recommended)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 4** | Server API, basic replication (position/rotation), input processing, relevancy distance |
| **Phase 5** | Property replication system, RPC framework, client-side prediction |
| **Phase 6** | Lag compensation (server rewind), interest management (custom checks), interpolation |
| **Phase 7** | Advanced: streaming levels over network, dynamic entity spawn/destroy, state checkpoints |
