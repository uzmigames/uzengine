# Proposal: Port Standalone VFX Demo Features to Engine Application

## Why

The standalone VFX viewport demo (DemoApplication-based) has 28+ features implemented including emissive particles, multiple blend modes, color/size over lifetime, 8 spawn shapes, curl noise, vortex forces, wind zones, ground collision, death spawn sub-emitters, 20+ presets, clickable editor UI, and multi-emitter compound effects. The engine Application-based viewport (vfx_viewport_app) currently only has basic spawn + update + billboard render with hardcoded parameters. All standalone features need to be ported to the engine path so they work through the SceneRenderer's VFX pipeline.

## What Changes

### Phase 1: Billboard Rendering Features (CB passthrough)
Wire the existing VFXBillboardCB fields from emitter config to the billboard render pass:
- Emissive intensity, soft fade distance
- Color over lifetime (start/end lerp)
- Size over lifetime (start/peak/end piecewise)
- Blend modes (Alpha/Additive/PremulAlpha PSO variants)
- Facing modes (5 modes already in shader)
- Velocity stretching, camera offset, pivot offset
- Screen-space size mode, size/color by speed, rotation over life

### Phase 2: Simulation Forces (Update CB passthrough)
Wire the existing update shader force parameters from emitter config:
- Curl noise (strength, frequency, speed)
- Vortex force (axis, tangential, radial, max radius)
- Wind zones (direction, strength, upload buffer)
- Ground collision (bounce, stick, kill modes)
- Orbit around point (radius, speed, axis)
- Quadratic drag, acceleration force
- Kill volumes (box/sphere regions)
- Death spawn sub-emitter

### Phase 3: Spawn Configuration
- All 8 spawn shapes (Point/Sphere/Box/Ring/Cone/Cylinder/Disc/Line)
- Shape parameters (shapeParam1/2/3)
- Initial rotation range, velocity inheritance

### Phase 4: Presets & Multi-Emitter
- Port 20+ VFXEmitterConfig presets from standalone
- Multi-emitter support (multiple VfxEmitterComponent entities)
- Compound effects (Fire=core+sparks, Explosion=burst+smoke+debris)
- Preset serialization (JSON save/load via VFS)

### Phase 5: Editor UI
- Clickable menu bar with TextRenderer
- Parameter panels (sliders, dropdowns, buttons)
- Preset browser/selector
- Module stack toggle UI

## Impact

- Affected specs: VFX emitter config, billboard render, simulation forces
- Affected code: frame_vfx_inline.cpp, frame_particles.cpp, vfx_viewport_app.cpp, VfxEmitterComponent
- Breaking change: NO (additive — extends existing emitter config)
- User benefit: Full VFX editing capability in the engine Application viewport matching the standalone demo
