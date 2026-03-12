# RPC System — Remote Procedure Calls, Targets, Reliability

> **Depends on**: Networking system (§24), Property replication (§24.3)
> **Used by**: Gameplay events (damage, death, ability activation), cosmetic RPCs (sounds, particles)
> **Transport**: ENet (reliable and unreliable channels)

## Purpose

Remote Procedure Calls (RPCs) invoke functions on remote machines. Unlike property replication (continuous state sync), RPCs are discrete events: "player was hit," "ability activated," "sound played." They may be reliable (must arrive) or unreliable (loss acceptable).

## RPC Declaration

```cpp
namespace uz::Networking {
    /// RPC target machine.
    enum class RPCTarget {
        Server,       // Call on server (client → server)
        Client,       // Call on owning client (server → client)
        Multicast,    // Call on all clients (server replicates to all)
        OtherClients  // Call on all clients EXCEPT caller (server excludes sender)
    };

    /// Guarantee of delivery.
    enum class RPCReliability {
        Reliable,     // ENet reliable channel — guaranteed to arrive in order
        Unreliable    // UDP — fast, may be lost (acceptable for cosmetic effects)
    };

    /// Condition for calling RPC.
    enum class RPCCondition {
        Always,       // Always execute
        OwnerOnly,    // Only for entity owner
        RelevantOnly  // Only for relevant clients
    };
}

// Macro to declare RPC
#define UZ_RPC(Name, Target, Reliability, Condition = Always) \
    void Name##_Impl(...);  /* Implementation */ \
    void Name(...);         /* Proxy that sends network packet */

// Usage:
class WeaponComponent : public uz::Component {
    UZ_COMPONENT(WeaponComponent);

public:
    /// Fire weapon. Client calls this; server executes.
    /// Server validates (ammo, cooldown), then calls OnFire_Multicast to all clients.
    UZ_RPC(Fire,
        uz::Networking::RPCTarget::Server,
        uz::Networking::RPCReliability::Reliable
    );

    /// Hit notification. Server calls this on hitting client.
    /// One-way, reliable.
    UZ_RPC(OnHit,
        uz::Networking::RPCTarget::Client,
        uz::Networking::RPCReliability::Reliable
    );

    /// Sound effect. Multicast: called on all clients (cosmetic).
    /// Unreliable — sound miss is acceptable.
    UZ_RPC(PlayFireSound,
        uz::Networking::RPCTarget::Multicast,
        uz::Networking::RPCReliability::Unreliable
    );

private:
    /// Implementation (called by networking layer).
    void Fire_Impl(Quat aim_rotation) {
        // Server-side logic: validate, apply damage
        int ammo = GetAmmo();
        if (ammo <= 0) return;

        DecrementAmmo();
        ApplyRecoil(aim_rotation);

        // Notify all clients
        OnHit_Multicast(hit_entity, impact_pos);
        PlayFireSound_Multicast();
    }

    void OnHit_Impl(uz::EntityID hit_entity, Vec3 impact_pos) {
        // Client-side: visual feedback
        SpawnHitParticles(impact_pos);
    }

    void PlayFireSound_Impl() {
        // Client-side: audio
        uz::Audio::PlaySound("sfx/weapon_fire.ogg");
    }
};
```

## RPC Invocation

Game code calls the RPC as if it's a local function:

```cpp
// Client code: player presses fire button
void PlayerInput::OnFirePressed() {
    auto weapon = player_entity_->Get<WeaponComponent>();
    weapon->Fire(aim_rotation);  // Sends to server
}

// Server code: receives Fire RPC
void WeaponComponent::Fire_Impl(Quat aim_rotation) {
    // Validate and execute on server
    // Then multicast to all clients
}
```

## Target Types

### Server Target

Call on server from client. Typically for:
- Player input (movement, abilities)
- Damage events (client requests hit)
- Game logic validation (only server decides outcomes)

```cpp
UZ_RPC(RequestDamage,
    uz::Networking::RPCTarget::Server,
    uz::Networking::RPCReliability::Reliable
);

// Client calls:
weapon->RequestDamage(target_entity, 25);  // Sends to server

// Server receives and validates:
void WeaponComponent::RequestDamage_Impl(uz::EntityID target, int damage) {
    // Server-side validation
    if (!CanDamage(target, damage)) return;

    // Apply damage server-side
    auto target_comp = world->Get(target)->Get<HealthComponent>();
    target_comp->TakeDamage(damage);

    // Notify all clients (via property replication)
}
```

