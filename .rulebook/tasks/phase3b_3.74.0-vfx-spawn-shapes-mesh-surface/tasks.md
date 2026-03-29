## Phase 3b-3.74.0 — VFX Spawn Shapes & Mesh Surface Emission

Expand particle spawn shapes beyond point/sphere/box to include ring, cone, cylinder,
disc, line, and mesh surface emission. Niagara reference: Shape Location module.

## 1. New Spawn Shapes (Shader)

- [ ] 1.1 Add `uint spawnShape` to VFXSpawnCB: 0=Point, 1=Sphere, 2=Box, 3=Ring, 4=Cone, 5=Cylinder, 6=Disc, 7=Line.
- [ ] 1.2 In `particle_spawn_cs.hlsl`: implement Ring shape — particles on a torus at `spawnRadius` with ring width. Normal points outward radially.
- [ ] 1.3 Implement Cone shape — particles distributed on a cone surface defined by angle and height. Velocity along cone normal.
- [ ] 1.4 Implement Cylinder shape — particles on the surface or volume of a cylinder (height + radius).
- [ ] 1.5 Implement Disc shape — flat circle spawn (XY plane in emitter local space).
- [ ] 1.6 Implement Line shape — particles along a line segment between two points.

## 2. Shape Parameters

- [ ] 2.1 Add shape-specific params to VFXEmitterConfig: `float shapeParam1, shapeParam2, shapeParam3` (ring width, cone angle, cylinder height, etc.).
- [ ] 2.2 Add `bool spawnOnSurface = false` — true = surface only, false = volume fill.
- [ ] 2.3 Pass shape params through VFXSpawnCB to compute shader.

## 3. Mesh Surface Emission (Advanced)

- [ ] 3.1 Add mesh vertex buffer SRV to spawn CB for mesh surface sampling.
- [ ] 3.2 In spawn CS: randomly pick a triangle (weighted by area), sample barycentric coordinates, compute position + normal on triangle surface.
- [ ] 3.3 Velocity direction from mesh surface normal (outward) + spread.

## 4. Demo Integration

- [ ] 4.1 Add shape cycling key (Shift+Num0: cycle through shapes).
- [ ] 4.2 Add presets demonstrating each shape (Ring of Fire, Cone Jet, Cylinder Volume).

## 5. Visual Verification

- [ ] 5.1 Screenshot each spawn shape — verify correct spatial distribution.
- [ ] 5.2 Verify surface vs volume modes produce visually distinct patterns.

## 6. Documentation

- [ ] 6.1 Document all spawn shapes with diagrams in VFX guide.
- [ ] 6.2 Update help overlay with shape cycling controls.
