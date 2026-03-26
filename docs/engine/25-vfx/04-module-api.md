---
title: VFX Module System API Reference
status: Done (Phase 3.61.0)
last-updated: 2026-03-24
---

# VFX Module System API Reference

Complete reference for the VFX module composition system: base interfaces, registry, compositor, and all builtin modules.

> **Source**: `runtime/engine/vfx/modules/`

## Overview

The VFX module system provides a composable architecture for particle behavior. Each module encapsulates one aspect of simulation (spawn, physics, decay, rendering, etc.) and emits a self-contained HLSL snippet. The ModuleCompositor stitches these snippets into complete compute shaders, enabling artists to mix-and-match modules without writing shader code.

Key design principles:
- **Modularity**: One concern per module (Single Responsibility)
- **Zero boilerplate**: Standard buffer bindings (positions, velocities, ages, colors, sizes)
- **GPU-first**: Heavy lifting on the compute shader, minimal CPU overhead
- **Composability**: Any module stack can be composed in any order

## Core Interfaces

### ExecuteContext

Per-dispatch data passed to VFX modules during CPU-side execution.

```cpp
struct ExecuteContext {
    uint32_t numParticles = 0;      // Active particles in this dispatch
    float dt = 0.0f;                // Frame delta time (seconds, >= 0)
    uint32_t frameNumber = 0;       // Monotonic frame number
    uint32_t positionsUav = 0;      // Bindless index: positions UAV
    uint32_t velocitiesUav = 0;     // Bindless index: velocities UAV
    uint32_t agesUav = 0;           // Bindless index: ages UAV
    uint32_t lifespansSrv = 0;      // Bindless index: lifespans SRV (read-only)
    uint32_t colorsUav = 0;         // Bindless index: colors UAV
    uint32_t sizesUav = 0;          // Bindless index: sizes UAV
    const void* parameterData = nullptr; // Module-specific cbuffer data
};
```

**Usage**: Modules receive this context in `Execute()` to access particle counts, time delta, and GPU buffer handles for any CPU-side setup (e.g., uploading dynamic constants).

---

### ParameterType

Describes the data type of a VFX module parameter.

```cpp
enum class ParameterType : uint8_t {
    Float,  // Single float scalar
    Vec3,   // 3D float vector (position, direction, color RGB, etc.)
    Vec4,   // 4D float vector (RGBA color, quaternion, etc.)
    Int,    // 32-bit signed integer
    Bool,   // Boolean (serialized as uint32_t in constant buffer)
    Curve,  // Reference to a VFXCurve asset (evaluated at particle age)
};
```

Used by the editor UI to generate appropriate widgets and by the serialization system to read/write parameter values.

---

### ParameterDef

Declares a single parameter exposed by a VFX module.

```cpp
struct ParameterDef {
    const char* name = nullptr;     // Display name in editor
    ParameterType type = ParameterType::Float; // Data type

    // Default values (union, only field matching type is valid)
    union {
        float defaultFloat;
        int32_t defaultInt;
        bool defaultBool;
    };

    // Default Vec3/Vec4 (outside union due to non-trivial constructors)
    Vec3 defaultVec3{};
    Vec4 defaultVec4{};
};
```

**Example**:
```cpp
ParameterDef gravityDef;
gravityDef.name = "Gravity";
gravityDef.type = ParameterType::Vec3;
gravityDef.defaultVec3 = Vec3{0.0f, -9.81f, 0.0f};
```

---

### IVFXModule

Abstract base class for composable particle behavior modules.

```cpp
class IVFXModule {
public:
    virtual ~IVFXModule() = default;

    /// Returns the unique module name (e.g., "Gravity", "Turbulence")
    [[nodiscard]] virtual const char* GetName() const noexcept = 0;

    /// Returns HLSL code snippet for GPU particle behavior
    [[nodiscard]] virtual std::string_view GetShaderCode() const = 0;

    /// Returns list of configurable parameters
    [[nodiscard]] virtual std::vector<ParameterDef> GetParameters() const = 0;

    /// CPU-side execution hook (called before GPU dispatch)
    /// Most modules leave this empty; only needed for per-frame CPU uploads
    virtual void Execute(const ExecuteContext& ctx) = 0;
};
```

**Implementing a Module**:

1. Override `GetName()` with a unique, stable identifier
2. Override `GetShaderCode()` returning HLSL that reads/writes standard particle UAVs
3. Override `GetParameters()` listing configurable inputs
4. Override `Execute()` for any CPU-side dispatch prep (often empty)

**Standard UAV Bindings** (available in shader code):