### Client Target

Call on owning client from server. Typically for:
- Personal notifications (only you see this)
- Private information (your health, inventory, etc.)

```cpp
UZ_RPC(OnHealthChanged,
    uz::Networking::RPCTarget::Client,
    uz::Networking::RPCReliability::Reliable
);

// Server-side change
void HealthComponent::TakeDamage(int amount) {
    health -= amount;
    OnHealthChanged_Client(health);  // Notify owner only
}

// Client receives:
void HealthComponent::OnHealthChanged_Impl(int new_health) {
    UpdateHealthBar(new_health);
    if (new_health <= 0) {
        ShowDeathScreen();
    }
}
```

### Multicast Target

Call on all clients. Typically for:
- Cosmetic effects (particle systems, sounds)
- Non-critical state (animation, effects)

```cpp
UZ_RPC(PlayExplosion,
    uz::Networking::RPCTarget::Multicast,
    uz::Networking::RPCReliability::Unreliable  // OK to lose some
);

// Server initiates:
void OnExplosion(Vec3 position) {
    PlayExplosion_Multicast(position);
}

// All clients receive:
void PlayExplosion_Impl(Vec3 position) {
    SpawnParticles("vfx/explosion.uzvfx", position);
    PlaySound("sfx/explosion.ogg", position);
    CameraShake(0.5f);  // Only local
}
```

### OtherClients Target

Call on all clients except the one that initiated. Useful for:
- Actions where originator has already played animation
- Avoiding redundant local effects

```cpp
UZ_RPC(PlayAttackAnimation,
    uz::Networking::RPCTarget::OtherClients,
    uz::Networking::RPCReliability::Unreliable
);

// Client-side prediction (no network latency):
void PlayerInput::OnAttackPressed() {
    // Play animation immediately (local)
    player_character->PlayAttackAnimation_Local();

    // Tell server
    weapon->Attack(aim_rotation);  // Sends to server
}

// Server receives and replicates to others:
void Weapon::Attack_Impl(Quat aim) {
    // ... validate, apply damage ...

    // Notify other clients (not the attacker)
    PlayAttackAnimation_OtherClients(aim);
}

// Other clients receive:
void PlayAttackAnimation_Impl(Quat aim) {
    character->PlayAttackAnimation(aim);
}
```

## Parameter Serialization

RPC parameters are automatically serialized to network packets:

```cpp
// Supported types (serializable)
UZ_RPC(Example,
    uz::Networking::RPCTarget::Server,
    uz::Networking::RPCReliability::Reliable,

    // Primitive types
    int param1,
    float param2,
    bool param3,

    // Engine types
    Vec3 param4,
    Quat param5,

    // Enums (serialized as uint8)
    enum class State { Idle, Running, Jumping } param6,

    // Entity/Component references (serialized as EntityID)
    uz::EntityID param7,
    uz::ComponentHandle param8
);

// Serialization:
// - int: 4 bytes
// - float: 4 bytes
// - Vec3: 12 bytes
// - Quat: 16 bytes (or 4 if compressed)
// - bool: 1 byte
// - EntityID: 4 bytes
```

Custom types can implement serialization:

```cpp
class CustomData {
    int value;
    std::string name;

    // Implement for RPC
    void Serialize(uz::NetworkWriter& w) const {
        w.Write(value);
        w.WriteString(name);  // Length-prefixed
    }

    void Deserialize(uz::NetworkReader& r) {
        r.Read(value);
        r.ReadString(name);
    }
};

UZ_RPC(SendCustom,
    uz::Networking::RPCTarget::Server,
    uz::Networking::RPCReliability::Reliable,
    CustomData data
);
```

## Reliability

### Reliable

Guaranteed to arrive in order. Uses ENet reliable channel.

```cpp
UZ_RPC(TakeDamage,
    uz::Networking::RPCTarget::Server,
    uz::Networking::RPCReliability::Reliable
);
// Critical: damage must arrive, must be in order
```

Overhead:
- Small header for sequencing
- Retransmission on packet loss
- In-order delivery guarantee
- Slightly higher latency

### Unreliable

UDP — may be lost. Uses ENet unreliable channel.

```cpp
UZ_RPC(PlaySound,
    uz::Networking::RPCTarget::Multicast,
    uz::Networking::RPCReliability::Unreliable
);
// OK to lose: missing a sound effect is acceptable
```

