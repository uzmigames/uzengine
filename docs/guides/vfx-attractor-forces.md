<!-- doc-meta
title: VFX Attractor Forces Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# VFX Attractor Forces Guide

## Overview

Attractor forces pull particles toward one or more world-space points using configurable distance-based falloff. UzEngine's attractor system supports up to 8 independent attractors per emitter, each with its own position, strength, and falloff mode. This guide covers configuring attractors, understanding falloff modes, and implementing complex multi-attractor effects like particle black holes or gravity wells.

## Context

The attractor system is implemented in `AttractorModule` and backed by `AttractorBuffer`, a GPU structured buffer that holds up to 8 `AttractorData` entries. The primary attractor's configuration (position, strength, radius, falloff) is automatically uploaded each frame; additional attractors can be added programmatically via the buffer's public API.

## Falloff Modes

Distance-based falloff controls how force magnitude decreases (or remains constant) with distance from the attractor point. Three modes are available:

### Constant Falloff

**Behavior**: Force magnitude is uniform regardless of distance.

**Use when**: You want a simple "magnet" effect where particles experience the same acceleration no matter how far they are from the attractor.

**Configuration**:
```cpp
module.falloff = uz::AttractorFalloff::Constant;
module.strength = 10.0f;  // Force magnitude (units/s²)
```

**Visual result**: Particles accelerate uniformly toward the attractor from any distance. The effect is sharp and game-like rather than physically realistic.

### Linear Falloff

**Behavior**: Force decreases linearly from `strength` to zero as distance increases to `radius`. Beyond the radius, no force is applied.

**Use when**: You want a smooth, bounded attraction effect with a clear outer boundary. Particles far enough away are unaffected.

**Configuration**:
```cpp
module.falloff = uz::AttractorFalloff::Linear;
module.strength = 10.0f;
module.radius = 50.0f;  // Distance at which force reaches zero (cm)
```

**Formula**: `force = strength * max(0, 1 - distance / radius)`

**Visual result**: Particles near the attractor experience full acceleration; particles near the radius edge weaken gradually; particles beyond the radius are unaffected.

### InverseSquare Falloff

**Behavior**: Force follows the inverse-square law commonly used in physics (gravity, electrostatics). Force = `strength / max(distance², kMinRadius²)`.

**Use when**: You want physically realistic gravitational or electromagnetic attraction. The force decreases with distance squared, creating a characteristic curved trajectory.

**Configuration**:
```cpp
module.falloff = uz::AttractorFalloff::InverseSquare;
module.strength = 10.0f;  // Peak force (units/s²)
```

**Formula**: `force = strength / max(distance², (0.01)²)`

**Note**: `kMinRadius = 0.01f` prevents division by zero when particles are extremely close to the attractor point.

**Visual result**: Particles near the attractor are pulled with great force; the attraction weakens rapidly with distance. Orbits are naturally curved without additional constraints.

## Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `attractorPosition` | Vec3 | {0, 0, 0} | World-space position of the primary attractor (cm). |
| `strength` | Float | 10.0 | Peak force magnitude in units/s². Exact interpretation depends on falloff mode. |
| `radius` | Float | 5.0 | Effective radius for Linear falloff. Ignored by Constant and InverseSquare modes. |
| `falloff` | Enum | InverseSquare | Distance falloff mode: Constant (0), Linear (1), InverseSquare (2). |
| `killOnArrival` | Bool | false | When true, particles reaching the attractor within `kKillDistance` are expired. |

## Kill-On-Arrival

When `killOnArrival` is enabled, particles that reach the attractor within a small threshold distance are automatically expired (age set equal to lifespan), causing the emit/expire system to recycle them on the next frame.

**Constants**:
- `kMinRadius = 0.01f` — Minimum distance used in InverseSquare falloff to prevent division by zero.
- `kKillDistance = 0.05f` — Distance threshold below which particles are considered "arrived" and expired.

**Use case**: Black hole effects, particle sinks, or any scenario where particles should vanish once they reach the attractor point.

## Multi-Attractor Support