| Name | Type | Description |
|------|------|-------------|
| `Positions` | `RWStructuredBuffer<float3>` | Particle world positions |
| `Velocities` | `RWStructuredBuffer<float3>` | Particle velocities (m/s) |
| `Ages` | `RWStructuredBuffer<float>` | Particle ages (0 = just spawned, -1 = dead) |
| `Lifespans` | `StructuredBuffer<float>` | Particle lifespans (read-only, set by spawn) |
| `Colors` | `RWStructuredBuffer<float4>` | Particle colors (RGBA, 0-1 range) |
| `Sizes` | `RWStructuredBuffer<float>` | Particle sizes (world units) |
| `FreeListCount` | `RWByteAddressBuffer` | Atomic counter for free-list allocation |

Module parameters are passed via `cbModule` constant buffer (defined by ModuleCompositor).

---

## ModuleRegistry

Factory for creating VFX modules by name.

```cpp
class ModuleRegistry {
public:
    /// Registers a VFX module type under the given name
    /// T must derive from IVFXModule and be default-constructible
    template <typename T>
    void Register(const char* name);

    /// Creates a module instance by name
    /// Returns unique_ptr<IVFXModule>, nullptr if name not found
    [[nodiscard]] std::unique_ptr<IVFXModule> Create(const char* name) const;

    /// Returns all registered module names
    [[nodiscard]] std::vector<std::string> GetRegisteredNames() const;

    /// Checks if a module is registered under the given name
    [[nodiscard]] bool IsRegistered(const char* name) const;
};
```

**Example**:
```cpp
ModuleRegistry registry;
registry.Register<SpawnModule>("spawn");
registry.Register<UpdateModule>("update");
registry.Register<TurbulenceModule>("turbulence");

auto gravity = registry.Create("update");  // unique_ptr<IVFXModule>
if (gravity) {
    // Use the module
}
```

**Thread Safety**: Not thread-safe. All registration and creation happens during system initialization on the main thread.

---

## ModuleCompositor

Stitches module HLSL snippets into complete compute shaders.

```cpp
class ModuleCompositor {
public:
    /// Composes a particle update (simulation) compute shader
    /// Concatenates module snippets in order, wrapped in standard buffer
    /// declarations and a [numthreads(256,1,1)] dispatch function
    [[nodiscard]] ComposedShader ComposeUpdateShader(
        const std::vector<IVFXModule*>& modules) const;

    /// Composes a particle spawn compute shader
    /// Similar to ComposeUpdateShader but uses spawn-specific declarations
    [[nodiscard]] ComposedShader ComposeSpawnShader(
        const std::vector<IVFXModule*>& spawnModules) const;
};

struct ComposedShader {
    std::string hlslSource;  // Complete HLSL compute shader source
    uint64_t cacheKey = 0;   // FNV-1a hash of module names + order
};
```

**Shader Structure** (Update Pass):

```
1. #include "vfx_common.hlsli"
2. Standard buffer declarations (positions, velocities, ages, etc.)
3. Per-module HLSL snippets (in order)
4. [numthreads(256,1,1)] void CSMain(...) dispatch wrapper
```

**Example**:
```cpp
ModuleCompositor compositor;
std::vector<IVFXModule*> modules = {
    gravityModule.get(),
    dragModule.get(),
    turbulenceModule.get()
};

auto shader = compositor.ComposeUpdateShader(modules);
// shader.hlslSource is ready for DXC compilation
// shader.cacheKey identifies this exact module combination for PSO caching
```

**Cache Key**: FNV-1a hash of module names in order. Two ComposedShaders with the same cacheKey produce identical HLSL, allowing the PSO cache to avoid redundant recompilation.

---

## Builtin Modules

### SpawnModule

Particle emission with shape-based positioning.

**Name**: `"spawn"`

**Parameters**:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `rate` | Float | 100.0 | Particles emitted per second |
| `lifespan_min` | Float | 1.0 | Minimum particle lifetime (seconds) |
| `lifespan_max` | Float | 5.0 | Maximum particle lifetime (seconds) |
| `velocity` | Vec3 | (0, 10, 0) | Base emission velocity direction and magnitude |
| `velocity_variation` | Float | 1.0 | Random velocity offset magnitude per-axis |
| `spread_angle` | Float | 30.0 | Emission cone half-angle (degrees) |
| `spawn_radius` | Float | 1.0 | Sphere shape radius (for SpawnShape::Sphere) |
| `spawn_box_extents` | Vec3 | (1, 1, 1) | Half-extents for SpawnShape::Box |

**Configuration**:
```cpp
class SpawnModule : public IVFXModule {
    SpawnShape spawnShape = SpawnShape::Point;  // Point, Sphere, or Box
    // + parameters listed above (public members for editor/serialization)
};

enum class SpawnShape : uint8_t {
    Point,   // All particles spawn at emitter origin
    Sphere,  // Random positions within a sphere
    Box,     // Random positions within an axis-aligned box
};
```

