# Proposal: phase3a-3.68.0-vfx-simulation-forces

## Why
The current VFX simulation only applies linear velocity, gravity, and damping. Real game effects require attractor forces (black holes, magnets), vector fields (wind tunnels, fluid flow), sub-emitters (fireworks, explosion sparks from dying particles), and advanced constraints (orbit, pin, distance). These modules transform simple particle sprays into believable physical phenomena.

## What Changes
- AttractorModule: point attractor with Linear/InverseSquare/Constant falloff, multi-attractor structured buffer
- VectorFieldModule: 3D texture sampling for wind/fluid flow, animated scrolling support
- SubEmitterModule: GPU→CPU trigger readback for OnDeath/OnCollision/OnEvent, child emitter spawning with inherited velocity/color
- OrbitModule, PinModule, DistanceConstraintModule: advanced per-particle positional constraints
- All modules registered in RegisterBuiltinModules()
- Demos: black hole attractor, wind tunnel, firework sub-emitter chain, orbit ring

## Impact
- Affected specs: vfx-modules-spawn-update-render, vfx-advanced-modules-lod-budget
- Affected code: runtime/engine/vfx/modules/, runtime/shaders/vfx/
- Breaking change: NO
- User benefit: Designers can author complex multi-stage effects (rockets that burst into sub-emitters, particles that swirl into attractors, wind-blown debris) without custom code
