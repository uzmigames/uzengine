<!-- doc-meta
title: VFX Advanced Constraint Modules Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# VFX Advanced Constraint Modules Guide

## Overview

Advanced constraint modules enforce spatial relationships and motion boundaries on particles using physics-inspired forces. This guide covers three GPU-based constraint modules: **Orbit** (circular motion), **Pin** (spring attraction), and **DistanceConstraint** (spherical shell). All are GPU-only; the CPU-side `Execute()` is a no-op.

These modules apply corrective impulses each simulation tick to keep particles within desired geometric or kinematic bounds.

## Context

Constraint modules are foundational to stable particle simulation. They enforce relationships like "particles should orbit this point" or "particles should stay within this distance range" without requiring per-particle state or feedback. All computation happens in the GPU compute shader as part of the main simulation dispatch.

@ref Verlet Integration — constraint-based simulation overview
@ref Hooke 1678 "De Potentia Restitutiva" — spring force fundamentals (F = kx)

## Orbit Module

The **OrbitModule** steers particles into circular orbits around a configurable world-space center point.

### How It Works

Two forces are applied each frame:

1. **Tangential Impulse**: Accelerates the particle along the direction perpendicular to the radius (in the orbit plane). This produces constant angular velocity around the center.

2. **Radial Spring**: Pulls the particle toward the desired `orbitRadius`. If a particle drifts inward or outward, the spring force corrects it. This damping prevents divergence from the ideal orbit.

**Key insight**: The combination of tangential and radial forces naturally produces stable circular motion without requiring precise initial conditions.

### Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `center` | Vec3 | {0, 0, 0} | World-space center point (cm). |
| `speed` | Float | 1.0 | Angular velocity in radians/second. |
| `orbitRadius` | Float | 5.0 | Desired orbit radius (cm). |
| `axis` | Vec3 | {0, 0, 1} | Orbit axis (Z-up by default). |

### Physics

**Tangential impulse** (constant angular velocity):
```
impulse_tangential = (speed * dt) * perpendicular_direction
```