**Behavior**:
- Each frame, the GPU compute shader claims free-list slots via atomic decrement
- Initializes position (based on spawn shape), velocity (cone distribution), age, lifespan, size, color, rotation
- Dead particles are marked with age = -1.0f

**Typical Usage**:
```cpp
auto spawn = registry.Create("spawn");
auto spawnModule = static_cast<SpawnModule*>(spawn.get());
spawnModule->rate = 50.0f;
spawnModule->spawnShape = SpawnShape::Sphere;
spawnModule->spawnRadius = 2.0f;
```

---

### UpdateModule

Core particle physics integration.

**Name**: `"update"`

**Parameters**:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `gravity` | Vec3 | (0, -9.81, 0) | Constant acceleration (m/s²) |
| `drag` | Float | 0.98 | Velocity multiplier per frame (1.0 = no drag) |
| `angular_velocity` | Float | 0.0 | Rotation speed (radians/second) |

**Behavior**:
- Applies constant acceleration (gravity) each frame
- Multiplicative drag: `velocity *= drag`
- Angular velocity: `rotation += angular_velocity * dt`
- Standard Euler integration: `position += velocity * dt`

**Example**:
```cpp
auto update = registry.Create("update");
auto updateModule = static_cast<UpdateModule*>(update.get());
updateModule->gravity = Vec3{0.0f, -20.0f, 0.0f};  // Custom gravity
updateModule->drag = 0.95f;  // More drag = slower deceleration
```

---

### DecayModule

Particle age tracking and death recycling.

**Name**: `"decay"`

**Parameters**: None (no configurable parameters)

**Behavior**:
- Increments particle age by `dt` each frame
- When age exceeds lifespan, returns slot to free list via atomic increment
- Marks dead particles with age = -1.0f (sentinel value)
- Other modules can skip dead particles by checking `age < 0`

**Notes**:
- Lifespan is set by SpawnModule
- DecayModule is the only way particles are recycled back to the free list
- Should typically run last in the update pass (after all physics)

---

### SpriteRenderModule

Billboard sprite rendering configuration.

**Name**: `"sprite_render"`

**Parameters**:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `sprite_width` | Float | 1.0 | Billboard width (world units) |
| `sprite_height` | Float | 1.0 | Billboard height (world units) |
| `depth_sort` | Bool | true | Enable back-to-front depth sorting |

**Behavior**:
- Computes normalized particle age (0 at spawn, 1 at death)
- Writes final size and color to particle attribute buffers
- Size-over-life and color-over-life curves planned (via curve IDs)

**Configuration**:
```cpp
class SpriteRenderModule : public IVFXModule {
    float spriteWidth = 1.0f;
    float spriteHeight = 1.0f;
    bool depthSort = true;
    uint32_t sizeOverLifeCurveId = 0;      // Placeholder (0 = no curve)
    uint32_t colorOverLifeCurveId = 0;     // Placeholder (0 = no curve)
};
```

**Future Enhancements**:
- Size-over-life curve sampling (referenced by `sizeOverLifeCurveId`)
- Color-over-life curve sampling (referenced by `colorOverLifeCurveId`)
- Sprite sheet animation frames

---

### TurbulenceModule

Fractal noise-based velocity perturbation.

**Name**: `"turbulence"`

**Parameters**:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `strength` | Float | 1.0 | Force magnitude multiplier |
| `frequency` | Float | 0.5 | Spatial frequency (higher = tighter noise) |
| `octaves` | Int | 3 | FBM octave count (clamped to [1, 8]) |

**Behavior**:
- Evaluates FBM (Fractional Brownian Motion) 3D noise at each particle position
- Adds the resulting force vector (scaled by strength) to the particle velocity
- Higher octave counts produce finer detail at increased GPU cost
- Frequency controls the scale of the noise pattern

**Example**:
```cpp
auto turb = registry.Create("turbulence");
auto turbModule = static_cast<TurbulenceModule*>(turb.get());
turbModule->strength = 5.0f;
turbModule->frequency = 2.0f;
turbModule->octaves = 4;  // More detail but higher cost
```

**Performance Notes**:
- Each octave adds one FBM3D evaluation, so octave count is the primary cost driver
- Typical range: 2-6 octaves for good visual results
- Strength scales the perturbation without changing GPU cost

---

### ConstraintModule

Axis-aligned bounding box position clamping.

**Name**: `"constraint"`

**Parameters**:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `bounds_min` | Vec3 | (-100, -100, -100) | AABB minimum corner |
| `bounds_max` | Vec3 | (100, 100, 100) | AABB maximum corner |

