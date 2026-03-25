# Light Particles (Particle-Emitted Lights)

> **Status**: Planned (Phase 3.75.0)
> **References**: [VFX Modules](03-vfx-modules.md), [Particle System](02-particle-system.md), [Clustered Lighting](../12-rendering-architecture/03-clustered-lighting.md)

## Purpose

The Light Render Module allows particles to emit transient point lights into the scene. This enables effects like:
- **Fire/flames** emitting warm light as they burn
- **Muzzle flashes** illuminating nearby surfaces
- **Magic spells** glowing with colored light
- **Explosions** creating bright, short-lived flashes

Unlike static lights placed in the scene, particle lights are **dynamic and short-lived**: they spawn with each particle and die when the particle expires, creating organic, flowing illumination.

## Design Overview

### Why GPU→CPU Readback?

The module uses a one-frame-delayed GPU→CPU readback rather than keeping lights on the GPU:

**Advantages**:
- Decouples the light system from the VFX backend (RHI-agnostic)
- Reuses the existing light management system (single point of truth)
- Simpler than maintaining GPU light lists in parallel
- One-frame latency is imperceptible for typical effects

**Trade-off**: Lights lag by one frame. Acceptable for fire, sparks, and explosions; not suitable for synchronous physics.

### Pipeline Flow

```
┌──────────────────────────────────────────────────────────────┐
│ Frame N: Particle Simulation & Light Emission                │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│ 1. GPU Compute (Particle Update)                            │
│    - Update particle positions, velocities, ages            │
│    - Write to LightParticleData SRV buffer:                 │
│      {position: float3, color: float4}                      │
│    - Alive particles write their light contribution         │
│    - Dead particles skip (no light written)                 │
│                                                               │
│ 2. GPU Readback (Async GPU→CPU)                             │
│    - Issue GPU→CPU readback of light particle buffer        │
│    - Request completes next frame (Frame N+1 latency)       │
│                                                               │
│ 3. Light Injection (Frame N Processing Deferred)            │
│    - Wait for Frame N-1 readback to complete                │
│    - Get LightParticleData array from readback              │
│    - Call LightRenderModule::ProcessReadback()              │
│    - For each entry:                                        │
│      * Validate position (check for NaN/infinity)          │
│      * Create transient light in light system                │
│      * Specify radius, intensity, color                      │
│    - Discard oldest lights if count > maxLights             │
│                                                               │
│ 4. Rendering (Frame N+1)                                   │
│    - Lights from Frame N particles illuminate Frame N+1     │
│    - One-frame latency imperceptible for fast-moving effects │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

## Light Particle Data Format

### LightParticleData Structure

```cpp
struct LightParticleData {
    Vec3 position;      // World-space particle position (12 bytes)
    float _pad;         // Alignment pad to 16 bytes (4 bytes)
    Vec4 color;         // Premultiplied RGBA color (16 bytes)
};
// Total: 32 bytes (GPU-friendly alignment)
```

**Color Format**: The `color` field stores premultiplied RGBA:
- RGB channels are already multiplied by the emitter's intensity
- The light system receives the color as-is with no additional scaling
- Example: intensity=2.0, particle_color=(1,0.5,0) → transmitted_color=(2, 1, 0)

**Position**: World-space position of the particle at the time of readback. The light system uses this directly for clustering and shadow depth.

## Configuration

### LightRenderModuleConfig

| Field | Default | Range | Description |
|-------|---------|-------|-------------|
| `radius` | 5.0 m | > 0 | Attenuation radius of each particle light |
| `intensity` | 1.0 | >= 0 | Brightness multiplier applied to particle color |
| `castShadow` | false | bool | Enable shadow casting (expensive) |
| `maxLights` | 64 | [1, 256] | Max simultaneous lights per emitter |

### Example .uzvfx Configuration

#### Fire Emitter

```json
{
  "emitter": {
    "name": "fire",
    "spawn_rate": 100,
    "max_particles": 200
  },
  "modules": [
    {
      "type": "sprite_render",
      "config": {
        "texture": "textures/flame.dds",
        "color": [1.0, 0.7, 0.3, 1.0]
      }
    },
    {
      "type": "light_render",
      "config": {
        "radius": 8.0,
        "intensity": 2.5,
        "cast_shadow": false,
        "max_lights": 100
      }
    },
    {
      "type": "gravity",
      "config": {
        "strength": 0.5
      }
    }
  ]
}
```

Fire particles emit warm light with a generous 8m radius. At 100 particles/sec and 2s lifespan, up to 200 lights active.

#### Muzzle Flash

```json
{
  "emitter": {
    "name": "muzzle_flash",
    "spawn_rate": 0,
    "max_particles": 20
  },
  "modules": [
    {
      "type": "sprite_render",
      "config": {
        "texture": "textures/flash.dds"
      }
    },
    {
      "type": "light_render",
      "config": {
        "radius": 12.0,
        "intensity": 4.0,
        "cast_shadow": false,
        "max_lights": 20
      }
    }
  ]
}
```

Muzzle flash is burst-spawned (20 particles in one frame), emitting bright white light. All 20 lights are active for the flash's brief lifetime.

#### Magical Glow (Shadow-Casting)

```json
{
  "emitter": {
    "name": "magic_orb",
    "spawn_rate": 50,
    "max_particles": 100
  },
  "modules": [
    {
      "type": "sprite_render"
    },
    {
      "type": "light_render",
      "config": {
        "radius": 3.0,
        "intensity": 1.5,
        "cast_shadow": true,
        "max_lights": 32
      }
    }
  ]
}
```

Magic orbs are lower-density but shadow-casting for hero moments. 32 lights is a reasonable budget for dynamic shadows.

## GPU→CPU Readback Pipeline

### Readback Buffer Allocation

At emitter creation, allocate a readback buffer:

```cpp
uint32_t bufferSize = module->GetReadbackBufferSize();
// = config.maxLights * sizeof(LightParticleData)
// = 64 * 32 = 2048 bytes (typical)