**Radial spring** (Hooke's law with damping):
```
radialError = orbitRadius - distance_to_center
impulse_radial = (radialError * speed * dt) * inward_direction
```

The `speed` parameter acts as both the angular velocity and the spring stiffness coefficient, providing a balanced, responsive orbit.

### HLSL Overview

The module's GPU code computes:

```hlsl
float3 toCenter = center - particlePosition;
float dist = length(toCenter);

// Skip if particle is too close (avoid division by zero)
if (dist < 1e-6) return;

// Radial direction (inward)
float3 radialDir = toCenter / dist;

// Tangential direction (cross product of axis and radial)
float3 tangent = cross(axis, radialDir);
if (length(tangent) < 1e-6) {
    // Particle aligned with axis; skip
    return;
}
tangent = normalize(tangent);

// Tangential impulse
velocity += tangent * speed * dt;

// Radial spring impulse
float radialError = orbitRadius - dist;
velocity += radialDir * (radialError * speed * dt);
```

### Examples

#### Planetary Ring

Particles orbit in a flat plane at a fixed radius, like a planet's ring system.

```cpp
OrbitModule ring;
ring.center = {0.0f, 0.0f, 0.0f};      // Ring center
ring.speed = 1.5f;                     // Radians/second
ring.orbitRadius = 100.0f;             // 100 cm radius
ring.axis = {0.0f, 0.0f, 1.0f};        // Z-up orbit plane
```

**Visual result**: Particles move in a stable circular orbit in the XY plane.

#### Rotating Halo

Particles spiral upward while orbiting, like a swirling halo around a magical object.

```cpp
OrbitModule halo;
halo.center = {0.0f, 0.0f, 0.0f};
halo.speed = 2.0f;
halo.orbitRadius = 50.0f;
halo.axis = {0.0f, 0.0f, 1.0f};

// Combine with VerticalVelocityModule or similar to add vertical motion
```

**Visual result**: Particles orbit horizontally while gravity or another module adds vertical drift, creating a spiraling effect.

## Pin Module

The **PinModule** attracts particles toward a fixed world-space point using Hooke's law spring force.

### How It Works

The spring force is proportional to the particle's displacement from the pin:

```
F = -k * (position - pinPosition)
```

Where `k` is the spring constant (`springStrength`). The negative sign indicates that the force points back toward the pin position.

Particles closer to the pin experience weaker force; particles farther experience stronger pull. The result is oscillatory motion around the pin when combined with velocity damping.

### Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pinPosition` | Vec3 | {0, 0, 0} | World-space anchor point (cm). |
| `springStrength` | Float | 5.0 | Spring constant k in Hooke's law (F = k * x). |

### Physics

@ref Hooke 1678 "De Potentia Restitutiva" — the foundational spring force law

**Spring impulse**:
```
displacement = pinPosition - particlePosition
if (length(displacement) > 1e-6 world units):
    impulse = displacement * springStrength * dt
    velocity += impulse
```

Typical range for `springStrength`: [0.1, 50.0]
- **0.1**: Weak, slow spring (gentle guidance)
- **5.0**: Moderate spring (typical swinging)
- **50.0**: Stiff spring (fast oscillation, nearly rigid)

### HLSL Overview

```hlsl
float3 displacement = pinPosition - particlePosition;
float dist = length(displacement);

if (dist > 1e-3) {  // Skip if particle is exactly at pin
    float3 direction = displacement / dist;
    velocity += direction * (dist * springStrength * dt);
}
```

### Examples

#### Pendulum

Particles swing back and forth like a pendulum from a fixed pivot.

```cpp
PinModule pendulum;
pendulum.pinPosition = {0.0f, 100.0f, 0.0f};  // Pivot point 1m above origin
pendulum.springStrength = 3.0f;               // Moderate swing speed

// Combine with gravity to create natural pendulum arc
```

**Visual result**: Particles swinging from a point with realistic arc motion.

#### Pinned Particle Fountain

Particles spawn at a pin point and are immediately pulled back by the spring.

```cpp
// Emitter configuration
emitterConfig.spawnPosition = {0.0f, 0.0f, 0.0f};
emitterConfig.spawnVelocity = {5.0f, 5.0f, 5.0f};  // Initial outward velocity

PinModule tether;
tether.pinPosition = {0.0f, 0.0f, 0.0f};
tether.springStrength = 10.0f;  // Strong pull (creates bursting fountain)
```

**Visual result**: Particles burst outward but are yanked back to the center by the spring, creating a pulsating fountain effect.

## Distance Constraint Module

The **DistanceConstraintModule** enforces a spherical shell constraint around a world-space origin. Particles are kept within a minimum and maximum distance range via corrective velocity impulses.

### How It Works

The constraint checks each particle's distance from the `constraintOrigin`:

1. **Inner boundary violation**: If a particle is closer than `minDistance`, it's pushed outward.
2. **Outer boundary violation**: If a particle is farther than `maxDistance`, it's pulled inward.
3. **Valid zone**: If distance is between `minDistance` and `maxDistance`, no force is applied.

The corrective force is proportional to the distance of the violation, scaled by `constraintStrength`.

### Configuration

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `constraintOrigin` | Vec3 | {0, 0, 0} | Center of the constraint sphere (cm). |
| `minDistance` | Float | 0.0 | Inner radius; closer particles are pushed out. |
| `maxDistance` | Float | 50.0 | Outer radius; farther particles are pulled in. |
| `constraintStrength` | Float | 10.0 | Force multiplier for correction impulses. |

### Physics

**Inner violation** (particle too close):
```
if (distance < minDistance):
    violation = minDistance - distance
    impulse_outward = violation * constraintStrength * dt
```

**Outer violation** (particle too far):
```
if (distance > maxDistance):
    violation = distance - maxDistance
    impulse_inward = violation * constraintStrength * dt
```

The impulse direction is always radial (along the line from `constraintOrigin` to the particle).

### HLSL Overview

```hlsl
float3 toParticle = particlePosition - constraintOrigin;
float dist = length(toParticle);

if (dist < 1e-6) {
    // Particle at origin; skip to avoid division by zero
    return;
}

float3 radialDir = toParticle / dist;

// Inner boundary
if (dist < minDistance) {
    float violation = minDistance - dist;
    velocity += radialDir * (violation * constraintStrength * dt);
}
// Outer boundary
else if (dist > maxDistance) {
    float violation = dist - maxDistance;
    velocity -= radialDir * (violation * constraintStrength * dt);
}
```

### Examples

#### Distance-Bounded Swarm

Particles are confined to a spherical volume, creating a cloud effect.

```cpp
DistanceConstraintModule swarmBound;
swarmBound.constraintOrigin = {0.0f, 0.0f, 0.0f};
swarmBound.minDistance = 0.0f;        // No inner boundary
swarmBound.maxDistance = 200.0f;      // 2-meter radius sphere
swarmBound.constraintStrength = 5.0f; // Gentle confinement
```

**Visual result**: A cloud of particles bounces and roams within a spherical volume. Particles that drift beyond 2 meters are smoothly pulled back inward.

#### Orbital Ring Confinement

Confines particles to a thin spherical shell (e.g., a ring or halo).

```cpp
DistanceConstraintModule ringShell;
ringShell.constraintOrigin = {0.0f, 0.0f, 0.0f};
ringShell.minDistance = 80.0f;        // Inner radius 80 cm
ringShell.maxDistance = 120.0f;       // Outer radius 120 cm
ringShell.constraintStrength = 15.0f; // Stiff shell

// Combine with OrbitModule for stable orbital rings
OrbitModule orbit;
orbit.center = {0.0f, 0.0f, 0.0f};
orbit.orbitRadius = 100.0f;  // Midpoint of shell
orbit.speed = 1.0f;
```

**Visual result**: Particles are confined to a 40 cm thick spherical shell and orbit within it.

#### Atmospheric Escape Prevention

Confines particles to a planet-sized atmosphere sphere.

```cpp
DistanceConstraintModule atmosphere;
atmosphere.constraintOrigin = {0.0f, 0.0f, 0.0f};  // Planet center
atmosphere.minDistance = 0.0f;
atmosphere.maxDistance = 500.0f;  // 5-meter atmosphere radius
atmosphere.constraintStrength = 2.0f;  // Soft boundary (allows some overshoot)
```

**Visual result**: Weather particles (clouds, dust) can wander freely within the atmosphere but are gently pulled back if they escape toward space.

## GPU-Only Execution

All three constraint modules are **GPU-only** — their CPU-side `Execute()` method is an empty no-op:

```cpp
void OrbitModule::Execute(const ExecuteContext& ctx) override {
    // No-op. All computation happens on the GPU.
}

void PinModule::Execute(const ExecuteContext& ctx) override {
    // No-op. All computation happens on the GPU.
}

void DistanceConstraintModule::Execute(const ExecuteContext& ctx) override {
    // No-op. All computation happens on the GPU.
}
```

All force computation is injected directly into the GPU simulation compute shader. No separate dispatch or data transfer is required.

## Combining Constraints

Constraints can be stacked to create complex behaviors. The VFX compositor accumulates forces from all active modules.

### Example: Spiraling Confined Orbit

Particles orbit while remaining confined to a shell.

```cpp
// Orbit behavior
OrbitModule orbit;
orbit.center = {0.0f, 0.0f, 0.0f};
orbit.speed = 2.0f;
orbit.orbitRadius = 100.0f;

// Confinement
DistanceConstraintModule shell;
shell.constraintOrigin = {0.0f, 0.0f, 0.0f};
shell.minDistance = 90.0f;
shell.maxDistance = 110.0f;
shell.constraintStrength = 10.0f;

// Result: Particles maintain stable orbits within a thin shell
```

### Example: Spring-Tethered Orbit

Particles orbit but are also attracted to a separate pin point.

```cpp
// Orbit around primary center
OrbitModule primary;
primary.center = {0.0f, 0.0f, 0.0f};
primary.speed = 1.0f;
primary.orbitRadius = 50.0f;

// Spring tether to a secondary point (e.g., a moving object)
PinModule tether;
tether.pinPosition = {200.0f, 0.0f, 0.0f};
tether.springStrength = 2.0f;  // Weak tether; orbit dominates
```

**Visual result**: Particles orbit, but if the tether pin moves, they're gently pulled along. A compromise motion between orbital mechanics and spring attraction.

## Performance Considerations

- **GPU cost per particle**:
  - **OrbitModule**: ~20-30 cycles (two cross products, normalize, multiply)
  - **PinModule**: ~15-20 cycles (subtract, length, multiply)
  - **DistanceConstraintModule**: ~20-25 cycles (subtract, length, conditional branches)
- **Scaling**: Multiple constraints combine additively. Three constraints = ~60-75 cycles per particle (still fast).
- **Memory**: No per-particle state; all configuration is per-module in the constant buffer.

## Common Issues

**Particles are not moving at all**:
Check that `speed` (Orbit) or `springStrength` (Pin) is non-zero. For DistanceConstraint, verify that particles are actually outside the valid distance range.

**Orbit is unstable or wobbly**:
Increase `speed` slightly or reduce the initial velocity magnitude. Very high initial velocities can cause the orbit to diverge.

**Pin constraint causes oscillation**:
This is expected behavior with a spring force. If oscillation is unwanted, add damping via a separate drag module or increase `springStrength` significantly (creating a stiff constraint).

**Distance constraint is too gentle or too aggressive**:
Adjust `constraintStrength`. Higher values = faster correction. Typical range: [1.0, 20.0].

## Key Takeaways

- **Orbit**: Circular motion with tangential + radial spring forces. Use for stable rotational behavior.
- **Pin**: Spring attraction to a fixed point (Hooke's law). Use for tethering and pendulum-like motion.
- **DistanceConstraint**: Spherical shell boundary enforcement. Use for confinement and escape prevention.
- **GPU-Executed**: All constraints run on the GPU; CPU Execute() is a no-op.
- **Stackable**: Multiple constraints can be combined for complex physics.
- **Parameters**: Simple configuration (position, speed/strength, radius/bounds).

## References

- `runtime/engine/vfx/modules/orbit_module.h` — OrbitModule configuration and API
- `runtime/engine/vfx/modules/pin_module.h` — PinModule configuration and API
- `runtime/engine/vfx/modules/distance_constraint_module.h` — DistanceConstraintModule configuration and API
- `runtime/shaders/runtime/vfx/vfx_constraints.hlsli` — HLSL implementation of all three constraints
- @ref Hooke 1678 "De Potentia Restitutiva" — spring force fundamentals
- @ref Verlet Integration — constraint-based particle simulation overview

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation covering Orbit, Pin, and DistanceConstraint modules with physics foundations and practical examples. |