Advantages:
- Minimal overhead (no acks)
- Lower latency
- Acceptable for cosmetic effects

## Conditions

### Always

RPC fires whenever called:

```cpp
UZ_RPC(OnPlayerJoined,
    uz::Networking::RPCTarget::Multicast,
    uz::Networking::RPCReliability::Reliable,
    uz::Networking::RPCCondition::Always
);
// All clients always hear about new players
```

### OwnerOnly

Only owner receives:

```cpp
UZ_RPC(OnOwnHealthChanged,
    uz::Networking::RPCTarget::Client,
    uz::Networking::RPCReliability::Reliable,
    uz::Networking::RPCCondition::OwnerOnly
);
// Server sends to owner's client only
```

### RelevantOnly

Only relevant clients receive (multicast target):

```cpp
UZ_RPC(PlayAttackAnimation,
    uz::Networking::RPCTarget::Multicast,
    uz::Networking::RPCReliability::Unreliable,
    uz::Networking::RPCCondition::RelevantOnly
);
// Only nearby players see animation
```

## Complete Example: Combat System

```cpp
class CombatComponent : public uz::Component {
    UZ_COMPONENT(CombatComponent);

public:
    // Client → Server: request attack
    UZ_RPC(RequestAttack,
        uz::Networking::RPCTarget::Server,
        uz::Networking::RPCReliability::Reliable
    );

    // Server → All: animate attack
    UZ_RPC(PlayAttackAnimation,
        uz::Networking::RPCTarget::Multicast,
        uz::Networking::RPCReliability::Unreliable,
        uz::Networking::RPCCondition::RelevantOnly
    );

    // Server → Owner: notify of hit
    UZ_RPC(OnHit,
        uz::Networking::RPCTarget::Client,
        uz::Networking::RPCReliability::Reliable,
        uz::Networking::RPCCondition::OwnerOnly
    );

    // Server → All: play hit effect
    UZ_RPC(PlayHitEffect,
        uz::Networking::RPCTarget::Multicast,
        uz::Networking::RPCReliability::Unreliable
    );

private:
    void RequestAttack_Impl(uz::EntityID target, Quat aim_rotation) {
        // Server-side validation
        if (!CanAttack(target)) return;

        // Apply damage
        auto target_comp = world->Get(target)->Get<HealthComponent>();
        target_comp->TakeDamage(30);

        // Animate on all clients
        PlayAttackAnimation_Multicast(aim_rotation);

        // Notify hit (owner only)
        OnHit_Client(target);

        // Effects
        PlayHitEffect_Multicast(target_comp->GetPosition());
    }

    void PlayAttackAnimation_Impl(Quat aim_rotation) {
        character->PlayAttackAnim(aim_rotation);
    }

    void OnHit_Impl(uz::EntityID target) {
        UZ_LOG(Combat, Info, "You hit entity {}", target);
    }

    void PlayHitEffect_Impl(Vec3 position) {
        SpawnParticles("vfx/hit.uzvfx", position);
        PlaySound("sfx/hit.ogg", position);
    }
};
```

## RPC Throttling

Prevent network spam:

```cpp
class WeaponComponent {
    uint32_t last_fire_frame_ = 0;
    const uint32_t fire_cooldown_frames = 6;  // 10 shots/sec at 60 Hz

    void Fire(Quat aim) {
        if (frame_count_ - last_fire_frame_ < fire_cooldown_frames) {
            return;  // Throttle
        }

        last_fire_frame_ = frame_count_;
        Fire_RPC(aim);  // Send to server
    }
};
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Source Engine | **UzEngine** |
|---------|----------|---------|---------------|--------------|
| RPC system | `Server/Client/NetMulticast` | `rpc()` | `CSVC/CLVC` | **`UZ_RPC` macro** |
| Parameters | Via reflection | Direct passing | Limited | **Full serialization** |
| Reliability | Implicit (reliable) | Via `@rpc` | Implicit | **Explicit (Reliable/Unreliable)** |
| Conditions | Via `validate()` | None | None | **OwnerOnly/RelevantOnly** |
| Bandwidth | High (all RPCs) | Medium | Medium | **Efficient (delta compression)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 5** | RPC macro, parameter serialization, Server/Client/Multicast targets |
| **Phase 6** | Reliability types, conditions (OwnerOnly/RelevantOnly), OtherClients target |
| **Phase 7** | Bandwidth optimization, RPC throttling, advanced parameter compression |
