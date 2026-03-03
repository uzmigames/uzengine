# Proposal: phase3-particle-system

## Why

UzEngine has no visual effects capability. Fire, smoke, sparks, explosions, magic spells, and hit impacts are all impossible to author without a particle system. This is a fundamental gap that makes the engine unsuitable for any visually rich game, regardless of genre. A GPU-driven approach is required to support tens of thousands of simultaneous particles without degrading frame rate.

## What Changes

- Add `.uzfx` plain-text effect format (INI-like): emitter shape, spawn rate, lifetime, velocity, color/alpha curves, texture reference, and stacked behavior module list
- Implement compute shader simulation: each frame dispatches one thread per particle, reads from `StructuredBuffer<ParticleState>` and writes to `RWStructuredBuffer<ParticleState>` — no CPU readback
- Add stackable behavior modules as compute shader permutations: gravity, linear drag, vortex attractor, curl noise, point attractor, collision bounce (against depth buffer)
- Implement billboard quad rendering: `DrawIndexedInstanced` using `SRV` of live particle transforms, camera-facing quad expansion in vertex shader
- Add `CParticleEmitter` ECS component: references `.uzfx` asset, supports one-shot and looping modes, warm-up seconds
- Integrate with `AnimNotify`: `spawn_vfx` notify type routes to `CParticleEmitter::spawn(world_pos, world_rot)` at the notified keyframe
- Expose Lua API: `ctx.vfx:spawn(effect_path, pos, rot)`, `ctx.vfx:stop(handle)`, `ctx.vfx:set_rate(handle, multiplier)`
- Support up to 65 536 particles per emitter via GPU ping-pong buffers; multiple emitters composited via `ExecuteIndirect`

## Impact

- Affected specs: `docs/specs/particle-system.md`, `docs/specs/animation-system.md`, `docs/specs/renderer.md`, `docs/specs/asset-pipeline.md`
- Affected code: `src/vfx/particle_emitter.cpp`, `shaders/particle_simulate.hlsl`, `shaders/particle_render.hlsl`, `src/scripting/lua_vfx_bindings.cpp`
- Breaking change: NO
- User benefit: Enables fire, smoke, magic, impacts, and any other visual effect needed for polished game presentation, integrated directly with the animation notify system