The `AttractorBuffer` holds up to 8 independent attractors, each with its own parameters:

```cpp
/// Per-attractor parameters uploaded to the GPU each frame.
struct AttractorData {
    float positionX, positionY, positionZ;  // World-space position (cm)
    float strength;                         // Force magnitude
    float radius;                           // Linear falloff radius
    uint32_t falloffType;                   // 0=Constant, 1=Linear, 2=InverseSquare
    uint32_t killOnArrival;                 // 0=false, 1=true
    float killDistance;                     // Distance threshold
};

static constexpr uint32_t kMaxAttractors = 8;
```

**Usage pattern**:

```cpp
// Frame loop
attractorModule.UploadAttractors(device);  // Clears buffer, adds primary attractor

// Add extra attractors (optional)
AttractorData extra;
extra.positionX = 20.0f;
extra.positionY = 0.0f;
extra.positionZ = 0.0f;
extra.strength = 5.0f;
extra.falloffType = 1;  // Linear
extra.radius = 30.0f;
if (!attractorModule.attractorBuffer.AddAttractor(extra)) {
    // Buffer is full; this attractor was not added
}

// Upload all attractors (primary + extras) to GPU
attractorModule.attractorBuffer.UploadToGPU(device);

// Dispatch simulation compute shader with the buffer bound as SRV
// The shader iterates all activeCount attractors and sums their forces
```

**Important**: The primary attractor (from `attractorPosition`, `strength`, etc.) is always added as slot 0 by `UploadAttractors()`. Extra attractors are added to subsequent slots.

## HLSL Integration Overview

The GPU-side computation applies forces from all active attractors each simulation tick. For each attractor, the shader:

1. Computes the direction from the particle to the attractor
2. Evaluates the appropriate falloff function based on the attractor's `falloffType`
3. Multiplies the falloff result by the attractor's `strength`
4. Accumulates the resulting acceleration into the particle's velocity

The three falloff modes are implemented in the HLSL snippet as:

```hlsl
// Constant: uniform force regardless of distance
float forceMagnitude_Constant = strength;

// Linear: zero beyond radius
float ratio = distance / radius;
float forceMagnitude_Linear = strength * max(0.0, 1.0 - ratio);

// InverseSquare: physically based 1/d²
float distSq = max(distance * distance, kMinRadius * kMinRadius);
float forceMagnitude_InverseSquare = strength / distSq;
```

The module's GPU code is injected automatically during the composition phase and runs as part of the main simulation compute dispatch — no separate dispatch required.

## Examples

### Example 1: Gravity Well (InverseSquare)

Create a gravitational attraction point that pulls particles in realistic curved trajectories.

```cpp
AttractorModule gravity;
gravity.attractorPosition = {0.0f, 0.0f, 100.0f};  // 1 meter above origin
gravity.strength = 25.0f;  // Strong gravity
gravity.falloff = uz::AttractorFalloff::InverseSquare;
gravity.killOnArrival = true;  // Particles vanish at the point
```

**Visual result**: Particles are drawn in curved paths toward the gravity point. Those that reach it are recycled. Particles far from the well are unaffected.

### Example 2: Explosion Ring (Linear)

Particles expand outward from an emitter but are contained within a ring boundary.

```cpp
// Primary attractor (repulsion would require inverse strength, not available)
// Instead, use negative strength if supported, or rely on velocity + walls
AttractorModule ring;
ring.attractorPosition = {0.0f, 0.0f, 50.0f};  // Ring center
ring.strength = -10.0f;  // Repulsion (if implementation supports negative)
ring.radius = 100.0f;
ring.falloff = uz::AttractorFalloff::Linear;
```

**Note**: If the implementation does not support negative strength, repulsion requires a different module (e.g., a repulsion field or velocity perturbation).

### Example 3: Black Hole (Multi-Attractor Sink)

Multiple particles streams converge on a central point and are destroyed.

