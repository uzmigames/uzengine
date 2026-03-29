## Phase 3b-3.72.0 — VFX Particle Stretching & Velocity Trails

Stretch particles along their velocity vector to create motion-blur-like trails.
Niagara reference: SubUV / Stretch / Camera Offset modules.

## 1. Shader Implementation

- [ ] 1.1 Add `float stretchFactor` to VFXBillboardCB (b2): 0.0 = no stretch (round), 1.0 = velocity-proportional stretch. Default 0.0.
- [ ] 1.2 In `particle_billboard_vs.hlsl`: when stretchFactor > 0, elongate the quad along velocity direction. Compute stretch length = `length(velocity) * stretchFactor * dt`. Extend quad verts by half-stretch along tangent.
- [ ] 1.3 Clamp max stretch to prevent extremely thin triangles (max 10x particle size).
- [ ] 1.4 Preserve UV mapping — U stretches along velocity, V stays [-1,+1] for width.

## 2. CPU Integration

- [ ] 2.1 Add `float stretchFactor = 0.0f` to VFXEmitterConfig.
- [ ] 2.2 Pass stretch factor through VFXBillboardCB.
- [ ] 2.3 Add stretch control key in demo (Shift+S: cycle 0/0.5/1.0/2.0/5.0).

## 3. Preset Updates

- [ ] 3.1 Sparks: stretchFactor = 3.0 (long streaks).
- [ ] 3.2 Fireball: stretchFactor = 0.5 (subtle elongation).
- [ ] 3.3 Explosion Debris: stretchFactor = 2.0 (fast debris trails).

## 4. Visual Verification

- [ ] 4.1 Screenshot with stretched sparks — visible elongation along movement.
- [ ] 4.2 Compare round vs stretched particles.
- [ ] 4.3 Verify stretch looks correct at different velocities and camera angles.

## 5. Documentation

- [ ] 5.1 Update help overlay with stretch controls.
- [ ] 5.2 Document stretch parameters in VFX guide.
