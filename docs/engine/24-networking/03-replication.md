# Property Replication — Delta Compression, Relevancy, Conditions

> **Depends on**: Networking system (§24), Server architecture (§24.2), Components (§10)
> **Used by**: Multiplayer synchronization, client state updates
> **Standards**: Delta-compressed binary serialization

## Purpose

Property replication automatically synchronizes component properties from server to clients. Only changed properties are sent (delta compression), reducing bandwidth. Relevancy filtering ensures clients only receive data for nearby/visible entities.

## Marking Properties as Replicated

```cpp
namespace uz::Networking {
    /// Direction of replication.
    enum class ReplicationType {
        ServerToClient,     // Server → All clients
        ClientToServer,     // Client → Server only
        Bidirectional       // Both directions
    };

    /// Conditions for replication.
    enum class ReplicateCondition {
        Always,           // Replicate every frame
        OwnerOnly,        // Only to owner (player's own character)
        RelevantOnly,     // Only to relevant clients (nearby players)
        OwnerOrRelevant   // Owner + relevant clients
    };

    /// Compression type for bandwidth reduction.
    enum class CompressionType {
        None,             // Full precision
        PackedQuat16,     // Quaternion: 16 bits per component
        PackedVec3Half,   // Vec3: 16-bit floats
        HalfAngle         // Rotation: 16-bit angle
    };
}

// Macro to declare replicated property
#define UZ_REPLICATED_PROPERTY(Type, Name, Replication, Condition, Compression)

// Usage:
class CharacterComponent : public uz::Component {
    UZ_COMPONENT(CharacterComponent);

public:
    /// Position replicated to all clients (full precision).
    UZ_REPLICATED_PROPERTY(
        Vec3, position,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::RelevantOnly
    );

    /// Rotation replicated with 16-bit quaternion compression (saves bandwidth).
    UZ_REPLICATED_PROPERTY(
        Quat, rotation,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::RelevantOnly,
        uz::Networking::CompressionType::PackedQuat16
    );

    /// Health replicated to owner only (player sees own health bar).
    UZ_REPLICATED_PROPERTY(
        int, health,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::OwnerOnly
    );

    /// Ammo replicated to owner.
    UZ_REPLICATED_PROPERTY(
        int, ammo,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::OwnerOnly
    );

    /// Animation state replicated to relevant clients (for animation blending).
    UZ_REPLICATED_PROPERTY(
        AnimState, anim_state,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::RelevantOnly
    );
};
```

## How Properties Are Tracked

Behind the scenes, the replication system tracks property changes:

```cpp
// Pseudocode for macro implementation
class CharacterComponent : public uz::Component {
private:
    Vec3 position_;
    Quat rotation_;
    int health_;

    // Replication tracking
    uint32_t property_dirty_mask_ = 0;
    enum PropertyBit : uint32_t {
        Position = (1 << 0),
        Rotation = (1 << 1),
        Health = (1 << 2),
        // ...
    };

public:
    // Property setter with automatic dirty flag
    void SetPosition(const Vec3& new_pos) {
        if (position_ != new_pos) {
            position_ = new_pos;
            property_dirty_mask_ |= PropertyBit::Position;
        }
    }

    Vec3 GetPosition() const { return position_; }
};
```

## Serialization — Binary Format

Properties are packed into binary for transmission:

```
┌──────────────────────────────┐
│ Replication Update Packet    │
├──────────────────────────────┤
│ Entity ID (4 bytes)          │ Identify which entity changed
├──────────────────────────────┤
│ Property mask (4 bytes)      │ Bitmask of changed properties
├──────────────────────────────┤
│ Property data (variable)     │ Only changed properties
│  ├─ Position [12 bytes]      │
│  ├─ Rotation [4 bytes]       │ Compressed to 16 bits each
│  └─ Health [4 bytes]         │
└──────────────────────────────┘
```

Implementation:

```cpp
namespace uz::Networking {
    /// Serialize changed properties from an entity.
    struct ReplicationUpdate {
        uz::EntityID entity;
        uint32_t property_mask;        // Bitmask of changed properties
        std::vector<uint8_t> data;     // Serialized property values
        uz::PlayerID target_player;    // Which client receives this
    };

    /// Collect all property changes this frame.
    std::vector<ReplicationUpdate> CollectReplicationUpdates(
        uz::World* world,
        uint32_t server_frame
    );
}
```

