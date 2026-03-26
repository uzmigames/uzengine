<!-- doc-meta
title: VFX Sub-Emitter System Architecture Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# VFX Sub-Emitter System Architecture Guide

## Overview

The sub-emitter system enables parent particles to trigger spawn events in child particle emitters when they die, collide, or receive named events. This guide covers the sub-emitter trigger modes, the GPU-to-CPU readback pipeline, data flow, and practical examples like fireworks cascades.

A parent particle's state (position, velocity, color) is captured at the trigger moment and passed to the child emitter, allowing offspring particles to inherit properties or spawn at inherited locations.

## Context

The sub-emitter system involves three main components:

1. **SubEmitterModule** — configures the trigger condition and child asset reference
2. **SubEmitterReadbackBuffer** — manages triple-buffered GPU→CPU data transfer
3. **VFXEventQueue** — inter-emitter event delivery queue

The GPU side marks triggered particles in a `SubEmitterFlags[]` buffer. The CPU side reads this buffer asynchronously (1-frame latency) and emits event payloads into the child emitter's queue.

@ref UE5 Niagara Event Handler — parent-to-child particle event spawning architecture
@ref UE5 NiagaraDataInterfaceEventWrite.cpp — OnDeath/OnCollision event detection

## Trigger Modes

Particles are flagged for sub-emission based on one of three conditions.

### OnDeath

**GPU Detection**: `age >= lifespan`

**Behavior**: When a parent particle reaches the end of its natural lifetime, the sub-emitter module flags it and the CPU emits an event payload at the particle's final position.

**Configuration**:
```cpp
module.trigger = uz::SubEmitterTrigger::OnDeath;
```

**Use case**: Explosions, bursts, or secondary effects triggered when particles age out. Most common trigger type.

**Example**: A firework particle that explodes when it finishes its rise and peak.

### OnCollision

**GPU Detection**: `Positions[i].z < 0` (placeholder ground plane test)

**Behavior**: The shader checks if a particle has penetrated below Z = 0 (a simple ground plane). If so, it marks the particle as collided, triggering the sub-emitter.

**Configuration**:
```cpp
module.trigger = uz::SubEmitterTrigger::OnCollision;
```

**Note**: This is a placeholder implementation. Once a dedicated `CollisionModule` is available, the collision flag will be read from that module's output instead of hardcoded to a ground plane.

**Use case**: Impact effects, dust clouds, or shattering when particles hit geometry.

**Example**: Raindrops that splash when they hit the ground.

### OnEvent

**GPU Detection**: None (CPU-only)

**Behavior**: The sub-emitter does not set any GPU flags. Instead, the CPU listens for named events arriving via `VFXEventQueue` and emits child particles when a matching event is received.

**Configuration**:
```cpp
module.trigger = uz::SubEmitterTrigger::OnEvent;
module.eventName = "explode";  // Listen for this event name
```

**Use case**: Manual triggering, designer-controlled effects, or event-driven spawning from game logic.

**Example**: A missile that spawns a trail of sparks when a gameplay event fires.

## Data Flow: GPU→CPU Readback Pipeline

The sub-emitter system uses a carefully orchestrated pipeline to transfer triggered particle data from GPU to CPU without stalling the render thread.

### Stage 1: GPU Flagging (Simulation Dispatch)

The main VFX simulation compute shader checks the trigger condition and writes to the `SubEmitterFlags[]` UAV:

```hlsl
if (age >= lifespan) {
    SubEmitterFlags[particleIndex] = 1u;  // Mark for sub-emission
}
```

**Binding**: `SubEmitterFlags` is a `RWBuffer<uint>` UAV. One flag per particle. Initial value is 0; triggered particles are set to 1.

### Stage 2: GPU Compacting (Compact Dispatch)

A small compute pass (running after simulation) scans the `SubEmitterFlags[]` buffer and gathers triggered particle data into a compact buffer:

```hlsl
// Pseudo-code: compact pass
if (SubEmitterFlags[idx] != 0) {
    uint writePos = InterlockedAdd(CompactCount[0], 1);
    if (writePos < kMaxReadbackParticles) {
        SubEmitterGPUPayload payload;
        payload.position = Positions[idx];
        payload.velocity = Velocities[idx];
        payload.color = Colors[idx];
        payload.age = Ages[idx];
        CompactPayloads[writePos] = payload;
    }
}
```

**Key points**:
- Scans all alive particles (count provided in constant buffer).
- Uses `InterlockedAdd` on `CompactCount[0]` to append triggered data.
- Silently discards payloads beyond `kMaxReadbackParticles` (256).
- Output is stored in `CompactPayloads[]`, a `RWStructuredBuffer<SubEmitterGPUPayload>`.

### Stage 3: GPU Copy (Command List Enqueue)

After the compact dispatch, the command list enqueues a GPU buffer copy operation:

```cpp
readbackBuffer.IssueCopy(cmdList);
```

This copies both `CompactPayloads[]` and `CompactCount[0]` from the Default heap to a Readback heap buffer. The write index advances to the next slot in the triple-buffered rotation.

**Triple Buffering**: Three readback slots rotate (write indices 0→1→2→0). The CPU always maps the slot that was written 2 frames ago, ensuring the GPU has completed the copy.

**No stall**: The GPU copy happens asynchronously. The frame continues while data transfers in the background.

### Stage 4: CPU Readback (Execute Phase, Next Frame)

On the next frame, before `SubEmitterModule::Execute()` runs, the VFX system calls:

```cpp
SubEmitterReadback readback = readbackBuffer.GatherTriggered(device);
```

This maps the oldest readback slot (guaranteed to contain valid GPU data), reads the count and payloads, and returns a `SubEmitterReadback` structure:

```cpp
struct SubEmitterReadback {
    std::vector<EventPayload> flaggedParticles;
};
```

**Latency**: 1 frame by design. Frame N's triggered particles become readable on frame N+1.

**No stall**: Because triple buffering ensures the oldest slot is 2 frames old, no GPU synchronization stall occurs.

### Stage 5: CPU Event Emission (Execute)

`SubEmitterModule::Execute()` reads the `SubEmitterReadback`, builds one `EventPayload` per flagged particle, and emits them into the child emitter's queue:

```cpp
void SubEmitterModule::Execute(const ExecuteContext& ctx) {
    const auto* readback = static_cast<const SubEmitterReadback*>(ctx.parameterData);
    if (!readback || !m_childEventQueue) return;

    for (const auto& payload : readback->flaggedParticles) {
        EventPayload eventPayload;
        eventPayload.position = payload.position;
        eventPayload.velocity = payload.velocity * inheritVelocity;
        eventPayload.color = inheritColor ? payload.color : float4{1, 1, 1, 1};
        eventPayload.age = 0.0f;  // Child starts fresh

        m_childEventQueue->EmitWithPayload(childGraphId, eventPayload);
    }
}
```

**Inheritance**: `inheritVelocity` blends the parent's velocity into the child (0 = no inheritance, 1 = full). `inheritColor` optionally copies the parent color.

## Data Structures

### SubEmitterGPUPayload

Per-particle data written by the GPU compact pass and read back by the CPU.

```cpp
struct alignas(16) SubEmitterGPUPayload {
    float posX, posY, posZ;  // World-space position (cm)
    float _pad0;             // Padding for 16-byte alignment
    float velX, velY, velZ;  // Velocity (cm/s)
    float age;               // Particle age at trigger (seconds)
    float colorR, colorG, colorB, colorA;  // Linear RGBA color
};
// Total: 48 bytes, 16-byte aligned
```

**Memory layout**: Stored in a structured buffer on the GPU. Each triggered particle occupies one entry.

### EventPayload

The higher-level struct carrying triggered particle state to the child emitter.

```cpp
struct EventPayload {
    Vec3 position;      // Where the child spawns
    Vec3 velocity;      // Inherited velocity (blended by inheritVelocity)
    Vec4 color;         // Inherited color (optional)
    float age;          // Usually 0 for new children
};
```

## Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `childGraphId` | Int | 0 | Asset ID of the child .uzvfx graph to activate. 0 = unassigned. |
| `trigger` | Enum | OnDeath | Trigger condition: OnDeath (0), OnCollision (1), OnEvent (2). |
| `inheritVelocity` | Float | 0.5 | Velocity inheritance blend [0, 1]. 0 = no inheritance, 1 = full. |
| `inheritColor` | Bool | true | Whether child particles inherit the parent's color. |
| `eventName` | String | "" | Named event to listen for (OnEvent mode only). |

## Wiring: Connecting Parent to Child

Sub-emitters are not automatically wired. You must explicitly connect the child emitter's event queue to the module:

```cpp
// During emitter initialization
SubEmitterModule subEmitter;
subEmitter.childGraphId = childAssetID;
subEmitter.trigger = uz::SubEmitterTrigger::OnDeath;
subEmitter.inheritVelocity = 0.5f;
subEmitter.inheritColor = true;

// Retrieve the child emitter instance at runtime
VFXEmitter* childEmitter = vfxSystem.GetEmitterById(childAssetID);
if (childEmitter) {
    subEmitter.SetChildEventQueue(&childEmitter->GetEventQueue());
}
```

**Important**: Without calling `SetChildEventQueue()`, the sub-emitter module is non-functional — events are emitted to a null queue (no crash, but no effect).

## Readback Buffer Lifecycle

The `SubEmitterReadbackBuffer` manages all GPU and readback heap resources:

```cpp
// During emitter construction
SubEmitterReadbackBuffer readback;
readback.Create(device, maxParticles);

// Each frame: issue the copy after simulation/compact dispatch
readback.IssueCopy(cmdList);

// Next frame: gather before Execute()
SubEmitterReadback triggered = readback.GatherTriggered(device);

// During emitter destruction
readback.Destroy(device);
```

**Memory budget** (50K particle emitter):
- Flags: 200 KB (50K × 4 bytes)
- Compact buffer: 12 KB (256 × 48 bytes)
- 3× readback payload: 36 KB (3 × 256 × 48)
- **Total**: ~250 KB per emitter

## HLSL Integration Overview

The GPU-side process is divided into two compute passes, both invoked by the VFX system:

### Simulation Pass (Main Dispatch)

The simulation compute shader sets flags for triggered particles:

```hlsl
// Expected bindings:
// - SubEmitterFlags (RWBuffer<uint>) — per-particle flag UAV
// - cbSubEmitterTrigger (uint) — trigger mode (0=OnDeath, 1=OnCollision, 2=OnEvent)
// - Ages[], Lifespans[], Positions[] — particle data

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint idx = DTid.x;
    if (idx >= cbAliveCount) return;

    uint trigger = cbSubEmitterTrigger;
    bool shouldFlag = false;

    if (trigger == 0) {  // OnDeath
        shouldFlag = (Ages[idx] >= Lifespans[idx]);
    } else if (trigger == 1) {  // OnCollision
        shouldFlag = (Positions[idx].z < 0.0);  // Placeholder ground plane
    }
    // OnEvent is CPU-only; GPU never flags for event mode

    if (shouldFlag) {
        SubEmitterFlags[idx] = 1u;
    }
}
```

### Compact Pass (Separate Dispatch)

A second compute pass gathers flagged particles:

```hlsl
// Expected bindings (from SubEmitterReadbackBuffer::GetCompactShaderCode()):
// - SubEmitterFlags (Buffer<uint>) — per-particle flags (SRV, read-only)
// - Positions, Velocities, Colors, Ages — particle data (SRV)
// - CompactPayloads (RWStructuredBuffer<SubEmitterGPUPayload>) — output UAV
// - CompactCount (RWBuffer<uint>) — atomic counter UAV
// - cbAliveCount (uint) — number of alive particles to scan
// - cbMaxReadback (uint) — cap on appended payloads (usually 256)

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint idx = DTid.x;
    if (idx >= cbAliveCount) return;

    if (SubEmitterFlags[idx] != 0) {
        uint writeIdx = InterlockedAdd(CompactCount[0], 1);
        if (writeIdx < cbMaxReadback) {
            SubEmitterGPUPayload payload;
            payload.position = Positions[idx];
            payload.velocity = Velocities[idx];
            payload.color = Colors[idx];
            payload.age = Ages[idx];
            CompactPayloads[writeIdx] = payload;
        }
    }
}
```