```cpp
AttractorModule blackHole;
blackHole.attractorPosition = {0.0f, 0.0f, 0.0f};  // Center of map
blackHole.strength = 50.0f;  // Very strong
blackHole.falloff = uz::AttractorFalloff::InverseSquare;
blackHole.killOnArrival = true;

// Programmatically add secondary attractors for multi-stream effects
AttractorData secondary;
secondary.positionX = 200.0f;
secondary.positionY = 0.0f;
secondary.positionZ = 0.0f;
secondary.strength = 30.0f;
secondary.falloffType = 2;  // InverseSquare
secondary.killOnArrival = 1;
blackHole.attractorBuffer.AddAttractor(secondary);
```

**Visual result**: Multiple particle streams collide and converge at the primary attractor, then vanish. The secondary attractor draws some particles along a different path.

### Example 4: Fireworks Finale (Linear + Constant)

A slow spiral ascent with a final collapse to the attractor.

```cpp
// Early phase: weak constant force for gentle pull
AttractorModule finale;
finale.attractorPosition = {0.0f, 0.0f, 300.0f};  // High in the air
finale.strength = 2.0f;  // Gentle
finale.falloff = uz::AttractorFalloff::Constant;
finale.killOnArrival = false;

// Late phase: add a strong linear attractor near ground
AttractorData impact;
impact.positionX = 0.0f;
impact.positionY = 0.0f;
impact.positionZ = 0.0f;
impact.strength = 30.0f;
impact.radius = 200.0f;
impact.falloffType = 1;  // Linear
impact.killOnArrival = 1;
finale.attractorBuffer.AddAttractor(impact);
```

**Visual result**: Particles rise slowly under gentle attraction, then near the finale cue, a strong linear attractor fires, pulling them all downward in an accelerating cascade before they vanish at ground level.

## Performance Considerations

- **GPU cost**: Per-particle, the attractor loop iterates all active attractors (up to 8) and evaluates distance and falloff. Total cost is roughly 40-80 GPU cycles per particle depending on falloff mode.
- **Memory**: `AttractorBuffer` uses 256 bytes (8 × 32-byte entries) of GPU memory per emitter.
- **CPU overhead**: `UploadAttractors()` is a single MapBuffer/memcpy/UnmapBuffer per frame — negligible cost.
- **Scaling**: Multiple attractors add O(n) cost where n ≤ 8. Reaching the 8-attractor limit has minimal visual impact on performance.

## Common Issues

**All particles move toward a single point but don't accelerate**:
Check that `strength` is large enough and that the falloff mode matches your intended effect. Constant falloff may feel less intuitive than InverseSquare.

**Particles oscillate around the attractor instead of settling**:
This is physically realistic behavior (harmonic oscillation) when the attractor is the only force. Add damping via a separate drag module or use `killOnArrival` to terminate particles at the point.

**Secondary attractors not being added**:
Ensure `AddAttractor()` is returning `true` — the buffer may be at capacity. If the return is `false`, no more than 8 attractors are supported per emitter.

**Kill-on-arrival is too aggressive or too lenient**:
Adjust `kKillDistance` (currently 0.05 cm) in the source or use a different termination strategy. Particles must fall below this threshold to trigger expiration.

## Key Takeaways

- **Three falloff modes**: Constant (uniform), Linear (bounded), InverseSquare (realistic gravity).
- **Multi-attractor**: Up to 8 attractors per emitter; primary attractor from module config, extras via buffer API.
- **Kill-on-arrival**: Optional automatic particle recycling when particles reach the attractor.
- **GPU-executed**: All force computation runs on the GPU as part of the main simulation dispatch.
- **Configuration**: Position, strength, radius, falloff type, and kill-on-arrival flag are the five tunable parameters.

## References

- `runtime/engine/vfx/modules/attractor_module.h` — AttractorModule configuration and API
- `runtime/engine/vfx/core/attractor_buffer.h` — AttractorBuffer and AttractorData structures
- `runtime/shaders/runtime/vfx/vfx_attractors.hlsli` — HLSL implementation of falloff modes and force accumulation
- @ref Stam 1999 "Stable Fluids" — vector field and force-based advection fundamentals

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation with falloff modes, multi-attractor support, and practical examples. |