## Delta Compression

Only send changed properties, skip unchanged ones:

```
Frame N:
  ├─ Position changed: send
  ├─ Rotation unchanged: skip
  └─ Health unchanged: skip
  → Packet size: 4 (position) + 4 (header) = 8 bytes

Frame N+1:
  ├─ Position unchanged: skip
  ├─ Rotation changed: send
  └─ Health changed: send
  → Packet size: 4 (rotation) + 4 (health) + 4 (header) = 12 bytes

Frame N+2:
  ├─ Position unchanged: skip
  ├─ Rotation unchanged: skip
  └─ Health unchanged: skip
  → No update sent (entity is "quiet")
```

## Relevancy Filtering

Clients only receive data for nearby entities (interest management).

```cpp
namespace uz::Networking {
    /// Check if an entity is relevant for a client.
    class RelevancyChecker {
    public:
        virtual ~RelevancyChecker() = default;

        /// Is this entity relevant to this client?
        virtual bool IsRelevant(
            uz::Entity entity,
            uz::PlayerConnection* client
        ) const = 0;
    };

    /// Default implementation: distance-based.
    class DistanceRelevancyChecker : public RelevancyChecker {
    public:
        explicit DistanceRelevancyChecker(float distance = 5000.0f)
            : relevancy_distance_(distance) {}

        bool IsRelevant(uz::Entity entity, uz::PlayerConnection* client) const override {
            auto client_pos = GetPlayerPosition(client);
            auto entity_pos = entity.GetComponent<TransformComponent>()->position;
            float dist = (entity_pos - client_pos).Length();
            return dist < relevancy_distance_;
        }

    private:
        float relevancy_distance_;
    };

    /// Set custom relevancy checker.
    void SetRelevancyChecker(std::unique_ptr<RelevancyChecker> checker);
}

// Usage: only replicate enemies within 50 meters
class EnemyRelevancy : public uz::Networking::RelevancyChecker {
    bool IsRelevant(uz::Entity entity, uz::PlayerConnection* client) const override {
        // Always relevant: same team
        if (entity.GetComponent<TeamComponent>()->team == GetPlayerTeam(client)) {
            return true;
        }

        // Enemies: only if within 50m
        auto dist = (GetEntityPos(entity) - GetPlayerPos(client)).Length();
        return dist < 50.0f;
    }
};
```

## Bandwidth Optimization

### Compression Types

Different data types can be compressed:

```cpp
// Full precision (12 bytes for Vec3)
UZ_REPLICATED_PROPERTY(Vec3, position, ...);
// Sends: float32 x, float32 y, float32 z = 12 bytes per update

// Half precision (6 bytes for Vec3)
UZ_REPLICATED_PROPERTY(
    Vec3, position,
    ...,
    uz::Networking::CompressionType::PackedVec3Half
);
// Sends: float16 x, float16 y, float16 z = 6 bytes per update

// Quaternion compression (4 bytes instead of 16)
UZ_REPLICATED_PROPERTY(
    Quat, rotation,
    ...,
    uz::Networking::CompressionType::PackedQuat16
);
// Sends: 16-bit compressed quaternion = 4 bytes per update
```

### Bandwidth Calculation

For a 64-player server with 100 entities per player:

```
Naive (send all properties):
  64 players × 100 entities × 20 bytes per property × 60 updates/sec
  = 7.68 million bytes/sec = 61.4 Mbps (unacceptable!)

With delta compression (only changed):
  64 players × 100 entities × (10% changed) × 20 bytes × 60 updates/sec
  = 769 million bytes/sec = 6.14 Mbps (acceptable)

With delta + relevancy (only nearby entities):
  64 players × 20 entities (relevancy distance) × (10% changed) × 20 bytes × 60 updates/sec
  = 1.536 million bytes/sec = 0.3 Mbps (very acceptable)

With delta + relevancy + compression (packed quaternions):
  64 players × 20 entities × (10% changed) × 8 bytes (pos + rot) × 60 updates/sec
  = 614 kbps (comfortable margin for other traffic)
```

