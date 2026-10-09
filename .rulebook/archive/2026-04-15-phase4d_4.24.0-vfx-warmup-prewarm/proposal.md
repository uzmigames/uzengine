# Proposal: phase4d_4.24.0-vfx-warmup-prewarm

## Why
When a particle emitter spawns, it starts from zero particles. Effects like ambient
smoke, fire, or environmental dust look wrong for the first few seconds because they
need time to "fill up". Niagara solves this with WarmupTime — the system simulates
N seconds of particles instantly before the first visible frame. Without warmup,
looping ambient effects pop into existence unnaturally.

## What Changes
- Add `warmupTime` (float, seconds) and `warmupTickRate` (float, default 1/30) to ParticleEmitter
- VFXSystem::SpawnEmitter() detects warmupTime > 0 and runs N = warmupTime/warmupTickRate
  simulation ticks (spawn + update dispatches) in a tight loop before the first rendered frame
- Each warmup tick uses warmupTickRate as deltaTime for deterministic results
- Warmup dispatches reuse the existing spawn/update compute pipeline (no new shaders)
- Add `warmupTime` field to .uzvfx JSON format
- Add warmup preset: `ambient_dust.uzvfx` that demonstrates instant fill

## Impact
- Affected specs: particle-system.md (add warmup parameters)
- Affected code: particle_emitter.h, vfx_system.cpp, vfx_graph.cpp, frame_particles.cpp
- Breaking change: NO (new optional field, default 0 = no warmup)
- User benefit: Ambient/looping effects appear fully populated from frame 1