RHIBufferHandle readbackBuffer = rhi->CreateBuffer({
    .size = bufferSize,
    .usage = RHIBufferUsage::CopyDest,
    .cpuAccess = RHIBufferAccess::Read
});
```

### Issuing Readback

After the particle simulation compute dispatch, insert a copy command:

```cpp
// In render graph or command list:
cmdList->CopyBufferToBuffer(
    sourceBuffer,  // RWStructuredBuffer<LightParticleData> from simulation
    0,
    readbackBuffer,
    0,
    bufferSize
);

// Request CPU-side readback
gpu->RequestBufferReadback(readbackBuffer);
```

### Processing Readback (One Frame Later)

```cpp
void UpdateParticleLights(const LightRenderModule* module,
                          uint32_t aliveCount) {
    // Wait for readback to complete
    void* readbackPtr = gpu->WaitForReadback(readbackBuffer);
    if (!readbackPtr) return;  // Not ready yet

    // Extract validated light data
    auto lights = module->ProcessReadback(
        readbackPtr,
        module->GetReadbackBufferSize(),
        aliveCount
    );

    // Inject lights into the light system
    for (const auto& lightData : lights) {
        lightSystem->AddTransientPointLight(
            /*position=*/ lightData.position,
            /*color=*/ lightData.color,
            /*radius=*/ config.radius,
            /*castShadow=*/ config.castShadow
        );
    }
}
```

### Validation in ProcessReadback

The `ProcessReadback()` method sanitizes the readback data:

```cpp
std::vector<LightParticleData> ProcessReadback(
    const void* readbackData,
    uint32_t dataSize,
    uint32_t aliveCount) const {
    std::vector<LightParticleData> result;
    const auto* entries = reinterpret_cast<const LightParticleData*>(readbackData);

    uint32_t count = std::min(aliveCount, config_.maxLights);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];

        // Validate position (no NaN/infinity)
        if (!IsValid(entry.position)) {
            UZ_LOG(VFX, Warning, "Skipping particle light with invalid position");
            continue;
        }

        result.push_back(entry);
    }

    return result;
}
```

## Integration with Clustered Lighting

The light system manages transient particle lights via the clustered light pipeline:

1. **Light injection** — `AddTransientPointLight()` adds to a temporary list
2. **Clustering** — Each frame, the light system clusters all lights (static + transient) into 3D grid
3. **Rendering** — Shaders sample the light grid for shading calculations
4. **Cleanup** — Transient lights expire after one frame or when count exceeds `maxLights`

### N-Closest Selection

When a cluster receives more lights than its capacity, the system selects the **N closest** by distance:

```cpp
// Pseudocode in clustered light system
std::vector<Light> allLights = staticLights + transientParticleLights;
std::sort(allLights.begin(), allLights.end(),
          [clusterCenter](const Light& a, const Light& b) {
              return distance(a.position, clusterCenter) <
                     distance(b.position, clusterCenter);
          });
