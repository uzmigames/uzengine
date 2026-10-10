# VFX Modules — Specification

## ADDED Requirements

### Requirement: Module Base Interface
The VFX system SHALL provide a Module abstract base class that all VFX modules inherit from, enabling composable and extensible particle behavior.

#### Scenario: Module Interface Contract
Given a class inheriting from Module
When GetShaderCode() is called
Then it SHALL return a valid HLSL code snippet that operates on particle attributes
And the snippet SHALL use standard buffer names (positions, velocities, ages, lifespans, sizes, colors)
And the snippet SHALL reference particle index via `idx` variable

#### Scenario: Module Parameter Export
Given a module with configurable parameters
When GetParameters() is called
Then it SHALL return a vector of ParameterDef describing each parameter
And each ParameterDef SHALL include name, type (Float/Vec3/Vec4/Curve), and default value

### Requirement: Module Registry
The VFX system SHALL provide a ModuleRegistry for runtime registration and factory creation of module types.

#### Scenario: Register and Create Module
Given ModuleRegistry with "turbulence" registered
When Create("turbulence") is called
Then a new TurbulenceModule instance SHALL be returned
And it SHALL have default configuration values

#### Scenario: Unknown Module Type
Given ModuleRegistry without "custom_wind" registered
When Create("custom_wind") is called
Then nullptr SHALL be returned
And UZ_LOG(VFX, Warning) SHALL log the unknown module name

### Requirement: Shader Composition
The ModuleCompositor SHALL concatenate module HLSL snippets into a single compute shader per emitter for efficient execution.

#### Scenario: Compose Three Modules
Given modules [SpawnModule, UpdateModule, DecayModule] in order
When CompileEmitterShader() is called
Then a single HLSL compute shader SHALL be generated
And the shader SHALL include the common header (buffer declarations, cbuffer)
And the shader SHALL include each module's snippet in order: spawn → update → decay
And a compute PSO SHALL be compiled and cached

#### Scenario: PSO Caching
Given an emitter with modules [Spawn, Update, Decay] already compiled
When another emitter with identical module types and order is compiled
Then the cached PSO SHALL be reused (no recompilation)

### Requirement: Curve System
The VFX system SHALL support parameter curves that define how values change over particle lifetime, baked to GPU-sampled 1D textures.

#### Scenario: Curve Sampling (Linear)
Given a curve with keys [{time:0, value:0}, {time:0.5, value:1}, {time:1, value:0}] and interpolation="linear"
When Sample(0.25) is called
Then the result SHALL be 0.5 (linear interpolation between key 0 and key 1)

#### Scenario: Curve Bake to GPU Texture
Given a curve with 4 keyframes
When BakeToTexture(device) is called
Then a 1D texture of 256 texels SHALL be created (R32_FLOAT for float curves, RGBA32_FLOAT for vec4)
And each texel SHALL contain the curve value at that normalized position
And the texture SHALL use linear filtering for smooth GPU sampling

#### Scenario: .uzcurve Loading
Given a valid .uzcurve JSON file with keys sorted by time [0..1]
When VFXCurveLoader::Load(path) is called
Then a VFXCurve object SHALL be created with all keyframes
And interpolation mode SHALL be parsed (linear/cubic/smoothstep)

### Requirement: Spawn Module
The SpawnModule SHALL initialize new particles with configurable position, velocity, and lifetime distributions.

#### Scenario: Sphere Spawn Shape
Given SpawnModule with spawn_shape=Sphere and spawn_radius=5.0
When a particle is spawned
Then its initial position SHALL be emitter_pos + random point within sphere of radius 5.0
And the distribution SHALL be uniform within the sphere volume

#### Scenario: Cone Velocity Spread
Given SpawnModule with velocity=[0,10,0] and spread_angle=45°
When a particle is spawned
Then its initial velocity direction SHALL be within 45° of [0,1,0]
And velocity magnitude SHALL be 10 ± velocity_variation

### Requirement: Turbulence Module
The TurbulenceModule SHALL apply 3D noise-based velocity perturbation to particles for organic, natural-looking motion.

#### Scenario: Turbulence Application
Given TurbulenceModule with strength=2.0 and frequency=0.5
When the compute shader processes a particle at position=[10,5,3]
Then velocity SHALL be perturbed by Perlin3D(position * 0.5, time) * 2.0 * dt
And the noise SHALL be coherent (nearby particles get similar perturbation)

### Requirement: Module Execution Order
Modules SHALL be executed in the order specified by the .uzvfx asset, with spawn modules before update modules before render modules before decay modules.

#### Scenario: Correct Execution Order
Given modules [Spawn, Update, Turbulence, SpriteRender, Decay]
When the simulation pass executes
Then Spawn SHALL run first (initialize new particles)
Then Update SHALL run (gravity, drag)
Then Turbulence SHALL run (noise velocity)
Then SpriteRender SHALL run (curve sampling for size/color)
Then Decay SHALL run last (kill expired particles)