## Replication Conditions

### Always

Sent every frame if changed:

```cpp
UZ_REPLICATED_PROPERTY(
    Vec3, position,
    uz::Networking::ReplicationType::ServerToClient,
    uz::Networking::ReplicateCondition::Always
);
```

### OwnerOnly

Only the owning player receives updates (e.g., their own health):

```cpp
UZ_REPLICATED_PROPERTY(
    int, health,
    uz::Networking::ReplicationType::ServerToClient,
    uz::Networking::ReplicateCondition::OwnerOnly
);
// Client only receives their own health, not other players'
```

### RelevantOnly

Only clients within relevancy distance receive updates:

```cpp
UZ_REPLICATED_PROPERTY(
    Vec3, position,
    uz::Networking::ReplicationType::ServerToClient,
    uz::Networking::ReplicateCondition::RelevantOnly
);
// Each client only receives positions of nearby players
```

### OwnerOrRelevant

Owner always receives; others only if relevant:

```cpp
UZ_REPLICATED_PROPERTY(
    AnimState, anim_state,
    uz::Networking::ReplicationType::ServerToClient,
    uz::Networking::ReplicateCondition::OwnerOrRelevant
);
// Your own animation always visible; others' animations only if nearby
```

## Entity Spawning and Despawning

When an entity becomes relevant or goes out of scope:

```
Entity spawned at server:
  ├─ Add to world
  └─ Mark replicated: true

Entity enters relevancy range:
  ├─ Send EntitySpawn packet to client (full state snapshot)
  └─ Client creates entity locally

Entity leaves relevancy range:
  ├─ Send EntityDestroy packet to client
  └─ Client removes entity

Entity destroyed at server:
  ├─ Send EntityDestroy to all relevant clients
  └─ Remove from world
```

## Replication Statistics

```cpp
struct ReplicationStats {
    uint32_t total_updates_sent = 0;
    uint32_t total_updates_received = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;

    // Per-frame average
    float avg_updates_per_frame = 0.0f;
    float avg_bytes_per_frame = 0.0f;

    // Compression ratio
    float compression_ratio = 0.0f;  // bytes_sent / (uncompressed_size)
};
```

## Example: Player Character

Complete example with multiple replicated properties:

```cpp
class PlayerCharacter : public uz::Entity {
public:
    /// Position replicated to all clients within 100m.
    UZ_REPLICATED_PROPERTY(
        Vec3, position,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::RelevantOnly
    );

    /// Rotation compressed (4 bytes instead of 16).
    UZ_REPLICATED_PROPERTY(
        Quat, rotation,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::RelevantOnly,
        uz::Networking::CompressionType::PackedQuat16
    );

    /// Health only replicated to owner (privacy).
    UZ_REPLICATED_PROPERTY(
        int, health,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::OwnerOnly
    );

    /// Animation state replicated to nearby players.
    UZ_REPLICATED_PROPERTY(
        uint32_t, anim_state_hash,
        uz::Networking::ReplicationType::ServerToClient,
        uz::Networking::ReplicateCondition::RelevantOnly
    );

    /// Server processes movement.
    void ProcessInput(const uz::InputState& input) {
        // Server authorizes movement
        position += input.move_direction * speed * dt;
        SetPosition(position);  // Marks dirty
    }
};
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Netcode 1 | **UzEngine** |
|---------|----------|---------|----------|--------------|
| Property replication | `UPROPERTY(Replicated)` | None | Manual | **`UZ_REPLICATED_PROPERTY`** |
| Delta compression | Yes (automatic) | None | Manual | **Yes (automatic)** |
| Relevancy filtering | Replication Graph | None | Manual | **Distance + custom checks** |
| Compression types | Via `RepNotify` | N/A | Manual | **PackedQuat16, PackedVec3Half** |
| Bandwidth efficiency | ~0.3 Mbps/64p | High (P2P) | Varies | **~0.3 Mbps/64p** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 5** | Property replication macros, delta compression, basic serialization |
| **Phase 6** | **Relevancy filtering** (distance + custom), compression types, owned properties |
| **Phase 7** | Advanced: bandwidth budgeting, priority replication, prediction compensation |
