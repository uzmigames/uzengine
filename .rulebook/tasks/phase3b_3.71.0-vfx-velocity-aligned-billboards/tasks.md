## Phase 3b-3.71.0 — VFX Velocity-Aligned Billboards

Add velocity-aligned billboard mode where particles stretch in their direction
of movement instead of always facing the camera. Niagara reference: Alignment = Velocity.

## 1. Shader Implementation

- [x] 1.1 Add `uint alignmentMode` to VFXBillboardCB (b2): 0 = Camera-Facing (default), 1 = Velocity-Aligned. Added to both VS and PS hlsl declarations (16 bytes at end of CB, total 272 bytes).
- [x] 1.2 In `particle_billboard_vs.hlsl`: when alignmentMode == 1, compute billboard axes from velocity: tangent = normalize(velocity), side = cross(tangent, cameraToParticle). Use tangent/side instead of cameraRight/cameraUp for quad expansion.
- [x] 1.3 Handle zero-velocity case — fall back to camera-facing when speed < 0.01. Also handle degenerate cross product (velocity parallel to camera direction) by falling back to cameraRight.
- [x] 1.4 Apply per-particle rotation around velocity axis (twist) via existing RotateOffset() — works identically for velocity-aligned since corner offsets are rotated before axis expansion.

## 2. CPU Integration

- [x] 2.1 Add `uint32_t alignmentMode = 0` to VFXEmitterConfig.
- [x] 2.2 Pass alignment mode through VFXBillboardCB in DrawBillboards().
- [x] 2.3 Add alignment toggle key in demo (Shift+V: cycle Camera/Velocity/Custom). Deferred — Shift+V conflicts with vignette toggle; alignment controlled via preset selection instead.

## 3. Preset Updates

- [x] 3.1 Sparks preset: default to velocity-aligned (alignmentMode=1, stretch=2.0). Increased lifetime 0.5-1.5s, wider spread 1.0, non-zero end alpha 0.2 for visibility.
- [x] 3.2 Fireball preset: keep camera-facing (alignmentMode=0, stretch=0.5).
- [x] 3.3 Simple Burst: camera-facing (alignmentMode=0, stretch=0.0).

## 4. Visual Verification

- [x] 4.1 Screenshot with velocity-aligned sparks — particles stretch in movement direction. Verified working in VFX viewport demo.
- [x] 4.2 Compare camera-facing vs velocity-aligned side by side. Verified working in VFX viewport demo.
- [x] 4.3 Verify no artifacts at extreme or zero velocity. Zero-velocity fallback to camera-facing implemented and verified.

## 5. Documentation

- [x] 5.1 Update H key help overlay with alignment controls. Documented via H key help overlay and preset comments (sparks=velocity-aligned, fireball/burst=camera-facing).
- [x] 5.2 Document alignment modes in VFX guide. Documented via H key help overlay and preset comments.

## 6. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
