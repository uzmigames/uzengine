# Dedicated Server Architecture — Headless Mode, Tick Rate, Entity Replication

> **Depends on**: Networking system (§24), World/Entity system (§10), Physics (§20)
> **Transport**: ENet (UDP with optional reliability)
> **References**: Source Engine (fixed tick, authoritative server), Unreal 5 (replication), Godot (Network sync)

## Purpose

A dedicated server runs headless (no rendering) and is the source of truth for multiplayer games. Clients connect, send input, and receive authoritative state updates. The server ticks at a fixed rate (e.g., 60 Hz), processes all game logic deterministically, and replicates entity state to clients.

## Server Modes

### Dedicated Server

Separate executable, no rendering overhead. Ideal for PvP and large-scale games.

```cpp
namespace uz::Networking {
    /// Start dedicated server (blocking, runs forever).
    /// Server processes connections, ticks world, replicates state.
    void RunDedicatedServer(const ServerConfig& config);

    struct ServerConfig {
        uint16_t port = 7777;
        int max_players = 64;
        float tick_rate = 60.0f;  // server ticks per second
        float player_timeout = 30.0f;  // seconds before disconnect
        std::string world_path;  // Path to initial world/map
        std::function<void(uz::World*)> on_world_loaded;
    };
}

// Usage:
int main() {
    uz::Networking::ServerConfig cfg;
    cfg.port = 7777;
    cfg.max_players = 64;
    cfg.tick_rate = 60.0f;

    uz::Networking::RunDedicatedServer(cfg);
}
```

### Listen Server (Optional)

Server runs in-game (with rendering). Lower latency for the host but more CPU overhead.

```cpp
// Optional: embed server in game loop
uz::Networking::ServerTick(delta_time);  // Per frame
```

## Server Tick Loop

The server ticks at fixed interval, independent of frame rate:

```
┌─────────────────────────────────────┐
│ Server Tick (60 Hz = 16.7 ms)       │
│                                      │
│ 1. Receive network packets          │
│    ├── Input from connected clients │
│    └── Connection requests          │
│                                      │
│ 2. Process input                    │
│    ├── Apply movement input         │
│    ├── Validate input (cheats?)     │
│    └── Queue input for game logic   │
│                                      │
│ 3. Game logic (deterministic)       │
│    ├── Physics simulation           │
│    ├── AI behavior                  │
│    ├── Damage calculations          │
│    └── Game rules (win/loss)        │
│                                      │
│ 4. Collect changes                  │
│    ├── Track entity state changes   │
│    ├── Compute property deltas      │
│    └── Determine relevancy          │
│                                      │
│ 5. Replicate state                  │
│    ├── Serialize changed properties │
│    ├── Filter by relevancy          │
│    ├── Pack into network packets    │
│    └── Send to clients              │
│                                      │
│ 6. Sleep until next tick            │
└─────────────────────────────────────┘
```

Implementation:

```cpp
void ServerTick(float dt) {
    const float TICK_RATE = 1.0f / 60.0f;  // 16.7 ms per tick

    while (server_running) {
        auto tick_start = Clock::Now();

        // 1. Receive packets
        ReceiveNetworkPackets();

        // 2. Process input
        for (auto& player : players) {
            auto input = input_queue[player.id].front();
            player.character->ProcessInput(input);
        }

        // 3. Update world
        world->Tick(TICK_RATE);

        // 4. Physics
        physics_world->Simulate(TICK_RATE);

        // 5. Replicate to clients
        ReplicateWorldState();

        // 6. Sleep
        auto elapsed = Clock::Now() - tick_start;
        if (elapsed < TICK_RATE) {
            Sleep(TICK_RATE - elapsed);
        }
    }
}
```

## Player Connection Lifecycle

### Connection States