Source: `runtime/shaders/runtime/vfx/sub_emitter_compact.hlsli`

## Examples

### Example 1: Fireworks Burst (OnDeath)

A firework shell that explodes when it reaches its peak.

```cpp
// Parent emitter: rising firework shells
ParticleEmitterConfig shellConfig;
shellConfig.lifetime = 2.0f;
shellConfig.spawnRate = 10;

// Child emitter: burst particles
ParticleEmitterConfig burstConfig;
burstConfig.lifetime = 1.5f;
burstConfig.spawnRate = 0;  // Only spawned by sub-emitter events

// Connect
SubEmitterModule shellBurst;
shellBurst.childGraphId = burstAssetID;
shellBurst.trigger = uz::SubEmitterTrigger::OnDeath;
shellBurst.inheritVelocity = 0.2f;  // Slight upward momentum
shellBurst.inheritColor = true;

VFXEmitter* burstEmitter = vfxSystem.GetEmitterById(burstAssetID);
shellBurst.SetChildEventQueue(&burstEmitter->GetEventQueue());
```

**Visual result**: Each shell rises and, when it dies, spawns a burst of colorful particles around its final position. The burst particles inherit some of the shell's upward momentum and its color.

### Example 2: Rain Impact Splash (OnCollision)

Raindrops create splashes when they hit the ground.

```cpp
// Parent emitter: rain particles falling
ParticleEmitterConfig rainConfig;
rainConfig.lifetime = 5.0f;
rainConfig.spawnRate = 100;

// Child emitter: splash effects
ParticleEmitterConfig splashConfig;
splashConfig.lifetime = 0.5f;
splashConfig.spawnRate = 0;

// Connect
SubEmitterModule raindropImpact;
raindropImpact.childGraphId = splashAssetID;
raindropImpact.trigger = uz::SubEmitterTrigger::OnCollision;
raindropImpact.inheritVelocity = 0.0f;  // Splashes start at rest (roughly)
raindropImpact.inheritColor = false;    // Use splash's own color

VFXEmitter* splashEmitter = vfxSystem.GetEmitterById(splashAssetID);
raindropImpact.SetChildEventQueue(&splashEmitter->GetEventQueue());
```

**Visual result**: As raindrops hit the ground (Z < 0), splashes emit at impact locations. Splashes don't inherit the rain's velocity, creating the effect of liquid impact resistance.

### Example 3: Missile Trail (OnEvent)

A missile fired by gameplay code, spawning a trail of smoke.

```cpp
// Parent emitter: missile particle (one per missile instance)
ParticleEmitterConfig missileConfig;
missileConfig.lifetime = 10.0f;

// Child emitter: smoke trail
ParticleEmitterConfig smokeConfig;
smokeConfig.lifetime = 2.0f;
smokeConfig.spawnRate = 0;

// Connect
SubEmitterModule missileSmoke;
missileSmoke.childGraphId = smokeAssetID;
missileSmoke.trigger = uz::SubEmitterTrigger::OnEvent;
missileSmoke.eventName = "fire_missile";  // Listen for this gameplay event
missileSmoke.inheritVelocity = 1.0f;      // Smoke inherits missile velocity
missileSmoke.inheritColor = false;

VFXEmitter* smokeEmitter = vfxSystem.GetEmitterById(smokeAssetID);
missileSmoke.SetChildEventQueue(&smokeEmitter->GetEventQueue());

// In gameplay code
gameplaySystem.OnMissileFired([&](MissileInstance* missile) {
    VFXEventQueue& queue = parentEmitter->GetEventQueue();
    queue.EmitEvent("fire_missile");  // Trigger sub-emission manually
});
```

