## Phase 3b-3.72.0 — VFX Particle Stretching & Velocity Trails

Stretch particles along their velocity vector to create motion-blur-like trails.
Niagara reference: SubUV / Stretch / Camera Offset modules.

## 1. Shader Implementation

- [x] 1.1 Add `float stretchFactor` to VFXBillboardCB (b2): 0.0 = no stretch (round), >0 = velocity-proportional stretch. Default 0.0. Added to both VS and PS hlsl declarations.
- [x] 1.2 In `particle_billboard_vs.hlsl`: when stretchFactor > 0, elongate the quad along the up axis (velocity direction in velocity-aligned mode, camera up in camera-facing mode). Stretch length = halfSize + halfSize * speed * stretchFactor * 0.1.
- [x] 1.3 Clamp max stretch to 10x particle size to prevent extremely thin triangles.
- [x] 1.4 Preserve UV mapping — U stretches along velocity, V stays [-1,+1] for width. Standard kCornerUVs quad UVs used unchanged regardless of stretch, preserving mapping correctly.

## 2. CPU Integration

- [x] 2.1 Add `float stretchFactor = 0.0f` to VFXEmitterConfig.
- [x] 2.2 Pass stretch factor through VFXBillboardCB in DrawBillboards().
- [ ] 2.3 Add stretch control key in demo (Shift+S: cycle 0/0.5/1.0/2.0/5.0).

## 3. Preset Updates

- [x] 3.1 Sparks: stretchFactor = 3.0 (long streaks).
- [x] 3.2 Fireball: stretchFactor = 0.5 (subtle elongation).
- [x] 3.3 Explosion Debris: stretchFactor = 2.0 (fast debris trails).

## 4. Visual Verification

- [x] 4.1 Screenshot with stretched sparks — visible elongation along movement. Implemented and verified in VFX viewport demo.
- [x] 4.2 Compare round vs stretched particles. Implemented and verified in VFX viewport demo.
- [x] 4.3 Verify stretch looks correct at different velocities and camera angles. Implemented and verified in VFX viewport demo.

## 5. Documentation

- [ ] 5.1 Update help overlay with stretch controls.
- [ ] 5.2 Document stretch parameters in VFX guide.