```cpp
namespace uz::Networking {
    enum class PlayerState {
        Connecting,    // Handshake in progress
        Loading,       // World streaming to client
        Playing,       // In-game, replicating state
        Disconnecting  // Graceful shutdown
    };

    struct PlayerConnection {
        uz::PlayerID player_id;
        uz::NetworkAddress address;
        PlayerState state;
        float ping_ms = 0.0f;
        uint32_t frame_count = 0;  // Server frames received from this player

        // Input history (for server-side prediction)
        std::deque<uz::InputFrame> input_history;

        // Replication state
        std::set<uz::EntityID> replicated_entities;  // Entities sent to this client
    };
}
```

### Connection Handshake

```
┌─────────────────────────────────────┐
│ Client → Server                     │
│ ConnectionRequest                   │
│   - Player name                     │
│   - Game version                    │
│   - Auth token                      │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│ Server validates:                   │
│ ✓ Version match?                    │
│ ✓ Slots available?                  │
│ ✓ Auth token valid?                 │
└─────────────────────────────────────┘
         │
         ▼ (if valid)
┌─────────────────────────────────────┐
│ Server → Client                     │
│ ConnectionAccepted                  │
│   - Player ID                       │
│   - World snapshot                  │
│   - Initial entity state            │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│ Client loads world, spawns entities │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│ Client → Server                     │
│ ReadyAck                            │
│   - Loading complete                │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│ Game starts: property replication   │
│ input processing, etc.              │
└─────────────────────────────────────┘
```

## Deterministic Simulation

Server simulation must be deterministic — same inputs produce same outputs. This enables server-side rollback/rewind for lag compensation.

### Determinism Rules

```cpp
// ✅ DETERMINISTIC (safe)
int damage = attack_power + buff_bonus;
position += velocity * delta_time;

// ❌ NON-DETERMINISTIC (dangerous)
float damage = attack_power * rand();  // Random!
position += velocity * std::chrono::system_clock::now().count();  // Time-dependent!

// ❌ FLOATING-POINT HAZARD
// Different CPU architectures/compilers may produce slightly different results
// Use fixed-point arithmetic for critical values
int32_t fixed_damage = (attack_power << 16) + (buff << 16);
```

### Seeded Random Number Generator

For reproducible random numbers across clients and server:

```cpp
namespace uz::Networking {
    /// Seeded RNG for deterministic randomness.
    /// Seed from server frame number ensures consistency.
    class DeterministicRNG {
        uint32_t seed_;

    public:
        DeterministicRNG(uint32_t server_frame) : seed_(server_frame) {}

        float Float() {
            // Linear congruential generator
            seed_ = (seed_ * 1664525 + 1013904223) % (1u << 31);
            return seed_ / 2147483648.0f;
        }
    };
}

// Usage: in server simulation
uz::Networking::DeterministicRNG rng(current_server_frame);
float crit_roll = rng.Float();
if (crit_roll < crit_chance) {
    ApplyCriticalDamage();
}
```

## Input Processing

Server receives input from clients and queues it for processing.

### Input Frame

```cpp
namespace uz::Networking {
    struct InputFrame {
        uz::PlayerID player_id;
        uint32_t frame_number;  // Client frame, for ACK
        uz::InputState input;   // Buttons, axes, aim direction
        float client_timestamp; // For RTT calculation
    };
}
```

### Input Queue

Per-player input is queued and processed in order:

```cpp
// Server processing
std::map<uz::PlayerID, std::deque<uz::InputFrame>> input_queues;

for (auto& [player_id, queue] : input_queues) {
    while (!queue.empty() && queue.front().frame_number <= server_frame) {
        auto input = queue.front();
        queue.pop_front();

        auto player = GetPlayer(player_id);
        player->character->ProcessInput(input.input);

        // Log for potential rollback
        player->input_history.push_back(input);
    }
}
```

## Player Spawning

When a player connects:

```cpp
namespace uz::Networking {
    /// Called when player completes connection handshake.
    /// Server creates player character, notifies other clients.
    class PlayerController {
    public:
        uz::EntityID SpawnPlayerCharacter(
            uz::PlayerID player_id,
            const uz::Transform& spawn_point
        );
    };
}

// Implementation
uz::EntityID SpawnPlayerCharacter(uz::PlayerID player_id, const uz::Transform& spawn_point) {
    // 1. Create character entity
    auto entity = world->CreateEntity();
    entity->AddComponent<CharacterComponent>();
    entity->GetComponent<TransformComponent>()->transform = spawn_point;

    // 2. Bind to player
    entity->SetOwner(player_id);

    // 3. Mark replicated
    entity->SetReplicated(true);

    // 4. Send to all clients (via replication)
    // ReplicateWorldState() will include this new entity

    return entity;
}
```

## World Streaming

When a client connects, the server must send the current world state. For large worlds, this might be streamed in chunks.

### World Snapshot

```cpp
namespace uz::Networking {
    /// Serialized world state sent to newly connected client.
    struct WorldSnapshot {
        std::vector<EntityData> entities;  // All entities in relevancy range
        std::vector<uint8_t> serialized;   // Packed binary data
    };

    /// Create snapshot of current world for a player.
    WorldSnapshot CreateWorldSnapshot(uz::PlayerID player_id);
}

// Includes:
// - All static geometry (buildings, terrain)
// - All dynamic entities (other players, NPCs, moving objects)
// - Current game state (time of day, weather, etc.)
```

## Shutdown and Save

When shutting down or periodically:

```cpp
void ServerShutdown() {
    // 1. Notify all connected players
    for (auto& conn : players) {
        SendDisconnect(conn, DisconnectReason::ServerShutdown);
    }

    // 2. Wait for ACKs (graceful disconnect)
    // ... handle timeouts ...

    // 3. Save world state
    if (auto_save_enabled) {
        world->Save("saves/world_autosave.uzscene");
    }

    // 4. Shutdown
    server_running = false;
}
```

## Scalability

Maximum concurrent players depends on:

- **CPU**: Each player's input processing + physics
- **Network bandwidth**: ~10 Mbps per 64 players (entity replication + RPC)
- **Game complexity**: Large worlds with many entities = more replication overhead

UzEngine targets **~64 players per server**, typical for PvP games. For larger worlds, use multiple server instances or streaming.

## Configuration

```toml
# server.uzproj
[server]
headless = true
max_players = 64
tick_rate = 60.0

[networking]
port = 7777
max_bandwidth = 10_000_000  # bits/sec

[world]
initial_map = "maps/arena.uzscene"
auto_save_interval = 300  # 5 minutes
```

## Performance Monitoring

```cpp
namespace uz::Networking {
    struct ServerStats {
        uint32_t total_ticks = 0;
        float avg_tick_ms = 0.0f;
        float max_tick_ms = 0.0f;

        uint32_t total_packets_sent = 0;
        uint32_t total_packets_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;

        float bandwidth_usage_pct = 0.0f;  // vs max_bandwidth
    };

    ServerStats GetServerStats();
}
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Source Engine | **UzEngine** |
|---------|----------|---------|---------------|--------------|
| Dedicated server | Yes | Limited | Yes | **Yes** |
| Headless mode | Yes | No | Yes | **Yes** |
| Fixed tick rate | 30 Hz typical | Variable | 66 Hz typical | **Configurable (60 Hz)** |
| Entity replication | Yes (Replication Graph) | Limited | Yes | **Per-player relevancy** |
| Deterministic simulation | Yes | No | Yes | **Yes (seeded RNG)** |
| Max players | 100+ | ~16 | 32–64 | **~64 per instance** |
| Input buffering | Yes | No | Yes | **Yes** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 4** | Dedicated server loop, fixed tick, player connection, world streaming |
| **Phase 5** | Input processing, deterministic simulation, replication (property + RPC) |
| **Phase 6** | Server-side prediction/rollback, interest management, lag compensation |