std::vector<Light> selectedLights(allLights.begin(),
                                   allLights.begin() + maxLightsPerCluster);
```

This ensures nearby particle lights take precedence over distant ones.

## Performance Analysis

### CPU Cost

| Operation | Time (per emitter) |
|-----------|-------------------|
| ProcessReadback() validation | 0.1–0.2 ms @ 64 lights |
| Light injection (AddTransientPointLight × N) | 0.01 ms per light |
| **Total** | <1 ms for typical emitter |

### GPU Cost

| Operation | Cost |
|-----------|------|
| Readback copy (2KB buffer) | <0.1 ms (PCIe copy) |
| Clustering update (all lights) | 0.5–2 ms (depends on light count) |
| Shading with lights | Varies by material (1–5 ms) |

### Memory

- Per emitter readback buffer: `maxLights * 32 bytes` = 2 KB (typical)
- Transient light list: 50 emitters × 64 lights × 64 bytes/light ≈ 200 KB (total system)

## Optimization Tips

1. **Cap `maxLights` per emitter** — Limit to 32–64 for typical effects; higher budgets are expensive
2. **Disable shadow casting** — Reduces clustering complexity; enable only for critical lights
3. **Use `AddTransient` lights, not persistent** — Expired lights cleaned up automatically
4. **Batch readback requests** — Copy all emitters' buffers in one pass if possible
5. **Pre-allocate readback buffers** — Avoid allocation/deallocation every frame
6. **Tune light radius** — Larger radius affects more clusters; smaller is faster

## Common Issues

### Lights Appear One Frame Late

**Problem**: Particle light lags visibly behind particle motion.

**Solution**: This is by design (one-frame GPU→CPU latency). If sync is critical:
- Disable particle lights; use separate light spawning system
- Or pre-bake light response in the particle shader

### Not Enough Lights Being Injected

**Problem**: Only 10 lights appear when expecting 64.

**Solution**:
- Check `aliveCount` — are particles actually spawning?
- Verify readback completed: is `gpu->WaitForReadback()` returning non-null?
- Check `maxLights` config — is it capping lower than expected?

### Lights Flicker or Disappear

**Problem**: Particle lights blink in and out.

**Solution**:
- Verify readback buffer is not overwritten mid-frame
- Check that light injection happens exactly once per frame
- Ensure `ProcessReadback()` is called synchronously, not in parallel

### NaN Warnings in Logs

**Problem**: "Skipping particle light with invalid position" logged every frame.

**Solution**:
- Check particle update shader for uninitialized positions
- Verify particles are being spawned (not all dead at frame start)
- If using physics, check for solver divergence (NaN propagation)

## Related Documentation

- [VFX Modules](03-vfx-modules.md) — Module architecture
- [Particle System](02-particle-system.md) — Emitter & compute
- [Clustered Lighting](../12-rendering-architecture/03-clustered-lighting.md) — Light system
- [.uzvfx Format](07-uzvfx-format.md) — Asset format

## Phase Roadmap

| Phase | Milestone | Status |
|-------|-----------|--------|
| 3.75.0 | Light render module & readback pipeline | Planned |
| 3.76.0 | Clustered light integration & testing | Planned |
| 3.77.0 | Selective light culling per-cluster | Planned |
| 3.78.0 | Lua API for dynamic light control | Planned |
