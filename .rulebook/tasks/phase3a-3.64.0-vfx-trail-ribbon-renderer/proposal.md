# VFX Trail/Ribbon Renderer — Spline Trails, Width Curves, UV Scrolling

## Summary

Implement the trail and ribbon rendering system: spline-based trails that follow entities, tessellated into camera-facing ribbon geometry with width curves, color over distance, UV scrolling, and transparency sorting. Used for sword swipes, projectile trails, magic effects, and vehicle tire marks.

## Why

Trails and ribbons are the second most common VFX primitive after billboard particles:

1. **Projectile trails**: Every bullet, arrow, and magic projectile needs a visible trail
2. **Melee trails**: Sword swipes, claw attacks show motion arcs
3. **Environmental**: Jet exhausts, comet tails, light streaks
4. **Motion emphasis**: Speed lines, dash effects, teleport ribbons

## UE5 Niagara Reference (Clean-Room)

### FNiagaraRibbonRenderer Architecture
1. **Trail point collection**: Each frame, the emitter's current position is added as a trail point. Points are stored as a ring buffer (FIFO) — oldest points expire.
2. **Tessellation**: Adjacent trail points are connected by camera-facing quads. Width varies along the trail via width curve. The quad's up vector faces the camera (camera-facing ribbon).
3. **UV mapping**: U = 0..1 across trail width. V = 0..1 along trail length (normalized by total trail distance). UV scroll rate moves V over time for animated textures.
4. **Sorting**: Ribbons are sorted per-segment by depth for correct transparency.
5. **Twist**: Optional twist along trail for corkscrew effects (angular interpolation between points).

## Impact

- **New files**: `runtime/engine/vfx/trails/` — trail component, trail renderer
- **New shaders**: `runtime/shaders/vfx/trail_vs.hlsl`, `trail_ps.hlsl`
- **New render pass**: Trail render pass (transparent phase, after particles)
- **Performance target**: 100 trails × 100 segments each < 0.5ms

## Dependencies

- VFX Modules (3.61.0) — curve system (.uzcurve)
- GPU Particle Core (3.60.0) — render graph integration patterns
- RHI (✅ exists) — dynamic vertex/index buffer creation

## Acceptance Criteria

1. TrailComponent ECS component: material, max_trail_points, width_start, width_end, width_curve, uv_scroll_rate, color_over_distance curve, lifetime per point
2. Trail point ring buffer: each tick adds current position as new trail point, oldest points expire
3. Trail geometry generation: camera-facing quads between adjacent trail points
4. Width curve: trail width varies along length (e.g., thick at head, thin at tail)
5. Color over distance curve: trail color fades along length
6. UV scrolling: V coordinate scrolls over time for animated textures
7. Dynamic vertex/index buffer updated each frame (no CPU→GPU stall via ring buffer)
8. Transparent rendering with depth-sorted segments
9. TrailSystem: manages all active trails, generates geometry, dispatches render
10. Demo: projectile with glowing trail, sword swipe arc
11. All unit tests pass, 95%+ coverage