**Visual result**: When gameplay fires a missile, the sub-emitter activates and spawns smoke at the missile's position. Smoke inherits the missile's velocity and trails behind it naturally.

### Example 4: Chain Reaction Cascade (Multiple Levels)

A multi-level cascade: parent → level1 child → level2 grandchild.

```cpp
// Level 0: initial explosion
// Level 1: secondary sparks (OnDeath of level 0)
// Level 2: dust puffs (OnDeath of level 1)

SubEmitterModule explosion0to1;
explosion0to1.childGraphId = sparksAssetID;
explosion0to1.trigger = uz::SubEmitterTrigger::OnDeath;
explosion0to1.inheritVelocity = 0.1f;

SubEmitterModule sparks1to2;
sparks1to2.childGraphId = dustAssetID;
sparks1to2.trigger = uz::SubEmitterTrigger::OnDeath;
sparks1to2.inheritVelocity = 0.05f;

// Wire both
VFXEmitter* sparksEmitter = vfxSystem.GetEmitterById(sparksAssetID);
VFXEmitter* dustEmitter = vfxSystem.GetEmitterById(dustAssetID);

explosion0to1.SetChildEventQueue(&sparksEmitter->GetEventQueue());
sparks1to2.SetChildEventQueue(&dustEmitter->GetEventQueue());
```

**Visual result**: The initial explosion spawns secondary sparks on death. Those sparks die quickly, spawning dust puffs. The result is a visually complex multi-stage cascade emanating from a single point.

## Performance Considerations

- **GPU readback overhead**: 1-frame latency by design (no stall). Readback buffer copies are small (max 12 KB per frame).
- **Memory**: ~250 KB per emitter for 50K particles (flags + triple-buffered readback).
- **CPU cost**: `GatherTriggered()` is a single MapBuffer/memcpy/UnmapBuffer. Negligible.
- **Event queue**: Each event emission into the child queue is O(1).
- **Scaling**: Multiple sub-emitters per parent have additive cost (one module = one readback).

## Common Issues

**Child particles not spawning**:
Check that `SetChildEventQueue()` was called and the queue pointer is non-null. Verify that `childGraphId` is valid and the child emitter exists.

**Triggered particles are 1 frame behind**:
This is expected due to triple-buffered readback latency. Particle state is captured on frame N and emitted on frame N+1. Design for this 1-frame delay in visual effects.

**OnCollision triggers on particles that didn't hit anything**:
The placeholder check is `position.z < 0`, a simple ground plane test. Once a dedicated `CollisionModule` is available, it will be more precise. For now, design around the ground plane assumption.

**Too many particles beyond kMaxReadbackParticles are lost**:
The compact pass silently discards payloads beyond 256. If many particles trigger simultaneously, increase `kMaxReadbackParticles` in the source or reduce trigger rate.

## Key Takeaways

- **Three trigger modes**: OnDeath (age reaches lifetime), OnCollision (Z < 0), OnEvent (named events).
- **GPU→CPU pipeline**: Flagging → compacting → GPU copy → CPU readback → event emission (1-frame latency).
- **Triple buffering**: Ensures no GPU/CPU stall during readback.
- **Inheritance**: Child particles can inherit velocity (blended by `inheritVelocity`) and color (optional).
- **Wiring**: Must explicitly call `SetChildEventQueue()` to connect parent and child.
- **Memory efficient**: Compact format (48 bytes/particle) and bounded (max 256 payloads/frame).

## References

- `runtime/engine/vfx/modules/sub_emitter_module.h` — SubEmitterModule configuration and API
- `runtime/engine/vfx/core/sub_emitter_readback.h` — SubEmitterReadbackBuffer and data structures
- `runtime/shaders/runtime/vfx/sub_emitter_compact.hlsli` — Compact compute pass source
- `runtime/engine/vfx/events/vfx_event.h` — VFXEventQueue and event payload structures
- @ref UE5 Niagara Event Handler — parent-to-child particle event spawning architecture

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation with trigger modes, GPU→CPU pipeline, data structures, and multi-level cascade examples. |