**Behavior**:
- Clamps each particle position to the box defined by [boundsMin, boundsMax]
- Particles that would exit the bounds are snapped back to the nearest face
- No velocity bounce or reflection, just hard clamping

**Use Cases**:
- Keep fire/smoke contained in a room or lantern
- Restrict water particles to a basin
- Bound explosions to a defined volume

**Example**:
```cpp
auto constraint = registry.Create("constraint");
auto constraintModule = static_cast<ConstraintModule*>(constraint.get());
constraintModule->boundsMin = Vec3{-5.0f, -5.0f, -5.0f};
constraintModule->boundsMax = Vec3{5.0f, 5.0f, 5.0f};
```

---

## Module Composition Patterns

### Standard Particle Effect Stack

A typical update pass combines modules in this order:

```cpp
std::vector<IVFXModule*> updateModules = {
    spawnModule.get(),      // Allocate and initialize new particles
    updateModule.get(),     // Physics: gravity, drag, rotation
    turbulenceModule.get(), // Noise-based perturbation
    constraintModule.get(), // Boundary clamping
    decayModule.get()       // Age increment and recycling
};

auto shader = compositor.ComposeUpdateShader(updateModules);
```

**Order matters**:
- Spawn modules first (allocate free-list slots)
- Physics middle (gravity, drag, forces)
- Decay last (recycle dead particles)
- Constraints typically run after physics but before decay (prevent particles escaping bounds)

### Custom Module Example

```cpp
class CustomGravityModule : public IVFXModule {
public:
    const char* GetName() const noexcept override { return "custom_gravity"; }

    std::string_view GetShaderCode() const override {
        static constexpr std::string_view kCode = R"(
// Custom gravity with time-varying strength
float gravityStrength = cbModule.gravity.x;
float time = cbFrameData.frameTime;
float modulatedStrength = gravityStrength * (1.0 + sin(time * 2.0));

uint idx = GroupIndex;
if (idx < cbDispatchData.numParticles && Ages[idx] >= 0.0) {
    float3 gravityForce = float3(0, -modulatedStrength, 0);
    Velocities[idx] += gravityForce * cbFrameData.dt;
}
        )";
        return kCode;
    }

    std::vector<ParameterDef> GetParameters() const override {
        return {
            {"gravity", ParameterType::Float, 9.81f}
        };
    }

    void Execute(const ExecuteContext& ctx) override {
        // No CPU-side work needed
    }
};
```

---

## Shader Code Guidelines

When implementing `GetShaderCode()`:

**Do's**:
- Use standard UAV bindings: `Positions`, `Velocities`, `Ages`, `Colors`, `Sizes`, `Lifespans`
- Access module parameters via `cbModule` constant buffer
- Check `Ages[idx] >= 0` to skip dead particles
- Use `[numthreads(256,1,1)]` dispatch (handled by compositor wrapper)
- Call helper functions from `vfx_common.hlsli` (noise, interpolation, etc.)

**Don'ts**:
- Don't declare your own buffers or constant buffers (ModuleCompositor provides these)
- Don't hardcode buffer binding slots (rely on bindless indexing)
- Don't assume a specific dispatch size (NumThreads defined by compositor)
- Don't modify particle count or allocate new particles outside SpawnModule

**Helper Functions** (available in `vfx_common.hlsli`):

- `float Perlin3D(float3 pos)` — 3D Perlin noise
- `float SimplexNoise3D(float3 v)` — Simplex noise (cheaper, fewer artifacts)
- `float3 FBM3D(float3 pos, float time, int octaves)` — Fractal Brownian motion
- `float3 CurlNoise3D(float3 pos, float time)` — Divergence-free curl noise
- `float3 Gradient3D(int3 ip)` — Gradient vector lookup

---

## Rules & Guidelines

**MUST DO**:
- Register all builtin modules during engine initialization
- Validate module name stability — renaming breaks saved assets
- Document parameter ranges and default values for editors
- Use meaningful parameter names (avoid `value1`, `param`, etc.)
- Test module composition with different module orders
- Profile GPU cost of custom modules (particularly those using expensive noise)

**MUST NOT**:
- Modify free-list allocation outside SpawnModule (leads to buffer corruption)
- Change memory layout assumptions in particle buffers (breaks compatibility)
- Assume module execution order (support any arbitrary order by design)
- Share mutable state between modules (each module runs independently)
- Hardcode parameter values in shader code (use `cbModule` for all configuration)

---

## See Also

- [GPU Noise Functions](06-gpu-noise.md) — Detailed noise algorithm reference
- [.uzcurve Format](05-uzcurve-format.md) — Keyframe-based curve system
- [VFX System Overview](01-overview.md) — High-level architecture
- [Particle System](02-particle-system.md) — GPU particle implementation
