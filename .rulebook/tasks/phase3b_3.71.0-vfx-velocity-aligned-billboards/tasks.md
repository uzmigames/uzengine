## Phase 3b-3.71.0 — VFX Velocity-Aligned Billboards

Add velocity-aligned billboard mode where particles stretch in their direction
of movement instead of always facing the camera. Niagara reference: Alignment = Velocity.

## 1. Shader Implementation

- [ ] 1.1 Add `uint alignmentMode` to VFXBillboardCB (b2): 0 = Camera-Facing (default), 1 = Velocity-Aligned, 2 = Custom-Axis.
- [ ] 1.2 In `particle_billboard_vs.hlsl`: when alignmentMode == 1, compute billboard axes from velocity: tangent = normalize(velocity), bitangent = normalize(cross(tangent, cameraForward)). Use tangent/bitangent instead of cameraRight/cameraUp for quad expansion.
- [ ] 1.3 Handle zero-velocity case — fall back to camera-facing to avoid degenerate billboard.
- [ ] 1.4 Apply per-particle rotation around velocity axis (twist) instead of billboard facing axis.

## 2. CPU Integration

- [ ] 2.1 Add `uint32_t alignmentMode = 0` to VFXEmitterConfig.
- [ ] 2.2 Pass alignment mode through VFXBillboardCB in DrawBillboards().
- [ ] 2.3 Add alignment toggle key in demo (Shift+V: cycle Camera/Velocity/Custom).

## 3. Preset Updates

- [ ] 3.1 Sparks preset: default to velocity-aligned (streaks).
- [ ] 3.2 Fireball preset: keep camera-facing (dense ball).
- [ ] 3.3 Simple Burst: velocity-aligned for dynamic feel.

## 4. Visual Verification

- [ ] 4.1 Screenshot with velocity-aligned sparks — particles stretch in movement direction.
- [ ] 4.2 Compare camera-facing vs velocity-aligned side by side.
- [ ] 4.3 Verify no artifacts at extreme or zero velocity.

## 5. Documentation

- [ ] 5.1 Update H key help overlay with alignment controls.
- [ ] 5.2 Document alignment modes in VFX guide.
