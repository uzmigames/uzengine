## 1. Module Base Architecture

- [x] 1.1 Create `runtime/engine/vfx/modules/vfx_module.h` — Module abstract base class: virtual Execute(ExecuteContext&), virtual GetShaderCode() → std::string_view, virtual GetParameters() → std::vector<ParameterDef>. ExecuteContext struct: num_particles, dt, GPU buffer handles, parameter map. ParameterDef struct: name, type (Float/Vec3/Vec4/Curve), default value.
- [x] 1.2 Create `runtime/engine/vfx/modules/module_registry.h/.cpp` — ModuleRegistry singleton: Register<T>(name), Create(name) → std::unique_ptr<Module>. Uses std::unordered_map<std::string, FactoryFn>. Thread-safe registration. Logs registered modules at startup via UZ_LOG(VFX, Info).
- [x] 1.3 Create `runtime/engine/vfx/modules/module_compositor.h/.cpp` — ModuleCompositor: takes ordered list of modules, concatenates their HLSL snippets into a single compute shader source. Handles common header (buffer declarations, constants), per-module body, shared utilities (#include "vfx_common.hlsli"). Caches compiled PSO per unique module combination (hash of module types + order).

## 2. Curve System

- [x] 2.1 Create `runtime/engine/vfx/curves/vfx_curve.h/.cpp` — VFXCurve class: stores keyframes (time, value) pairs. Sample(float t) → float/Vec3/Vec4 with linear/cubic/smoothstep interpolation. BakeToTexture(device) → RHITextureHandle (1D texture, 256 texels, R32_FLOAT or RGBA32_FLOAT, linear filter). Curves are sampled on GPU via texture fetch.
- [x] 2.2 Create `runtime/engine/vfx/curves/vfx_curve_loader.h/.cpp` — Load .uzcurve JSON files: parse keys array [{time, value}], interpolation mode, curve type (float/vec3/vec4). Validate keys are sorted by time, range [0,1].
- [x] 2.3 Create `runtime/shaders/vfx/vfx_curves.hlsli` — GPU curve sampling utilities: SampleCurveFloat(Texture1D tex, float t) → float, SampleCurveVec4(Texture1D tex, float t) → float4. Uses linear filtering on 1D texture.

## 3. Core Modules

- [x] 3.1 Create `runtime/engine/vfx/modules/spawn_module.h/.cpp` — SpawnModule: config (rate, lifespan_min, lifespan_max, velocity, velocity_variation, spread_angle, spawn_shape [Point/Sphere/Box], spawn_radius). GetShaderCode() returns HLSL for initializing particle at free-list index with random position/velocity/lifespan. Spawn shape determines initial position distribution.
- [x] 3.2 Create `runtime/engine/vfx/modules/update_module.h/.cpp` — UpdateModule: config (gravity Vec3, drag float, angular_velocity float). GetShaderCode() returns HLSL: vel += gravity*dt, vel *= drag, pos += vel*dt, rotation += angular_vel*dt.
- [x] 3.3 Create `runtime/engine/vfx/modules/decay_module.h/.cpp` — DecayModule: no config. GetShaderCode() returns HLSL: if (age >= lifespan) { append index to free_list, mark dead }. Always last in update chain.
- [x] 3.4 Create `runtime/engine/vfx/modules/sprite_render_module.h/.cpp` — SpriteRenderModule: config (material handle, sprite_width, sprite_height, size_over_life curve, color_over_life curve, depth_sort bool). GetShaderCode() returns HLSL: sample size/color curves at age_norm = age/lifespan, write to size/color buffers. Render method configures billboard PSO and DrawIndirect.
- [x] 3.5 Create `runtime/engine/vfx/modules/turbulence_module.h/.cpp` — TurbulenceModule: config (strength, frequency, octaves). GetShaderCode() returns HLSL: vel += Perlin3D(pos * frequency, time) * strength * dt. Uses gradient noise from vfx_common.hlsli.
- [x] 3.6 Create `runtime/engine/vfx/modules/constraint_module.h/.cpp` — ConstraintModule: config (bounds_min Vec3, bounds_max Vec3). GetShaderCode() returns HLSL: pos = clamp(pos, bounds_min, bounds_max).

## 4. GPU Noise Functions

- [x] 4.1 Add to `runtime/shaders/vfx/vfx_common.hlsli` — Gradient noise functions: Perlin3D(float3 pos, float time) using hash-based gradient lookup. SimplexNoise3D for smoother results. FBM (fractal Brownian motion) wrapper for multi-octave turbulence.

## 5. Shader Composition Pipeline

- [x] 5.1 Implement compute shader generation — ModuleCompositor::CompileEmitterShader(modules, device) generates HLSL source from module snippets, compiles via RHI shader compilation, returns RHIPipelineHandle. Cache key = hash of (module_types + module_configs).
- [x] 5.2 Integrate composed shaders into simulation pass — ParticleSimulationPass uses composed compute shader per emitter instead of hardcoded update shader from 3.60.0.

## 6. Module Registration

- [x] 6.1 Create `runtime/engine/vfx/modules/register_builtin_modules.h/.cpp` — RegisterBuiltinModules() registers all core modules: "spawn", "update", "decay", "sprite_render", "turbulence", "constraint". Called at VFXSystem initialization.

## 7. Testing

- [x] 7.1 Unit tests for VFXCurve — create curve, sample at t=0/0.5/1.0, verify interpolation (linear, smoothstep)
- [x] 7.2 Unit tests for VFXCurveLoader — load valid .uzcurve JSON, load invalid (unsorted keys, out of range), verify error handling
- [x] 7.3 Unit tests for ModuleRegistry — register/create modules, duplicate registration warning, unknown module error
- [x] 7.4 Unit tests for ModuleCompositor — compose 3 modules, verify HLSL output contains all snippets in order
- [x] 7.5 Unit tests for SpawnModule — verify config defaults, parameter export, shader code generation
- [x] 7.6 Unit tests for each module — UpdateModule, DecayModule, TurbulenceModule, ConstraintModule config and shader output

## 8. Demo

- [x] 8.1 Create fire effect demo — Spawn (rate=500, spread=60°, velocity=[0,8,0]) + Update (gravity=[0,-3,0], drag=0.98) + Turbulence (strength=2, freq=0.5) + SpriteRender (size_over_life: grow then shrink, color_over_life: orange→yellow→gray→transparent) + Decay
- [x] 8.2 Add module parameter sliders — runtime tweaking of gravity, turbulence strength, emission rate

## 9. Documentation

- [x] 9.1 Document module API and custom module creation guide
- [x] 9.2 Document .uzcurve format specification with examples
